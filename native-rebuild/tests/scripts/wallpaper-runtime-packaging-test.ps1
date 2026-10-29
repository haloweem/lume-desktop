param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'
$script = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'native-rebuild\tests\scripts\package-original-pages.ps1') -Raw

if ($script -match 'throw "Missing wallpaper runtime audit input:') {
    throw 'Packaging must not require ignored audit sidecars after the runtime hashes were verified'
}
if ($script -notmatch 'Wallpaper runtime audit sidecars are unavailable') {
    throw 'Packaging must record when optional wallpaper runtime audit sidecars are unavailable'
}
if ($script -notmatch "'mpv\.exe'\s*=\s*'3BA74E") {
    throw 'Packaging must continue to pin the wallpaper runtime executable hash'
}

Write-Output 'PASS: wallpaper package recovery depends on pinned payload hashes, not ignored audit sidecars'
