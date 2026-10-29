param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../../build-artifacts/native-rebuild/release'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot ('../../../build-artifacts/native-rebuild/verification/clock-acceptance-resources-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [ValidateRange(1, 100)][int]$Rounds = 10,
    [ValidateRange(2000, 30000)][int]$DwellMilliseconds = 4000,
    [ValidateRange(250, 5000)][int]$SampleMilliseconds = 1000,
    [ValidateSet('combined', 'clock-only', 'manager-only', 'weather-only', 'almanac-only', 'performance-only', 'organizer-only', 'widgets-combined', 'product-combined')][string]$Mode = 'combined',
    [ValidateRange(0, 600)][int]$CooldownSeconds = 0,
    [switch]$CollectGpu
)
$Mode = $Mode.ToLowerInvariant()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$widgetMode = $Mode -in @('weather-only', 'almanac-only', 'performance-only', 'organizer-only', 'widgets-combined')
$productMode = $Mode -eq 'product-combined'
$probeName = if ($productMode) { 'lume-product-resource-probe.exe' } elseif ($widgetMode) { 'lume-widget-resource-probe.exe' } else { 'lume-clock-resource-probe.exe' }
$probePath = Join-Path $BuildDirectory $probeName
if (-not (Test-Path -LiteralPath $probePath)) { throw "Build the explicit lume-clock-resource-probe target first: $probePath" }
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$phasePath = Join-Path $OutputDirectory 'phase.json'
if (Test-Path -LiteralPath $phasePath) { throw 'Use a fresh output directory for every resource measurement.' }
$scriptSnapshot = Join-Path $OutputDirectory 'measure-clock-resources.executed.ps1'
Copy-Item -LiteralPath $PSCommandPath -Destination $scriptSnapshot
$scriptHash = (Get-FileHash -LiteralPath $scriptSnapshot -Algorithm SHA256).Hash
$probeHash = (Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash

if (-not ('ClockAcceptance.NativeCounters' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace ClockAcceptance {
    public static class NativeCounters {
        [DllImport("user32.dll", SetLastError=true)] public static extern uint GetGuiResources(IntPtr process, uint flags);
        [DllImport("kernel32.dll")] public static extern ulong GetTickCount64();
    }
}
'@
}

$samples = [Collections.Generic.List[object]]::new()
$processSamples = [Collections.Generic.List[object]]::new()
$gpuMemorySamples = [Collections.Generic.List[object]]::new()
$helperTraceRecords = [Collections.Generic.List[object]]::new()
$helperSubscriptions = @()
$helperTraceRequired = $Mode -in @('organizer-only','widgets-combined')
$helperTraceError = $null
$known = @{}
$previousCpu = @{}
$logicalProcessors = [Environment]::ProcessorCount
$started = [DateTime]::UtcNow
$root = $null
$rootStart = $null
$failure = $null
$exitCode = $null
$survivors = @()
$postExitSamples = 0
$timeoutSeconds = 60 + $CooldownSeconds + $Rounds * (25 + 5 * $DwellMilliseconds / 1000 + 2)

function Get-Phase {
    if (Test-Path -LiteralPath $phasePath) {
        $phaseStream = $null
        $phaseReader = $null
        try {
            # The writer atomically replaces this path; readers must allow deletion/rename.
            $sharing = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
            $phaseStream = [IO.File]::Open($phasePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, $sharing)
            $phaseReader = [IO.StreamReader]::new($phaseStream)
            return ($phaseReader.ReadToEnd() | ConvertFrom-Json)
        } catch { } finally {
            if ($null -ne $phaseReader) { $phaseReader.Dispose() }
            elseif ($null -ne $phaseStream) { $phaseStream.Dispose() }
        }
    }
    return [pscustomobject]@{ phase = 'startup'; round = 0; pid = 0; tickMs = 0 }
}
function Get-Sum($Rows, [string]$Property) {
    $measurement = $Rows | Measure-Object -Property $Property -Sum
    if ($null -eq $measurement) { return 0 }
    $sum = $measurement.Sum
    if ($null -eq $sum) { return 0 }
    return $sum
}
function Get-CpuIntervalPercent($Previous, $Current) {
    if ($null -eq $Previous -or $null -eq $Current -or
        -not $Previous.stablePhase -or -not $Current.stablePhase -or
        $Previous.phase -eq 'diagnostic' -or $Previous.phase -ne $Current.phase -or
        $Previous.round -ne $Current.round -or $Previous.phaseStartMs -ne $Current.phaseStartMs -or
        $Previous.tickBeforeMs - $Previous.phaseStartMs -lt 2000 -or
        $Current.tickBeforeMs -le $Previous.tickAfterMs -or
        $Current.cpuSeconds -lt $Previous.cpuSeconds) { return $null }
    # CPU is read between the two ticks. Use midpoint-to-midpoint elapsed time for this PID,
    # including skipped observations, rather than the duration of the last global sample.
    $elapsedSeconds = (($Current.tickBeforeMs + $Current.tickAfterMs) - ($Previous.tickBeforeMs + $Previous.tickAfterMs)) / 2000.0
    return 100 * ($Current.cpuSeconds - $Previous.cpuSeconds) / $elapsedSeconds
}
function Get-TreeCpuMeasurement($Rows, [int]$QueryGaps, [bool]$StablePhase, [int]$LogicalProcessors) {
    $measured = @($Rows | Where-Object { $null -ne $_.cpuPercentOneCore }).Count
    $complete = $Rows.Count -gt 0 -and $measured -eq $Rows.Count -and $QueryGaps -eq 0 -and $StablePhase
    $oneCore = if ($complete) { Get-Sum $Rows cpuPercentOneCore } else { $null }
    [pscustomobject]@{
        cpuPercentOneCore = $oneCore
        cpuPercentMachine = if ($complete) { $oneCore / $LogicalProcessors } else { $null }
        cpuMeasuredProcesses = $measured; cpuComplete = $complete
    }
}
function Get-GpuMeasurement($MemoryRows, $EngineRows, [int[]]$ProcessIds) {
    $identities = @{}
    foreach ($processIdentity in $ProcessIds) { $identities[$processIdentity] = $true }
    $dedicated = 0.0
    $shared = 0.0
    $committed = 0.0
    $memoryInstances = 0
    foreach ($counter in $MemoryRows) {
        if ($counter.Name -notmatch '^pid_(\d+)_(.+)$' -or -not $identities.ContainsKey([int]$Matches[1])) { continue }
        $dedicated += [double]$counter.DedicatedUsage
        $shared += [double]$counter.SharedUsage
        $committed += [double]$counter.TotalCommitted
        ++$memoryInstances
    }
    $engines = @{}
    foreach ($counter in $EngineRows) {
        if ($counter.Name -notmatch '^pid_(\d+)_(.+)$' -or -not $identities.ContainsKey([int]$Matches[1])) { continue }
        # The remaining name identifies physical adapter + engine. Combine the
        # sampled process tree on that engine, then report its busiest engine.
        $engineIdentity = $Matches[2]
        if (-not $engines.ContainsKey($engineIdentity)) { $engines[$engineIdentity] = 0.0 }
        $engines[$engineIdentity] += [double]$counter.UtilizationPercentage
    }
    $busiest = 0.0
    foreach ($utilization in $engines.Values) { $busiest = [Math]::Max($busiest, $utilization) }
    [pscustomobject]@{
        dedicatedBytes = $dedicated; sharedBytes = $shared; committedBytes = $committed
        busiestEnginePercent = $busiest
        memoryInstances = $memoryInstances; engineInstances = $engines.Count
    }
}
function Get-DxgiValidation($Records,[int]$ProbePid,[int]$Rounds,[int]$CooldownSeconds,[bool]$Required) {
    $expected=@([pscustomobject]@{phase='initial_disabled';round=0})
    foreach($number in 1..$Rounds) {
        $expected+=@([pscustomobject]@{phase='components_enabled';round=$number},
                     [pscustomobject]@{phase='component_disabled';round=$number})
    }
    if($CooldownSeconds -gt 0){$expected+=[pscustomobject]@{phase='cooldown';round=$Rounds}}
    $errors=[Collections.Generic.List[string]]::new()
    $valid=[Collections.Generic.List[object]]::new()
    $seen=@{}
    if($Required) {
        foreach($record in @($Records)) {
            $fields=@('schemaVersion','phase','round','pid','complete','source','adapterIndex','nodeIndex',
                'adapterLuid','localHresult','nonLocalHresult','localCurrentUsageBytes','nonLocalCurrentUsageBytes')
            if($null -eq $record -or @($fields|Where-Object {$null -eq $record.PSObject.Properties[$_]}).Count) {
                $errors.Add('DXGI snapshot has missing required fields.');continue
            }
            $key="$($record.phase)/$($record.round)"
            if($seen.ContainsKey($key)){$errors.Add("Duplicate DXGI phase: $key")}
            $seen[$key]=$true
            if(@($expected|Where-Object {$_.phase -eq $record.phase -and $_.round -eq $record.round}).Count -ne 1) {
                $errors.Add("Unexpected DXGI phase: $key");continue
            }
            $numeric=$true
            foreach($value in @($record.localCurrentUsageBytes,$record.nonLocalCurrentUsageBytes)) {
                if($null -eq $value -or $value -is [bool] -or $value -isnot [ValueType] -or
                    [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value) -or [double]$value -lt 0) {$numeric=$false}
            }
            if($record.schemaVersion -ne 1 -or $record.pid -ne $ProbePid -or -not $record.complete -or
                $record.source -ne 'IDXGIAdapter3::QueryVideoMemoryInfo' -or
                $record.adapterIndex -ne 0 -or $record.nodeIndex -ne 0 -or
                [string]::IsNullOrWhiteSpace([string]$record.adapterLuid) -or
                $null -eq $record.localHresult -or $record.localHresult -ne 0 -or
                $null -eq $record.nonLocalHresult -or $record.nonLocalHresult -ne 0 -or -not $numeric) {
                $errors.Add("Incomplete or invalid DXGI query: $key");continue
            }
            $valid.Add([pscustomobject]@{
                phase=$record.phase;round=$record.round;adapterLuid=$record.adapterLuid
                localCurrentUsageMiB=$record.localCurrentUsageBytes/1MB
                nonLocalCurrentUsageMiB=$record.nonLocalCurrentUsageBytes/1MB
            })
        }
        foreach($phase in $expected) {
            if(-not $seen.ContainsKey("$($phase.phase)/$($phase.round)")){$errors.Add("Missing DXGI phase: $($phase.phase)/$($phase.round)")}
        }
        foreach($record in @($valid|Where-Object phase -eq 'components_enabled')) {
            if($record.localCurrentUsageMiB -eq 0 -and $record.nonLocalCurrentUsageMiB -eq 0) {
                $errors.Add("No populated DXGI enabled observation for round $($record.round).")
            }
        }
        if(@($valid|Select-Object -ExpandProperty adapterLuid -Unique).Count -gt 1) {
            $errors.Add('DXGI material adapter identity changed during this run.')
        }
    }
    $retained=@($valid|Where-Object {$_.phase -ne 'components_enabled' -and
        ($_.localCurrentUsageMiB -ne 0 -or $_.nonLocalCurrentUsageMiB -ne 0)})
    [pscustomobject]@{
        required=$Required;source='IDXGIAdapter3::QueryVideoMemoryInfo'
        coverage='probe process, material adapter index 0, node 0; no child processes or cross-adapter sum'
        snapshotCount=@($Records).Count;expectedSnapshotCount=$(if($Required){$expected.Count}else{0})
        incomplete=$errors.Count -gt 0;lifecycleFailed=$retained.Count -gt 0;errors=@($errors)
        retainedInactiveSnapshots=$retained;phaseSnapshots=@($valid)
        phaseSummary=@($valid|Group-Object phase|ForEach-Object {
            [pscustomobject]@{phase=$_.Name;snapshots=$_.Count
                localCurrentUsageMiBMin=($_.Group.localCurrentUsageMiB|Measure-Object -Minimum).Minimum
                localCurrentUsageMiBMax=($_.Group.localCurrentUsageMiB|Measure-Object -Maximum).Maximum
                nonLocalCurrentUsageMiBMax=($_.Group.nonLocalCurrentUsageMiB|Measure-Object -Maximum).Maximum}
        })
    }
}
function Get-JobResourceValidation($Records,[int]$ProbePid,[int]$Rounds,[int]$CooldownSeconds,
    [bool]$Required,[bool]$RequireChildren,[bool]$ProductMode=$false) {
    function Test-JobInteger($Value) {
        if($null -eq $Value -or $Value -is [bool] -or $Value -isnot [ValueType]){return $false}
        try {
            $number=[decimal]$Value
            return $number -ge 0 -and $number -le [decimal][UInt64]::MaxValue -and
                [decimal]::Truncate($number) -eq $number
        } catch {return $false}
    }
    $expected=@([pscustomobject]@{phase='initial_disabled';round=0})
    foreach($number in 1..$Rounds) {
        if($ProductMode) {
            $expected+=@([pscustomobject]@{phase='components_only_before';round=$number},
                [pscustomobject]@{phase='manager_open';round=$number},
                [pscustomobject]@{phase='manager_minimized';round=$number},
                [pscustomobject]@{phase='manager_restored';round=$number},
                [pscustomobject]@{phase='components_only_after_close';round=$number},
                [pscustomobject]@{phase='component_disabled';round=$number})
        } else {
            $expected+=@([pscustomobject]@{phase='components_enabled';round=$number},
                [pscustomobject]@{phase='component_disabled';round=$number})
        }
    }
    if($CooldownSeconds -gt 0){$expected+=[pscustomobject]@{phase='cooldown';round=$Rounds}}
    $errors=[Collections.Generic.List[string]]::new()
    $valid=[Collections.Generic.List[object]]::new()
    $roundChildren=[Collections.Generic.List[object]]::new()
    $seen=@{}
    $previous=$null
    if($Required) {
        $position=0
        foreach($record in @($Records)) {
            $fields=@('schemaVersion','phase','round','pid','tickMs','source','coverage','complete',
                'timeError','memoryError','limitFlags','totalProcesses','activeProcesses','terminatedByLimit',
                'kernelTime100ns','userTime100ns','peakProcessCommitBytes','peakJobCommitBytes')
            if($null -eq $record -or @($fields|Where-Object {$null -eq $record.PSObject.Properties[$_]}).Count) {
                $errors.Add('Job snapshot has missing required fields.');++$position;continue
            }
            $key="$($record.phase)/$($record.round)"
            if($seen.ContainsKey($key)){$errors.Add("Duplicate Job phase: $key")}
            $seen[$key]=$true
            if($position -ge $expected.Count -or $expected[$position].phase -ne $record.phase -or
                $expected[$position].round -ne $record.round) {
                $errors.Add("Unexpected or reordered Job phase: $key")
            }
            ++$position
            $numbers=@('schemaVersion','round','pid','tickMs','timeError','memoryError','limitFlags',
                'totalProcesses','activeProcesses','terminatedByLimit','kernelTime100ns','userTime100ns',
                'peakProcessCommitBytes','peakJobCommitBytes')
            $numeric=@($numbers|Where-Object {-not (Test-JobInteger $record.$_)}).Count -eq 0
            if(-not $numeric -or $record.schemaVersion -ne 1 -or $record.pid -ne $ProbePid -or
                $record.complete -isnot [bool] -or -not $record.complete -or
                $record.source -cne 'QueryInformationJobObject' -or
                $record.coverage -cne 'probe_and_all_future_descendants_including_nested_icon_jobs' -or
                $record.timeError -ne 0 -or $record.memoryError -ne 0 -or $record.limitFlags -ne 0 -or
                $record.totalProcesses -lt 1 -or $record.activeProcesses -lt 1 -or
                $record.activeProcesses -gt $record.totalProcesses -or
                $record.peakProcessCommitBytes -le 0 -or $record.peakJobCommitBytes -le 0) {
                $errors.Add("Incomplete or invalid Job query: $key");continue
            }
            if($null -ne $previous) {
                if($record.tickMs -le $previous.tickMs -or
                    $record.kernelTime100ns -lt $previous.kernelTime100ns -or
                    $record.userTime100ns -lt $previous.userTime100ns -or
                    $record.totalProcesses -lt $previous.totalProcesses) {
                    $errors.Add("Job timestamp or cumulative counter regressed: $key")
                }
                if($record.phase -eq 'components_enabled' -and
                    ([decimal]$record.kernelTime100ns+[decimal]$record.userTime100ns) -le
                    ([decimal]$previous.kernelTime100ns+[decimal]$previous.userTime100ns)) {
                    $errors.Add("Job enabled interval has no measured CPU progress: $key")
                }
            }
            $valid.Add($record)
            $previous=$record
        }
        foreach($phase in $expected) {
            if(-not $seen.ContainsKey("$($phase.phase)/$($phase.round)")) {
                $errors.Add("Missing Job phase: $($phase.phase)/$($phase.round)")
            }
        }
        if(@($Records).Count -ne $expected.Count){$errors.Add('Unexpected Job snapshot count.')}
        $initial=@($valid|Where-Object phase -eq 'initial_disabled')
        if($initial.Count -eq 1 -and $initial[0].totalProcesses -ne 1) {
            $errors.Add('Initial accounting Job must contain only its assigned probe.')
        }
        foreach($number in 1..$Rounds) {
            $before=@($valid|Where-Object {
                ($number -eq 1 -and $_.phase -eq 'initial_disabled') -or
                ($number -gt 1 -and $_.phase -eq 'component_disabled' -and $_.round -eq $number-1)
            })
            $enabledPhase=if($ProductMode){'components_only_before'}else{'components_enabled'}
            $enabled=@($valid|Where-Object {$_.phase -eq $enabledPhase -and $_.round -eq $number})
            if($before.Count -eq 1 -and $enabled.Count -eq 1) {
                $newProcesses=[decimal]$enabled[0].totalProcesses-[decimal]$before[0].totalProcesses
                $roundChildren.Add([pscustomobject]@{round=$number;newProcesses=$newProcesses})
                if($RequireChildren -and $newProcesses -lt 1) {
                    $errors.Add("Organizer did not create a Job-accounted child in round $number.")
                }
            }
        }
    }
    $activePhases = if ($ProductMode) {
        @('components_only_before','manager_open','manager_minimized','manager_restored')
    } else { @('components_enabled') }
    $retained=@($valid|Where-Object {
        ($_.phase -notin $activePhases -and $_.activeProcesses -ne 1) -or $_.terminatedByLimit -ne 0
    })
    $last=$valid|Select-Object -Last 1
    [pscustomobject]@{
        required=$Required;childrenRequired=$RequireChildren;source='QueryInformationJobObject'
        coverage='probe and all descendants created after assignment, including nested icon Jobs; excludes any earlier console host'
        snapshotCount=@($Records).Count;expectedSnapshotCount=$(if($Required){$expected.Count}else{0})
        incomplete=$errors.Count -gt 0;lifecycleFailed=$retained.Count -gt 0;errors=@($errors)
        retainedInactiveSnapshots=$retained;phaseSnapshots=@($valid);roundChildProcesses=@($roundChildren)
        finalTotalProcesses=$(if($null -ne $last){$last.totalProcesses}else{$null})
        finalActiveProcesses=$(if($null -ne $last){$last.activeProcesses}else{$null})
        cumulativeCpuSeconds=$(if($null -ne $last){([decimal]$last.kernelTime100ns+[decimal]$last.userTime100ns)/10000000}else{$null})
        peakProcessCommitBytes=$(if($valid.Count){($valid.peakProcessCommitBytes|Measure-Object -Maximum).Maximum}else{$null})
        peakJobCommitBytes=$(if($valid.Count){($valid.peakJobCommitBytes|Measure-Object -Maximum).Maximum}else{$null})
        limitations=@(
            'Kernel Job CPU is cumulative and includes completed descendants and diagnostic overhead; it is not the poll-based steady CPU mean.'
            'Peak Job/process memory values are committed bytes, not private working set, and cannot be subtracted to derive per-helper peaks.'
            'Job aggregate accounting establishes transient descendant coverage without individual helper PID lifetimes or private-working-set samples.'
        )
    }
}
function Get-WidgetDataValidation($Records,[string]$Mode,[int]$Rounds) {
    $required=$Mode -in @('weather-only','almanac-only','performance-only','organizer-only','widgets-combined')
    $weather=$Mode -in @('weather-only','widgets-combined')
    $performance=$Mode -in @('performance-only','widgets-combined')
    $errors=[Collections.Generic.List[string]]::new()
    if($required) {
        foreach($number in 1..$Rounds) {
            $matching=@($Records|Where-Object {$null -ne $_.PSObject.Properties['round'] -and $_.round -eq $number})
            if($matching.Count -ne 1){$errors.Add("Missing or duplicate business-data round: $number");continue}
            $record=$matching[0]
            $fields=@('weatherEnabled','weatherAvailable','weatherError','weatherLoading','performanceAvailable')
            if(@($fields|Where-Object {$null -eq $record.PSObject.Properties[$_]}).Count) {
                $errors.Add("Missing business-data fields in round $number");continue
            }
            if($record.weatherEnabled -ne $weather){$errors.Add("Wrong enabled weather state in round $number")}
            if($weather -and (-not $record.weatherAvailable -or $record.weatherError -ne 0 -or $record.weatherLoading)) {
                $errors.Add("Real weather is unavailable or not settled in round $number")
            }
            if($performance -and -not $record.performanceAvailable){$errors.Add("Actual performance sample is unavailable in round $number")}
        }
        if(@($Records).Count -ne $Rounds){$errors.Add('Unexpected business-data record count.')}
    }
    [pscustomobject]@{required=$required;weatherRequired=$weather;performanceRequired=$performance
        incomplete=$errors.Count -gt 0;errors=@($errors);records=@($Records)}
}
function Get-OwnedHelperTraceRecords($Events,[int]$ProbePid,[long]$RootStartFileTime) {
    foreach($event in $Events) {
        $value=$event.SourceEventArgs.NewEvent
        if($value.ProcessName -ine 'LumeIconHost.exe' -or $value.ParentProcessID -ne $ProbePid -or
            [long]$value.TIME_CREATED -lt $RootStartFileTime){continue}
        $kind=if($event.SourceIdentifier -match '_start$'){'start'}elseif($event.SourceIdentifier -match '_stop$'){'stop'}else{continue}
        [pscustomobject]@{
            timestampUtc=[DateTime]::FromFileTimeUtc([long]$value.TIME_CREATED).ToString('o')
            timeCreatedFileTime=[long]$value.TIME_CREATED;event=$kind
            pid=[int]$value.ProcessID;parentPid=[int]$value.ParentProcessID;name=$value.ProcessName
            exitStatus=$(if($kind -eq 'stop'){$value.ExitStatus}else{$null})
        }
    }
}
function Receive-HelperTrace {
    foreach($subscription in $helperSubscriptions) {
        $events=@(Get-Event -SourceIdentifier $subscription -ErrorAction SilentlyContinue)
        foreach($record in @(Get-OwnedHelperTraceRecords $events $root.Id $rootStart.ToFileTimeUtc())) {
            $helperTraceRecords.Add($record)
        }
        foreach($event in $events){Remove-Event -EventIdentifier $event.EventIdentifier}
    }
}

try {
    # Initialize the Windows performance provider before the timed probe phases begin.
    $null = Get-CimInstance Win32_PerfRawData_PerfProc_Process -Property IDProcess, WorkingSetPrivate
    if($helperTraceRequired) {
        # Capture helpers shorter than one polling interval without extending
        # their lifetimes or modifying the production extraction workload.
        $tracePrefix='LumeWidget_'+[guid]::NewGuid().ToString('N')
        foreach($kind in @('start','stop')) {
            $className=if($kind -eq 'start'){'Win32_ProcessStartTrace'}else{'Win32_ProcessStopTrace'}
            $source=$tracePrefix+'_'+$kind
            try {
                $null=Register-CimIndicationEvent -Query "SELECT * FROM $className WHERE ProcessName='LumeIconHost.exe'" -SourceIdentifier $source -ErrorAction Stop
                $helperSubscriptions+=$source
            } catch {$helperTraceError=$_.Exception.Message}
        }
    }
    $root = Start-Process -FilePath $probePath -ArgumentList @(('"' + $phasePath + '"'), $Rounds, $DwellMilliseconds, $Mode, $CooldownSeconds) `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'probe.stdout.log') `
        -RedirectStandardError (Join-Path $OutputDirectory 'probe.stderr.log')
    $root.Refresh()
    $rootId = $root.Id
    $rootStart = $root.StartTime.ToUniversalTime()
    $known[$rootId] = $rootStart
    while ($postExitSamples -lt 5) {
        if (([DateTime]::UtcNow - $started).TotalSeconds -gt $timeoutSeconds) { throw 'Resource probe exceeded its bounded timeout.' }
        $phaseBefore = Get-Phase
        if($helperSubscriptions.Count){Receive-HelperTrace}
        $allProcesses = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, Name, CreationDate)
        $alive = @{}
        foreach ($entry in $allProcesses) { $alive[[int]$entry.ProcessId] = $entry }
        # Retain descendant identities after their parent exits; guard against PID reuse.
        $added = $true
        while ($added) {
            $added = $false
            foreach ($entry in $allProcesses) {
                $entryId = [int]$entry.ProcessId
                $parentId = [int]$entry.ParentProcessId
                if ($known.ContainsKey($entryId) -and [Math]::Abs(($entry.CreationDate.ToUniversalTime() - $known[$entryId]).TotalMilliseconds) -gt 10) {
                    $known.Remove($entryId)
                    $previousCpu.Remove($entryId)
                }
                if ($known.ContainsKey($entryId) -or -not $known.ContainsKey($parentId)) { continue }
                $creation = $entry.CreationDate.ToUniversalTime()
                if ($creation -lt $known[$parentId]) { continue }
                if ($alive.ContainsKey($parentId) -and [Math]::Abs(($alive[$parentId].CreationDate.ToUniversalTime() - $known[$parentId]).TotalMilliseconds) -gt 10) { continue }
                $known[$entryId] = $creation
                $added = $true
            }
        }
        $privateWorkingSets = @{}
        foreach ($counter in Get-CimInstance Win32_PerfRawData_PerfProc_Process -Property IDProcess, WorkingSetPrivate) {
            $privateWorkingSets[[int]$counter.IDProcess] = [long]$counter.WorkingSetPrivate
        }
        $sampleTick = [ClockAcceptance.NativeCounters]::GetTickCount64()
        $rows = [Collections.Generic.List[object]]::new()
        $queryGaps = 0
        foreach ($entryId in @($known.Keys)) {
            if (-not $alive.ContainsKey($entryId)) { continue }
            $entry = $alive[$entryId]
            if ([Math]::Abs(($entry.CreationDate.ToUniversalTime() - $known[$entryId]).TotalMilliseconds) -gt 10) { continue }
            try {
                $process = Get-Process -Id $entryId -ErrorAction Stop
                if ([Math]::Abs(($process.StartTime.ToUniversalTime() - $known[$entryId]).TotalMilliseconds) -gt 10) {
                    $process.Dispose()
                    throw 'Process identity changed between discovery and measurement.'
                }
                $cpuPhaseBefore = Get-Phase
                $cpuTickBefore = [ClockAcceptance.NativeCounters]::GetTickCount64()
                $cpuSeconds = $process.TotalProcessorTime.TotalSeconds
                $cpuTickAfter = [ClockAcceptance.NativeCounters]::GetTickCount64()
                $cpuPhaseAfter = Get-Phase
                $cpuObservation = [pscustomobject]@{
                    tickBeforeMs = $cpuTickBefore; tickAfterMs = $cpuTickAfter; cpuSeconds = $cpuSeconds
                    phase = $cpuPhaseBefore.phase; round = $cpuPhaseBefore.round; phaseStartMs = [long]$cpuPhaseBefore.tickMs
                    stablePhase = ($cpuPhaseBefore.phase -eq $cpuPhaseAfter.phase -and $cpuPhaseBefore.round -eq $cpuPhaseAfter.round -and $cpuPhaseBefore.tickMs -eq $cpuPhaseAfter.tickMs)
                }
                $cpuPrevious = if ($previousCpu.ContainsKey($entryId)) { $previousCpu[$entryId] } else { $null }
                $cpuPercent = Get-CpuIntervalPercent $cpuPrevious $cpuObservation
                $privateWs = if ($privateWorkingSets.ContainsKey($entryId)) { $privateWorkingSets[$entryId] } else { $null }
                $rows.Add([pscustomobject]@{
                    timestampUtc = [DateTime]::UtcNow.ToString('o'); sample = $samples.Count; phase = $cpuObservation.phase; round = $cpuObservation.round
                    pid = $entryId; parentPid = [int]$entry.ParentProcessId; name = $entry.Name; isRoot = ($entryId -eq $rootId)
                    creationUtc = $known[$entryId].ToString('o'); privateWorkingSetBytes = $privateWs; privateBytes = $process.PrivateMemorySize64
                    workingSetBytes = $process.WorkingSet64; cpuTotalSeconds = $cpuSeconds; cpuPercentOneCore = $cpuPercent
                    cpuReadBeforeTickMs = $cpuTickBefore; cpuReadAfterTickMs = $cpuTickAfter
                    cpuPhaseStartTickMs = $cpuObservation.phaseStartMs; cpuObservationStablePhase = $cpuObservation.stablePhase
                    cpuPreviousReadBeforeTickMs = if ($null -ne $cpuPrevious) { $cpuPrevious.tickBeforeMs } else { $null }
                    cpuPreviousReadAfterTickMs = if ($null -ne $cpuPrevious) { $cpuPrevious.tickAfterMs } else { $null }
                    handles = $process.HandleCount; threads = $process.Threads.Count
                    gdiObjects = [ClockAcceptance.NativeCounters]::GetGuiResources($process.Handle, 0)
                    userObjects = [ClockAcceptance.NativeCounters]::GetGuiResources($process.Handle, 1)
                })
                $previousCpu[$entryId] = $cpuObservation
                $process.Dispose()
            } catch {
                # A child can exit between WMI and Get-Process; record the gap instead of inventing zero values.
                ++$queryGaps
                Add-Content -LiteralPath (Join-Path $OutputDirectory 'sampling-gaps.log') -Value "$(Get-Date -Format o) sample=$($samples.Count) phase=$($phaseBefore.phase) round=$($phaseBefore.round) pid=$entryId $($_.Exception.Message)"
            }
        }
        $gpu = $null
        $gpuError = $null
        if ($CollectGpu) {
            try {
                $gpuMemory = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -Property Name,DedicatedUsage,SharedUsage,TotalCommitted -ErrorAction Stop)
                $gpuEngines = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -Property Name,UtilizationPercentage -ErrorAction Stop)
                $processIds=@($rows|ForEach-Object pid)
                $gpu = Get-GpuMeasurement $gpuMemory $gpuEngines $processIds
                foreach($counter in $gpuMemory) {
                    if($counter.Name -match '^pid_(\d+)_' -and [int]$Matches[1] -in $processIds) {
                        $gpuMemorySamples.Add([pscustomobject]@{
                            timestampUtc=[DateTime]::UtcNow.ToString('o');sample=$samples.Count
                            phase=$phaseBefore.phase;round=$phaseBefore.round;pid=[int]$Matches[1]
                            instance=$counter.Name;DedicatedUsage=$counter.DedicatedUsage
                            SharedUsage=$counter.SharedUsage;TotalCommitted=$counter.TotalCommitted
                        })
                    }
                }
            } catch { $gpuError = $_.Exception.Message }
        }
        $phaseAfter = Get-Phase
        $stable = $phaseBefore.phase -eq $phaseAfter.phase -and $phaseBefore.round -eq $phaseAfter.round -and $phaseBefore.tickMs -eq $phaseAfter.tickMs
        $treeCpu = Get-TreeCpuMeasurement $rows $queryGaps $stable $logicalProcessors
        $rootRows = @($rows | Where-Object isRoot)
        $privateWsMeasured = @($rows | Where-Object { $null -ne $_.privateWorkingSetBytes }).Count
        $rootPrivateWsMeasured = @($rootRows | Where-Object { $null -ne $_.privateWorkingSetBytes }).Count
        $samples.Add([pscustomobject]@{
            timestampUtc = [DateTime]::UtcNow.ToString('o'); sample = $samples.Count; phase = $phaseBefore.phase; round = $phaseBefore.round
            stablePhase = $stable; phaseAgeMs = ($sampleTick - [long]$phaseBefore.tickMs); processes = $rows.Count
            children = @($rows | Where-Object { -not $_.isRoot }).Count
            testConsoleChildren = @($rows | Where-Object { -not $_.isRoot -and $_.name -eq 'conhost.exe' }).Count
            webviewChildren = @($rows | Where-Object name -eq 'msedgewebview2.exe').Count
            nonConsoleChildren = @($rows | Where-Object { -not $_.isRoot -and $_.name -ne 'conhost.exe' }).Count
            processQueryGaps = $queryGaps
            privateWorkingSetMiB = if ($privateWsMeasured -eq $rows.Count) { (Get-Sum $rows privateWorkingSetBytes) / 1MB } else { $null }
            privateBytesMiB = (Get-Sum $rows privateBytes) / 1MB
            rootPrivateWorkingSetMiB = if ($rootPrivateWsMeasured -eq $rootRows.Count) { (Get-Sum $rootRows privateWorkingSetBytes) / 1MB } else { $null }
            rootPrivateBytesMiB = (Get-Sum $rootRows privateBytes) / 1MB
            cpuPercentOneCore = $treeCpu.cpuPercentOneCore
            cpuPercentMachine = $treeCpu.cpuPercentMachine
            cpuMeasuredProcesses = $treeCpu.cpuMeasuredProcesses; cpuComplete = $treeCpu.cpuComplete
            privateWsMeasuredProcesses = $privateWsMeasured
            handles = Get-Sum $rows handles; rootHandles = Get-Sum $rootRows handles
            threads = Get-Sum $rows threads; rootThreads = Get-Sum $rootRows threads
            gdiObjects = Get-Sum $rows gdiObjects; rootGdiObjects = Get-Sum $rootRows gdiObjects
            userObjects = Get-Sum $rows userObjects; rootUserObjects = Get-Sum $rootRows userObjects
            gpuAvailable = $null -ne $gpu; gpuError = $gpuError
            gpuDedicatedMiB = if ($null -ne $gpu) { $gpu.dedicatedBytes / 1MB } else { $null }
            gpuSharedMiB = if ($null -ne $gpu) { $gpu.sharedBytes / 1MB } else { $null }
            wmiGpuProcessDedicatedMiB = if ($null -ne $gpu) { $gpu.dedicatedBytes / 1MB } else { $null }
            wmiGpuProcessSharedMiB = if ($null -ne $gpu) { $gpu.sharedBytes / 1MB } else { $null }
            wmiGpuProcessTotalCommittedMiB = if ($null -ne $gpu) { $gpu.committedBytes / 1MB } else { $null }
            gpuBusiestEnginePercent = if ($null -ne $gpu) { $gpu.busiestEnginePercent } else { $null }
            gpuMemoryInstances = if ($null -ne $gpu) { $gpu.memoryInstances } else { $null }
            gpuEngineInstances = if ($null -ne $gpu) { $gpu.engineInstances } else { $null }
        })
        foreach ($row in $rows) { $processSamples.Add($row) }
        $root.Refresh()
        if ($root.HasExited) { $root.WaitForExit(); ++$postExitSamples; $exitCode = $root.ExitCode; $survivors = @($rows) }
        Start-Sleep -Milliseconds $SampleMilliseconds
    }
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($null -ne $root) {
        $root.Refresh()
        if (-not $root.HasExited -and $null -ne $rootStart -and $root.StartTime.ToUniversalTime() -eq $rootStart) {
            # Only this script's own probe can be stopped; no process-name based termination.
            $root.Kill()
            $root.WaitForExit(5000) | Out-Null
        }
        if($null -ne $rootStart -and $helperSubscriptions.Count){Receive-HelperTrace}
    }
    foreach($subscription in $helperSubscriptions) {
        Unregister-Event -SourceIdentifier $subscription -ErrorAction SilentlyContinue
        Get-Event -SourceIdentifier $subscription -ErrorAction SilentlyContinue|Remove-Event
    }
    $samples | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'samples.csv') -NoTypeInformation -Encoding utf8
    $processSamples | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'process-samples.csv') -NoTypeInformation -Encoding utf8
    $gpuMemorySamples | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'gpu-memory-wmi-raw.csv') -NoTypeInformation -Encoding utf8
    $helperTraceRecords|Sort-Object timeCreatedFileTime|Export-Csv -LiteralPath (Join-Path $OutputDirectory 'helper-process-events.csv') -NoTypeInformation -Encoding utf8
}

