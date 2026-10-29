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
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Get-ChildItem -LiteralPath $destination -Force | Where-Object { $_.Name -ne '.git' } | Remove-Item -Recurse -Force

# Committed files only, with the same line endings as a checkout.
$archive = Join-Path ([IO.Path]::GetTempPath()) ("lume-open-source-$([guid]::NewGuid()).zip")
try {
    & git -C $repoRoot archive --format=zip -o $archive HEAD native-rebuild .gitattributes
    if ($LASTEXITCODE -ne 0) { throw 'git archive failed' }
    Expand-Archive -LiteralPath $archive -DestinationPath $destination
} finally { Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue }

# Not part of the program: release upload (server, bucket, SSH key path).
foreach ($excluded in @('native-rebuild\tools\release\publish-release.ps1')) {
    $path = Join-Path $destination $excluded
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}

Copy-Item -LiteralPath (Join-Path $destination 'native-rebuild\ui\music-effects\reference\licenses\Folia-AGPL-3.0.txt') -Destination (Join-Path $destination 'LICENSE')
foreach ($name in @('README.md', 'BUILDING.md', 'THIRD-PARTY.md')) {
    Copy-Item -LiteralPath (Join-Path $destination "native-rebuild\tools\open-source\$name") -Destination (Join-Path $destination $name)
}
@('build-artifacts/', 'native-rebuild/build/', 'native-rebuild/out/', 'native-rebuild/.vs/', '*.user', '*.suo') -join "`r`n" |
    Set-Content -LiteralPath (Join-Path $destination '.gitignore') -Encoding ascii

$cmake = Get-Content -LiteralPath (Join-Path $destination 'native-rebuild\CMakeLists.txt') -Raw
$version = if ($cmake -match 'project\(\w+ VERSION ([0-9.]+)') { $Matches[1] } else { 'unknown' }
@"
版本：$version（对应安装包 LumeSetup-$version.exe）
源码提交：$commit
导出时间：$([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'))
"@ | Set-Content -LiteralPath (Join-Path $destination 'SOURCE-REVISION.txt') -Encoding utf8

$files = (Get-ChildItem -LiteralPath $destination -Recurse -File -Force | Where-Object { $_.FullName -notlike "*\.git\*" }).Count
Write-Host "EXPORTED=$destination"
Write-Host "VERSION=$version COMMIT=$commit FILES=$files"
