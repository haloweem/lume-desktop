$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$sourceRoot = Join-Path $repoRoot 'native-rebuild'
$verificationRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\verification'
$reportPath = Join-Path $verificationRoot 'phase-07.txt'
New-Item -ItemType Directory -Force -Path $verificationRoot | Out-Null
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("phase=07")
$report.Add("timestamp=$([DateTime]::UtcNow.ToString('o'))")
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
    $report.Add("pass=$Command $($Arguments -join ' ')")
}
Push-Location $sourceRoot
try {
    Invoke-Checked 'cmake' @('--preset', 'windows-debug')
    Invoke-Checked 'cmake' @('--build', '--preset', 'windows-debug')
    Invoke-Checked 'ctest' @('--preset', 'windows-debug', '--output-on-failure')
    Invoke-Checked 'cmake' @('--preset', 'windows-release')
    Invoke-Checked 'cmake' @('--build', '--preset', 'windows-release')
    Invoke-Checked 'ctest' @('--preset', 'windows-release', '--output-on-failure')
} finally { Pop-Location }
foreach ($configuration in @('debug', 'release')) {
    $shellPath = Join-Path $repoRoot "build-artifacts\native-rebuild\$configuration\LumeShell.exe"
    if (-not (Test-Path -LiteralPath $shellPath -PathType Leaf)) { throw "Missing Shell executable: $shellPath" }
    $process = Start-Process -FilePath $shellPath -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "$shellPath failed with exit code $($process.ExitCode)" }
    $report.Add("pass=shell-$configuration exit=0 path=$shellPath")
}
$portableRoots = @(
    (Join-Path $sourceRoot 'core\wallpaper'),
    (Join-Path $sourceRoot 'core\ui\wallpaper_page_protocol.h'),
    (Join-Path $sourceRoot 'core\ui\wallpaper_page_protocol.cpp')
)
$portableFiles = foreach ($root in $portableRoots) {
    if (Test-Path -LiteralPath $root -PathType Container) {
        Get-ChildItem -LiteralPath $root -Recurse -File
    } elseif (Test-Path -LiteralPath $root -PathType Leaf) {
        Get-Item -LiteralPath $root
    }
}
$portableFiles = $portableFiles | Where-Object { $_.Extension -in @('.cpp', '.h', '.cmake') }
$violations = $portableFiles | Select-String -Pattern '(?i)\bHWND\b|CreateProcess|mpv\.exe|WinExec'
if ($violations) { throw "Portable wallpaper core/protocol contains platform coupling: $($violations | Out-String)" }
$report.Add('pass=portable-wallpaper-core-and-protocol-boundary-scan')
$mpv = Get-Command mpv.exe -ErrorAction SilentlyContinue
$report.Add("mpv-present=$([bool]$mpv)")
$report | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Output 'PHASE_07_PASS'
Write-Output "REPORT=$reportPath"
