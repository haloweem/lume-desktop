$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$sourceRoot = Join-Path $repoRoot 'native-rebuild'
$verificationRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\verification'
New-Item -ItemType Directory -Force -Path $verificationRoot | Out-Null
$report = [System.Collections.Generic.List[string]]::new()
$report.Add("timestamp=$([DateTime]::UtcNow.ToString('o'))")
$report.Add('scope=original-pages-clock-calendar-local-reminders-and-seven-native-features')
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed: $LASTEXITCODE" }
    $report.Add("pass=$Command $($Arguments -join ' ')")
}
Push-Location $sourceRoot
try {
    foreach ($configuration in @('debug', 'release')) {
        Invoke-Checked 'cmake' @('--preset', "windows-$configuration")
        Invoke-Checked 'cmake' @('--build', '--preset', "windows-$configuration")
        Invoke-Checked 'ctest' @('--preset', "windows-$configuration", '--output-on-failure')
    }
    $nodeTests = @(Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'ui') -Filter '*.test.*' -Recurse -File | ForEach-Object FullName)
    if (-not $nodeTests) { throw 'Missing UI behavior tests' }
    Invoke-Checked 'node' (@('--test') + $nodeTests)
} finally { Pop-Location }
$imports = & objdump -p (Join-Path $repoRoot 'build-artifacts\native-rebuild\release\LumeShell.exe')
if ($LASTEXITCODE -ne 0 -or ($imports | Select-String 'DLL Name:.*(libgcc|libstdc|libwinpthread)')) { throw 'Invalid compiler runtime dependencies' }
$report.Add('pass=static-compiler-runtime')
$violations = Get-ChildItem (Join-Path $sourceRoot 'core\ui') -File | Select-String -Pattern '#include.*windows|\bHWND\b'
if ($violations) { throw 'Core page model leaks platform types' }
$report.Add('pass=core-ui-platform-boundary')
$calendarViolations = Get-ChildItem (Join-Path $sourceRoot 'core\calendar') -File | Select-String -Pattern '#include.*(windows|core/clock|platform/)|\b(HWND|WebView|CreateThread)\b'
if ($calendarViolations) { throw 'Calendar core leaks platform or another widget ownership' }
$report.Add('pass=calendar-core-module-boundary')
$report.Add('visual-check=manual real-window screenshots required; unit tests do not prove pixel parity')
$todoViolations = Get-ChildItem (Join-Path $sourceRoot 'core\todo') -File | Select-String -Pattern '#include.*(windows|platform/)|\b(HWND|WebView|CreateThread)\b'
if ($todoViolations) { throw 'Todo core leaks platform ownership' }
$report.Add('pass=todo-core-module-boundary')
foreach ($feature in @('weather','almanac','performance','organizer','desktop_icons','taskbar','minimize_animation')) {
    $featureViolations = Get-ChildItem (Join-Path $sourceRoot "core\$feature") -File |
        Select-String -Pattern '#include.*(windows\.h|platform/)|\b(HWND|WebView|CreateThread)\b' |
        Where-Object { $_.Line -notmatch '^\s*(//|\*)' }
    if ($featureViolations) { throw "Feature core leaks platform ownership: $feature" }
    $report.Add("pass=$feature-core-module-boundary")
}
$report.Add('visual-and-resource-gates=run separate fixed-source screenshot comparisons and process-tree DXGI resource matrix; this script does not replace those gates')
$report.Add('remaining=reminder-notification-glass,Inside-Dream-audio-spectrum,wallpaper-services,music,legacy-data-import,full-hardware-DPI-and-Explorer-matrix,long-run-whole-product-resource-acceptance,installation-upgrade-uninstall')
$report.Add('out-of-scope=account-and-cloud-sync')
$report | Set-Content -LiteralPath (Join-Path $verificationRoot 'original-pages.txt') -Encoding utf8
Write-Output 'ORIGINAL_PAGES_CHECKS_PASS'
