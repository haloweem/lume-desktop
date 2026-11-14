# Shared by packaging and the Inno Setup callback. Only public certificate
# selectors are accepted; the registered key provider owns the private key.
function Initialize-LumeSigningPowerShellModules {
    # A Windows PowerShell child of pwsh/Inno can inherit a module search path
    # that omits its own system modules. Select this host's built-in modules
    # explicitly for certificates, hashing, JSON and file I/O, without relying
    # on provider/function autoload or changing the inherited PSModulePath.
    foreach ($name in @('Microsoft.PowerShell.Management', 'Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Security')) {
        $manifest = Join-Path $PSHOME ('Modules\' + $name + '\' + $name + '.psd1')
        if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "The current PowerShell host has no built-in $name module." }
        $null = Import-Module -Name $manifest -Global -ErrorAction Stop
    }
    foreach ($command in @('Get-FileHash','Get-AuthenticodeSignature','Get-Item','Get-Content','Set-Content',
            'ConvertFrom-Json','ConvertTo-Json','Resolve-Path','Get-Date','Add-Member','Write-Host')) {
        $null = Get-Command $command -ErrorAction Stop
    }
    $drive = Get-PSDrive -Name Cert -ErrorAction SilentlyContinue
    if (-not $drive -or $drive.Provider.Name -ne 'Certificate' -or
            $drive.Provider.ModuleName -ne 'Microsoft.PowerShell.Security') {
        throw 'The built-in certificate provider did not initialize the Cert drive.'
    }
}

function Initialize-LumeSigningSecurityModule {
    # Preserve the existing standalone provider probe entry point.
    Initialize-LumeSigningPowerShellModules
}

