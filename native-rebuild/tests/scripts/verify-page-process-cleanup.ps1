$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$probe = Join-Path $repoRoot 'build-artifacts\native-rebuild\release\lume-webview-page-window-test.exe'
$reportPath = Join-Path $repoRoot 'build-artifacts\native-rebuild\verification\page-process-cleanup.json'
$runs = @()
foreach ($iteration in 1..3) {
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $probe
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.EnvironmentVariables['LUME_VERIFY_HOLD_MS'] = '6000'
    $process = [System.Diagnostics.Process]::Start($info)
    $owned = @{}
    $profileMarker = 'test-webview-profile\' + $process.Id
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(20)
        do {
            foreach ($child in @(Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -like "*$profileMarker*" })) {
                $owned[[int]$child.ProcessId] = $child.CreationDate
            }
            if (-not $process.HasExited) { Start-Sleep -Milliseconds 250 }
        } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $deadline)
        if (-not $process.HasExited) { throw 'Page probe did not exit before its deadline' }
        if ($process.ExitCode -ne 0) { throw "Page probe failed with $($process.ExitCode)" }
        if ($owned.Count -lt 1) { throw 'No probe WebView child was observed; cleanup check would be inconclusive' }
        $cleanupDeadline = [DateTime]::UtcNow.AddSeconds(8)
        do {
            $remaining = @(Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object {
                $owned.ContainsKey([int]$_.ProcessId) -and $owned[[int]$_.ProcessId] -eq $_.CreationDate
            })
            if ($remaining.Count) { Start-Sleep -Milliseconds 250 }
        } while ($remaining.Count -and [DateTime]::UtcNow -lt $cleanupDeadline)
        $runs += [pscustomobject]@{ iteration=$iteration; probePid=$process.Id; observedChildren=$owned.Count; remainingChildren=$remaining.Count; exitCode=$process.ExitCode }
        if ($remaining.Count) { throw "WebView children remained after close: $($remaining.ProcessId -join ',')" }
    } finally {
        if (-not $process.HasExited) { $process.Kill() }
        $process.Dispose()
    }
}
$runs | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding utf8
$runs | ConvertTo-Json -Compress
