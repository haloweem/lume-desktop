param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

function Require-Match {
    param([string]$Text, [string]$Pattern, [string]$Message)
    if ($Text -notmatch $Pattern) { throw $Message }
}

$packageScript = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'native-rebuild\tests\scripts\package-original-pages.ps1') -Raw
$installer = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'native-rebuild\packaging\LumeSetup.iss') -Raw
$manifestPath = Join-Path $RepositoryRoot 'native-rebuild\packaging\webview2-runtime.json'

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'WebView2 runtime provenance manifest is missing'
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.architecture -ne 'bootstrapper' -or $manifest.installerFile -ne 'MicrosoftEdgeWebView2Setup.exe') {
    throw 'WebView2 runtime manifest must pin the official Evergreen bootstrapper'
}
if ($manifest.url -notmatch '^https://go\.microsoft\.com/' -or $manifest.sha256 -notmatch '^[A-F0-9]{64}$') {
    throw 'WebView2 runtime manifest must include an official HTTPS source and SHA-256'
}

Require-Match $packageScript 'webview2-runtime\.json' 'Packaging script must load the WebView2 runtime provenance manifest'
Require-Match $packageScript 'webview-runtime-packaging-test\.ps1' 'Packaging entry point must run the WebView2 deployment regression check'
Require-Match $packageScript 'Unexpected WebView2 runtime hash' 'Packaging script must reject an unpinned WebView2 runtime'
Require-Match $packageScript 'MicrosoftEdgeWebView2Setup\.exe' 'Packaging script must stage the WebView2 bootstrapper'
Require-Match $installer 'WebView2RuntimeInstallerFile' 'Installer must receive the staged WebView2 runtime installer path'
Require-Match $installer 'DestDir: "\{tmp\}"; DestName: "MicrosoftEdgeWebView2Setup\.exe"; Flags: dontcopy' 'WebView2 runtime installer must be an installer-only temporary payload'
Require-Match $installer 'F3017226-FE2A-4295-8BDF-00C3A9A7E4C5' 'Installer must use the documented WebView2 runtime registration key'
Require-Match $installer 'procedure EnsureWebView2Runtime' 'Installer must repair a missing WebView2 runtime before launch'
Require-Match $installer 'Exec\(ExpandConstant\(''\{tmp\}\\MicrosoftEdgeWebView2Setup\.exe''\)' 'Installer must launch the staged WebView2 runtime installer'
Require-Match $installer 'WebView2 Runtime 安装失败' 'Installer must stop when a required WebView2 runtime cannot be installed'

Require-Match $installer '(?s)ExtractTemporaryFile\(''MicrosoftEdgeWebView2Setup\.exe''\).*?Exec\(ExpandConstant\(''\{tmp\}\\MicrosoftEdgeWebView2Setup\.exe''' 'A dontcopy bootstrapper must be extracted before execution'
Require-Match $installer '\[event\(''PrepareToInstall''\)\]' 'Runtime prerequisites must be checked before application files are changed'
Require-Match $installer '(?s)function PrepareWebView2Runtime.*?try.*?EnsureWebView2Runtime.*?except.*?Result := GetExceptionMessage' 'Runtime failures must stop preparation with a retryable error'
Require-Match $installer 'Result = ''0\.0\.0\.0''' 'An uninstalled runtime registration must not count as an installed runtime'
if ($installer -match 'WebView2RuntimeVersion\(HKCU') {
    throw 'An elevated installer must not accept a different administrator account per-user runtime'
}
Write-Output 'PASS: WebView2 deployment source checks passed (runtime installation requires separate validation)'
