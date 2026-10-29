param(
    [Parameter(Mandatory=$true)][int]$RootProcessId,
    [Parameter(Mandatory=$true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$processes = @(Get-CimInstance Win32_Process)
if (-not ($processes | Where-Object ProcessId -eq $RootProcessId)) { throw 'Specified Shell process is not running' }
$ids = [System.Collections.Generic.HashSet[int]]::new()
[void]$ids.Add($RootProcessId)
do {
    $added = $false
    foreach ($process in $processes) {
        if ($ids.Contains([int]$process.ParentProcessId) -and $ids.Add([int]$process.ProcessId)) { $added = $true }
    }
} while ($added)
$counters = @(Get-CimInstance Win32_PerfFormattedData_PerfProc_Process | Where-Object { $ids.Contains([int]$_.IDProcess) })
$gpuCounters = @()
$gpuStatus = 'available'
try { $gpuCounters = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory) }
catch { $gpuStatus = 'unavailable' }
$samples = @(foreach ($process in $processes | Where-Object { $ids.Contains([int]$_.ProcessId) }) {
    $counter = $counters | Where-Object IDProcess -eq $process.ProcessId | Select-Object -First 1
    $role = 'webview-browser'
    if ($process.ProcessId -eq $RootProcessId) { $role = 'shell' }
    elseif ($process.CommandLine -match '--type=([^ ]+)') { $role = $Matches[1] }
    $gpu = @($gpuCounters | Where-Object { $_.Name -match "^pid_$($process.ProcessId)_" })
    [pscustomobject]@{
        pid = $process.ProcessId; parentPid = $process.ParentProcessId; name = $process.Name; role = $role
        privateWorkingSetBytes = $counter.WorkingSetPrivate; workingSetBytes = $counter.WorkingSet; privateBytes = $counter.PrivateBytes
        gpuDedicatedBytes = if ($gpuStatus -eq 'available') { ($gpu | Measure-Object DedicatedUsage -Sum).Sum } else { $null }
        gpuSharedBytes = if ($gpuStatus -eq 'available') { ($gpu | Measure-Object SharedUsage -Sum).Sum } else { $null }
    }
})
$result = [pscustomobject]@{
    sampledAt = (Get-Date -Format o); rootPid = $RootProcessId; gpuCounters = $gpuStatus; processes = $samples
    privateWorkingSetMiB = [Math]::Round(($samples | Measure-Object privateWorkingSetBytes -Sum).Sum / 1MB, 2)
    privateBytesMiB = [Math]::Round(($samples | Measure-Object privateBytes -Sum).Sum / 1MB, 2)
    note = 'Instantaneous complete descendant-tree sample. Working sets can share pages. GPU shared is not an additional private-memory total. Long-run acceptance remains pending.'
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding utf8
$result | Select-Object sampledAt,rootPid,privateWorkingSetMiB,privateBytesMiB | ConvertTo-Json -Compress
