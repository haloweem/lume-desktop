param([string]$Compiler = "g++", [switch]$LiveProbe, [switch]$LiveLyricsProbe, [switch]$LiveQQLyricsProbe)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$native = Join-Path $root "native-rebuild"
$output = Join-Path $root "build-artifacts/music-online-provider-tests"
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sources = @(
    "tests/unit/music_online_provider_test.cpp",
    "core/music/music_lyrics.cpp",
    "core/music/music_online_lyrics.cpp",
    "platform/windows/music_online_provider.cpp",
    "platform/windows/music_provider_http.cpp",
    "platform/windows/music_provider_crypto.cpp",
    "platform/windows/music_providers/netease_account.cpp",
    "platform/windows/music_providers/qq_account.cpp",
    "platform/windows/music_providers/kugou_account.cpp"
) | ForEach-Object { Join-Path $native $_ }
$exe = Join-Path $output "music_online_provider_test.exe"
& $Compiler "-std=c++17" "-D_WIN32_WINNT=0x0A00" "-Wall" "-Wextra" "-Wpedantic" "-I" $native @sources "-o" $exe "-lwinhttp" "-lbcrypt" "-lcrypt32"
if ($LASTEXITCODE -ne 0) { throw "在线适配器测试编译失败。" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "在线适配器假传输测试失败。" }
if ($LiveProbe) {
    & $exe "--live-read-only"
    if ($LASTEXITCODE -ne 0) { throw "部分匿名只读探测未通过，请查看平台错误类别。" }
}
if ($LiveLyricsProbe) {
    & $exe "--live-lyrics-read-only"
    if ($LASTEXITCODE -ne 0) { throw "酷狗匿名逐行歌词探测未通过，请查看错误类别。" }
}
if ($LiveQQLyricsProbe) {
    & $exe "--live-qq-lyrics-read-only"
    if ($LASTEXITCODE -ne 0) { throw "QQ 匿名逐行歌词探测未通过，请查看错误类别。" }
}
