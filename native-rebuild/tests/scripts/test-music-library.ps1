param([string]$Cxx = "g++")

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$output = Join-Path $root "build-artifacts/music-library-tests"
New-Item -ItemType Directory -Path $output -Force | Out-Null
$common = @("-std=c++17", "-DNOMINMAX", "-DUNICODE", "-D_UNICODE",
    "-Wall", "-Wextra", "-Werror", "-I", $root)
$native = @("apps/shell/music_library_session.cpp", "core/music/music_library.cpp",
    "platform/windows/music_local_files.cpp", "platform/windows/music_metadata.cpp")
$libraries = @("-lole32", "-lshell32", "-lwindowscodecs", "-luuid")
$cases = @(
    @{ Name = "music_library"; Sources = @("core/music/music_library.cpp", "core/music/music_model.cpp"); Libs = @() },
    @{ Name = "music_library_session"; Sources = $native; Libs = $libraries },
    @{ Name = "music_metadata"; Sources = $native; Libs = $libraries },
    @{ Name = "music_local_files"; Sources = @("platform/windows/music_local_files.cpp"); Libs = @() }
)
foreach ($case in $cases) {
    $sources = @("tests/unit/$($case.Name)_test.cpp") + $case.Sources
    $absolute = @($sources | ForEach-Object { Join-Path $root $_ })
    $exe = Join-Path $output "$($case.Name)_test.exe"
    Write-Host "BUILD: $($case.Name)"
    & $Cxx @common @absolute @($case.Libs) "-o" $exe
    if ($LASTEXITCODE -ne 0) { throw "$($case.Name) build failed: $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "$($case.Name) failed: $LASTEXITCODE" }
}
Write-Host "PASS: music library checks (4/4)"
