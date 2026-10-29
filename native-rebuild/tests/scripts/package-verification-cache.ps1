# Dot-sourced by packaging. Only successful, identical-input runs may be reused.
function Get-PackageTextHash {
    param([Parameter(Mandatory=$true)][string]$Text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text)))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Get-PackageFileSetHash {
    param([Parameter(Mandatory=$true)][string]$Root, [Parameter(Mandatory=$true)][object[]]$Files)
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $lines = @($Files | Sort-Object FullName | ForEach-Object {
        $path = [IO.Path]::GetFullPath($_.FullName)
        if (-not $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Verification input outside payload root' }
        $relative = $path.Substring($prefix.Length).Replace('\', '/')
        $relative + '=' + (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    })
    return Get-PackageTextHash ($lines -join "`n")
}

function Get-PackageProbeContext {
    param([string]$Name, [string]$RuntimeHash, [string]$UiHash)
    # Shell/WebView tests consume shipped UI; native model/session tests create
    # their own fixtures. All probes also depend on the auxiliary EXEs and DLLs.
    if ($Name -match '^lume-(desktop-|webview-|music-(webview|font))') {
        return Get-PackageTextHash ($RuntimeHash + '|' + $UiHash)
    }
    return $RuntimeHash
}

function Get-PackageCachedPass {
    param([string]$Path, [string]$Key, [string]$EvidenceRoot, [datetime]$NowUtc = [DateTime]::UtcNow)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try {
        $entry = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
        if ($entry.version -ne 1 -or $entry.key -ne $Key -or $entry.status -ne 'passed') { return $null }
        $completed = [datetime]::Parse($entry.completedAtUtc).ToUniversalTime()
        if ($completed -gt $NowUtc -or ($NowUtc - $completed).TotalHours -gt 24) { return $null }
        $prefix = [IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
        $logPath = [IO.Path]::GetFullPath([string]$entry.logPath)
        if (-not $logPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $logPath -PathType Leaf)) { return $null }
        if ((Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash -ne $entry.logHash) { return $null }
        return $entry
    } catch { return $null }
}

function Save-PackageCachedPass {
    param([string]$Path, [string]$Key, [string]$LogPath)
    $entry = [ordered]@{
        version = 1; key = $Key; status = 'passed'; completedAtUtc = [DateTime]::UtcNow.ToString('o')
        logPath = [IO.Path]::GetFullPath($LogPath); logHash = (Get-FileHash -LiteralPath $LogPath -Algorithm SHA256).Hash
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $Path -Parent) | Out-Null
    $pending = $Path + '.tmp-' + [guid]::NewGuid().ToString('N')
    $entry | ConvertTo-Json | Set-Content -LiteralPath $pending -Encoding utf8
    Move-Item -LiteralPath $pending -Destination $Path -Force
}
