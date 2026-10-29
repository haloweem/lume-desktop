param(
    [Parameter(Mandatory = $true)]
    [int]$RootProcessId,
    [Parameter(Mandatory = $false)]
    [string]$Executable,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [ValidateRange(5, 3600)]
    [int]$DurationSeconds = 30,
    [ValidateRange(250, 10000)]
    [int]$SampleMilliseconds = 1000
)

$ErrorActionPreference = 'Stop'

if ($Executable -and -not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Executable not found: $Executable"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Use a fresh output directory: $OutputDirectory"
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$sampleCsv = Join-Path $OutputDirectory 'process-samples.csv'
$treeJsonl = Join-Path $OutputDirectory 'process-tree.jsonl'
$summaryJson = Join-Path $OutputDirectory 'summary.json'
$scriptCopy = Join-Path $OutputDirectory 'measure-music-resources.executed.ps1'
Copy-Item -LiteralPath $PSCommandPath -Destination $scriptCopy

function Get-DescendantProcesses {
    param([int]$RootPid)

    $all = @(Get-CimInstance Win32_Process)
    if (-not ($all | Where-Object { [int]$_.ProcessId -eq $RootPid })) {
        return @()
    }
    $ids = [System.Collections.Generic.HashSet[int]]::new()
    [void]$ids.Add($RootPid)
    do {
        $added = $false
        foreach ($process in $all) {
            if ($ids.Contains([int]$process.ParentProcessId) -and $ids.Add([int]$process.ProcessId)) {
                $added = $true
            }
        }
    } while ($added)
    @($all | Where-Object { $ids.Contains([int]$_.ProcessId) })
}

function Get-Role {
    param(
        [object]$Process,
        [int]$RootPid
    )
    if ([int]$Process.ProcessId -eq $RootPid) { return 'shell' }
    if ($Process.Name -ieq 'LumeMusicHost.exe') { return 'music-host' }
    if ($Process.Name -ieq 'mpv.exe') { return 'wallpaper-player' }
    if ($Process.CommandLine -match '--type=([^\s"]+)') { return $Matches[1] }
    if ($Process.Name -match 'WebView|msedge|msedgewebview') { return 'webview' }
    'child'
}

$root = Get-CimInstance Win32_Process | Where-Object { [int]$_.ProcessId -eq $RootProcessId } | Select-Object -First 1
if (-not $root) {
    throw "Specified root process is not running: $RootProcessId"
}

$start = Get-Date
$deadline = $start.AddSeconds($DurationSeconds)
$rows = [System.Collections.Generic.List[object]]::new()
$summaries = [System.Collections.Generic.List[object]]::new()
$rootExited = $false

while ((Get-Date) -lt $deadline) {
    $tree = @(Get-DescendantProcesses -RootPid $RootProcessId)
    if ($tree.Count -eq 0) {
        $rootExited = $true
        break
    }

    $ids = [System.Collections.Generic.HashSet[int]]::new()
    foreach ($process in $tree) { [void]$ids.Add([int]$process.ProcessId) }
    $counters = @(Get-CimInstance Win32_PerfFormattedData_PerfProc_Process |
        Where-Object { $ids.Contains([int]$_.IDProcess) })
    $counterByPid = @{}
    foreach ($counter in $counters) { $counterByPid[[int]$counter.IDProcess] = $counter }
    $sampledAt = Get-Date
    $sampleRows = @(
        foreach ($process in $tree) {
            $processId = [int]$process.ProcessId
            $counter = $counterByPid[$processId]
            $role = Get-Role -Process $process -RootPid $RootProcessId
            [pscustomobject]@{
                sampledAt = $sampledAt.ToString('o')
                pid = $processId
                parentPid = [int]$process.ParentProcessId
                name = [string]$process.Name
                role = $role
                privateWorkingSetBytes = if ($counter) { [int64]$counter.WorkingSetPrivate } else { $null }
                workingSetBytes = if ($counter) { [int64]$counter.WorkingSet } else { $null }
                privateBytes = if ($counter) { [int64]$counter.PrivateBytes } else { $null }
                cpuPercent = if ($counter) { [double]$counter.PercentProcessorTime } else { $null }
                handles = if ($process.HandleCount) { [int64]$process.HandleCount } else { $null }
                threads = if ($process.ThreadCount) { [int]$process.ThreadCount } else { $null }
            }
        }
    )
    foreach ($row in $sampleRows) { $rows.Add($row) }

    $treeRecord = [pscustomobject]@{
        sampledAt = $sampledAt.ToString('o')
        rootPid = $RootProcessId
        processes = @($sampleRows)
    }
    $treeRecord | ConvertTo-Json -Depth 6 -Compress | Add-Content -LiteralPath $treeJsonl -Encoding utf8

    $musicHosts = @($sampleRows | Where-Object { $_.role -eq 'music-host' }).Count
    $rootRow = $sampleRows | Where-Object { $_.pid -eq $RootProcessId } | Select-Object -First 1
    $summaries.Add([pscustomobject]@{
        sampledAt = $sampledAt.ToString('o')
        treePrivateWorkingSetMiB = [Math]::Round((($sampleRows | Measure-Object -Property privateWorkingSetBytes -Sum).Sum / 1MB), 2)
        treePrivateBytesMiB = [Math]::Round((($sampleRows | Measure-Object -Property privateBytes -Sum).Sum / 1MB), 2)
        shellPrivateWorkingSetMiB = if ($rootRow) { [Math]::Round(($rootRow.privateWorkingSetBytes / 1MB), 2) } else { $null }
        shellPrivateBytesMiB = if ($rootRow) { [Math]::Round(($rootRow.privateBytes / 1MB), 2) } else { $null }
        musicHostCount = $musicHosts
        processCount = $sampleRows.Count
    })
    Start-Sleep -Milliseconds $SampleMilliseconds
}

if ($rows.Count -eq 0) { throw 'No process samples were collected.' }
$rows | Export-Csv -LiteralPath $sampleCsv -NoTypeInformation -Encoding utf8
$peakTreeWorkingSet = ($summaries | Measure-Object -Property treePrivateWorkingSetMiB -Maximum).Maximum
$peakTreePrivateBytes = ($summaries | Measure-Object -Property treePrivateBytesMiB -Maximum).Maximum
$peakShellWorkingSet = ($summaries | Measure-Object -Property shellPrivateWorkingSetMiB -Maximum).Maximum
$peakShellPrivateBytes = ($summaries | Measure-Object -Property shellPrivateBytesMiB -Maximum).Maximum
$peakMusicHosts = ($summaries | Measure-Object -Property musicHostCount -Maximum).Maximum
$end = Get-Date

$result = [pscustomobject]@{
    startedAt = $start.ToString('o')
    endedAt = $end.ToString('o')
    rootPid = $RootProcessId
    executable = if ($Executable) { [IO.Path]::GetFullPath($Executable) } else { $null }
    executableSha256 = if ($Executable) { (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash } else { $null }
    requestedDurationSeconds = $DurationSeconds
    actualDurationSeconds = [Math]::Round(($end - $start).TotalSeconds, 2)
    sampleMilliseconds = $SampleMilliseconds
    sampleCount = $summaries.Count
    rootExitedBeforeDeadline = $rootExited
    peakTreePrivateWorkingSetMiB = $peakTreeWorkingSet
    peakTreePrivateBytesMiB = $peakTreePrivateBytes
    peakShellPrivateWorkingSetMiB = $peakShellWorkingSet
    peakShellPrivateBytesMiB = $peakShellPrivateBytes
    peakMusicHostCount = $peakMusicHosts
    outputFiles = @('summary.json', 'process-samples.csv', 'process-tree.jsonl')
    note = 'Read-only process-tree sampling. Tree totals are comparison metrics, not exclusive physical memory. Private working set and Private Bytes are separate. The script does not start, terminate, or modify product processes.'
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryJson -Encoding utf8
$result | ConvertTo-Json -Compress
