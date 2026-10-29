param([string]$Compiler = "g++", [string]$Ffmpeg = "ffmpeg")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$native = Join-Path $root "native-rebuild"
$output = Join-Path $root "build-artifacts/music-host-bounds-tests"
New-Item -ItemType Directory -Path $output -Force | Out-Null
$common = @("-std=c++17", "-D_WIN32_WINNT=0x0A00", "-DUNICODE", "-D_UNICODE",
    "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX", "-Wall", "-Wextra", "-Wpedantic", "-I", $native)
$protocol = "core/music/music_host_protocol.cpp"
$client = "platform/windows/music_host_client.cpp"
$audio = "platform/windows/music_audio_engine.cpp"
$audioLibs = @("-lwinmm", "-lmfplat", "-lmfreadwrite", "-lmfuuid", "-lole32")
function Build([string]$name, [string[]]$sources, [string[]]$libs = @()) {
    $paths = $sources | ForEach-Object { Join-Path $native $_ }
    & $Compiler @common @paths "-o" (Join-Path $output "$name.exe") @libs
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $name" }
}
function Run([string]$name, [string[]]$arguments = @()) {
    & (Join-Path $output "$name.exe") @arguments
    if ($LASTEXITCODE -ne 0) { throw "Test failed: $name" }
}
Build "LumeMusicHost" @("apps/music-host/main.cpp", "apps/music-host/music_host_runtime.cpp", $protocol, $audio) ($audioLibs + "-lshell32")
Build "music_host_client_test" @("tests/unit/music_host_client_test.cpp", $protocol, $client)
Build "music_audio_engine_test" @("tests/unit/music_audio_engine_test.cpp", $audio) $audioLibs
Build "music_host_integration_test" @("tests/unit/music_host_integration_test.cpp", $protocol, $client)
Build "music_session_test" @("tests/unit/music_session_test.cpp", "apps/shell/music_session.cpp",
    "core/music/music_model.cpp", "core/music/music_lyrics.cpp", "core/ui/music_page_protocol.cpp",
    "platform/windows/music_local_files.cpp", "platform/windows/music_local_lyrics.cpp", $protocol, $client)
Run "music_host_client_test"
Run "music_host_integration_test" @((Join-Path $output "LumeMusicHost.exe"))
Run "music_session_test"
# Short deterministic local fixtures only; no download or credential access.
$formats = @(
    @{Name="stereo192.wav"; Rate=192000; Codec="pcm_s16le"; Extra=@()},
    @{Name="float96.wav"; Rate=96000; Codec="pcm_f32le"; Extra=@()},
    @{Name="large-block.flac"; Rate=48000; Codec="flac"; Extra=@("-frame_size", "32768")},
    @{Name="tone.mp3"; Rate=48000; Codec="libmp3lame"; Extra=@()},
    @{Name="tone.m4a"; Rate=48000; Codec="aac"; Extra=@()}
)
$inputs = @()
foreach ($format in $formats) {
    $file = Join-Path $output $format.Name
    $extra = $format.Extra
    & $Ffmpeg "-hide_banner" "-loglevel" "error" "-y" "-f" "lavfi" "-i" "sine=frequency=1000:duration=1.2:sample_rate=$($format.Rate)" "-ac" "2" "-c:a" $format.Codec @extra $file
    if ($LASTEXITCODE -ne 0) { throw "Fixture generation failed: $($format.Name)" }
    $inputs += $file
}
Run "music_audio_engine_test" $inputs
& node (Join-Path $native "tests/unit/music_host_http_fixture.mjs") (Join-Path $output "music_host_integration_test.exe") (Join-Path $output "LumeMusicHost.exe") (Join-Path $output "stereo192.wav")
if ($LASTEXITCODE -ne 0) { throw "Real MF loopback HTTP test failed" }
Write-Output "Independent music host/audio checks passed: $output"
