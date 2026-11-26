param(
    [Parameter(Mandatory = $true)][string]$RuntimeRoot,
    [string]$OutputDirectory = 'H:\refactor\project',
    [string]$NodePath = 'C:\Program Files\nodejs\node.exe'
)
$ErrorActionPreference = 'Stop'
$runtimeSource = Join-Path (Split-Path -Parent $PSScriptRoot) 'music-runtime'
$runtimeStage = (Resolve-Path -LiteralPath $RuntimeRoot).Path
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$verifier = Join-Path $PSScriptRoot 'verify-folia-runtime.cjs'
$verificationText = & $NodePath $verifier $runtimeSource $runtimeStage 2>&1
if ($LASTEXITCODE -ne 0) { throw ("Music runtime source/payload verification failed: " + ($verificationText -join "`n")) }
$verification = ($verificationText -join "`n") | ConvertFrom-Json
$inventoryPath = Join-Path $runtimeStage 'runtime-inventory.json'
$inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
$inventoryHash = (Get-FileHash -LiteralPath $inventoryPath -Algorithm SHA256).Hash.ToLowerInvariant()
$expected = @{}
foreach ($file in $inventory.files) {
    $expected[[string]$file.path] = @{ bytes = [long]$file.bytes; sha256 = [string]$file.sha256 }
}
$expected['runtime-inventory.json'] = @{ bytes = (Get-Item -LiteralPath $inventoryPath).Length; sha256 = $inventoryHash }
$unpackedBytes = [long]0
foreach ($file in $expected.Values) { $unpackedBytes += [long]$file['bytes'] }
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporary = Join-Path $outputRoot ('LumeMusicRuntime.pending.' + [guid]::NewGuid().ToString('N') + '.zip')
try {
    $reusedArchive = $null
    $previousDescriptorPath = Join-Path $outputRoot 'music-runtime.json'
    if (Test-Path -LiteralPath $previousDescriptorPath) {
        $previous = Get-Content -LiteralPath $previousDescriptorPath -Raw | ConvertFrom-Json
        if ($previous.schemaVersion -eq 1 -and $previous.id -ceq $inventoryHash -and $previous.sha256 -cmatch '^[0-9a-f]{64}$') {
            $candidate = Join-Path $outputRoot ('LumeMusicRuntime-' + $previous.sha256.Substring(0, 16) + '.zip')
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                if ((Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash -ine $previous.sha256 -or
                    (Get-Item -LiteralPath $candidate).Length -ne $previous.size) { throw 'Prepared runtime archive differs from its descriptor.' }
                $reusedArchive = $candidate
            }
        }
    }
    if ($reusedArchive) {
        # Preserve the prepared URL even when the installer is subsequently
        # built under a different .NET ZIP compression implementation.
        [IO.File]::Copy($reusedArchive, $temporary, $false)
    } else {
    $outputStream = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $archive = [IO.Compression.ZipArchive]::new($outputStream, [IO.Compression.ZipArchiveMode]::Create, $false)
        try {
            # Stable names/order/timestamps make repeated packaging of identical
            # runtime bytes produce the same immutable download URL.
            foreach ($relative in ($expected.Keys | Sort-Object -CaseSensitive)) {
                $entry = $archive.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = [DateTimeOffset]'2000-01-01T00:00:00+00:00'
                $inputStream = [IO.File]::OpenRead((Join-Path $runtimeStage $relative))
                try {
                    $entryStream = $entry.Open()
                    try { $inputStream.CopyTo($entryStream) } finally { $entryStream.Dispose() }
                } finally { $inputStream.Dispose() }
            }
        } finally { $archive.Dispose() }
    } finally { $outputStream.Dispose() }
    }

    # Read the actual compressed entries back, checking every hash and the
    # root layout before creating a descriptor that the installer can trust.
    $archive = [IO.Compression.ZipFile]::OpenRead($temporary)
    try {
        if ($archive.Entries.Count -ne $expected.Count) { throw 'Unexpected runtime ZIP entry count.' }
        $seen = @{}
        foreach ($entry in $archive.Entries) {
            $relative = $entry.FullName
            if (-not $expected.ContainsKey($relative) -or $seen.ContainsKey($relative)) { throw 'Unexpected or duplicate runtime ZIP path.' }
            $seen[$relative] = $true
            if ($entry.Length -ne $expected[$relative].bytes) { throw "Runtime ZIP length mismatch: $relative" }
            $stream = $entry.Open()
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
            finally { $hasher.Dispose(); $stream.Dispose() }
            if ($hash -cne $expected[$relative].sha256) { throw "Runtime ZIP hash mismatch: $relative" }
        }
    } finally { $archive.Dispose() }
    $finalVerification = & $NodePath $verifier $runtimeSource $runtimeStage 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'Runtime source changed while creating the download archive.' }
    if ((($finalVerification -join "`n") | ConvertFrom-Json).inventorySha256 -cne $inventoryHash) {
        throw 'Runtime inventory changed while creating the download archive.'
    }
    $zipHash = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant()
    $zipBytes = (Get-Item -LiteralPath $temporary).Length
    $zipName = 'LumeMusicRuntime-' + $zipHash.Substring(0, 16) + '.zip'
    $zipPath = Join-Path $outputRoot $zipName
    if (Test-Path -LiteralPath $zipPath) {
        if ((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash -ine $zipHash) { throw 'Existing immutable runtime archive has a different hash.' }
        Remove-Item -LiteralPath $temporary
    } else {
        Move-Item -LiteralPath $temporary -Destination $zipPath
    }
    $descriptor = [ordered]@{
        schemaVersion = 1
        id = $inventoryHash
        url = 'https://static.lumedesktop.com/downloads/' + $zipName
        sha256 = $zipHash
        size = $zipBytes
        unpackedBytes = $unpackedBytes
    }
    $descriptorPath = Join-Path $outputRoot 'music-runtime.json'
    $uploadManifestPath = Join-Path $outputRoot 'LumeMusicRuntime-upload.json'
    $encoding = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText($descriptorPath, ($descriptor | ConvertTo-Json) + "`n", $encoding)
    $uploadManifest = [ordered]@{
        schemaVersion = 1
        status = 'prepared-not-uploaded'
        descriptorPath = $descriptorPath
        runtimeId = $inventoryHash
        sourceFilesVerified = $verification.sourceFiles
        archiveEntriesVerified = $expected.Count
        files = @([ordered]@{ localPath = $zipPath; objectKey = 'downloads/' + $zipName;
            url = $descriptor.url; sha256 = $zipHash; size = $zipBytes })
    }
    [IO.File]::WriteAllText($uploadManifestPath, ($uploadManifest | ConvertTo-Json -Depth 5) + "`n", $encoding)
    [pscustomobject]@{ archivePath = $zipPath; descriptorPath = $descriptorPath; uploadManifestPath = $uploadManifestPath;
        id = $inventoryHash; sha256 = $zipHash; size = $zipBytes; unpackedBytes = $unpackedBytes;
        archiveEntries = $expected.Count; sourceFiles = $verification.sourceFiles }
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
