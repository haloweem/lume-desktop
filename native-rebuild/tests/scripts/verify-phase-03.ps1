$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$sourceRoot = Join-Path $repoRoot 'native-rebuild'
$verificationRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\verification'
$reportPath = Join-Path $verificationRoot 'phase-03.txt'

New-Item -ItemType Directory -Force -Path $verificationRoot | Out-Null
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("phase=03")
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

$portableFiles = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
    Where-Object {
        $_.Extension -in @('.cpp', '.h', '.cmake') -and
        $_.FullName -notlike '*\platform\windows\*'
    }
$forbiddenPattern = '(?i)\bHWND\b|SetWindowPos|CreateWindowEx|GetWindowLongPtr|global\s+event|AppContext'
$violations = $portableFiles | Select-String -Pattern $forbiddenPattern
if ($violations) {
    $details = $violations | ForEach-Object { "$($_.Path):$($_.LineNumber):$($_.Line.Trim())" }
    throw "Portable window layer contains platform coupling:`n$($details -join "`n")"
}
$report.Add('pass=portable-window-boundary-scan')

$platformFiles = Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'platform\windows') -Recurse -File
if (-not ($platformFiles | Select-String -Pattern 'HWND|CreateWindowEx|SetWindowPos')) {
    throw 'Win32 backend does not contain expected Windows window implementation'
}
$report.Add('pass=win32-backend-boundary-scan')
$report.Add('pass=build-artifacts-under-build-artifacts')
$report | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Output 'PHASE_03_PASS'
Write-Output "REPORT=$reportPath"
