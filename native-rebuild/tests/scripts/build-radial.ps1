param([switch]$ConfigureOnly)
$ErrorActionPreference='Stop'
[Console]::InputEncoding=[Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$output=Join-Path $root '..\build-artifacts\native-rebuild\radial'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'MSVC x64 Build Tools required'}
$version=(Get-Content -LiteralPath (Join-Path $installation 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt')).Trim()
$msvc=Join-Path $installation "VC\Tools\MSVC\$version"
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion=(Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'cppwinrt\winrt\Windows.Graphics.Capture.h')} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
$savedInclude=$env:INCLUDE;$savedLib=$env:LIB;$savedPath=$env:PATH
try {
  $env:INCLUDE="$msvc\include;$sdk\Include\$sdkVersion\ucrt;$sdk\Include\$sdkVersion\shared;$sdk\Include\$sdkVersion\um;$sdk\Include\$sdkVersion\winrt;$sdk\Include\$sdkVersion\cppwinrt"
  $env:LIB="$msvc\lib\x64;$sdk\Lib\$sdkVersion\ucrt\x64;$sdk\Lib\$sdkVersion\um\x64"
  $env:PATH="$msvc\bin\Hostx64\x64;$sdk\bin\$sdkVersion\x64;$savedPath"
  New-Item -ItemType Directory -Force -Path $output | Out-Null
  & cmake -S $root -B $output -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_CXX_COMPILER=$msvc\bin\Hostx64\x64\cl.exe" *> (Join-Path $output 'configure.log')
  if($LASTEXITCODE){Get-Content -LiteralPath (Join-Path $output 'configure.log') -Tail 30;throw 'Radial configure failed'}
  if(!$ConfigureOnly){
    & cmake --build $output --parallel 5 --target LumeShell lume-radial-menu-test lume-radial-shortcut-test lume-radial-window-test lume-radial-session-test lume-radial-page_protocol-test lume-radial-context_menu-test lume-radial-annular_region-test lume-screenshot-model-test lume-screenshot-render-test lume-screenshot-image-test lume-screenshot-editor-test lume-widget-canvas-test lume-native-material-test lume-material-shader-test lume-widget-menu-native-test lume-page_bridge-test lume-radial-preview lume-desktop-application-test lume-shell-activation-test lume-desktop-update-notification-test *> (Join-Path $output 'build.log')
    if($LASTEXITCODE){Get-Content -LiteralPath (Join-Path $output 'build.log') -Tail 45;throw 'Radial build failed'}
    Write-Output 'PASS MSVC Release shell and focused radial/material targets built'
  }
} finally {$env:INCLUDE=$savedInclude;$env:LIB=$savedLib;$env:PATH=$savedPath}
