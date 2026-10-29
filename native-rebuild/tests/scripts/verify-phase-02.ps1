$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$sourceRoot = Join-Path $repoRoot 'native-rebuild'
$verificationRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\verification'
$reportPath = Join-Path $verificationRoot 'phase-02.txt'

New-Item -ItemType Directory -Force -Path $verificationRoot | Out-Null
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("phase=02")
$report.Add("timestamp=$([DateTime]::UtcNow.ToString('o'))")
$report.Add("source=$sourceRoot")

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
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

$nativeFiles = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.h', '.cmake') }
$forbiddenPattern = '(?i)AppContext|WebView2|electron|global\s+event|static\s+[^\r\n]*HWND'
$violations = $nativeFiles | Select-String -Pattern $forbiddenPattern
if ($violations) {
    $details = $violations | ForEach-Object { "$($_.Path):$($_.LineNumber):$($_.Line.Trim())" }
    throw "Forbidden coupling detected:`n$($details -join "`n")"
}
$report.Add('pass=forbidden-coupling-scan')

$report.Add('pass=build-artifacts-under-build-artifacts')
$report | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Output 'PHASE_02_PASS'
Write-Output "REPORT=$reportPath"