function Find-LumeSigningSdkTool {
    param([Parameter(Mandatory=$true)][string]$Name, [string]$ExplicitPath)
    if ($ExplicitPath) {
        $resolved = (Resolve-Path -LiteralPath $ExplicitPath -ErrorAction Stop).Path
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "SDK tool is not a file: $resolved" }
        return $resolved
    }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    $sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path -LiteralPath $sdkBin) {
        foreach ($version in (Get-ChildItem -LiteralPath $sdkBin -Directory |
                Where-Object {$_.Name -match '^10\.\d+\.\d+\.\d+$'} | Sort-Object {[version]$_.Name} -Descending)) {
            $candidate = Join-Path $version.FullName ('x64\' + $Name)
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    throw "$Name was not found. Install the Windows SDK or provide its exact path."
}

function Assert-LumeSigningCertificate {
    param([Parameter(Mandatory=$true)]$Certificate)
    if (-not $Certificate.HasPrivateKey) { throw 'The code-signing certificate has no usable registered private-key provider.' }
    $now = Get-Date
    if ($Certificate.NotBefore -gt $now -or $Certificate.NotAfter -le $now) { throw 'The code-signing certificate is outside its validity period.' }
    if ([string]::IsNullOrWhiteSpace($Certificate.Subject) -or $Certificate.Subject.Length -gt 2048 -or
            $Certificate.Subject -match '[\x00-\x1f]') { throw 'The code-signing certificate has an invalid Subject.' }
    $hasCodeSigning = $false
    foreach ($extension in $Certificate.Extensions) {
        if ($extension.Oid.Value -eq '2.5.29.37') {
            $hasCodeSigning = @($extension.EnhancedKeyUsages | Where-Object {$_.Value -eq '1.3.6.1.5.5.7.3.3'}).Count -gt 0
        }
        if ($extension.Oid.Value -eq '2.5.29.15' -and
                -not ($extension.KeyUsages -band [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature)) {
            throw 'The certificate key usage does not permit digital signatures.'
        }
    }
    if (-not $hasCodeSigning) { throw 'The certificate must explicitly permit Code Signing.' }
    $chain = [Security.Cryptography.X509Certificates.X509Chain]::new()
    try {
        $chain.ChainPolicy.RevocationMode = [Security.Cryptography.X509Certificates.X509RevocationMode]::Online
        # OidCollection.Add returns an index. Do not leak it into the caller's
        # pipeline: New-LumeCodeSigningContext must return exactly one object.
        [void]$chain.ChainPolicy.ApplicationPolicy.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
        if (-not $chain.Build($Certificate)) { throw 'The signing certificate does not have an already trusted, valid code-signing chain.' }
    } finally { $chain.Dispose() }
}

function New-LumeCodeSigningContext {
    param(
        [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
        [ValidateSet('CurrentUser','LocalMachine')][string]$CertificateStoreLocation = 'CurrentUser',
        [Parameter(Mandatory=$true)][uri]$TimestampUrl,
        [string]$SignToolPath
    )
    $thumbprint = ($CertificateThumbprint -replace ' ', '').ToUpperInvariant()
    if ($thumbprint -notmatch '^[A-F0-9]{40}$') { throw 'CertificateThumbprint must be the complete SHA-1 certificate thumbprint.' }
    if (-not $TimestampUrl.IsAbsoluteUri -or $TimestampUrl.Scheme -notin @('http','https') -or
            $TimestampUrl.UserInfo -or -not $TimestampUrl.Host -or $TimestampUrl.Fragment) {
        throw 'TimestampUrl must be an HTTP(S) RFC 3161 endpoint without credentials or a fragment.'
    }
    Initialize-LumeSigningPowerShellModules
    $certificate = Get-Item -LiteralPath ("Cert:\$CertificateStoreLocation\My\$thumbprint") -ErrorAction Stop
    Assert-LumeSigningCertificate $certificate
    return [pscustomobject]@{
        Thumbprint = $thumbprint
        Subject = $certificate.Subject
        CertificateStoreLocation = $CertificateStoreLocation
        TimestampUrl = $TimestampUrl.AbsoluteUri
        SignToolPath = Find-LumeSigningSdkTool 'signtool.exe' $SignToolPath
    }
}

function Get-LumeOwnedSigningFiles {
    # Explicit ownership allowlist. Pinned mpv, Microsoft and taskbar dependencies
    # must retain their original bytes and are never selected recursively.
    return @('LumeLauncher.exe', 'LumeShell.exe', 'LumeMusicHost.exe', 'LumeIconHost.exe', 'LumeGlass.dll',
        'LumeMinimizeHost32.exe', 'LumeMinimizeHost64.exe', 'LumeMinimizeHook32.dll', 'LumeMinimizeHook64.dll',
        'LumeSetupShutdown.exe', 'lume-mpv-stream.dll')
}

function Write-LumeSigningCMakeCache {
    param([Parameter(Mandatory=$true)]$Context, [Parameter(Mandatory=$true)][string]$Path)
    # CMake bracket arguments preserve quotes, semicolons and Unicode in the DN.
    # Never splice the Subject into a shell command or a quoted CMake string.
    $delimiter = '===='
    while ($Context.Subject.Contains(']' + $delimiter + ']')) { $delimiter += '=' }
    $content = 'set(LUME_NOTIFICATION_IDENTITY ON CACHE BOOL "" FORCE)' + "`n" +
        'set(LUME_NOTIFICATION_IDENTITY_PUBLISHER [' + $delimiter + '[' + $Context.Subject + ']' + $delimiter +
        '] CACHE STRING "Exact signing certificate Subject" FORCE)' + "`n"
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($Path), $content, [Text.UTF8Encoding]::new($false))
}

function Assert-LumeSignedFile {
    param([Parameter(Mandatory=$true)][string]$Path, [Parameter(Mandatory=$true)]$Context)
    $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    $signature = Get-AuthenticodeSignature -LiteralPath $resolved
    if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or
            $signature.SignerCertificate.Thumbprint -ine $Context.Thumbprint -or
            $signature.SignerCertificate.Subject -cne $Context.Subject) {
        throw "The file does not have a valid signature from the selected certificate: $resolved"
    }
    if (-not $signature.TimeStamperCertificate) { throw "The signed file has no trusted timestamp: $resolved" }
    & $Context.SignToolPath verify /pa /all /v /tw $resolved | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw "SignTool verification failed ($LASTEXITCODE): $resolved" }
    return [pscustomobject]@{
        path = $resolved
        sha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash
        subject = $signature.SignerCertificate.Subject
        thumbprint = $signature.SignerCertificate.Thumbprint
        timestampSubject = $signature.TimeStamperCertificate.Subject
        status = 'signed-and-verified'
    }
}

function Invoke-LumeCodeSignFile {
    param([Parameter(Mandatory=$true)][string]$Path, [Parameter(Mandatory=$true)]$Context)
    $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "Signing input is not a file: $resolved" }
    $existing = Get-AuthenticodeSignature -LiteralPath $resolved
    $reused = $existing.Status -eq 'Valid' -and $existing.SignerCertificate -and
        $existing.SignerCertificate.Thumbprint -ieq $Context.Thumbprint -and
        $existing.SignerCertificate.Subject -ceq $Context.Subject -and $existing.TimeStamperCertificate
    if (-not $reused) {
        $arguments = @('sign', '/fd', 'SHA256', '/sha1', $Context.Thumbprint, '/s', 'My',
            '/tr', $Context.TimestampUrl, '/td', 'SHA256', '/d', 'Lume')
        if ($Context.CertificateStoreLocation -eq 'LocalMachine') { $arguments += '/sm' }
        $arguments += $resolved
        & $Context.SignToolPath @arguments | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE -ne 0) { throw "SignTool signing failed ($LASTEXITCODE): $resolved" }
    }
    $record = Assert-LumeSignedFile $resolved $Context
    Add-Member -InputObject $record -NotePropertyName action -NotePropertyValue $(if ($reused) {'verified-existing'} else {'signed'})
    return $record
}

function Assert-LumeRetainedSigningRecord {
    param([Parameter(Mandatory=$true)]$Record, [Parameter(Mandatory=$true)]$Context,
        [Parameter(Mandatory=$true)][string]$EvidenceDirectory)
    $prefix = [IO.Path]::GetFullPath($EvidenceDirectory).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $retained = [IO.Path]::GetFullPath([string]$Record.retainedPath)
    if (-not $retained.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
            $Record.sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or $Record.retainedSha256 -cne $Record.sha256) {
        throw 'The retained compiler input does not match its signing evidence.'
    }
    $verified = Assert-LumeSignedFile -Path $retained -Context $Context
    if ($verified.sha256 -cne $Record.sha256) { throw 'The retained signed compiler input was changed after its callback.' }
    return $verified
}

function New-LumeInnoSigningCommand {
    param([Parameter(Mandatory=$true)][string]$PowerShellPath, [Parameter(Mandatory=$true)][string]$HelperPath)
    foreach ($path in @($PowerShellPath, $HelperPath)) {
        if ($path -match '[\x00-\x1f"]') { throw 'An Inno signing command path contains invalid characters.' }
    }
    $hostArgument = '$q' + $PowerShellPath.Replace('$','$$') + '$q'
    $helperArgument = '$q' + $HelperPath.Replace('$','$$') + '$q'
    # Inno already quotes $f. A second pair of quotes would corrupt file paths.
    return $hostArgument + ' -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' + $helperArgument + ' -Path $f'
}
