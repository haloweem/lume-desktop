param([string]$Compiler = "g++")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$native = Join-Path $root "native-rebuild"
$output = Join-Path $root "build-artifacts/music-online-session-tests"
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @(
    "tests/unit/music_online_session_test.cpp",
    "apps/shell/music_online_session.cpp",
    "core/music/music_lyrics.cpp",
    "core/music/music_online_lyrics.cpp",
    "platform/windows/music_online_provider.cpp",
    "platform/windows/music_provider_http.cpp",
    "platform/windows/music_provider_crypto.cpp",
    "platform/windows/music_providers/netease_account.cpp",
    "platform/windows/music_providers/qq_account.cpp",
    "platform/windows/music_providers/kugou_account.cpp"
) | ForEach-Object { Join-Path $native $_ }
$exe = Join-Path $output "music_online_session_test.exe"
& $Compiler "-std=c++17" "-D_WIN32_WINNT=0x0A00" "-Wall" "-Wextra" "-Wpedantic" "-I" $native @sources "-o" $exe "-lwinhttp" "-lbcrypt" "-lcrypt32"
if ($LASTEXITCODE -ne 0) { throw "Music online session test build failed." }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Music online session tests failed." }
