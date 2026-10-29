param(
    [switch]$IncludeWallpaperValidationRuntime,
    [string]$WallpaperRuntimeRoot = '',
    [switch]$WallpaperValidationOnly,
    [switch]$AllowDirtySource,
    [ValidateSet('Quick', 'Incremental', 'Full')][string]$VerificationMode = 'Incremental',
    [string]$OutputDirectory = 'H:\refactor\project'
)
$ErrorActionPreference = 'Stop'
# Keep CMake's localized /showIncludes detection and Ninja's output in sync.
[Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
. (Join-Path $PSScriptRoot 'package-verification-cache.ps1')
$totalWatch = [Diagnostics.Stopwatch]::StartNew()
$phaseWatch = [Diagnostics.Stopwatch]::StartNew()
$phaseTimings = [Collections.Generic.List[object]]::new()
function Complete-PackagePhase {
    param([string]$Name)
    $phaseTimings.Add([ordered]@{phase=$Name; seconds=[math]::Round($phaseWatch.Elapsed.TotalSeconds, 3)})
    $phaseWatch.Restart()
}
# Windows PowerShell 5.1 on this build host can return $null from the first
# ProcessStartInfo.EnvironmentVariables access; the next access may return a
# partial dictionary. Seed every current variable explicitly, but only once
# per ProcessStartInfo, so a later probe-specific value cannot reset the clean
# probe PATH back to the developer machine's PATH.
function Initialize-ProbeEnvironment {
    param([System.Diagnostics.ProcessStartInfo]$Info)
    $null = $Info.EnvironmentVariables
    foreach ($key in [Environment]::GetEnvironmentVariables().Keys) {
        $Info.EnvironmentVariables[[string]$key] = [string][Environment]::GetEnvironmentVariable([string]$key)
    }
}
function Set-ProbeEnvironment {
    param([System.Diagnostics.ProcessStartInfo]$Info, [string]$Name, [string]$Value)
    if (-not $Info.PSObject.Properties['LumeEnvironmentSeeded']) {
        Initialize-ProbeEnvironment $Info
        Add-Member -InputObject $Info -NotePropertyName LumeEnvironmentSeeded -NotePropertyValue $true -Force
    }
    $Info.EnvironmentVariables[$Name] = $Value
}
& (Join-Path $PSScriptRoot 'webview-runtime-packaging-test.ps1') -RepositoryRoot $repoRoot
& (Join-Path $PSScriptRoot 'wallpaper-runtime-packaging-test.ps1') -RepositoryRoot $repoRoot
$releaseRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\release'
$packageRoot = Join-Path $repoRoot ('build-artifacts\native-rebuild\package-original-pages\' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
$stage = Join-Path $packageRoot 'stage'
$outputRoot = Join-Path $packageRoot 'output'
$extractRoot = Join-Path $packageRoot 'extracted'
$outputName = 'LumeSetup-1.0.8.exe'
$output = Join-Path $OutputDirectory $outputName
# LUME_ISCC / LUME_INNOUNP select other installs of Inno Setup 6 and innounp.
$iscc = if ($env:LUME_ISCC) { $env:LUME_ISCC } else { Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe' }
$innounp = if ($env:LUME_INNOUNP) { $env:LUME_INNOUNP } else { Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\JurgenRathlev.innounp_Microsoft.Winget.Source_8wekyb3d8bbwe\innounp.exe' }
foreach ($path in @($iscc, $innounp)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing packaging input: $path" }
}
$commit = & git -C $repoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Unable to resolve packaging commit' }
$dirty = & git -C $repoRoot status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect packaging source state' }
if ($dirty -and -not $AllowDirtySource) { throw 'Commit reviewed source changes before packaging, or pass -AllowDirtySource for a local acceptance package' }
$sourceDescription = if ($dirty) {
    "$commit + uncommitted working-tree changes"
} else {
    $commit
}
if (-not $IncludeWallpaperValidationRuntime) {
    throw 'Wallpaper runtime is validation-only until mpv provenance and dependency licenses are complete; use -IncludeWallpaperValidationRuntime for the explicitly labeled local acceptance package'
}
if ($WallpaperValidationOnly -and -not $IncludeWallpaperValidationRuntime) {
    throw 'WallpaperValidationOnly requires IncludeWallpaperValidationRuntime'
}
# Bind the shipped products and statically linked probes to this clean checkout.
# A passing probe alone cannot prove an older LumeShell.exe contains the change.
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
$configureLog = Join-Path $packageRoot 'release-configure.log'
$buildLog = Join-Path $packageRoot 'release-build.log'
& cmake --preset windows-release -S (Join-Path $repoRoot 'native-rebuild') *> $configureLog
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $configureLog -Tail 50; throw 'Release configure failed before packaging' }
Complete-PackagePhase 'configure-and-prerequisites'
& cmake --build $releaseRoot --parallel 5 *> $buildLog
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $buildLog -Tail 60; throw 'Release build failed before packaging' }
Complete-PackagePhase 'release-build'
foreach ($path in @((Join-Path $releaseRoot 'ui\index.html'), (Join-Path $releaseRoot 'lume-webview-page-window-test.exe'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing built packaging input: $path" }
}
# This x86 test binary is only used by interactive acceptance. The shipped
# x86 hosts/hooks already come from the normal Release build; do not rebuild
# a separate verification-only tree on every isolated acceptance package.
$animationTools = $null
if (-not $WallpaperValidationOnly) {
    do {
        $animationTools = Join-Path $repoRoot ('build-artifacts\av-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    } while (Test-Path -LiteralPath $animationTools)
    $animationTools | Set-Content -LiteralPath (Join-Path $packageRoot 'minimize-verification-path.txt') -Encoding utf8
    & (Join-Path $repoRoot 'native-rebuild/platform/windows/minimize_animation/build-minimize-hosts.ps1') `
        -OutputDirectory $animationTools -Architecture 32 -IncludeVerification
}
Complete-PackagePhase 'interactive-animation-build'
$licenseRoot = Join-Path $repoRoot 'native-rebuild\licenses'
$licenses = @('dr-flac-LICENSE.txt', 'dr-flac-SOURCES.md', 'lunar-javascript-LICENSE.txt', 'lunar-javascript-SOURCES.md', 'nlohmann-json-LICENSE.txt', 'nlohmann-json-SOURCES.md',
  'opencc-data-LICENSE.txt', 'opencc-data-SOURCES.md', 'opencc-js-LICENSE.txt', 'opencc-js-THIRD_PARTY_LICENSES.md')
$musicOnlineNotices = [ordered]@{
    'ONLINE_PROVENANCE.md' = 'native-rebuild/platform/windows/music_providers/ONLINE_PROVENANCE.md'
    'Folia-AGPL-3.0.txt' = 'native-rebuild/ui/music-visualizers/vendor/licenses/Folia-AGPL-3.0.txt'
    'Folia-NOTICE.md' = 'native-rebuild/ui/music-visualizers/vendor/licenses/Folia-NOTICE.md'
}
$settingsAccountProbes = @(
  'lume-dynamic-island-test', 'lume-desktop-dynamic-island-test',
  'lume-today-todo-session-test', 'lume-desktop-today-todo-test',
  'lume-dock-context-menu-test',
  'lume-dock-reflection-test',
  'lume-wallpaper-session-test',
  'lume-taskbar-controller-test', 'lume-taskbar-store-test', 'lume-taskbar-runtime-test', 'lume-taskbar-session-test',
  'lume-organizer-desktop-test', 'lume-organizer-icons-test', 'lume-organizer-model-test',
  'lume-dock-application-layer-test',
  'lume-launchpad-collection-test',
  'lume-diagnostics-collector-test',
  'lume-application-settings-protocol-test', 'lume-application-settings-service-test',
  'lume-application-settings-update-test', 'lume-application-settings-file-test', 'lume-application-settings-session-test',
  'lume-account-security-test', 'lume-account-jwt-test', 'lume-account-storage-test', 'lume-account-avatar-test',
  'lume-account-http-test', 'lume-account-backend-test', 'lume-account-session-test', 'lume-account-protocol-test',
  'lume-account-rotation-test', 'lume-account-background-test', 'lume-account-engagement-test',
  'lume-shell-activation-test', 'lume-webview-application-commands-test', 'lume-desktop-settings-account-test'
)
$installerProbes = @('lume-installer-shutdown-test')
$musicProbes = @(
  'lume-music-model-test', 'lume-music-cache-test', 'lume-music-provider-test',
  'lume-music-local-files-test', 'lume-music-host-protocol-test', 'lume-music-host-runtime-test',
  'lume-music-audio-engine-test', 'lume-music-http-stream-test', 'lume-music-host-client-test', 'lume-music-host-integration-test',
  'lume-music-session-test', 'lume-music-lyrics-test', 'lume-music-preferences-test',
  'lume-desktop-music-preferences-test', 'lume-music-background-session-test',
  'lume-music-auth-account-test', 'lume-music-auth-http-test', 'lume-music-auth-credentials-test',
  'lume-music-auth-netease_account-test', 'lume-music-auth-qq_account-test', 'lume-music-auth-kugou_account-test',
  'lume-desktop-music-account-test', 'lume-music-system-fonts-test',
  'lume-music-font-assets-test', 'lume-music-webview-resources-test',
  'lume-music-library-test', 'lume-music-library_session-test', 'lume-music-library_worker-test',
  'lume-music-library_protocol-test', 'lume-music-metadata-test', 'lume-desktop-music-library-test',
  'lume-music-online_lyrics-test', 'lume-music-online_provider-test', 'lume-music-online_session-test',
  'lume-music-provider_credential_lease-test', 'lume-music-desktop_controls-test', 'lume-music-desktop_session-test'
)
$updateProbes = @(
  'lume-application-settings-update-observer-test', 'lume-application-settings-installer-test', 'lume-update-notification-policy-test',
  'lume-update-notification-session-test', 'lume-update-presenter-test', 'lume-desktop-update-notification-test'
)
$acceptanceProbes = @(
  'lume-reminder-ledger-test', 'lume-local_reminder_runtime-test', 'lume-reminder_presenter-test',
  'lume-todo-session-test', 'lume-desktop-reminder-test', 'lume-native-material-test', 'lume-material-shader-test',
  'lume-calendar-todo-editor-test', 'lume-calendar-todo-material-test', 'lume-calendar-todo-render-test',
  'lume-native-calendar-window-test', 'lume-native-widget-window-test', 'lume-native-clock-window-test', 'lume-widget-layer-stress-test',
  'lume-weather-session-test', 'lume-weather-view-test', 'lume-almanac-session-test', 'lume-performance-session-test', 'lume-spectrum-session-test',
  'lume-organizer-session-test', 'lume-organizer-icons-test', 'lume-organizer-icon-host-test',
  'lume-organizer-interaction-test', 'lume-organizer-motion-test', 'lume-organizer-drop-target-test', 'lume-widget-menu-native-test',
  'lume-desktop-icons-runtime-test', 'lume-desktop-icons-session-test',
  'lume-taskbar-runtime-test', 'lume-taskbar-session-test', 'lume-taskbar-payload-test',
  'lume-minimize-animation-test', 'lume-minimize-animation-runtime-test', 'lume-minimize-animation-session-test',
  'lume-desktop-taskbar-test', 'minimize-runtime-test32'
  # lume-desktop-minimize-test drives the minimize card, hidden for now; restore
  # it here and in CMakeLists.txt with options.minimizeVisible.
)
foreach ($name in $licenses) {
    if (-not (Test-Path -LiteralPath (Join-Path $licenseRoot $name) -PathType Leaf)) { throw "Missing license: $name" }
}
foreach ($relative in $musicOnlineNotices.Values) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $relative) -PathType Leaf)) {
        throw "Missing native music online provenance or license: $relative"
    }
}
$probeManifestPath = Join-Path $releaseRoot 'acceptance-probes-Release.json'
$probeManifest = Get-Content -LiteralPath $probeManifestPath -Raw | ConvertFrom-Json
Copy-Item -LiteralPath $probeManifestPath -Destination (Join-Path $packageRoot 'acceptance-probes-Release.json')
$probeSources = @{}
foreach ($name in (@('lume-webview-page-window-test', 'lume-organizer-icon-fixture') + $settingsAccountProbes + $installerProbes + $updateProbes + $acceptanceProbes + $musicProbes)) {
    if ($name -eq 'minimize-runtime-test32') {
        if ($WallpaperValidationOnly) { continue }
        $source = Join-Path $animationTools 'minimize-runtime-test32.exe'
        $expectedRoot = $animationTools
    } else {
        $property = $probeManifest.PSObject.Properties[$name]
        if (-not $property) { throw "Acceptance target missing from current CMake manifest: $name" }
        $source = [string]$property.Value
        $expectedRoot = $releaseRoot
    }
    $source = [IO.Path]::GetFullPath($source)
    $rootPrefix = [IO.Path]::GetFullPath($expectedRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $source.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing or out-of-checkout acceptance target: $name ($source)"
    }
    $probeSources[$name] = $source
}
$probeSources.GetEnumerator() | Sort-Object Name | ForEach-Object {
    [ordered]@{ target = $_.Name; source = $_.Value; sha256 = (Get-FileHash -LiteralPath $_.Value -Algorithm SHA256).Hash }
} | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $packageRoot 'acceptance-probe-hashes.json') -Encoding utf8
$probeHashes = @{}
foreach ($entry in (Get-Content -LiteralPath (Join-Path $packageRoot 'acceptance-probe-hashes.json') -Raw | ConvertFrom-Json)) {
    $probeHashes[$entry.target] = $entry.sha256
}
New-Item -ItemType Directory -Force -Path $stage, $outputRoot, $extractRoot, (Split-Path $output -Parent) | Out-Null
$products = @('LumeLauncher.exe', 'LumeShell.exe', 'LumeMusicHost.exe', 'LumeIconHost.exe', 'LumeGlass.dll', 'LumeGlass.NOTICE',
  'WebView2Loader.dll', 'WebView2-LICENSE.txt', 'WebView2-NOTICE.txt',
  'LumeMinimizeHost32.exe', 'LumeMinimizeHost64.exe', 'LumeMinimizeHook32.dll', 'LumeMinimizeHook64.dll',
  'LumeMinimize.NOTICE.md', 'LumeMinimize.SOURCES.md')
foreach ($name in $products) {
    Copy-Item -LiteralPath (Join-Path $releaseRoot $name) -Destination (Join-Path $stage $name)
}
$webViewRuntimeManifestPath = Join-Path $repoRoot 'native-rebuild\packaging\webview2-runtime.json'
$webViewRuntime = Get-Content -LiteralPath $webViewRuntimeManifestPath -Raw | ConvertFrom-Json
if ($webViewRuntime.architecture -ne 'bootstrapper' -or
    $webViewRuntime.installerFile -ne 'MicrosoftEdgeWebView2Setup.exe' -or
    $webViewRuntime.url -notmatch '^https://go\.microsoft\.com/' -or
    $webViewRuntime.sha256 -notmatch '^[A-F0-9]{64}$') {
    throw 'Invalid WebView2 runtime provenance manifest'
}
$webViewRuntimeRoot = Join-Path $repoRoot 'build-artifacts\dependencies\webview2-runtime'
$webViewRuntimeInstaller = Join-Path $webViewRuntimeRoot $webViewRuntime.installerFile
if (-not (Test-Path -LiteralPath $webViewRuntimeInstaller -PathType Leaf)) {
    New-Item -ItemType Directory -Force -Path $webViewRuntimeRoot | Out-Null
    $download = $webViewRuntimeInstaller + '.download-' + [guid]::NewGuid().ToString('N')
    try {
        Invoke-WebRequest -Uri $webViewRuntime.url -OutFile $download -UseBasicParsing
        $signature = Get-AuthenticodeSignature -FilePath $download
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=Microsoft Corporation') {
            throw 'Downloaded WebView2 runtime installer is not signed by Microsoft Corporation'
        }
        Move-Item -LiteralPath $download -Destination $webViewRuntimeInstaller
    } finally {
        if (Test-Path -LiteralPath $download -PathType Leaf) { Remove-Item -LiteralPath $download -Force }
    }
}
if ((Get-FileHash -LiteralPath $webViewRuntimeInstaller -Algorithm SHA256).Hash -ne $webViewRuntime.sha256) {
    throw "Unexpected WebView2 runtime hash: $webViewRuntimeInstaller"
}
Copy-Item -LiteralPath $webViewRuntimeInstaller -Destination (Join-Path $stage $webViewRuntime.installerFile)
$runtimeRoot = if ($WallpaperRuntimeRoot) {
    [IO.Path]::GetFullPath($WallpaperRuntimeRoot)
} else {
    Join-Path $repoRoot 'build-artifacts\native-rebuild\wallpaper-runtime'
}
$allowedRuntimeRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build-artifacts\native-rebuild\wallpaper-runtime')).TrimEnd('\', '/')
if (-not $runtimeRoot.Equals($allowedRuntimeRoot, [StringComparison]::OrdinalIgnoreCase) -and
    -not $runtimeRoot.StartsWith($allowedRuntimeRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Wallpaper runtime must remain under the repository validation directory: $runtimeRoot"
}
$runtimeExpectedHashes = @{
    'mpv.exe' = '3BA74E88277E76E830967BEA421E63492A43603F6950858B1217CC57C2D1A4E5'
    'd3dcompiler_43.dll' = '4B074A3976399DC735484F5D43D04B519B7BDEE8AC719D9AB8ED6BD4E6BE0345'
}
foreach ($runtimeFile in $runtimeExpectedHashes.Keys) {
    $runtimeSource = Join-Path $runtimeRoot $runtimeFile
    if (-not (Test-Path -LiteralPath $runtimeSource -PathType Leaf)) {
        throw "Missing validation wallpaper runtime input: $runtimeSource"
    }
    $runtimeHash = (Get-FileHash -LiteralPath $runtimeSource -Algorithm SHA256).Hash
    if ($runtimeHash -ne $runtimeExpectedHashes[$runtimeFile]) {
        throw "Unexpected validation wallpaper runtime hash: $runtimeFile ($runtimeHash)"
    }
    $runtimeDestination = Join-Path $stage ("wallpaper-runtime\$runtimeFile")
    New-Item -ItemType Directory -Force -Path (Split-Path $runtimeDestination -Parent) | Out-Null
    Copy-Item -LiteralPath $runtimeSource -Destination $runtimeDestination
}
# Lume's own mpv plugin (built here) reads protected wallpapers over a named pipe.
Copy-Item -LiteralPath (Join-Path $releaseRoot 'lume-mpv-stream.dll') -Destination (Join-Path $stage 'wallpaper-runtime\lume-mpv-stream.dll')
$auditFiles = @('player-provenance.json', 'provenance-audit.json', 'REVIEW-HANDOFF.txt')
if (@($auditFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $runtimeRoot $_) -PathType Leaf) }).Count -eq 0) {
    $auditRoot = Join-Path $packageRoot 'wallpaper-runtime-audit'
    New-Item -ItemType Directory -Force -Path $auditRoot | Out-Null
    foreach ($auditFile in $auditFiles) {
        Copy-Item -LiteralPath (Join-Path $runtimeRoot $auditFile) -Destination (Join-Path $auditRoot $auditFile)
    }
} else {
    Write-Warning 'Wallpaper runtime audit sidecars are unavailable; pinned payload hashes remain required and formal-release provenance remains pending.'
}
$runtimeHash = (Get-FileHash -LiteralPath (Join-Path $stage 'wallpaper-runtime\mpv.exe') -Algorithm SHA256).Hash
$runtimeNotice = @"
Lume 壁纸本机验收运行时

本目录只用于本机功能验收，不代表正式商业发行载荷。
播放器来自原版只读目录的验证副本，完整 mpv/FFmpeg/libplacebo 构建来源和依赖许可仍在审计。
请在正式发行前完成来源、哈希、依赖和许可证核对。

mpv.exe SHA-256: $runtimeHash
"@
$runtimeNotice | Set-Content -LiteralPath (Join-Path $stage 'wallpaper-runtime\VALIDATION-ONLY.txt') -Encoding utf8
# Keep the narrowly pinned taskbar dependency complete without copying a stale
# build directory or allowing extra executables into the installer.
$taskbarFiles = @('LICENSE.md', 'NOTICE.md', 'SOURCE.md', 'lume-taskbar-core.patch', 'settings.json', 'SHA256SUMS.txt',
    'MSVC-RUNTIME-LICENSE.docx', 'MSVC-RUNTIME-SOURCES.md', 'MSVC-RUNTIME-DEPENDENCIES.json')
foreach ($architecture in @('x64', 'arm64')) {
    foreach ($file in @('LumeTaskbarCore.exe', 'ExplorerHooks.dll', 'ExplorerTAP.dll', 'ProgramLog.dll',
        'Xaml.dll', 'resources.pri', 'Assets/SplashScreen.jpeg', 'Assets/SplashScreen.scale-400.jpeg',
        'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll')) {
        $taskbarFiles += "$architecture/$file"
    }
    if ($architecture -eq 'x64') { $taskbarFiles += 'x64/vcruntime140_1.dll' }
}
foreach ($relative in $taskbarFiles) {
    $source = Join-Path $repoRoot ('native-rebuild/vendor/taskbar-runtime/2026.1/' + $relative)
    $builtFile = Join-Path $releaseRoot ('taskbar-runtime/' + $relative)
    if (-not (Test-Path -LiteralPath $builtFile -PathType Leaf) -or
        (Get-FileHash -LiteralPath $builtFile).Hash -ne (Get-FileHash -LiteralPath $source).Hash) {
        throw "Stale pinned taskbar payload: $relative"
    }
    $destination = Join-Path $stage ('taskbar-runtime/' + $relative)
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath $builtFile -Destination $destination
}
# Use the build's UI manifest so packaging and Release share one resource policy.
$uiSource = Join-Path $repoRoot 'native-rebuild\ui'
foreach ($relative in Get-Content -LiteralPath (Join-Path $releaseRoot 'ui/payload-manifest.txt')) {
    if (-not $relative -or $relative -match '(^/|(^|/)\.\.(/|$)|:|\\)') { throw "Invalid UI manifest entry: $relative" }
    $sourceUi = Join-Path $uiSource $relative
    $builtUi = Join-Path $releaseRoot ('ui/' + $relative)
    if (-not (Test-Path -LiteralPath $builtUi -PathType Leaf) -or (Get-FileHash -LiteralPath $builtUi).Hash -ne (Get-FileHash -LiteralPath $sourceUi).Hash) { throw "Stale Release UI: $relative" }
    $destination = Join-Path $stage ('ui/' + $relative)
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath $builtUi -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'native-rebuild\platform\windows\widget_ui\LICENSE') -Destination (Join-Path $stage 'WidgetUI-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'native-rebuild\platform\windows\widget_ui\SOURCES.md') -Destination (Join-Path $stage 'WidgetUI-SOURCES.md')
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'licenses') | Out-Null
foreach ($name in $licenses) { Copy-Item -LiteralPath (Join-Path $licenseRoot $name) -Destination (Join-Path $stage ('licenses/' + $name)) }
$musicOnlineNoticeRoot = Join-Path $stage 'licenses/music-online'
New-Item -ItemType Directory -Force -Path $musicOnlineNoticeRoot | Out-Null
foreach ($entry in $musicOnlineNotices.GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $entry.Value) -Destination (Join-Path $musicOnlineNoticeRoot $entry.Key)
}
Copy-Item -LiteralPath (Join-Path $repoRoot 'native-rebuild\packaging\AcceptanceNotes-seven-features.txt') -Destination (Join-Path $stage 'AcceptanceNotes.txt')
Add-Content -LiteralPath (Join-Path $stage 'AcceptanceNotes.txt') -Value ("`r`nSource: " + $sourceDescription) -Encoding utf8
if ($WallpaperValidationOnly) {
    Add-Content -LiteralPath (Join-Path $stage 'AcceptanceNotes.txt') -Value @"

本包为音乐、设置、账号和壁纸本机验收模式。校验模式：$VerificationMode。每包重新检查完整解包、所有载荷哈希、播放器、WebView2 首屏/关闭、安装退出与真实磨砂和桌面层级；其他隔离回归可复用 24 小时内相同测试程序、运行依赖和环境的成功记录，具体执行/复用列表见打包记录 probe-results.json。Full 模式重新执行全部回归。
依赖当前桌面焦点、鼠标位置或系统窗口时序的组件交互探针本轮未执行，不能据此判断全部组件验收通过。
音乐三平台仅应用内扫码；在线推荐/搜索/歌单/授权播放（T10）已有原生适配和生产接线，真实账号授权、联网读写和播放仍待用户验收。
新增本地库、元数据/封面、队列恢复、音量/静音、真实音频分析，以及默认隐藏的原生桌面音乐组件和系统媒体控制。
桌面音乐包探针仅离屏或verification，不显示实际组件、不注册SMTC；在线包探针使用假传输，不执行匿名或真实账号联网探测。
本地图片缩略图及已授权当前目录条目的LUMEWP01到LUMEWP02转换已实现；旧库自动发现/清单导入、视频缩略图仍未完成。包内背景测试未传历史夹具，旧密文迁移专项跳过。
系统/导入字体、独立背景图库与原版底栏玻璃保留；QQ加密歌词/KRC逐字解码、T11歌词偏移UI、完整设置差异、最终视觉签收和整进程内存对比仍未完成。
在线原生协议来源和相关许可见licenses/music-online；携带文本不代表正式发行许可审查已经完成。
"@ -Encoding utf8
}
$iss = Join-Path $repoRoot 'native-rebuild\packaging\LumeSetup.iss'
$icon = Join-Path $repoRoot 'native-rebuild\packaging\lume.ico'
$shutdownHelper = Join-Path $releaseRoot 'LumeSetupShutdown.exe'
Complete-PackagePhase 'stage-and-source-hashes'
& $iscc "/DSourceDir=$stage" "/DOutputDir=$outputRoot" "/DOutputBaseFilename=$([IO.Path]::GetFileNameWithoutExtension($outputName))" "/DIconFile=$icon" "/DShutdownHelperFile=$shutdownHelper" "/DWebView2RuntimeInstallerFile=$webViewRuntimeInstaller" $iss
if ($LASTEXITCODE -ne 0) { throw "Inno compilation failed: $LASTEXITCODE" }
Complete-PackagePhase 'installer-compression'
$built = Join-Path $outputRoot $outputName
# A full extraction already decodes and checks the compressed data. The old
# -t followed by -x decompressed the same archive twice. Keep extraction and
# the independent SHA-256 comparison of every extracted file.
& $innounp -x -q -b "-d$extractRoot" $built
if ($LASTEXITCODE -ne 0) { throw 'Installer extraction failed' }
Complete-PackagePhase 'installer-extraction'
$extractedShutdownHelper = Join-Path $extractRoot '{tmp}\LumeSetupShutdown.exe'
if ((Get-FileHash -LiteralPath $shutdownHelper).Hash -ne (Get-FileHash -LiteralPath $extractedShutdownHelper).Hash) {
    throw 'Installer shutdown helper payload mismatch'
}
Get-FileHash -LiteralPath $shutdownHelper, $extractedShutdownHelper |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $packageRoot 'installer-shutdown-helper-hashes.json') -Encoding utf8
foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
    $relative = $file.FullName.Substring($stage.Length + 1)
    $payloadRoot = if ($relative -eq 'MicrosoftEdgeWebView2Setup.exe') { '{tmp}' } else { '{app}' }
    $payload = Join-Path $extractRoot "$payloadRoot\$relative"
    if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $payload).Hash) { throw "Payload mismatch: $relative" }
}
$applicationStageFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { $_.Name -ne 'MicrosoftEdgeWebView2Setup.exe' })
if (@(Get-ChildItem -LiteralPath (Join-Path $extractRoot '{app}') -Recurse -File).Count -ne $applicationStageFiles.Count) { throw 'Unexpected file in installer application payload' }
Complete-PackagePhase 'all-payload-hashes'
if ($IncludeWallpaperValidationRuntime) {
    $runtimeProbe = Join-Path $extractRoot '{app}\wallpaper-runtime\mpv.exe'
    $runtimeInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $runtimeInfo.FileName = $runtimeProbe
    $runtimeInfo.Arguments = '--no-config --version'
    $runtimeInfo.WorkingDirectory = Join-Path $extractRoot '{app}\wallpaper-runtime'
    $runtimeInfo.UseShellExecute = $false
    $runtimeInfo.CreateNoWindow = $true
    $runtimeInfo.RedirectStandardOutput = $true
    $runtimeInfo.RedirectStandardError = $true
    Set-ProbeEnvironment $runtimeInfo 'PATH' "$env:SystemRoot\System32;$env:SystemRoot"
    $runtimeProcess = [System.Diagnostics.Process]::Start($runtimeInfo)
    try {
        $runtimeStdout = $runtimeProcess.StandardOutput.ReadToEndAsync()
        $runtimeStderr = $runtimeProcess.StandardError.ReadToEndAsync()
        if (-not $runtimeProcess.WaitForExit(20000)) {
            $runtimeProcess.Kill()
            $runtimeProcess.WaitForExit()
            throw 'Packaged wallpaper runtime probe timed out'
        }
        ($runtimeStdout.GetAwaiter().GetResult() + $runtimeStderr.GetAwaiter().GetResult()) |
            Set-Content -LiteralPath (Join-Path $packageRoot 'clean-path-wallpaper-runtime.txt') -Encoding utf8
        if ($runtimeProcess.ExitCode -ne 0) { throw 'Packaged wallpaper runtime probe failed' }
    } finally { $runtimeProcess.Dispose() }
}
Complete-PackagePhase 'wallpaper-runtime-smoke'
# Test executable is copied only to the verification extraction, never to the installer.
Copy-Item -LiteralPath $probeSources['lume-organizer-icon-fixture'] -Destination (Join-Path $extractRoot '{app}\lume-organizer-icon-fixture.exe')
$probe = Join-Path $extractRoot '{app}\lume-webview-page-window-test.exe'
Copy-Item -LiteralPath $probeSources['lume-webview-page-window-test'] -Destination $probe
$info = [System.Diagnostics.ProcessStartInfo]::new()
$info.FileName = $probe
$info.Arguments = ''
$info.WorkingDirectory = Join-Path $extractRoot '{app}'
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
Set-ProbeEnvironment $info 'PATH' "$env:SystemRoot\System32;$env:SystemRoot"
$pageDiagnostics = Join-Path $packageRoot 'webview-page-artifacts'
New-Item -ItemType Directory -Force -Path $pageDiagnostics | Out-Null
Set-ProbeEnvironment $info 'LUME_CORNER_DIAGNOSTICS' $pageDiagnostics
$process = [System.Diagnostics.Process]::Start($info)
try {
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(25000)) {
        $process.Kill(); $process.WaitForExit()
        throw 'Packaged WebView2 first-homepage and composed-corner probe timed out'
    }
    $probeOutput = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    $probeOutput | Set-Content -LiteralPath (Join-Path $packageRoot 'clean-path-page-test.txt') -Encoding utf8
    if ($process.ExitCode -ne 0) { throw "Packaged WebView2 page failed: $probeOutput" }
} finally {
    $process.Dispose()
    $info.Arguments = ''
    $info.EnvironmentVariables.Remove('LUME_CORNER_DIAGNOSTICS')
}
Complete-PackagePhase 'webview-startup-smoke'
$acceptanceRuns = @($settingsAccountProbes | ForEach-Object {
    [pscustomobject]@{name=$_; mode=$(if($_ -eq 'lume-wallpaper-session-test'){'download-admission-only'}else{''})}
})
# Keep online probes on fake transports, desktop controls offscreen, and desktop
# composition in verification mode. Live/SMTC smoke switches are never automatic.
$acceptanceRuns += @($musicProbes | ForEach-Object { [pscustomobject]@{name=$_; mode=''} })
$acceptanceRuns += @($installerProbes | ForEach-Object { [pscustomobject]@{name=$_; mode=''} })
$acceptanceRuns += @($updateProbes | ForEach-Object { [pscustomobject]@{name=$_; mode=''} })
$acceptanceRuns += [pscustomobject]@{name='lume-update-presenter-test'; mode='real-material'}
$acceptanceRuns += [pscustomobject]@{name='lume-widget-layer-stress-test'; mode=''}
$acceptanceRuns += [pscustomobject]@{name='lume-desktop-icons-session-test'; mode='persistence-only'}
if (-not $WallpaperValidationOnly) {
    $acceptanceRuns += @($acceptanceProbes | Where-Object { $_ -ne 'lume-widget-layer-stress-test' } | ForEach-Object { [pscustomobject]@{name=$_; mode=''} })
    $acceptanceRuns += [pscustomobject]@{name='lume-native-widget-window-test'; mode='snap-system-release'}
    $acceptanceRuns += [pscustomobject]@{name='lume-organizer-session-test'; mode='editor-material'}
    foreach ($name in @('lume-minimize-animation-runtime-test', 'minimize-runtime-test32')) {
        foreach ($mode in @('initialization-regression', 'shutdown-regression')) {
            $acceptanceRuns += [pscustomobject]@{name=$name; mode=$mode}
        }
    }
}
$cacheRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\package-verification-cache'
$environment = [ordered]@{
    os = [Environment]::OSVersion.Version.ToString()
    user = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    runtime = [Environment]::Version.ToString()
    webview = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}' -ErrorAction SilentlyContinue).pv
    graphics = @(Get-CimInstance Win32_VideoController -ErrorAction Stop | Select-Object Name, DriverVersion)
    fonts = @(foreach ($folder in @((Join-Path $env:SystemRoot 'Fonts'), (Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Fonts'))) {
        if (Test-Path -LiteralPath $folder) {
            Get-ChildItem -LiteralPath $folder -File | Sort-Object Name | Select-Object FullName, Length, LastWriteTimeUtc
        }
    })
}
$policyHash = Get-PackageTextHash ((Get-FileHash -LiteralPath $PSCommandPath).Hash +
    (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'package-verification-cache.ps1')).Hash + ($environment | ConvertTo-Json -Compress))
$uiRoot = Join-Path $stage 'ui'
$uiPrefix = [IO.Path]::GetFullPath($uiRoot).TrimEnd('\') + '\'
$runtimeFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {
    -not $_.FullName.StartsWith($uiPrefix, [StringComparison]::OrdinalIgnoreCase) -and
    $_.Name -notin @('LumeShell.exe', 'AcceptanceNotes.txt')
})
# LumeShell code is statically linked into the desktop integration probes.
# Auxiliary executables and DLLs are external inputs and must invalidate them.
$runtimeHash = Get-PackageTextHash ((Get-PackageFileSetHash $stage $runtimeFiles) + '|' + $probeHashes['lume-organizer-icon-fixture'])
$uiHash = Get-PackageFileSetHash $uiRoot @(Get-ChildItem -LiteralPath $uiRoot -Recurse -File)
$probeResults = [Collections.Generic.List[object]]::new()
$executedProbes = 0
$reusedProbes = 0
$skippedProbes = 0
foreach ($run in $acceptanceRuns) {
    $name = $run.name
    $logName = if ($run.mode) { "$name-$($run.mode)" } else { $name }
    $probeWatch = [Diagnostics.Stopwatch]::StartNew()
    $context = Get-PackageProbeContext $name $runtimeHash $uiHash
    $cacheKey = Get-PackageTextHash ($policyHash + '|' + $probeHashes[$name] + '|' + $logName + '|' + $context)
    $cachePath = Join-Path $cacheRoot ($cacheKey + '.json')
    $alwaysRun = $name -in @('lume-installer-shutdown-test', 'lume-update-presenter-test',
        'lume-widget-layer-stress-test', 'lume-music-system-fonts-test')
    # Quick: a build to try out. Packaging, unpack, payload hashes and the
    # install/update probes above still run; the isolated regressions do not.
    if ($VerificationMode -eq 'Quick' -and -not $alwaysRun) {
        ++$skippedProbes
        $probeResults.Add([ordered]@{name=$logName; status='skipped-quick'})
        continue
    }
    $cached = $null
    if ($VerificationMode -eq 'Incremental' -and $WallpaperValidationOnly -and -not $alwaysRun) {
        $cached = Get-PackageCachedPass $cachePath $cacheKey (Join-Path $repoRoot 'build-artifacts')
    }
    if ($null -ne $cached) {
        ++$reusedProbes
        $probeResults.Add([ordered]@{name=$logName; status='reused'; key=$cacheKey;
            evidence=$cached.logPath; completedAtUtc=$cached.completedAtUtc; seconds=[math]::Round($probeWatch.Elapsed.TotalSeconds, 3)})
        continue
    }
    # A forced run that fails must invalidate an older success for this key.
    if (Test-Path -LiteralPath $cachePath -PathType Leaf) { Remove-Item -LiteralPath $cachePath -Force }
    $acceptanceProbe = Join-Path $extractRoot ('{app}\' + $name + '.exe')
    if ($name -eq 'lume-update-presenter-test' -and -not $run.mode) {
        $fallbackRoot = Join-Path $packageRoot 'update-presenter-fallback'
        New-Item -ItemType Directory -Force -Path $fallbackRoot | Out-Null
        $acceptanceProbe = Join-Path $fallbackRoot ($name + '.exe')
    }
    Copy-Item -LiteralPath $probeSources[$name] -Destination $acceptanceProbe
    $info.FileName = $acceptanceProbe
    $info.Arguments = if ($name -eq 'lume-native-material-test') {
        '"' + (Join-Path $extractRoot '{app}\LumeGlass.dll') + '"'
    } elseif ($name -eq 'lume-calendar-todo-material-test') {
        $materialArtifacts = Join-Path $packageRoot 'todo-material-artifacts'
        New-Item -ItemType Directory -Force -Path $materialArtifacts | Out-Null
        '"' + $materialArtifacts + '"'
    } elseif ($name -eq 'lume-taskbar-payload-test') {
        '"' + (Join-Path $extractRoot '{app}\taskbar-runtime') + '"'
    } elseif ($name -eq 'minimize-runtime-test32') {
        '--payload-directory "' + (Join-Path $extractRoot '{app}') + '"'
    } elseif ($name -eq 'lume-application-settings-file-test') {
        '"' + (Join-Path $packageRoot 'settings-fixtures') + '"'
    } elseif ($name -eq 'lume-installer-shutdown-test') {
        '"' + $extractedShutdownHelper + '"'
    } elseif ($name -eq 'lume-music-host-integration-test') {
        '"' + (Join-Path $extractRoot '{app}\LumeMusicHost.exe') + '"'
    } elseif ($name -eq 'lume-music-desktop_controls-test') {
        '"' + (Join-Path $packageRoot 'music-desktop-artifacts') + '"'
    } elseif ($name -eq 'lume-update-notification-session-test' -or $name -eq 'lume-desktop-update-notification-test') {
        '"' + (Join-Path $packageRoot ($name + '-fixtures')) + '"'
    } else { '' }
    if ($run.mode -eq 'real-material') {
        $updateMaterialArtifacts = Join-Path $packageRoot 'update-material-artifacts'
        New-Item -ItemType Directory -Force -Path $updateMaterialArtifacts | Out-Null
        $info.Arguments = '--real-material "' + $updateMaterialArtifacts + '"'
    } elseif ($run.mode -eq 'initialization-regression') {
        $info.Arguments = '--initialization-regression'
    } elseif ($run.mode -eq 'shutdown-regression') {
        $info.Arguments = '--shutdown-regression ' + $info.Arguments
    } elseif ($run.mode -eq 'snap-system-release' -or $run.mode -eq 'editor-material' -or $run.mode -eq 'persistence-only' -or $run.mode -eq 'download-admission-only') {
        $info.Arguments = '--' + $run.mode
    }
    Set-ProbeEnvironment $info 'LUME_CALENDAR_PROBE_OUTPUT' (Join-Path $packageRoot 'calendar-artifacts')
    Set-ProbeEnvironment $info 'LUME_CALENDAR_TODO_PROBE_OUTPUT' (Join-Path $packageRoot 'calendar-todo-artifacts')
    Set-ProbeEnvironment $info 'LUME_ORGANIZER_EDITOR_DIAGNOSTICS' (Join-Path $packageRoot 'organizer-editor-artifacts')
    $process = [System.Diagnostics.Process]::Start($info)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(120000)) { $process.Kill(); $process.WaitForExit(); throw "Packaged $logName timed out" }
        $probeOutput = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        $probeLog = Join-Path $packageRoot ("clean-path-$logName.txt")
        $probeOutput | Set-Content -LiteralPath $probeLog -Encoding utf8
        if ($process.ExitCode -ne 0) { throw "Packaged $logName failed (exit $($process.ExitCode)): $probeOutput" }
        ++$executedProbes
        Save-PackageCachedPass $cachePath $cacheKey $probeLog
        $probeResults.Add([ordered]@{name=$logName; status='executed'; key=$cacheKey;
            evidence=$probeLog; seconds=[math]::Round($probeWatch.Elapsed.TotalSeconds, 3)})
    } finally { $process.Dispose() }
}
$probeResults | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $packageRoot 'probe-results.json') -Encoding utf8
Complete-PackagePhase 'acceptance-probes'
$previousHash = $null
if (Test-Path -LiteralPath $output -PathType Leaf) {
    $previousHash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    $previous = Join-Path $packageRoot 'previous-LumeSetup-1.0.8.exe'
    Copy-Item -LiteralPath $output -Destination $previous
    if ((Get-FileHash -LiteralPath $previous -Algorithm SHA256).Hash -ne $previousHash) { throw 'Previous installer backup verification failed' }
}
foreach ($suffix in @('.sha256.txt', '.验收说明.txt')) {
    if (Test-Path -LiteralPath ($output + $suffix) -PathType Leaf) {
        Copy-Item -LiteralPath ($output + $suffix) -Destination (Join-Path $packageRoot ('previous-LumeSetup-1.0.8.exe' + $suffix))
    }
}
# Prepare and verify on the destination volume, then replace the installer in
# one filesystem operation. Interrupted copying must not truncate the old EXE.
$pendingOutput = $output + '.pending-' + [guid]::NewGuid().ToString('N')
Copy-Item -LiteralPath $built -Destination $pendingOutput
if ((Get-FileHash -LiteralPath $pendingOutput -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash) {
    throw 'Pending installer copy verification failed; existing installer was preserved'
}
if ($null -ne $previousHash) {
    if ((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash -ne $previousHash) { throw 'Installer changed during packaging; refusing to replace it' }
    # PowerShell can bind $null to an empty string for this string overload.
    # Use the already verified backup path explicitly and verify it again.
    [IO.File]::Replace($pendingOutput, $output, $previous)
    if ((Get-FileHash -LiteralPath $previous -Algorithm SHA256).Hash -ne $previousHash) { throw 'Replaced installer backup hash mismatch' }
} else {
    [IO.File]::Move($pendingOutput, $output)
}
$hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
if ($hash -ne (Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash) { throw 'Published installer hash mismatch' }
Copy-Item -LiteralPath (Join-Path $stage 'AcceptanceNotes.txt') -Destination (Join-Path $packageRoot ($outputName + '.验收说明.txt')) -Force
$verificationLines = @(
    "package=$output",
    "sha256=$hash",
    "bytes=$((Get-Item -LiteralPath $output).Length)",
    "source=$sourceDescription",
    "previousSha256=$previousHash",
    "verification-mode=$VerificationMode",
    "executed-acceptance-probes=$executedProbes",
    "reused-acceptance-probes=$reusedProbes",
    "skipped-acceptance-probes=$skippedProbes",
    'probe-evidence=probe-results.json; reused records are previous identical-input successes, not fresh runs',
    'integrity=pass',
    'all-payload-hashes=pass',
    'clean-path-webview-page-ready-and-close=pass',
    'first-visible-homepage=pass; startup-ready-gate-and-first-composed-frame',
    'install-upgrade-uninstall-and-commercial-release-eligibility=pending',
    'defaultDir=D:\lume; existing install path reused',
    'wallpaper-runtime=validation-only; formal-release-provenance-pending',
    'clean-path-wallpaper-runtime-version-probe=pass'
    'clean-path-settings-account-isolated-and-lifecycle=pass'
    'clean-path-music-model-local-lyrics-session-host=pass'
    'clean-path-music-library-metadata-worker-protocol-and-persistence=pass'
    'clean-path-music-online-provider-lyrics-session-credential-lease=pass; fake-transports'
    'clean-path-music-desktop-controls-and-session=pass; offscreen-or-verification; no-widget-show-or-smtc-registration'
    'clean-path-desktop-music-library-manager-close-reopen=pass; real-webview-fake-audio-verification'
    'music-online-provenance-and-license-payload-hashes=pass; licenses/music-online'
    'music-background-legacy-conversion=not-run; historical-fixture-not-supplied'
    'music-real-provider-authorization-network-mutations-and-playback=pending-user-verification'
    'music-qq-encrypted-lyrics-krc-word-decoding-and-t11-offset-ui=not-complete'
    'music-visual-video-settings-parity-and-process-memory-ab=pending'
    'clean-path-installer-shutdown-and-helper-hash=pass'
    'clean-path-update-observer-policy-schedule-hidden-shell=pass'
    'clean-path-update-presenter-fallback-and-real-material=pass'
    'real-account-authorization-and-profile-upload=pending-user-verification'
)
if ($WallpaperValidationOnly) {
    $verificationLines += 'interactive-acceptance-probes=skipped; wallpaper-validation-only-mode'
    $verificationLines += 'desktop-state-dependent-component-probes=not-run'
} else {
    $verificationLines += @(
        'clean-path-shared-material-capture-and-lifecycle=pass',
        'visual-corner-screen-sampling=pass; strict-black-white-composition-and-independent-pixel-area-reference',
        'clean-path-local-reminders-and-shell-lifecycle=pass',
        'clean-path-todo-material-render-and-month-picker-hit-test=pass',
        'clean-path-weather-almanac-performance-organizer-lifecycle=pass',
        'clean-path-widget-slow-system-drag-release=pass',
        'clean-path-organizer-transparent-editor-composition-and-input=pass',
        'clean-path-desktop-icons-private-fixture-lifecycle=pass',
        'clean-path-taskbar-controller-and-pinned-payload=pass',
        'clean-path-animation-host-hooks-and-manager-settings=pass',
        'clean-path-animation-x86-and-x64-cross-process-fixtures=pass'
    )
}
$verificationLines += 'scope=music-settings-account-wallpaper-validation-package; calendar-and-todo-local-only; live-system-matrix-see-acceptance-notes'
$verificationLines | Set-Content -LiteralPath (Join-Path $packageRoot ($outputName + '.sha256.txt')) -Encoding utf8
Complete-PackagePhase 'delivery'
[ordered]@{totalSeconds=[math]::Round($totalWatch.Elapsed.TotalSeconds, 3); phases=@($phaseTimings.ToArray());
    executedProbes=$executedProbes; reusedProbes=$reusedProbes; skippedProbes=$skippedProbes} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $packageRoot 'timings.json') -Encoding utf8
Write-Output "PACKAGE=$output"
Write-Output "SHA256=$hash"
Write-Output "EVIDENCE=$packageRoot"
Write-Output "VERIFICATION=$VerificationMode; executed=$executedProbes; reused=$reusedProbes; skipped=$skippedProbes; seconds=$([math]::Round($totalWatch.Elapsed.TotalSeconds, 1))"
