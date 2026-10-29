param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateSet('32','64','Both')][string]$Architecture = 'Both',
    [switch]$IncludeVerification
)
$ErrorActionPreference = 'Stop'
$output = [IO.Path]::GetFullPath($OutputDirectory)
$source = $PSScriptRoot
if ($output.Contains('"') -or $source.Contains('"')) { throw 'Build paths cannot contain double quotes.' }
New-Item -ItemType Directory -Force -Path $output | Out-Null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$install = if (Test-Path -LiteralPath $vswhere) {
    & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $install -and (Test-Path -LiteralPath 'C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat')) {
    $install = 'C:\BuildTools'
}
$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvarsall.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw 'Existing MSVC x86/x64 build tools are required; this script does not install them.' }
$bitsList = if ($Architecture -eq 'Both') { @('32','64') } else { @($Architecture) }
foreach ($bits in $bitsList) {
    $build = Join-Path $output "minimize-host-build-$bits"
    $buildLog = Join-Path $output "minimize-host-build-$bits.log"
    $machine = if ($bits -eq '32') { 'x86' } else { 'x64' }
    $testing = if ($IncludeVerification) { 'ON' } else { 'OFF' }
    $targets = if ($IncludeVerification) { 'LumeMinimizeHost lume-minimize-animation-runtime-test' } else { 'LumeMinimizeHost' }
    $command = '""{0}" {1} >nul && set "VSLANG=1033" && cmake -S "{2}" -B "{3}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DBUILD_TESTING={4} && cmake --build "{3}" --target {5} --parallel 2"' -f $vcvars,$machine,$source,$build,$testing,$targets
    & $env:COMSPEC /d /s /c $command *> $buildLog
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $buildLog -Tail 50
        throw "Minimize animation $bits-bit build failed. Full log: $buildLog"
    }
    foreach ($file in @("LumeMinimizeHost$bits.exe","LumeMinimizeHook$bits.dll")) {
        $built = Join-Path $build $file
        if (-not (Test-Path -LiteralPath $built -PathType Leaf)) { throw "Missing build output: $built" }
        Copy-Item -LiteralPath $built -Destination (Join-Path $output $file) -Force
    }
    if ($IncludeVerification) {
        Copy-Item -LiteralPath (Join-Path $build 'lume-minimize-animation-runtime-test.exe') `
            -Destination (Join-Path $output "minimize-runtime-test$bits.exe") -Force
    }
    Write-Host "Built minimize animation $bits-bit Host/Hook. Log: $buildLog"
}
Copy-Item -LiteralPath (Join-Path $source 'NOTICE.md') -Destination (Join-Path $output 'LumeMinimize.NOTICE.md') -Force
Copy-Item -LiteralPath (Join-Path $source 'SOURCES.md') -Destination (Join-Path $output 'LumeMinimize.SOURCES.md') -Force
