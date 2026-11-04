param([string]$ReleaseRoot,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
if(-not $ReleaseRoot -or -not $OutputDirectory){throw 'Supply ReleaseRoot and a dedicated output directory'}
$release=(Resolve-Path -LiteralPath $ReleaseRoot).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach($name in @('lume-webview-page-window-test.exe','WebView2Loader.dll')){Copy-Item -LiteralPath (Join-Path $release $name) -Destination (Join-Path $output $name)}
foreach($relative in Get-Content -LiteralPath (Join-Path $release 'ui\payload-manifest.txt')){
    if(-not $relative -or $relative -match '(^/|(^|/)\.\.(/|$)|:|\\)'){throw 'Invalid verification UI path'}
    foreach($folder in @('ui','ui-recovery')){
        $destination=Join-Path $output ($folder+'/'+$relative)
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $release ('ui/'+$relative)) -Destination $destination
    }
}
foreach($relative in @('localization.js','music-collections.js','music-visualizers/reference/preference-protocol.js')){
    $target=Join-Path $output ('ui/'+$relative)
    [IO.File]::WriteAllBytes($target,[byte[]]::new((Get-Item -LiteralPath $target).Length))
}
$exe=Join-Path $output 'lume-webview-page-window-test.exe'
$info=[Diagnostics.ProcessStartInfo]::new($exe,'--handshake-only')
$info.UseShellExecute=$false;$info.CreateNoWindow=$true
$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
$process=[Diagnostics.Process]::Start($info)
$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
if(-not $process.WaitForExit(30000)){throw 'Real WebView verification exceeded its deadline'}
($stdout.Result+$stderr.Result) | Set-Content -LiteralPath (Join-Path $output 'webview.log') -Encoding UTF8
if($process.ExitCode -ne 0){Get-Content -LiteralPath (Join-Path $output 'webview.log') -Tail 12;throw 'Real WebView did not recover from the three all-zero scripts'}
Get-Content -LiteralPath (Join-Path $output 'webview.log') -Tail 2
Write-Output 'PASS real WebView: verified recovery of the exact three zero-file failures, ready handshake and clean close'
