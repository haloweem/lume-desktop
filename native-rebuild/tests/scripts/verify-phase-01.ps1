$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$buildRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\debug'
$verificationRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\verification'
New-Item -ItemType Directory -Force -Path $verificationRoot | Out-Null
$reportPath = Join-Path $verificationRoot 'phase-01.txt'

$executables = @('LumeLauncher.exe', 'LumeShell.exe', 'LumeMusicHost.exe')
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Phase=01")
$lines.Add("GeneratedAt=$([DateTime]::UtcNow.ToString('o'))")

foreach ($name in $executables) {
    $path = Join-Path $buildRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing executable: $path"
    }
    $file = Get-Item -LiteralPath $path
    $process = Start-Process -FilePath $path -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "Executable failed: $name exit=$($process.ExitCode)"
    }
    $lines.Add("$name=$($file.Length) bytes exit=0")
}

$sourceFiles = Get-ChildItem -LiteralPath (Join-Path $repoRoot 'native-rebuild') -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.h', '.cmake', '.json', '.txt') }
$forbidden = $sourceFiles | Select-String -Pattern '#include\s*[<"]node\.h[>"]|electron|vscode[-_ ]?shim' -CaseSensitive:$false
if ($forbidden) {
    $details = $forbidden | ForEach-Object { "$($_.Path):$($_.LineNumber):$($_.Line.Trim())" }
    throw ("Forbidden runtime dependency found: " + ($details -join [Environment]::NewLine))
}

$ctestRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\debug'
Push-Location $ctestRoot
try {
    ctest --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}

$lines.Add('CTest=PASS')
$lines.Add('ForbiddenDependencies=NONE')
$lines.Add('Result=PHASE_01_PASS')
$lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Output 'PHASE_01_PASS'
