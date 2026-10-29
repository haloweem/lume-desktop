param(
    [string]$NativeProbe,
    [string]$OriginalWidgets,
    [string]$OutputDirectory,
    [string]$PlaywrightDirectory,
    [ValidateSet(96,120,144,192)][int]$Dpi = 96,
    [switch]$ReferenceOnly
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$verification = Join-Path $repository 'build-artifacts\native-rebuild\verification'
if (-not $NativeProbe) {
    $NativeProbe = Join-Path $repository 'build-artifacts\native-rebuild\release\lume-widget-foreground-capture.exe'
}
if (-not $OriginalWidgets) {
    $weatherReport = Join-Path $verification 'weather-reference-20260926\reference-layout.json'
    if (-not (Test-Path -LiteralPath $weatherReport -PathType Leaf)) {
        throw 'Pass -OriginalWidgets; the existing weather source-location report was not found.'
    }
    $OriginalWidgets = (Get-Content -LiteralPath $weatherReport -Raw -Encoding UTF8 | ConvertFrom-Json).source
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $verification "widget-foreground-reference-${Dpi}dpi"
}
if (-not $PlaywrightDirectory) {
    $PlaywrightDirectory = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\node\node_modules\playwright'
}
$NativeProbe = [IO.Path]::GetFullPath($NativeProbe)
$OriginalWidgets = [IO.Path]::GetFullPath($OriginalWidgets)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$PlaywrightDirectory = [IO.Path]::GetFullPath($PlaywrightDirectory)
if (-not (Test-Path -LiteralPath (Join-Path $OriginalWidgets 'index.html') -PathType Leaf)) {
    throw "Missing original widget document: $OriginalWidgets"
}
if (-not (Test-Path -LiteralPath (Join-Path $PlaywrightDirectory 'package.json') -PathType Leaf)) {
    throw "Pass an existing Playwright installation: $PlaywrightDirectory"
}
if ($OutputDirectory.TrimEnd('\','/').Equals($OriginalWidgets.TrimEnd('\','/'),[StringComparison]::OrdinalIgnoreCase) -or
    $OutputDirectory.StartsWith($OriginalWidgets.TrimEnd('\','/')+'\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Capture output must be outside the read-only original widget directory.'
}
$manifestPath = Join-Path $OutputDirectory 'foreground-manifest.json'
$provenancePath = Join-Path $OutputDirectory 'native-capture-provenance.json'
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    $prior = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($prior.dpi -ne $Dpi) { throw 'Use a separate output directory for each DPI.' }
}
if (-not $ReferenceOnly) {
    if (-not (Test-Path -LiteralPath $NativeProbe -PathType Leaf)) {
        throw "Native probe has not been built. This script never builds or changes shared targets: $NativeProbe"
    }
    $nativeHash = (Get-FileHash -LiteralPath $NativeProbe -Algorithm SHA256).Hash.ToLowerInvariant()
    & $NativeProbe --capture $OutputDirectory --dpi $Dpi
    if ($LASTEXITCODE -ne 0) { throw "Native foreground capture failed: $LASTEXITCODE" }
    if ((Get-FileHash -LiteralPath $NativeProbe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $nativeHash) {
        throw 'Native capture executable changed while the probe was running.'
    }
    $capturedManifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $capturedFiles = @{}
    foreach ($captureFile in @($capturedManifest.icon.pixels) + @($capturedManifest.cases | ForEach-Object { $_.pixels })) {
        if ([IO.Path]::GetFileName($captureFile) -ne $captureFile) { throw 'Unsafe native capture filename.' }
        $capturedFiles[$captureFile] = (Get-FileHash -LiteralPath (Join-Path $OutputDirectory $captureFile) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $provenance = [ordered]@{
        version = 1
        capturedAtUtc = [DateTime]::UtcNow.ToString('o')
        nativeExecutable = $NativeProbe
        nativeExecutableSha256 = $nativeHash
        manifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
        files = $capturedFiles
    }
    [IO.File]::WriteAllText($provenancePath, ($provenance | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Missing native capture manifest: $manifestPath"
}
$node = (Get-Command node -ErrorAction Stop).Source
& $node (Join-Path $PSScriptRoot 'capture-widget-foreground-reference.mjs') $OriginalWidgets $OutputDirectory $PlaywrightDirectory
if ($LASTEXITCODE -ne 0) { throw "Original/native comparison failed: $LASTEXITCODE" }
$reportPath = Join-Path $OutputDirectory 'foreground-reference-layout.json'
$report = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$report.results | ForEach-Object {
    $maximum = ($_.geometryDeltas | Measure-Object -Property maxAbsoluteDelta -Maximum).Maximum
    [PSCustomObject]@{
        Scene = $_.id
        SizeDip = "$($_.width)x$($_.height)"
        Dpi = $_.dpi
        MaxGeometryDeltaDip = $maximum
        PixelDifferenceRatio = [Math]::Round($_.pixelDifference.changedRatioOver24,4)
    }
} | Format-Table
Write-Output "Foreground comparison report: $reportPath"
Write-Output 'Diagnostic differences require visual review. Native glass and live desktop composition remain unverified.'
