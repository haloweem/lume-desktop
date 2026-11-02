# Lume health sampler - captures the state when the app feels slow.
#
# Lume is healthy while idle (see docs/current/memory-leak-audit-20261101.md),
# so "slow after long use" has to be caught while it is happening. This records:
#   - dedicated / shared GPU memory (the only visible signal of the glass WGC leak)
#   - GDI / USER handles, kernel handles, thread count (resource-exhaustion stalls)
#   - UI thread round-trip latency (direct measure of "slow"; ~0.1-0.3 ms normally)
#   - process CPU, mpv memory, WebView2 process count, foreground window title
#
# Usage:
#   pwsh -NoProfile -ExecutionPolicy Bypass -File native-rebuild/tools/sample-lume-health.ps1
#   pwsh ... -DurationSeconds 7200 -IntervalSeconds 5
#
# Output: <workspace>/build-artifacts/lume-health/<time>/health.jsonl
# Send that folder after a slow session.

param(
    [int]$DurationSeconds = 3600,
    [int]$IntervalSeconds = 5,
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Continue'

Add-Type -Namespace LumeHealth -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern uint GetGuiResources(System.IntPtr h, uint f);
[System.Runtime.InteropServices.DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint a, bool i, uint pid);
[System.Runtime.InteropServices.DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
[System.Runtime.InteropServices.DllImport("kernel32.dll")] public static extern bool GetProcessTimes(System.IntPtr h, out long c, out long e, out long k, out long u);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool EnumWindows(Cb cb, System.IntPtr p);
public delegate bool Cb(System.IntPtr h, System.IntPtr p);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool IsWindowVisible(System.IntPtr h);
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet=System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr SendMessageTimeoutW(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l, uint f, uint t, out System.IntPtr r);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet=System.Runtime.InteropServices.CharSet.Unicode)]
public static extern int GetWindowTextW(System.IntPtr h, System.Text.StringBuilder s, int n);
public static System.IntPtr PingTarget = System.IntPtr.Zero;
public static bool PickTarget(uint wanted) {
  PingTarget = System.IntPtr.Zero;
  EnumWindows(delegate(System.IntPtr h, System.IntPtr p) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    if (pid != wanted || !IsWindowVisible(h)) return true;
    PingTarget = h; return false;
  }, System.IntPtr.Zero);
  return PingTarget != System.IntPtr.Zero;
}
public static double Ping() {
  if (PingTarget == System.IntPtr.Zero) return -1;
  System.IntPtr r;
  var sw = System.Diagnostics.Stopwatch.StartNew();
  SendMessageTimeoutW(PingTarget, 0, System.IntPtr.Zero, System.IntPtr.Zero, 0x2, 1000, out r);
  sw.Stop();
  return sw.Elapsed.TotalMilliseconds;
}
public static string Title(System.IntPtr h) {
  var sb = new System.Text.StringBuilder(256);
  GetWindowTextW(h, sb, 256);
  return sb.ToString();
}
'@

function Get-LumeProcessCounts {
    # Count only WebView2 processes that descend from LumeShell. Other apps on
    # the machine also use WebView2, so a system-wide count is meaningless.
    param([int]$RootPid)
    $all = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue
    if (-not $all) { return [pscustomobject]@{ webview = $null; children = $null } }
    $ids = [Collections.Generic.HashSet[int]]::new()
    [void]$ids.Add($RootPid)
    do {
        $added = $false
        foreach ($item in $all) {
            if ($ids.Contains([int]$item.ParentProcessId) -and $ids.Add([int]$item.ProcessId)) { $added = $true }
        }
    } while ($added)
    $webview = 0
    $children = 0
    foreach ($item in $all) {
        if (-not $ids.Contains([int]$item.ProcessId)) { continue }
        if ([int]$item.ProcessId -eq $RootPid) { continue }
        $children++
        if ($item.Name -like 'msedgewebview2*') { $webview++ }
    }
    [pscustomobject]@{ webview = $webview; children = $children }
}

function Get-GpuMemory {
    # Sum dedicated/shared GPU memory for one PID. The WMI class name is not localized.
    param([int]$TargetPid)
    $rows = Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match ('pid_' + $TargetPid + '_') }
    if (-not $rows) { return $null }
    [pscustomobject]@{
        dedicatedBytes = [long](($rows | ForEach-Object { [double]$_.DedicatedUsage } | Measure-Object -Sum).Sum)
        sharedBytes    = [long](($rows | ForEach-Object { [double]$_.SharedUsage } | Measure-Object -Sum).Sum)
    }
}

$shell = Get-Process -Name LumeShell -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $shell) { Write-Host 'LumeShell is not running'; exit 1 }