$steady = @($samples | Where-Object { $_.stablePhase -and $_.phase -ne 'diagnostic' -and $_.phaseAgeMs -ge 2000 -and $_.processes -gt 0 })
$phaseSummary = @($steady | Group-Object phase | ForEach-Object {
    $group = $_.Group
    $cpuGroup = @($group | Where-Object cpuComplete)
    $gpuGroup = @($group | Where-Object gpuAvailable)
    [pscustomobject]@{
        phase = $_.Name; samples = $group.Count
        privateWorkingSetMiBMin = ($group.privateWorkingSetMiB | Measure-Object -Minimum).Minimum
        privateWorkingSetMiBMax = ($group.privateWorkingSetMiB | Measure-Object -Maximum).Maximum
        privateBytesMiBMin = ($group.privateBytesMiB | Measure-Object -Minimum).Minimum
        privateBytesMiBMax = ($group.privateBytesMiB | Measure-Object -Maximum).Maximum
        cpuCompleteSamples = $cpuGroup.Count
        cpuIncompleteSamples = $group.Count - $cpuGroup.Count
        cpuPercentOneCoreMean = if ($cpuGroup.Count -gt 0) { ($cpuGroup.cpuPercentOneCore | Measure-Object -Average).Average } else { $null }
        cpuPercentMachineMean = if ($cpuGroup.Count -gt 0) { ($cpuGroup.cpuPercentMachine | Measure-Object -Average).Average } else { $null }
        childrenMin = ($group.children | Measure-Object -Minimum).Minimum
        childrenMax = ($group.children | Measure-Object -Maximum).Maximum
        gpuMeasuredSamples = $gpuGroup.Count
        gpuDedicatedMiBMax = if ($gpuGroup.Count) { ($gpuGroup.gpuDedicatedMiB | Measure-Object -Maximum).Maximum } else { $null }
        gpuSharedMiBMax = if ($gpuGroup.Count) { ($gpuGroup.gpuSharedMiB | Measure-Object -Maximum).Maximum } else { $null }
        wmiGpuProcessDedicatedMiBMax = if ($gpuGroup.Count) { ($gpuGroup.wmiGpuProcessDedicatedMiB | Measure-Object -Maximum).Maximum } else { $null }
        wmiGpuProcessSharedMiBMax = if ($gpuGroup.Count) { ($gpuGroup.wmiGpuProcessSharedMiB | Measure-Object -Maximum).Maximum } else { $null }
        wmiGpuProcessTotalCommittedMiBMax = if ($gpuGroup.Count) { ($gpuGroup.wmiGpuProcessTotalCommittedMiB | Measure-Object -Maximum).Maximum } else { $null }
        gpuBusiestEnginePercentMax = if ($gpuGroup.Count) { ($gpuGroup.gpuBusiestEnginePercent | Measure-Object -Maximum).Maximum } else { $null }
    }
})
$roundEndpoints = @($steady | Where-Object { $_.phase -in @('clock_only_after_close', 'components_only_after_close', 'component_disabled') } |
    Group-Object round, phase | ForEach-Object { $_.Group | Select-Object -Last 1 })
