# One-line packaging. Default: Quick (build, installer, full unpack, every
# payload hash, install/update probes; isolated regressions skipped) for a
# build to try out. -Full re-runs every regression before a release.
param([switch]$Full)
$mode = if ($Full) { 'Full' } else { 'Quick' }
$workspace = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$runner = Join-Path $workspace 'build-artifacts\native-rebuild\msvc-run.ps1'
$log = Join-Path $workspace "build-artifacts\pack-$($mode.ToLower()).log"
& powershell -NoProfile -File $runner -Command ".\tests\scripts\package-original-pages.ps1 -IncludeWallpaperValidationRuntime -WallpaperValidationOnly -VerificationMode $mode -OutputDirectory 'H:\refactor\project'" -Log $log
$code = $LASTEXITCODE
Get-Content -LiteralPath $log -Encoding Unicode | Where-Object { $_ -match '^(PACKAGE|SHA256|EVIDENCE|VERIFICATION)=|failed \(' }
exit $code
