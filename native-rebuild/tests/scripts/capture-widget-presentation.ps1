param(
    [string]$NativeProbe,
    [string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if (-not $NativeProbe) {
    $NativeProbe=Join-Path $repository 'build-artifacts\native-rebuild\release\lume-native-widget-window-test.exe'
}
if (-not $OutputDirectory) {
    $OutputDirectory=Join-Path $repository 'build-artifacts\native-rebuild\verification\widget-presentation'
}
$NativeProbe=[IO.Path]::GetFullPath($NativeProbe)
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$artifactRoot=(Join-Path $repository 'build-artifacts').TrimEnd('\')+'\'
if (-not $OutputDirectory.StartsWith($artifactRoot,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Screen evidence must stay in this repository build-artifacts directory.'
}
$material=Join-Path ([IO.Path]::GetDirectoryName($NativeProbe)) 'LumeGlass.dll'
$binaries=@{}
foreach ($file in @($NativeProbe,$material)) {
    $binaries[$file]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$started=[DateTime]::UtcNow.ToString('o')
& $NativeProbe --capture-presentation $OutputDirectory *> (Join-Path $OutputDirectory 'capture.log')
$nativeExit=$LASTEXITCODE
foreach ($file in @($NativeProbe,$material)) {
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $binaries[$file]) {
        throw "Binary changed during visual capture: $file"
    }
}
$reportPath=Join-Path $OutputDirectory 'presentation-report.json'
if (-not (Test-Path -LiteralPath $reportPath)) {
    Get-Content -LiteralPath (Join-Path $OutputDirectory 'capture.log') -Tail 10
    throw "Capture did not produce a report. Exit code: $nativeExit"
}
$report=Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8|ConvertFrom-Json
Add-Type -AssemblyName System.Drawing
$hashes=@{}
$reviews=@()
foreach ($mode in $report.modes) {
    $modePath=Join-Path $OutputDirectory $mode.mode
    foreach ($frame in $mode.frames) {
        if ([IO.Path]::GetFileName($frame.file) -ne $frame.file) { throw 'Unsafe frame name.' }
        $bmp=Join-Path $modePath $frame.file
        $image=[Drawing.Image]::FromFile($bmp)
        try {
            if ($image.Width -ne $mode.width -or $image.Height -ne $mode.height) { throw 'Unexpected frame size.' }
            $png=[IO.Path]::ChangeExtension($bmp,'.png')
            $image.Save($png,[Drawing.Imaging.ImageFormat]::Png)
        } finally { $image.Dispose() }
        foreach ($file in @($bmp,$png)) {
            $relative=$file.Substring($OutputDirectory.Length+1)
            $hashes[$relative]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $picked=@(
        ($mode.frames|Where-Object { $_.phaseBefore -eq 0 -and $_.phaseAfter -eq 0 }|Select-Object -Last 1),
        ($mode.frames|Where-Object { $_.phaseBefore -eq 1 -and $_.changedFromBackgroundRatio -gt 0 }|Select-Object -First 1),
        ($mode.frames|Where-Object { $_.phaseBefore -eq 1 }|Select-Object -Last 1),
        ($mode.frames|Where-Object { $_.phaseBefore -eq 2 -and $_.phaseAfter -eq 2 }|Select-Object -First 1),
        ($mode.frames|Where-Object { $_.phaseBefore -eq 2 -and $_.phaseAfter -eq 2 }|Select-Object -Last 1),
        ($mode.frames|Where-Object { $_.phaseBefore -eq 3 -or $_.phaseAfter -eq 3 }|Select-Object -First 1),
        ($mode.frames|Where-Object { $_.phaseBefore -eq 4 -and $_.phaseAfter -eq 4 }|Select-Object -First 1),
        ($mode.frames|Where-Object { $_.phaseBefore -eq 4 -and $_.phaseAfter -eq 4 }|Select-Object -Last 1)
    ) | Where-Object { $null -ne $_ } | Sort-Object startMs -Unique
    $rowHeight=[int]$mode.height+30
    $sheet=[Drawing.Bitmap]::new([int]$mode.width*2,[int][Math]::Ceiling($picked.Count/2)*$rowHeight)
    $graphics=[Drawing.Graphics]::FromImage($sheet)
    $font=[Drawing.Font]::new('Segoe UI',10)
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(32,32,32))
        for ($i=0;$i -lt $picked.Count;$i++) {
            $frame=$picked[$i];$x=($i%2)*[int]$mode.width;$y=[int][Math]::Floor($i/2)*$rowHeight
            $label="$($mode.mode) $($frame.file)  $($frame.startMs)ms  $($report.phases[$frame.phaseBefore])"
            $graphics.DrawString($label,$font,[Drawing.Brushes]::White,[single]($x+5),[single]($y+5))
            $image=[Drawing.Image]::FromFile((Join-Path $modePath ([IO.Path]::ChangeExtension($frame.file,'.png'))))
            try { $graphics.DrawImageUnscaled($image,$x,$y+30) } finally { $image.Dispose() }
        }
        $sheetPath=Join-Path $OutputDirectory "$($mode.mode)-review.png"
        $sheet.Save($sheetPath,[Drawing.Imaging.ImageFormat]::Png)
    } finally { $font.Dispose();$graphics.Dispose();$sheet.Dispose() }
    $reviews+=@{mode=$mode.mode;frames=@($picked.file);sheet=[IO.Path]::GetFileName($sheetPath)}
    Write-Output "$($mode.mode): $($mode.frames.Count) frames, DPI $($mode.dpi), passed=$($mode.passed), max near-black ratio=$($mode.maximumNearBlackRatio), final background delta=$($mode.finalBackgroundDifferenceRatio)"
}
$hashes['presentation-report.json']=(Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToLowerInvariant()
$provenance=[ordered]@{
    version=1;capturedAtUtc=$started;nativeExitCode=$nativeExit
    binaries=$binaries;files=$hashes;reviews=$reviews
    scope='Actual production host and material, isolated patterned background, actual monitor DPI, sampled compositor frames.'
}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'presentation-provenance.json'),
    ($provenance|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
if ($nativeExit -ne 0 -or -not $report.passed) { throw "Presentation checks failed. Inspect $reportPath" }
Write-Output "Presentation evidence: $OutputDirectory"
