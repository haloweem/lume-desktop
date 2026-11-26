param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$NodePath = 'C:\Program Files\nodejs\node.exe',
    [string]$LogDirectory = '',
    [switch]$DependenciesOnly
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repositoryRoot = Split-Path -Parent $sourceRoot
$runtimeRoot = Join-Path $sourceRoot 'music-runtime'
$stage = [IO.Path]::GetFullPath($Destination)
if (-not $LogDirectory) { $LogDirectory = Join-Path $repositoryRoot 'build-artifacts\music-folia-six' }
$evidenceRoot = [IO.Path]::GetFullPath($LogDirectory)
New-Item -ItemType Directory -Force -Path $evidenceRoot | Out-Null
if ((Test-Path -LiteralPath $stage) -and @(Get-ChildItem -LiteralPath $stage -Force).Count -gt 0) {
    throw 'Destination is not empty. Choose a new stage directory; no files were removed.'
}
if (-not (Test-Path -LiteralPath $NodePath -PathType Leaf)) { throw 'Node.js 24.15.0 is required.' }
$nodeVersion = (& $NodePath --version).Trim()
if ($nodeVersion -ne 'v24.15.0') { throw "Expected Node v24.15.0, found $nodeVersion" }
$nodeChecksums = Join-Path $evidenceRoot 'node-v24.15.0-SHASUMS256.txt'
Invoke-WebRequest -Uri 'https://nodejs.org/dist/v24.15.0/SHASUMS256.txt' -OutFile $nodeChecksums
$nodeLine = Get-Content -LiteralPath $nodeChecksums | Where-Object { $_ -match '  win-x64/node.exe$' }
if (@($nodeLine).Count -ne 1) { throw 'Official Node checksum is missing or ambiguous.' }
$nodeHash = (Get-FileHash -LiteralPath $NodePath -Algorithm SHA256).Hash
if ($nodeLine.Substring(0, 64) -ine $nodeHash) { throw 'Installed Node differs from the official Windows x64 binary.' }
$nodeLicense = Join-Path $evidenceRoot 'NODE-v24.15.0-LICENSE.txt'
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/nodejs/node/v24.15.0/LICENSE' -OutFile $nodeLicense

$npmCli = Join-Path (Split-Path -Parent $NodePath) 'node_modules\npm\bin\npm-cli.js'
if (-not (Test-Path -LiteralPath $npmCli)) { throw 'Node npm CLI is required for locked production dependency staging.' }
$dependencyRoot = Join-Path $evidenceRoot 'production-dependencies'
New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $runtimeRoot 'package.json'),(Join-Path $runtimeRoot 'package-lock.json') -Destination $dependencyRoot -Force
$lockHash = (Get-FileHash -LiteralPath (Join-Path $runtimeRoot 'package-lock.json')).Hash
$receipt = Join-Path $dependencyRoot 'dependency-receipt.json'
$reuse = $false
if (Test-Path -LiteralPath $receipt) {
    $previous = Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    $reuse = $previous.lockHash -eq $lockHash -and (Test-Path -LiteralPath (Join-Path $dependencyRoot 'node_modules\@koromix\koffi-win32-x64\package.json'))
}
if (-not $reuse) {
    & $NodePath $npmCli ci --prefix $dependencyRoot --omit=dev --ignore-scripts --no-audit --no-fund *> (Join-Path $evidenceRoot 'npm-production-stage.log')
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath (Join-Path $evidenceRoot 'npm-production-stage.log') -Tail 35; throw 'Locked production dependency install failed.' }
    @{ lockHash = $lockHash; nodeVersion = $nodeVersion } | ConvertTo-Json | Set-Content -LiteralPath $receipt -Encoding utf8
}

if ($DependenciesOnly) { Write-Output 'FOLIA_RUNTIME_DEPENDENCIES_READY'; exit 0 }
& $NodePath (Join-Path $runtimeRoot 'provider\build.cjs') *> (Join-Path $evidenceRoot 'provider-build.log')
if ($LASTEXITCODE -ne 0) { throw 'Provider bundle build failed.' }
& $NodePath (Join-Path $PSScriptRoot 'stage-folia-runtime.cjs') $runtimeRoot $stage $dependencyRoot $NodePath $nodeLicense *> (Join-Path $evidenceRoot 'runtime-stage.log')
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath (Join-Path $evidenceRoot 'runtime-stage.log') -Tail 35; throw 'Runtime assembly failed.' }
& $NodePath (Join-Path $PSScriptRoot 'verify-folia-runtime.cjs') $runtimeRoot $stage *> (Join-Path $evidenceRoot 'runtime-verification.json')
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath (Join-Path $evidenceRoot 'runtime-verification.json') -Tail 25; throw 'Runtime payload or source verification failed.' }
$inventory = Get-Content -LiteralPath (Join-Path $stage 'runtime-inventory.json') -Raw | ConvertFrom-Json
Write-Output ("FOLIA_RUNTIME_STAGE_OK files={0} packages={1} nodeSha256={2}" -f $inventory.files.Count,$inventory.packages.Count,$nodeHash)
