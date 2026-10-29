param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$installation){throw 'MSVC x64 Build Tools are required for the C++/WinRT material module.'}
$version=(Get-Content (Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$msvc=Join-Path $installation "VC/Tools/MSVC/$version"
$sdk=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion=(Get-ChildItem (Join-Path $sdk Include) -Directory | Where-Object {Test-Path (Join-Path $_.FullName 'cppwinrt/winrt/Windows.Graphics.Capture.h')} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1).Name
if(!$sdkVersion){throw 'Windows SDK with C++/WinRT capture headers is required.'}
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $output | Out-Null
$savedInclude=$env:INCLUDE;$savedLib=$env:LIB;$savedPath=$env:PATH
try {
  $env:INCLUDE="$msvc/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt;$sdk/Include/$sdkVersion/cppwinrt"
  $env:LIB="$msvc/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
  $env:PATH="$msvc/bin/Hostx64/x64;$savedPath"
  & "$msvc/bin/Hostx64/x64/cl.exe" /nologo /MT /O2 /std:c++20 /EHsc /W3 /DNOMINMAX /utf-8 "/Fo$output/compile-material-shaders.obj" "/Fe$output/compile-material-shaders.exe" "$PSScriptRoot/compile-material-shaders.cpp" /link d3dcompiler.lib
  if($LASTEXITCODE){throw "MSVC material shader compiler build failed ($LASTEXITCODE)."}
  & "$output/compile-material-shaders.exe" "$output/material-shader-bytecode.h"
  if($LASTEXITCODE){throw "Material shader compilation failed ($LASTEXITCODE)."}
  & "$msvc/bin/Hostx64/x64/cl.exe" /nologo /LD /MT /O2 /std:c++20 /EHsc /W3 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Zc:__cplusplus /utf-8 "/I$output" "/Fo$output/material_engine.obj" "/Fe$output/LumeGlass.dll" "$PSScriptRoot/material_engine.cpp" /link user32.lib gdi32.lib comctl32.lib dwmapi.lib windowsapp.lib runtimeobject.lib "/IMPLIB:$output/LumeGlass.lib"
  if($LASTEXITCODE){throw "MSVC material build failed ($LASTEXITCODE)."}
  Copy-Item -LiteralPath "$PSScriptRoot/LumeGlass.NOTICE" -Destination "$output/LumeGlass.NOTICE"
} finally {$env:INCLUDE=$savedInclude;$env:LIB=$savedLib;$env:PATH=$savedPath}
