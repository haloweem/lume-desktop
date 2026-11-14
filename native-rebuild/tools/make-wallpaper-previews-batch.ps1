# Batch previews for the current workstation; no cloud credentials required.
[CmdletBinding()]
param(
    [string]$Output = '',
    [ValidateSet('auto','nvenc','libx264')][string]$Encoder = 'auto',
    [ValidateRange(1,3)][int]$Workers = 3,
    [int]$Limit = 0,
    [string]$Python = '',
    [string]$Ffmpeg = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Output) { $Output = Join-Path $repoRoot 'build-artifacts\wallpaper-previews' }
$Output = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path (Join-Path $Output 'source') | Out-Null
if (-not $Python) {
    $pythonCandidates = @(
        (Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe')
    )
    $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($pythonCommand -and $pythonCommand.Source -notmatch 'WindowsApps') { $pythonCandidates += $pythonCommand.Source }
    $Python = $pythonCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $Python) { throw 'Python 3 is required. Pass -Python with the existing python.exe path.' }
if (-not $Ffmpeg) {
    $ffmpegCommand = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if (-not $ffmpegCommand) { throw 'FFmpeg is required. Pass -Ffmpeg with the existing ffmpeg.exe path.' }
    $Ffmpeg = $ffmpegCommand.Source
}
if ($Encoder -eq 'auto') {
    $savedErrorPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Ffmpeg -nostdin -hide_banner -v error -f lavfi -i 'color=size=320x180:rate=24' -t 0.1 -an -c:v h264_nvenc -f null - 2>$null
        $Encoder = if ($LASTEXITCODE -eq 0) { 'nvenc' } else { 'libx264' }
    } finally { $ErrorActionPreference = $savedErrorPreference }
}
$manifest = Join-Path $Output 'source\official-manifest.json'
$manifestPending = $manifest + '.pending'
Invoke-WebRequest -UseBasicParsing -Uri 'https://static.lumedesktop.com/lume/wallpapers/official/manifest.json' -OutFile $manifestPending -TimeoutSec 30
$catalog = Get-Content -LiteralPath $manifestPending -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $catalog.wallpapers -or @($catalog.wallpapers).Count -lt 1) { throw 'Official manifest validation failed.' }
Move-Item -LiteralPath $manifestPending -Destination $manifest -Force
$previewArgs = @((Join-Path $PSScriptRoot 'wallpaper-previews.py'), '--offline', '--manifest', $manifest,
    '--root', $Output, '--ffmpeg', $Ffmpeg, '--encoder', $Encoder, '--workers', [string]$Workers, '--run')
if ($Limit -gt 0) { $previewArgs += @('--limit', [string]$Limit) }
Write-Host "Encoder: $Encoder; parallel jobs: $Workers; output: $Output"
& $Python @previewArgs 2>&1 | Tee-Object -FilePath (Join-Path $Output 'latest-run.log')
$previewExit = $LASTEXITCODE
if ($previewExit -ne 0) { throw 'Some previews failed. Run this tool again to resume; the live manifest was not changed.' }
$report = Get-Content -LiteralPath (Join-Path $Output 'latest-report.json') -Raw -Encoding UTF8 | ConvertFrom-Json
Write-Host "Ready: $($report.verified) previews; $([math]::Round($report.previewBytes / 1MB, 1)) MiB."
Write-Host ('Upload MP4s first: ' + (Join-Path $Output 'upload\lume\wallpapers\official\previews'))
Write-Host ('After all MP4s are verified, upload manifest: ' + (Join-Path $Output 'upload\lume\wallpapers\official\manifest.json'))
Write-Host 'Refresh only the official manifest if supported; do not refresh or prewarm the entire site.'