$isSmoke = $DwellMilliseconds -lt 4000
$requiredPhases = @('clock_only_before', 'manager_open', 'manager_minimized', 'clock_only_after_close', 'component_disabled')
if ($Mode -eq 'clock-only') { $requiredPhases = @('clock_only_before', 'component_disabled') }
if ($Mode -eq 'manager-only') { $requiredPhases = @('manager_open', 'manager_minimized', 'clock_only_after_close', 'component_disabled') }
if ($widgetMode) { $requiredPhases = @('components_enabled', 'component_disabled') }
if ($productMode) { $requiredPhases = @('components_only_before', 'manager_open', 'manager_minimized', 'manager_restored', 'components_only_after_close', 'component_disabled') }
$coverage = @(foreach ($roundNumber in 1..$Rounds) {
    foreach ($requiredPhase in $requiredPhases) {
        $matching = @($steady | Where-Object { $_.round -eq $roundNumber -and $_.phase -eq $requiredPhase })
        [pscustomobject]@{
            round = $roundNumber; phase = $requiredPhase; steadySamples = $matching.Count
            cpuCompleteSamples = @($matching | Where-Object cpuComplete).Count
            cpuIncompleteSamples = @($matching | Where-Object { -not $_.cpuComplete }).Count
            privateWsIncompleteSamples = @($matching | Where-Object { $_.privateWsMeasuredProcesses -ne $_.processes }).Count
            processQueryGapSamples = @($matching | Where-Object { $_.processQueryGaps -gt 0 }).Count
        }
    }
})
$incompleteCoverage = @($coverage | Where-Object { $_.steadySamples -lt 1 -or $_.privateWsIncompleteSamples -gt 0 -or $_.processQueryGapSamples -gt 0 })
$unreleasedEndpoints = @($roundEndpoints | Where-Object { $_.nonConsoleChildren -gt 0 })
$measurementIncomplete = -not $isSmoke -and $incompleteCoverage.Count -gt 0
$endpointCleanupFailed = -not $isSmoke -and $unreleasedEndpoints.Count -gt 0
$snapshotPath = Join-Path $OutputDirectory 'memory-snapshots.jsonl'
$snapshots = @(if (Test-Path -LiteralPath $snapshotPath) { Get-Content -LiteralPath $snapshotPath | ForEach-Object { $_ | ConvertFrom-Json } } else { })
$expectedSnapshots = 1 + $Rounds * $requiredPhases.Count + [int]($CooldownSeconds -gt 0)
$snapshotIncomplete = $snapshots.Count -ne $expectedSnapshots -or @($snapshots | Where-Object { -not $_.diagnosticsCompleteWithinDeclaredCoverage }).Count -gt 0
$cooldownSamples = @($steady | Where-Object phase -eq 'cooldown')
if (-not $isSmoke -and $CooldownSeconds -ge 4 -and ($cooldownSamples.Count -eq 0 -or @($cooldownSamples | Where-Object { $_.processQueryGaps -gt 0 -or $_.privateWsMeasuredProcesses -ne $_.processes }).Count -gt 0)) { $measurementIncomplete = $true }
$dxgiPath=Join-Path $OutputDirectory 'gpu-snapshots.jsonl'
$dxgiSnapshots=@();$dxgiReadError=$null
if(Test-Path -LiteralPath $dxgiPath) {
    try {$dxgiSnapshots=@(Get-Content -LiteralPath $dxgiPath|ForEach-Object {$_|ConvertFrom-Json})}
    catch {$dxgiReadError=$_.Exception.Message}
}
$probeIdentity=if($root){$root.Id}else{0}
$dxgiValidation=Get-DxgiValidation $dxgiSnapshots $probeIdentity $Rounds $CooldownSeconds ($widgetMode -and $CollectGpu.IsPresent)
$dxgiValidation|Add-Member -NotePropertyName readError -NotePropertyValue $dxgiReadError
$dxgiValidation.phaseSnapshots|Export-Csv -LiteralPath (Join-Path $OutputDirectory 'gpu-dxgi-phases.csv') -NoTypeInformation -Encoding utf8
$jobPath=Join-Path $OutputDirectory 'job-snapshots.jsonl'
$jobSnapshots=@();$jobReadError=$null
if(Test-Path -LiteralPath $jobPath) {
    try {$jobSnapshots=@(Get-Content -LiteralPath $jobPath|ForEach-Object {$_|ConvertFrom-Json})}
    catch {$jobReadError=$_.Exception.Message}
}
$jobResourceValidation=Get-JobResourceValidation $jobSnapshots $probeIdentity $Rounds $CooldownSeconds ($widgetMode -or $productMode) ($helperTraceRequired) $productMode
$jobResourceValidation|Add-Member -NotePropertyName readError -NotePropertyValue $jobReadError
$jobResourceValidation.phaseSnapshots|Export-Csv -LiteralPath (Join-Path $OutputDirectory 'job-accounting-phases.csv') -NoTypeInformation -Encoding utf8
$dataPath=Join-Path $OutputDirectory 'data-status.jsonl'
$dataRecords=@();$dataReadError=$null
if(Test-Path -LiteralPath $dataPath) {
    try {$dataRecords=@(Get-Content -LiteralPath $dataPath|ForEach-Object {$_|ConvertFrom-Json})}
    catch {$dataReadError=$_.Exception.Message}
}
$widgetDataValidation=Get-WidgetDataValidation $dataRecords $Mode $Rounds
$widgetDataValidation|Add-Member -NotePropertyName readError -NotePropertyValue $dataReadError
$helperStarts=@($helperTraceRecords|Where-Object event -eq 'start')
$helperStops=@($helperTraceRecords|Where-Object event -eq 'stop')
$unmatchedHelpers=@($helperStarts|Where-Object {
    $start=$_
    @($helperStops|Where-Object {$_.pid -eq $start.pid -and $_.timeCreatedFileTime -ge $start.timeCreatedFileTime}).Count -ne 1
})
$helperTraceIncomplete=$helperTraceRequired -and ($null -ne $helperTraceError -or $helperSubscriptions.Count -ne 2 -or
    $helperStarts.Count -lt 1 -or $unmatchedHelpers.Count -gt 0 -or $helperStarts.Count -ne $helperStops.Count)
