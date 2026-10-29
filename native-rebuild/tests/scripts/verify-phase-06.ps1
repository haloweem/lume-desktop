$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$sourceRoot = Join-Path $repoRoot 'native-rebuild'
$verificationRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\verification'
$reportPath = Join-Path $verificationRoot 'phase-06.txt'
New-Item -ItemType Directory -Force -Path $verificationRoot | Out-Null
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("phase=06")
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
$portableFiles = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object {
    $_.Extension -in @('.cpp', '.h', '.cmake') -and $_.FullName -notlike '*\platform\windows\*'
}
$violations = $portableFiles | Select-String -Pattern '(?i)\bHWND\b|ICoreWebView2|WebView2|CoInitialize|CreateCoreWebView2'
if ($violations) { throw "Portable bridge layer contains platform coupling: $($violations | Out-String)" }
$report.Add('pass=portable-bridge-boundary-scan')
$report.Add('webview2-runtime=optional-adapter')
$report | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Output 'PHASE_06_PASS'
Write-Output "REPORT=$reportPath"
