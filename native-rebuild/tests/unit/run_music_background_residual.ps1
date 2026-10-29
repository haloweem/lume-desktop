param([string]$History = 'H:/refactor/history/lume1.0.1')
$ErrorActionPreference = 'Stop'
$native = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$repository = Split-Path $native -Parent
$cache = Join-Path $repository 'build-artifacts/native-rebuild/debug/CMakeCache.txt'
$compiler = (Select-String -LiteralPath $cache -Pattern '^CMAKE_CXX_COMPILER:FILEPATH=').Line.Split('=', 2)[1]
$output = Join-Path $repository ('build-artifacts/native-rebuild/music-background-residual-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
Push-Location $native
try {
    $fixture = & node tests/unit/music_background_legacy_fixture.mjs $History
    if ($LASTEXITCODE -ne 0) { throw '历史密文样本生成失败' }
    $sources = @(
        'tests/unit/music_background_session_test.cpp', 'apps/shell/music_background_session.cpp',
        'core/music/music_background.cpp', 'core/music/music_background_store.cpp',
        'core/ui/music_background_protocol.cpp', 'core/wallpaper/library.cpp', 'core/wallpaper/catalog.cpp',
        'platform/windows/local_settings_file_store.cpp', 'platform/windows/wallpaper/download.cpp',
        'platform/windows/wallpaper/media_server.cpp'
    )
    $binary = Join-Path $output 'music-background-residual-test.exe'
    & $compiler -std=c++17 -O0 -g -Wall -Wextra -Wpedantic -DUNICODE -D_UNICODE `
        -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -I. @sources -static `
        -lole32 -lshell32 -luuid -lwindowscodecs -lwinhttp -lbcrypt -lcrypt32 -lws2_32 -o $binary `
        2>&1 | Tee-Object -FilePath (Join-Path $output 'build.log')
    if ($LASTEXITCODE -ne 0) { throw '背景定向构建失败' }
    & $binary $fixture 2>&1 | Tee-Object -FilePath (Join-Path $output 'native-test.log')
    if ($LASTEXITCODE -ne 0) { throw '背景定向测试失败' }
    Write-Output "验证证据：$output"
    Write-Output "历史隔离样本：$fixture"
} finally {
    Pop-Location
}