$helperCoverageIncomplete=$helperTraceRequired -and ($jobResourceValidation.incomplete -or
    $jobResourceValidation.lifecycleFailed -or -not $jobResourceValidation.required -or
    -not $jobResourceValidation.childrenRequired)
$materialDll=Join-Path $BuildDirectory 'LumeGlass.dll'
$summary = [ordered]@{
    startedUtc = $started.ToString('o'); finishedUtc = [DateTime]::UtcNow.ToString('o'); probePath = $probePath
    executedScriptSha256 = $scriptHash; probeSha256 = $probeHash
    materialDllSha256 = if(Test-Path -LiteralPath $materialDll){(Get-FileHash -LiteralPath $materialDll -Algorithm SHA256).Hash}else{$null}
    probePid = if ($root) { $root.Id } else { $null }; rounds = $Rounds; dwellMilliseconds = $DwellMilliseconds
    sampleMilliseconds = $SampleMilliseconds; mode = $Mode; cooldownSeconds = $CooldownSeconds
    gpuRequested = $CollectGpu.IsPresent
    gpuMetrics = @{
        dxgi='In-process QueryVideoMemoryInfo CurrentUsage at each diagnostic endpoint; probe PID, material adapter 0/node 0, not a process-tree or cross-adapter sum.'
        wmi='Original GPU Process Memory counters for the observed process tree. DedicatedUsage can accumulate incorrectly and is not a standalone leak gate.'
        legacyAliases='gpuDedicatedMiB and gpuSharedMiB are unchanged aliases of wmiGpuProcessDedicatedMiB and wmiGpuProcessSharedMiB; neither is DXGI.'
    }
    dxgiValidation=$dxgiValidation;widgetDataValidation=$widgetDataValidation;jobResourceValidation=$jobResourceValidation
    organizerHelperTrace=@{required=$helperTraceRequired;source='Win32_ProcessStartTrace/Win32_ProcessStopTrace'
        subscriptions=$helperSubscriptions.Count;error=$helperTraceError;incomplete=$helperTraceIncomplete
        observedStarts=$helperStarts.Count;observedStops=$helperStops.Count;unmatchedStarts=$unmatchedHelpers
        limitation='Trace records establish short-lived helper lifetimes. Poll-based CPU and memory samples may not overlap those lifetimes.'}
    organizerHelperCoverage=@{required=$helperTraceRequired;incomplete=$helperCoverageIncomplete
        method='kernel_job_accounting';wmiLifetimeTraceIncomplete=$helperTraceIncomplete
        counters='Cumulative process-tree CPU and peak committed bytes, including descendants that finish between polling samples.'
        limitation='This is aggregate Job accounting, not individual helper PID lifetimes or transient private-working-set samples.'}
    logicalProcessors = $logicalProcessors; exitCode = $exitCode; failure = $failure
    observedProcessIdentities = $known.Count; postExitSurvivorPids = @($survivors | ForEach-Object pid)
    privateWorkingSetIncompleteSamples = @($samples | Where-Object { $_.privateWsMeasuredProcesses -ne $_.processes }).Count
    privateWorkingSetIncompleteSteadySamples = @($steady | Where-Object { $_.privateWsMeasuredProcesses -ne $_.processes }).Count
    cpuCompleteSteadySamples = @($steady | Where-Object cpuComplete).Count
    cpuIncompleteSteadySamples = @($steady | Where-Object { -not $_.cpuComplete }).Count
    smokeOnly = $isSmoke; measurementIncomplete = $measurementIncomplete; endpointCleanupFailed = $endpointCleanupFailed
    snapshotCount = $snapshots.Count; expectedSnapshotCount = $expectedSnapshots; snapshotIncomplete = $snapshotIncomplete
    requiredPhaseCoverage = $coverage
    phaseSummary = $phaseSummary; roundEndpoints = $roundEndpoints
    limitations = @(
        'Bounded sampling cannot prove absence of long-term leaks.',
        'Memory snapshots inspect the default process heap only; they are not allocation stacks or complete allocator coverage.',
        'Diagnostic phases are excluded; snapshot costs can still affect following warm-up samples.',
        'In manager-only mode clock_only_after_close means manager closed without a native clock.',
        'Private working set is resident private memory; private bytes is committed private memory; they are different metrics.',
        'CPU one-core percent may exceed 100 for a multiprocess tree; machine percent divides by logical processor count.',
        'CPU excludes the first observation of each PID and CPU used by children that start and exit between samples.',
        'CPU intervals require both PID observations to remain in the same phase, round and phase-start identity, with the interval start at least two seconds after phase start; diagnostic intervals and counter rollback are excluded.',
        'CPU uses per-PID read midpoint elapsed time across successful observations; missing observations never become zero CPU, and incomplete process-tree CPU samples are excluded from CPU means without failing memory coverage.',
        'Process-tree discovery uses sampled parent PID plus creation time; very short-lived children may be missed.',
        'The probe accounting-only Job independently retains cumulative CPU and peak committed-memory information for future descendants, including completed nested icon helpers. Any console host created before probe assignment is outside that Job.',
        'Job peaks are committed bytes, not private working set. Cumulative Job CPU includes diagnostic phases and cannot replace the poll-based steady CPU means.',
        'WMI helper lifetime access errors remain recorded even when complete kernel Job accounting proves aggregate transient-descendant coverage.',
        'GDI/USER counters can be zero for processes with no GUI objects; this is not GPU memory measurement.',
        'DXGI local/nonlocal CurrentUsage is distinct from WMI DedicatedUsage/SharedUsage and from CPU Private Commit. Missing or failed DXGI queries remain incomplete, never zero.',
        'Widget GPU acceptance requires complete DXGI endpoint coverage, populated enabled observations, and exactly zero local/nonlocal CurrentUsage at every inactive endpoint. WMI raw counters remain unchanged even when they disagree.',
        'DXGI uses the material renderer adapter index 0/node 0. Other adapters and child processes are outside that API snapshot coverage; raw WMI records retain their process and physical-adapter identities.',
        'Microsoft KB4490156 documents cumulative GPU Process Memory counter errors. An observed API/counter discrepancy does not identify the exact OS or driver defect.',
        'External GDI/USER zero values are not checked for API failure; root GUI-resource conclusions use the complete counters in memory-snapshots.jsonl.',
        'Measurements cover the isolated verification probe and its descendants, not the existing user LumeShell.',
        'No production settings are loaded or saved; WebView profile is test-webview-profile/<probe PID>.',
        'Minimization is exercised by ShowWindow and verifies WM_SIZE handling, not a browser click on the minimize button.',
        'Phase summaries exclude the first two seconds; close endpoints allow additional child-process shutdown time.'
        'Dwell below 4000 ms is smoke-only: it cannot pass formal steady-state coverage or per-round child-cleanup acceptance.'
        'The console probe can create conhost.exe; complete-tree totals include it, root totals exclude it, and endpoint cleanup gates ignore only this named test host.'
        'Only the probe clock and manager HWNDs are disabled to isolate input; rendering and timers stay active. Any unexpected native action invalidates the run.'
    )
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding utf8
$phaseSummary | Format-Table -AutoSize | Out-String | Write-Output
Write-Output "Resource artifacts: $OutputDirectory"
if ($failure) { throw $failure }
if ($exitCode -ne 0) { throw "Probe failed with exit code $exitCode" }
if ($survivors.Count -gt 0) { throw 'Observed test descendants remained alive after the post-exit grace samples; inspect summary.json.' }
if ($measurementIncomplete) { throw 'Required round/phase samples or private-working-set measurements are incomplete; inspect requiredPhaseCoverage.' }
if ($endpointCleanupFailed) { throw 'Observed WebView descendants remained at a per-round closed/disabled endpoint; inspect roundEndpoints.' }
if ($snapshotIncomplete) { throw 'Process memory snapshots are missing or incomplete within declared coverage; inspect memory-snapshots.jsonl.' }
if ($dxgiValidation.incomplete) { throw 'DXGI process GPU measurements are incomplete; inspect dxgiValidation and gpu-snapshots.jsonl.' }
if ($dxgiValidation.lifecycleFailed) { throw 'DXGI CurrentUsage did not return to zero at a widget inactive endpoint; inspect dxgiValidation.' }
if ($jobResourceValidation.incomplete) { throw 'Kernel Job process-tree resource measurements are incomplete; inspect jobResourceValidation and job-snapshots.jsonl.' }
if ($jobResourceValidation.lifecycleFailed) { throw 'Kernel Job still contains a child at a widget inactive endpoint or reports limit termination; inspect jobResourceValidation.' }
if ($widgetDataValidation.incomplete) { throw 'Required real widget business data is missing or invalid; inspect widgetDataValidation and data-status.jsonl.' }
if ($helperCoverageIncomplete) { throw 'Organizer transient-child resource accounting is incomplete; inspect organizerHelperCoverage and jobResourceValidation.' }
