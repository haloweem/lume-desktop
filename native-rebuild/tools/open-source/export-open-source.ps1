# Exports the AGPL-3.0 corresponding source of the Lume client to a folder that
# is uploaded as the public source repository.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File native-rebuild\tools\open-source\export-open-source.ps1
#
# Exports the committed HEAD (never uncommitted edits) of native-rebuild/, plus
# LICENSE, README.md, BUILDING.md, THIRD-PARTY.md (from this folder) and
# SOURCE-REVISION.txt at the top. Everything in the destination is replaced except a .git folder, so the
# destination can itself be a Git checkout of the public repository: export,
# review `git status`, commit, push. Deployment scripts that hold server and
# upload details are left out; they are not needed to build the program.
param([string]$Destination = 'H:\refactor\AGPL')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$templates = $PSScriptRoot

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Not a git checkout' }
$dirty = & git -C $repoRoot status --porcelain -- native-rebuild
if ($dirty) { Write-Warning 'Uncommitted changes are not exported; only the committed HEAD is.' }

$destination = [IO.Path]::GetFullPath($Destination)
if ($destination.TrimEnd('\') -eq $repoRoot.TrimEnd('\') -or $destination.StartsWith($repoRoot.TrimEnd('\') + '\')) {
    throw 'The destination must be outside this repository'
}
if ($destination.TrimEnd('\') -eq [IO.Path]::GetPathRoot($destination).TrimEnd('\')) {
    throw 'The destination cannot be a drive root'
}
New-Item -ItemType Directory -Force -Path $destination | Out-Null
if ((Get-Item -LiteralPath $destination -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'The destination cannot be a junction or symbolic link'
}
if (Test-Path -LiteralPath (Join-Path $destination '.git')) {
    $publicRoot = (& git -C $destination rev-parse --show-toplevel).Trim()
    if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($publicRoot).TrimEnd('\') -ine $destination.TrimEnd('\')) {
        throw 'The destination must be the public repository root'
    }
    $publicChanges = & git -C $destination status --porcelain --untracked-files=normal
    if ($LASTEXITCODE -ne 0 -or $publicChanges) { throw 'Commit or preserve changes in the public repository before exporting' }
}
$entries = @(Get-ChildItem -LiteralPath $destination -Force | Where-Object { $_.Name -ne '.git' })
foreach ($entry in $entries) {
    $target = [IO.Path]::GetFullPath($entry.FullName)
    if (-not $target.StartsWith($destination.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Refusing to replace an entry outside the export directory or a linked entry'
    }
}
foreach ($entry in $entries) { Remove-Item -LiteralPath $entry.FullName -Recurse -Force }

# Committed files only, with the same line endings as a checkout.
$archive = Join-Path ([IO.Path]::GetTempPath()) ("lume-open-source-$([guid]::NewGuid()).zip")
try {
    & git -C $repoRoot archive --format=zip -o $archive HEAD native-rebuild .gitattributes
    if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
    Expand-Archive -LiteralPath $archive -DestinationPath $destination
} finally { Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue }

# Not part of the program: release upload (server, bucket, SSH key path).
foreach ($excluded in @('native-rebuild\tools\release\publish-release.ps1',
        'native-rebuild\tools\server-session-lifecycle')) {
    $path = Join-Path $destination $excluded
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
}

Copy-Item -LiteralPath (Join-Path $destination 'native-rebuild\ui\music-effects\reference\licenses\Folia-AGPL-3.0.txt') -Destination (Join-Path $destination 'LICENSE')
foreach ($name in @('README.md', 'BUILDING.md', 'THIRD-PARTY.md')) {
    Copy-Item -LiteralPath (Join-Path $destination "native-rebuild\tools\open-source\$name") -Destination (Join-Path $destination $name)
}
@('build-artifacts/', 'native-rebuild/build/', 'native-rebuild/out/', 'native-rebuild/.vs/',
  'native-rebuild/.cache/', 'native-rebuild/music-runtime/node_modules/', 'native-rebuild/music-runtime/dist/',
  'native-rebuild/music-runtime/runtime/', 'LumeSetup-*.exe', '*.user', '*.suo', '*.log', '*.pfx', '*.p12', '.env', '.env.local') -join "`r`n" |
    Set-Content -LiteralPath (Join-Path $destination '.gitignore') -Encoding ascii

$cmake = Get-Content -LiteralPath (Join-Path $destination 'native-rebuild\CMakeLists.txt') -Raw
$version = if ($cmake -match 'project\(\w+ VERSION ([0-9.]+)') { $Matches[1] } else { 'unknown' }
@"
版本：$version（客户端源码，安装包发布单独进行）
源码提交：$commit
导出时间：$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'))
"@ | Set-Content -LiteralPath (Join-Path $destination 'SOURCE-REVISION.txt') -Encoding utf8

$files = (Get-ChildItem -LiteralPath $destination -Recurse -File -Force | Where-Object { $_.FullName -notlike "*\.git\*" }).Count
Write-Host "EXPORTED=$destination"
Write-Host "VERSION=$version COMMIT=$commit FILES=$files"
