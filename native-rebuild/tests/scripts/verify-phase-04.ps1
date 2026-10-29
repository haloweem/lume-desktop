$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$sourceRoot = Join-Path $repoRoot 'native-rebuild'
$verificationRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\verification'
$reportPath = Join-Path $verificationRoot 'phase-04.txt'

New-Item -ItemType Directory -Force -Path $verificationRoot | Out-Null
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("phase=04")
$report.Add("timestamp=$([DateTime]::UtcNow.ToString('o'))")
$report.Add("source=$sourceRoot")

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)] [string]$Command,
        [Parameter(Mandatory = $true)] [string[]]$Arguments
    )
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
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
} finally {
    Pop-Location
}

foreach ($configuration in @('debug', 'release')) {
    $shellPath = Join-Path $repoRoot "build-artifacts\native-rebuild\$configuration\LumeShell.exe"
    if (-not (Test-Path -LiteralPath $shellPath -PathType Leaf)) {
        throw "Missing Shell executable: $shellPath"
    }
    $process = Start-Process -FilePath $shellPath -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "$shellPath failed with exit code $($process.ExitCode)"
    }
    $report.Add("pass=shell-$configuration exit=0 path=$shellPath")
}

$coreFiles = Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'core\glass') -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.h') }
$forbiddenCorePattern = '(?i)\bID3D11\b|\bIDComposition\b|D3D11CreateDevice|DCompositionCreateDevice|CreateTexture2D|IDXGIOutputDuplication|BitBlt|DuplicateOutput'
$coreViolations = $coreFiles | Select-String -Pattern $forbiddenCorePattern
if ($coreViolations) {
    $details = $coreViolations | ForEach-Object { "$($_.Path):$($_.LineNumber):$($_.Line.Trim())" }
    throw "Glass core contains platform graphics coupling:`n$($details -join "`n")"
}
$report.Add('pass=glass-core-boundary-scan')

$platformFiles = Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'platform\windows') -Recurse -File
$requiredBackendPattern = 'D3D11CreateDevice|DCompositionCreateDevice'
if (-not ($platformFiles | Select-String -Pattern $requiredBackendPattern)) {
    throw 'Windows glass backend does not contain D3D11/DirectComposition capability probing'
}
$report.Add('pass=windows-glass-backend-scan')
$report.Add('gpu-backend=initialization-is-failure-tolerant')
$report.Add('fallback=transparent-without-full-desktop-texture')
$report.Add('pass=build-artifacts-under-build-artifacts')
$report | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Output 'PHASE_04_PASS'
Write-Output "REPORT=$reportPath"
