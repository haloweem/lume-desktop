param(
    [Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$RootProcessId,
    [ValidateRange(15,600)][int]$DurationSeconds = 60,
    [ValidateRange(250,10000)][int]$SampleMilliseconds = 2000,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-CpuIntervalPercent($Previous, $Current) {
    if ($null -eq $Previous -or $null -eq $Current -or
        $Previous.identityKey -ne $Current.identityKey -or
        $Current.sampleTimeMs -le $Previous.sampleTimeMs -or
        $Current.cpuSeconds -lt $Previous.cpuSeconds) { return $null }
    $elapsed = ($Current.sampleTimeMs - $Previous.sampleTimeMs) / 1000.0
    if ($elapsed -le 0) { return $null }
    return [Math]::Round(100 * ($Current.cpuSeconds - $Previous.cpuSeconds) / $elapsed, 10)
}

function Get-IdentityKey($Row) {
    if ($null -eq $Row) { return $null }
    if ($null -ne $Row.IdentityKey) { return [string]$Row.IdentityKey }
    return "$($Row.ProcessId)|$($Row.CreationTimeUtc.Ticks)"
}

function Get-ProcessTreeRows([string]$RootIdentityKey, $CurrentRows, $KnownRows) {
    $current = @($CurrentRows)
    $duplicates = @($current | Group-Object { Get-IdentityKey $_ } | Where-Object Count -gt 1)
    if ($duplicates.Count -gt 0) {
        return [pscustomobject]@{ status='identity_ambiguous'; rootPresent=$false; rows=@() }
    }
    $root = @($current | Where-Object { (Get-IdentityKey $_) -eq $RootIdentityKey })
    if ($root.Count -ne 1) {
        return [pscustomobject]@{ status='root_missing'; rootPresent=$false; rows=@() }
    }
    $byKey = @{}
    foreach ($row in $current) { $byKey[(Get-IdentityKey $row)] = $row }
    $selected = @{}
    $selected[$RootIdentityKey] = $true
    $changed = $true
    while ($changed) {
        $changed = $false
        foreach ($row in $current) {
            $key = Get-IdentityKey $row
            if ($selected.ContainsKey($key)) { continue }
            $parentKey = $null
            if ($null -ne $row.PSObject.Properties['ParentIdentityKey'] -and $null -ne $row.ParentIdentityKey) {
                $parentKey = [string]$row.ParentIdentityKey
            }
            else {
                $parent = @($current | Where-Object { $_.ProcessId -eq $row.ParentProcessId })
                if ($parent.Count -eq 1) { $parentKey = Get-IdentityKey $parent[0] }
            }
            if ($null -ne $parentKey -and $selected.ContainsKey($parentKey)) {
                $selected[$key] = $true
                $changed = $true
            }
        }
    }
    [pscustomobject]@{
        status='ok'; rootPresent=$true
        rows=@($current | Where-Object { $selected.ContainsKey((Get-IdentityKey $_)) })
    }
}

function Get-ProcessGroup($Row, [string]$RootIdentityKey) {
    if ((Get-IdentityKey $Row) -eq $RootIdentityKey) { return 'shell' }
    switch -Regex ([string]$Row.Name) {
        '(?i)^msedgewebview2\.exe$' { return 'webview' }
        '(?i)^mpv\.exe$' { return 'wallpaper' }
        default { return 'other' }
    }
}

function Get-NullableSum($Rows, [string]$Property) {
    $values = @($Rows | ForEach-Object { $_.$Property })
    if ($values.Count -eq 0 -or @($values | Where-Object { $null -eq $_ }).Count -gt 0) { return $null }
    return (($values | Measure-Object -Sum).Sum)
}

function Get-GroupedResourceAggregates($Rows) {
    $result = [ordered]@{}
    $groups = @('shell','webview','wallpaper','other','total','nonWallpaper')
    foreach ($group in $groups) {
        $members = @(if ($group -eq 'total') { @($Rows) } elseif ($group -eq 'nonWallpaper') {
            @($Rows | Where-Object group -ne 'wallpaper')
        } else { @($Rows | Where-Object group -eq $group) })
        $metrics = [ordered]@{}
        foreach ($property in @('workingSetBytes','privateWorkingSetBytes','privateBytes','cpuPercent','gpuDedicatedBytes','gpuSharedBytes')) {
            $metrics[$property] = Get-NullableSum $members $property
        }
        $missing = @($members | Where-Object {
            $null -eq $_.workingSetBytes -or $null -eq $_.privateWorkingSetBytes -or
            $null -eq $_.privateBytes -or $null -eq $_.cpuPercent
        }).Count -gt 0
        $metrics.coverage = [ordered]@{
            status = if ($members.Count -eq 0) { 'missing' } elseif ($missing) { 'partial' } else { 'complete' }
            rows = $members.Count
        }
        $result[$group] = [pscustomobject]$metrics
    }
    return [pscustomobject]$result
}

function Get-GpuMeasurement($Rows, [int]$ProcessId) {
    $matches = @($Rows | Where-Object { [string]$_.Name -match "^pid_$ProcessId(?:_|$)" })
    if ($matches.Count -eq 0) {
        return [pscustomobject]@{ dedicatedBytes=$null; sharedBytes=$null; matchCount=0; status='missing_row' }
    }
    [pscustomobject]@{
        dedicatedBytes = (($matches | Measure-Object -Property DedicatedUsage -Sum).Sum)
        sharedBytes = (($matches | Measure-Object -Property SharedUsage -Sum).Sum)
        matchCount = $matches.Count; status='complete'
    }
}

function Get-SummaryStats($Rows) {
    $result = [ordered]@{}
    if (@($Rows).Count -eq 0) { return [pscustomobject]$result }
    $properties = @($Rows[0].PSObject.Properties.Name | Where-Object { $_ -notin @('timestampUtc','groups','coverage') })
    foreach ($property in $properties) {
        $values = @($Rows | ForEach-Object { $_.$property } | Where-Object { $null -ne $_ -and $_ -is [ValueType] })
        if ($values.Count -eq 0) {
            $result[$property] = [pscustomobject]@{ count=0; mean=$null; min=$null; max=$null }
        } else {
            $measure = $values | Measure-Object -Average -Minimum -Maximum
            $result[$property] = [pscustomobject]@{ count=$values.Count; mean=$measure.Average; min=$measure.Minimum; max=$measure.Maximum }
        }
    }
    return [pscustomobject]$result
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../')).Path
$allowedRoot = (Resolve-Path (Join-Path $repoRoot 'build-artifacts/native-rebuild')).Path
$requestedOutput = [IO.Path]::GetFullPath($OutputDirectory)
$allowedPrefix = $allowedRoot.TrimEnd('\') + [IO.Path]::DirectorySeparatorChar
if (-not $requestedOutput.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDirectory must remain under $allowedRoot"
}
if (Test-Path -LiteralPath $requestedOutput) { throw "OutputDirectory must be new: $requestedOutput" }
$null = New-Item -ItemType Directory -Path $requestedOutput -Force
$scriptSnapshot = Join-Path $requestedOutput 'measure-live-resources.executed.ps1'
Copy-Item -LiteralPath $PSCommandPath -Destination $scriptSnapshot

$process = Get-CimInstance Win32_Process -Filter "ProcessId=$RootProcessId" -Property ProcessId,ParentProcessId,Name,CreationDate,ExecutablePath
if ($null -eq $process) { throw "Root process not found: $RootProcessId" }
$rootStart = $process.CreationDate.ToUniversalTime()
$rootIdentity = "$RootProcessId|$($rootStart.Ticks)"
$started = [DateTime]::UtcNow
$previous = @{}
$samples = [Collections.Generic.List[object]]::new()
$processLines = [Collections.Generic.List[object]]::new()
$gpuLines = [Collections.Generic.List[object]]::new()
$status = 'complete'
$stopReason = $null

$manifest = [ordered]@{
    schemaVersion=1; startedUtc=$started.ToString('o'); rootProcessId=$RootProcessId
    rootCreationUtc=$rootStart.ToString('o'); durationSeconds=$DurationSeconds
    sampleMilliseconds=$SampleMilliseconds; outputDirectory=$requestedOutput
    executablePath=$process.ExecutablePath; scriptSha256=(Get-FileHash $scriptSnapshot -Algorithm SHA256).Hash
    limitations=@('Sampling is bounded and cannot prove absence of long-term leaks.','Shared working-set pages may be counted in total WS.',
        'Short-lived descendants may be missed between samples; missing metrics remain null.',
        'GPU counters are raw WMI process-instance counters and are not a standalone leak gate.')
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $requestedOutput 'manifest.json') -Encoding utf8

try {
    while (([DateTime]::UtcNow - $started).TotalSeconds -lt $DurationSeconds) {
        $now = [DateTime]::UtcNow
        $sampleTime = [Environment]::TickCount64
        $all = @(Get-CimInstance Win32_Process -Property ProcessId,ParentProcessId,Name,CreationDate,ExecutablePath)
        $rows = foreach ($entry in $all) {
            $created = $entry.CreationDate.ToUniversalTime()
            [pscustomobject]@{ ProcessId=[int]$entry.ProcessId; ParentProcessId=[int]$entry.ParentProcessId
                Name=[string]$entry.Name; CreationTimeUtc=$created; IdentityKey="$($entry.ProcessId)|$($created.Ticks)"
                ParentIdentityKey=$null; ExecutablePath=$entry.ExecutablePath }
        }
        $byPid = @{}
        foreach ($row in $rows) { $byPid[$row.ProcessId] = $row }
        foreach ($row in $rows) {
            if ($byPid.ContainsKey($row.ParentProcessId)) { $row.ParentIdentityKey = $byPid[$row.ParentProcessId].IdentityKey }
        }
        $tree = Get-ProcessTreeRows $rootIdentity $rows @($rows | Where-Object IdentityKey -eq $rootIdentity)
        if (-not $tree.rootPresent) {
            $status='root_missing'; $stopReason=$tree.status; break
        }
        $privateMap = @{}
        foreach ($counter in @(Get-CimInstance Win32_PerfRawData_PerfProc_Process -Property IDProcess,WorkingSetPrivate)) {
            $privateMap[[int]$counter.IDProcess] = [long]$counter.WorkingSetPrivate
        }
        $gpuRaw = @()
        try { $gpuRaw = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -Property Name,DedicatedUsage,SharedUsage,TotalCommitted) } catch { }
        $resourceRows = [Collections.Generic.List[object]]::new()
        foreach ($row in @($tree.rows)) {
            $cpu = $null; $cpuTotal = $null; $private = $null; $commit = $null; $working = $null; $gpu = Get-GpuMeasurement $gpuRaw $row.ProcessId
            try {
                $live = Get-Process -Id $row.ProcessId -ErrorAction Stop
                if ([Math]::Abs(($live.StartTime.ToUniversalTime() - $row.CreationTimeUtc).TotalMilliseconds) -le 10) {
                    $cpuTotal = $live.TotalProcessorTime.TotalSeconds
                    $cpu = Get-CpuIntervalPercent $previous[$row.IdentityKey] ([pscustomobject]@{ identityKey=$row.IdentityKey; sampleTimeMs=$sampleTime; cpuSeconds=$cpuTotal })
                    $commit = $live.PrivateMemorySize64; $working = $live.WorkingSet64
                }
                $live.Dispose()
            } catch { }
            if ($privateMap.ContainsKey($row.ProcessId)) { $private = $privateMap[$row.ProcessId] }
            $resource = [pscustomobject]@{ group=(Get-ProcessGroup $row $rootIdentity); workingSetBytes=$working
                privateWorkingSetBytes=$private; privateBytes=$commit; cpuPercent=$cpu
                gpuDedicatedBytes=$gpu.dedicatedBytes; gpuSharedBytes=$gpu.sharedBytes }
            $resourceRows.Add($resource)
            $processLines.Add([pscustomobject]@{ timestampUtc=$now.ToString('o'); sample=$samples.Count; pid=$row.ProcessId
                parentPid=$row.ParentProcessId; name=$row.Name; creationUtc=$row.CreationTimeUtc.ToString('o')
                group=$resource.group; workingSetBytes=$working; privateWorkingSetBytes=$private; privateBytes=$commit
                cpuPercent=$cpu; gpuDedicatedBytes=$gpu.dedicatedBytes; gpuSharedBytes=$gpu.sharedBytes
                gpuCoverage=$gpu.status })
            if ($null -ne $cpuTotal) {
                $previous[$row.IdentityKey] = [pscustomobject]@{ identityKey=$row.IdentityKey; sampleTimeMs=$sampleTime
                    cpuSeconds=$cpuTotal }
            }
        }
        $aggregates = Get-GroupedResourceAggregates @($resourceRows)
        $total = $aggregates.total
        $sample = [pscustomobject]@{ timestampUtc=$now.ToString('o'); sample=$samples.Count; processCount=$resourceRows.Count
            childCount=(@($resourceRows | Where-Object group -ne 'shell').Count); totalWorkingSetBytes=$total.workingSetBytes
            totalPrivateWorkingSetBytes=$total.privateWorkingSetBytes; totalPrivateBytes=$total.privateBytes
            totalCpuPercent=$total.cpuPercent; nonWallpaperPrivateWorkingSetBytes=$aggregates.nonWallpaper.privateWorkingSetBytes
            nonWallpaperPrivateBytes=$aggregates.nonWallpaper.privateBytes; groups=$aggregates
            coverage=$total.coverage }
        $samples.Add($sample)
        $gpuLines.Add([pscustomobject]@{ timestampUtc=$now.ToString('o'); sample=$sample.sample; rows=$gpuRaw.Count })
        Start-Sleep -Milliseconds $SampleMilliseconds
    }
} finally {
    $samples | ForEach-Object { $_ | ConvertTo-Json -Depth 12 -Compress } | Set-Content -LiteralPath (Join-Path $requestedOutput 'samples.jsonl') -Encoding utf8
    $processLines | ForEach-Object { $_ | ConvertTo-Json -Depth 8 -Compress } | Set-Content -LiteralPath (Join-Path $requestedOutput 'process-samples.jsonl') -Encoding utf8
    $gpuLines | ForEach-Object { $_ | ConvertTo-Json -Depth 8 -Compress } | Set-Content -LiteralPath (Join-Path $requestedOutput 'gpu-samples.jsonl') -Encoding utf8
}

$sampleArray = @($samples | ForEach-Object { $_ })
$summary = [ordered]@{ schemaVersion=1; startedUtc=$started.ToString('o'); finishedUtc=[DateTime]::UtcNow.ToString('o')
    rootProcessId=$RootProcessId; rootCreationUtc=$rootStart.ToString('o'); status=$status; stopReason=$stopReason
    sampleCount=$samples.Count; sampleIntervalMilliseconds=$SampleMilliseconds
    summary=Get-SummaryStats $sampleArray
    groups=Get-SummaryStats @($sampleArray | ForEach-Object { $_.groups.total })
    nonWallpaper=Get-SummaryStats @($sampleArray | ForEach-Object { $_.groups.nonWallpaper })
    limitations=$manifest.limitations }
$summary | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $requestedOutput 'summary.json') -Encoding utf8
Write-Output "Live resource artifacts: $requestedOutput"
if ($status -eq 'root_missing') { throw "Root process disappeared during sampling: $stopReason" }