if (-not $OutputDirectory) {
    $root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $OutputDirectory = Join-Path $root ('build-artifacts\lume-health\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$file = Join-Path $OutputDirectory 'health.jsonl'

$pidSample = $shell.Id
$handle = [LumeHealth.Native]::OpenProcess(0x1000, $false, $pidSample)
if ($handle -eq [IntPtr]::Zero) { Write-Host 'Cannot open LumeShell process'; exit 1 }

$watch = [Diagnostics.Stopwatch]::StartNew()
$previousCpu = $null
$previousAt = 0.0
$samples = 0
$worstPing = 0.0
$worstAt = 0.0

Write-Host ('Sampling -> {0}' -f $file)
Write-Host ('Duration {0}s, interval {1}s. Ctrl+C to stop.' -f $DurationSeconds, $IntervalSeconds)

try {
    do {
        $at = $watch.Elapsed.TotalSeconds
        $process = Get-Process -Id $pidSample -ErrorAction SilentlyContinue
        if (-not $process) { Write-Host 'LumeShell exited'; break }

        $cpuSeconds = $null
        $c = [long]0; $e = [long]0; $k = [long]0; $u = [long]0
        if ([LumeHealth.Native]::GetProcessTimes($handle, [ref]$c, [ref]$e, [ref]$k, [ref]$u)) {
            $cpuSeconds = ($k + $u) / 1e7
        }
        $cpuPercent = $null
        if ($null -ne $cpuSeconds -and $null -ne $previousCpu -and $at -gt $previousAt) {
            $cpuPercent = [math]::Round(100.0 * ($cpuSeconds - $previousCpu) / ($at - $previousAt) / [Environment]::ProcessorCount, 3)
        }
        if ($null -ne $cpuSeconds) { $previousCpu = $cpuSeconds; $previousAt = $at }

        if ([LumeHealth.Native]::PingTarget -eq [IntPtr]::Zero) { [void][LumeHealth.Native]::PickTarget([uint32]$pidSample) }
        $ping = [math]::Round([LumeHealth.Native]::Ping(), 3)
        if ($ping -gt $worstPing) { $worstPing = $ping; $worstAt = $at }

        $gpu = Get-GpuMemory -TargetPid $pidSample
        $mpv = Get-Process -Name mpv -ErrorAction SilentlyContinue | Select-Object -First 1
        $tree = Get-LumeProcessCounts -RootPid $pidSample
        $foreground = [LumeHealth.Native]::Title([LumeHealth.Native]::GetForegroundWindow())

        $record = [ordered]@{
            utc              = [DateTime]::UtcNow.ToString('o')
            elapsedSeconds   = [math]::Round($at, 1)
            shellPrivateMB   = [math]::Round($process.PrivateMemorySize64 / 1MB, 1)
            shellWorkingMB   = [math]::Round($process.WorkingSet64 / 1MB, 1)
            gdi              = [LumeHealth.Native]::GetGuiResources($handle, 0)
            user             = [LumeHealth.Native]::GetGuiResources($handle, 1)
            handles          = $process.HandleCount
            threads          = $process.Threads.Count
            shellCpuPercent  = $cpuPercent
            uiPingMs         = $ping
            gpuDedicatedMB   = $(if ($gpu) { [math]::Round($gpu.dedicatedBytes / 1MB, 2) } else { $null })
            gpuSharedMB      = $(if ($gpu) { [math]::Round($gpu.sharedBytes / 1MB, 2) } else { $null })
            mpvPrivateMB     = $(if ($mpv) { [math]::Round($mpv.PrivateMemorySize64 / 1MB, 1) } else { $null })
            webviewProcesses = $tree.webview
            childProcesses   = $tree.children
            foreground       = $foreground
        }
        ($record | ConvertTo-Json -Compress) | Add-Content -LiteralPath $file -Encoding utf8
        $samples++

        if ($samples % 12 -eq 0) {
            $vram = if ($null -ne $record.gpuDedicatedMB) { $record.gpuDedicatedMB } else { 0 }
            $cpu = if ($null -ne $cpuPercent) { $cpuPercent } else { 0 }
            Write-Host ('t={0,6:N0}s  priv={1,6:N1}MB  gdi/user={2}/{3}  vram={4,7:N1}MB  ping={5,6:N2}ms  cpu={6,5:N2}%' -f `
                $record.elapsedSeconds, $record.shellPrivateMB, $record.gdi, $record.user, $vram, $ping, $cpu)
        }
        if ($watch.Elapsed.TotalSeconds -ge $DurationSeconds) { break }
        Start-Sleep -Seconds $IntervalSeconds
    } while ($true)
}
finally {
    [void][LumeHealth.Native]::CloseHandle($handle)
}

Write-Host ''
Write-Host ('Samples: {0}. Worst UI latency {1:N2} ms at {2:N0}s.' -f $samples, $worstPing, $worstAt)
Write-Host ('Output: {0}' -f $OutputDirectory)
