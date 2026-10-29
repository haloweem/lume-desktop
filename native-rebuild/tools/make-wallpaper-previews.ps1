param(
    # Folder of the original official videos (the files behind downloadUrl).
    [Parameter(Mandatory = $true)][string]$Source,
    # Output folder; upload its files to the CDN under PreviewBase.
    [Parameter(Mandatory = $true)][string]$Output,
    # Public URL prefix of the uploaded previews.
    [string]$PreviewBase = 'https://static.lumedesktop.com/lume/wallpapers/official/previews/',
    # Optional official video manifest; when given, a copy with previewUrl
    # added for every preview made is written to Output\manifest-with-previews.json.
    [string]$Manifest = '',
    [int]$Seconds = 10,
    [int]$Height = 720,
    [string]$Bitrate = '1500k',
    [string]$Ffmpeg = 'ffmpeg'
)
# Makes short low-bitrate clips so the wallpaper page can preview a video
# before it is downloaded. The CDN caps each connection (about 0.1-0.2 MB/s
# measured); a 10 Mbps original cannot stream at that rate, a 1.5 Mbps
# 720p clip of 10 s (about 2 MB) starts within a second or two.
# Read-only for Source; writes only into Output. Nothing is uploaded.
$ErrorActionPreference = 'Stop'
$Source = (Resolve-Path -LiteralPath $Source).Path
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path -LiteralPath $Output).Path
if (-not $PreviewBase.EndsWith('/')) { $PreviewBase += '/' }
$made = @{}
$videos = Get-ChildItem -LiteralPath $Source -File -Recurse |
    Where-Object { $_.Extension -in '.mp4', '.webm', '.mov', '.m4v' }
foreach ($video in $videos) {
    $name = [IO.Path]::GetFileNameWithoutExtension($video.Name) + '.mp4'
    $target = Join-Path $Output $name
    if (-not (Test-Path -LiteralPath $target)) {
        # H.264 main profile, no audio, fast start so playback begins before
        # the whole clip arrives; the frame keeps its aspect ratio.
        & $Ffmpeg -v error -y -t $Seconds -i $video.FullName -an `
            -vf "scale=-2:'min($Height,ih)'" -c:v libx264 -profile:v main -preset slow `
            -b:v $Bitrate -maxrate $Bitrate -bufsize $Bitrate -pix_fmt yuv420p -movflags +faststart $target
        if ($LASTEXITCODE -ne 0) { Write-Warning "failed: $($video.FullName)"; continue }
    }
    $made[$video.Name] = $PreviewBase + [Uri]::EscapeDataString($name)
    '{0,-60} {1,8:N1} MB -> {2,6:N2} MB' -f $video.Name, ($video.Length / 1MB), ((Get-Item -LiteralPath $target).Length / 1MB)
}
if ($Manifest) {
    $json = Get-Content -LiteralPath $Manifest -Raw -Encoding utf8 | ConvertFrom-Json
    $added = 0
    foreach ($item in $json.wallpapers) {
        if (-not $item.downloadUrl) { continue }
        $file = [Uri]::UnescapeDataString(([Uri]$item.downloadUrl).Segments[-1])
        if ($made.ContainsKey($file)) {
            $item | Add-Member -NotePropertyName previewUrl -NotePropertyValue $made[$file] -Force
            $added++
        }
    }
    $out = Join-Path $Output 'manifest-with-previews.json'
    $json | ConvertTo-Json -Depth 32 -Compress | Set-Content -LiteralPath $out -Encoding utf8
    "manifest: previewUrl added to $added items -> $out"
}
"previews: $($made.Count) in $Output"
