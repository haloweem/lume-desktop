$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'package-verification-cache.ps1')
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$root = Join-Path $repo ('build-artifacts\package-cache-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
function Assert-Cache { param([bool]$Value, [string]$Message) if (-not $Value) { throw $Message } }
try {
    $log = Join-Path $root 'probe.txt'
    $cache = Join-Path $root 'probe.json'
    'PASS' | Set-Content -LiteralPath $log
    Assert-Cache ($null -eq (Get-PackageCachedPass $cache 'key' $root)) 'A cold run must execute'
    Save-PackageCachedPass $cache 'key' $log
    Assert-Cache ($null -ne (Get-PackageCachedPass $cache 'key' $root)) 'Identical successful input must be reusable'
    Assert-Cache ($null -eq (Get-PackageCachedPass $cache 'changed-binary' $root)) 'Changed executable or dependency must execute'
    Assert-Cache ($null -eq (Get-PackageCachedPass $cache 'key' $root ([DateTime]::UtcNow.AddDays(2)))) 'Expired evidence must execute'
    Assert-Cache ($null -eq (Get-PackageCachedPass $cache 'key' $root ([DateTime]::UtcNow.AddDays(-2)))) 'Future evidence must execute'
    Assert-Cache ($null -eq (Get-PackageCachedPass $cache 'key' (Join-Path $root 'other'))) 'Evidence outside this checkout must not be reused'
    'modified log' | Set-Content -LiteralPath $log
    Assert-Cache ($null -eq (Get-PackageCachedPass $cache 'key' $root)) 'Modified evidence must execute'
    Save-PackageCachedPass $cache 'key' $log
    $entry = Get-Content -LiteralPath $cache -Raw | ConvertFrom-Json
    $entry.status = 'failed'
    $entry | ConvertTo-Json | Set-Content -LiteralPath $cache
    Assert-Cache ($null -eq (Get-PackageCachedPass $cache 'key' $root)) 'Failed tests never count as cached passes'
    '{broken' | Set-Content -LiteralPath $cache
    Assert-Cache ($null -eq (Get-PackageCachedPass $cache 'key' $root)) 'Corrupt cache must fall back to execution'
    Assert-Cache ((Get-PackageProbeContext 'lume-music-background-session-test' 'runtime' 'ui-one') -eq
                  (Get-PackageProbeContext 'lume-music-background-session-test' 'runtime' 'ui-two')) 'UI-only edits must not invalidate self-contained background tests'
    Assert-Cache ((Get-PackageProbeContext 'lume-desktop-music-library-test' 'runtime' 'ui-one') -ne
                  (Get-PackageProbeContext 'lume-desktop-music-library-test' 'runtime' 'ui-two')) 'UI edits must invalidate desktop/WebView tests'
    $firstHash = Get-PackageFileSetHash $root @(Get-Item -LiteralPath $log)
    'new dependency' | Set-Content -LiteralPath $log
    Assert-Cache ($firstHash -ne (Get-PackageFileSetHash $root @(Get-Item -LiteralPath $log))) 'Payload hashes must use contents, not timestamps'
    Write-Output 'PASS: packaging cache invalidation, evidence integrity, age and dependency selection'
} finally {
    $safePrefix = [IO.Path]::GetFullPath((Join-Path $repo 'build-artifacts')).TrimEnd('\') + '\'
    if (-not ([IO.Path]::GetFullPath($root)).StartsWith($safePrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid test cleanup path' }
    Remove-Item -LiteralPath $root -Recurse -Force
}
