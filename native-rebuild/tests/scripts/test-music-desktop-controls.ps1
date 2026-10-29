param(
    [string]$BuildDirectory = "$PSScriptRoot/../../../build-artifacts/native-rebuild/debug",
    [string]$OutputDirectory = "$PSScriptRoot/../../../build-artifacts/music-desktop-controls"
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath("$PSScriptRoot/../..")
$build = [IO.Path]::GetFullPath($BuildDirectory)
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$compiler = (Get-Command g++ -ErrorAction Stop).Source
$libraries = @('lume-widget-window','lume-widget-ui','lume-widget-material','lume-window-surface','lume-core') |
    ForEach-Object { Join-Path $build "lib$_.a" }
foreach ($library in $libraries) {
    if (!(Test-Path -LiteralPath $library)) { throw "缺少既有构建库：$library" }
}
$exe = Join-Path $output 'music-desktop-controls-test.exe'
$flags = @('-std=c++17','-Wall','-Wextra','-Wpedantic','-Werror','-static',
    '-DUNICODE','-D_UNICODE','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-D_WIN32_WINNT=0x0A00','-DWINVER=0x0A00',
    '-finput-charset=UTF-8','-fexec-charset=UTF-8', '-I', $root)
& $compiler @flags "$root/tests/unit/music_desktop_controls_test.cpp" -o $exe @libraries `
    -lruntimeobject -lshcore -lcrypt32 -lwindowscodecs -ld2d1 -ldwrite -lole32 -luuid `
    -luser32 -lgdi32 -lcomctl32 -ldwmapi -lgdiplus -luxtheme -lshell32
if ($LASTEXITCODE) { throw "T13 独立测试编译失败：$LASTEXITCODE" }
& $exe $output
if ($LASTEXITCODE) { throw "T13 独立测试失败：$LASTEXITCODE" }
