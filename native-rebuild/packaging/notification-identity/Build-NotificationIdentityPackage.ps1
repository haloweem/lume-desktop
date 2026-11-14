[CmdletBinding(DefaultParameterSetName='Unsigned')]
param(
    [Parameter(Mandatory)][string]$IdentityDirectory,
    [Parameter(Mandatory)][string]$OutputPath,
    [Parameter(Mandatory,ParameterSetName='Sign')][ValidatePattern('^[A-Fa-f0-9 ]{40,59}$')][string]$CertificateThumbprint,
    [Parameter(ParameterSetName='Sign')][ValidateSet('CurrentUser','LocalMachine')][string]$CertificateStoreLocation='CurrentUser',
    [Parameter(Mandatory,ParameterSetName='Sign')][uri]$TimestampUrl,
    [string]$MakeAppxPath,
    [string]$SignToolPath
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function Find-SdkTool([string]$Name,[string]$ExplicitPath) {
    if ($ExplicitPath) { return (Resolve-Path -LiteralPath $ExplicitPath -ErrorAction Stop).Path }
    $command=Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    $sdkBin=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path -LiteralPath $sdkBin) {
        $versions=Get-ChildItem -LiteralPath $sdkBin -Directory | Where-Object {$_.Name -match '^10\.\d+\.\d+\.\d+$'} |
            Sort-Object {[version]$_.Name} -Descending
        foreach($version in $versions) {
            $candidate=Join-Path $version.FullName ('x64\'+$Name)
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    throw "$Name was not found. Install the Windows SDK or specify its path."
}
function Read-SafeXml([string]$Path) {
    $settings=[Xml.XmlReaderSettings]::new(); $settings.DtdProcessing=[Xml.DtdProcessing]::Prohibit; $settings.XmlResolver=$null
    $reader=[Xml.XmlReader]::Create($Path,$settings)
    try { $document=[Xml.XmlDocument]::new();$document.XmlResolver=$null;$document.Load($reader);return ,$document }
    finally { $reader.Dispose() }
}

$identityRoot=(Resolve-Path -LiteralPath $IdentityDirectory -ErrorAction Stop).Path
$manifestPath=Join-Path $identityRoot 'AppxManifest.xml'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw 'Use the generated notification-identity directory from a matching Lume build, containing AppxManifest.xml; do not use the source .in template.'
}
$manifest=Read-SafeXml $manifestPath
$ns=[Xml.XmlNamespaceManager]::new($manifest.NameTable)
$ns.AddNamespace('p','http://schemas.microsoft.com/appx/manifest/foundation/windows10')
$ns.AddNamespace('uap','http://schemas.microsoft.com/appx/manifest/uap/windows10')
$ns.AddNamespace('uap3','http://schemas.microsoft.com/appx/manifest/uap/windows10/3')
$ns.AddNamespace('uap10','http://schemas.microsoft.com/appx/manifest/uap/windows10/10')
$ns.AddNamespace('uap11','http://schemas.microsoft.com/appx/manifest/uap/windows10/11')
$identity=$manifest.SelectSingleNode('/p:Package/p:Identity',$ns)
$app=$manifest.SelectSingleNode('/p:Package/p:Applications/p:Application',$ns)
$external=$manifest.SelectSingleNode('/p:Package/p:Properties/uap10:AllowExternalContent',$ns)
$capability=$manifest.SelectSingleNode('/p:Package/p:Capabilities/uap3:Capability[@Name="userNotificationListener"]',$ns)
$borderlessCapability=$manifest.SelectSingleNode('/p:Package/p:Capabilities/uap11:Capability[@Name="graphicsCaptureWithoutBorder"]',$ns)
if (-not $identity -or $identity.GetAttribute('Name') -cne 'Lume.Desktop.NotificationIdentity' -or
    -not $app -or $app.GetAttribute('Id') -cne 'LumeShell' -or $app.GetAttribute('Executable') -cne 'LumeShell.exe' -or
    -not $external -or $external.InnerText -cne 'true' -or -not $capability -or -not $borderlessCapability) {
    throw 'The generated manifest is not the expected Lume notification identity.'
}
if ($identity.GetAttribute('Publisher') -match 'OID\.2\.25\.311729368913984317654407730594956997722') {
    throw 'The Windows unsigned-test publisher OID is not allowed in this production package workflow.'
}
$outputFull=[IO.Path]::GetFullPath($OutputPath)
if ([IO.Path]::GetExtension($outputFull) -ine '.msix') { throw 'OutputPath must end with .msix.' }
if (Test-Path -LiteralPath $outputFull) { throw 'Output already exists. Choose a new path to preserve the prior artifact.' }
$outputParent=Split-Path -Parent $outputFull
New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
$makeAppx=Find-SdkTool 'makeappx.exe' $MakeAppxPath
$signing=$PSCmdlet.ParameterSetName -eq 'Sign'
$signTool=$null
if ($signing) {
    if (-not $TimestampUrl.IsAbsoluteUri -or $TimestampUrl.Scheme -notin @('http','https') -or
        $TimestampUrl.UserInfo -or -not $TimestampUrl.Host) { throw 'TimestampUrl must be an HTTP(S) RFC 3161 endpoint without credentials.' }
    $thumbprint=($CertificateThumbprint -replace ' ','').ToUpperInvariant()
    if ($thumbprint -notmatch '^[A-F0-9]{40}$') { throw 'CertificateThumbprint must be a complete SHA-1 certificate thumbprint.' }
    $certificate=Get-Item -LiteralPath ("Cert:\$CertificateStoreLocation\My\$thumbprint") -ErrorAction Stop
    if (-not $certificate.HasPrivateKey) { throw 'The existing certificate must have a usable private key (local provider or hardware token); private-key files are not accepted.' }
    if ($certificate.Subject -cne $identity.GetAttribute('Publisher')) {
        throw 'Certificate Subject must exactly match the generated manifest Publisher. Update all matching identity metadata and rebuild Lume before signing.'
    }
    $now=Get-Date
    if ($certificate.NotBefore -gt $now -or $certificate.NotAfter -le $now) { throw 'The signing certificate is not currently valid.' }
    foreach($extension in $certificate.Extensions) {
        if ($extension.Oid.Value -eq '2.5.29.37' -and
            -not @($extension.EnhancedKeyUsages | Where-Object {$_.Value -eq '1.3.6.1.5.5.7.3.3'}).Count) {
            throw 'The certificate is not valid for code signing.'
        }
        if ($extension.Oid.Value -eq '2.5.29.15' -and
            -not ($extension.KeyUsages -band [Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature)) {
            throw 'The certificate key usage does not permit a digital signature.'
        }
    }
    $chain=[Security.Cryptography.X509Certificates.X509Chain]::new()
    try {
        $chain.ChainPolicy.RevocationMode=[Security.Cryptography.X509Certificates.X509RevocationMode]::Online
        $chain.ChainPolicy.ApplicationPolicy.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'))
        if (-not $chain.Build($certificate)) { throw 'The existing signing certificate is not trusted/valid on this build machine. This script will not add certificate trust.' }
    } finally { $chain.Dispose() }
    $signTool=Find-SdkTool 'signtool.exe' $SignToolPath
}

# Keep a dedicated, reviewable stage containing only the required metadata and
# images. Installer scripts and executables do not belong inside this MSIX.
$stage=Join-Path $outputParent ('.notification-identity-stage-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $stage 'AppxManifest.xml')
$visual=$manifest.SelectSingleNode('/p:Package/p:Applications/p:Application/uap:VisualElements',$ns)
$logo=$manifest.SelectSingleNode('/p:Package/p:Properties/p:Logo',$ns)
if (-not $visual -or -not $logo) { throw 'The generated manifest is missing its required logo references.' }
$assets=@(
    @{Name='StoreLogo.png';Relative=$logo.InnerText},
    @{Name='Square44x44Logo.png';Relative=$visual.GetAttribute('Square44x44Logo')},
    @{Name='Square150x150Logo.png';Relative=$visual.GetAttribute('Square150x150Logo')}
)
foreach($asset in $assets) {
    # Follow the generated manifest exactly. Sparse manifests may resolve
    # resources under notification-identity\Assets relative to ExternalLocation.
    # Only these known filenames and two explicit relative layouts are allowed.
    $relative=$asset.Relative.Replace('/','\')
    if ($relative -cne ('Assets\'+$asset.Name) -and $relative -cne ('notification-identity\Assets\'+$asset.Name)) {
        throw "Unexpected generated asset reference: $relative"
    }
    $destination=Join-Path $stage $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $identityRoot ('Assets\'+$asset.Name)) -Destination $destination
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) { throw "The staged package is missing $relative" }
}
& $makeAppx pack /d $stage /p $outputFull /nv /h SHA256
if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed with exit code $LASTEXITCODE. Stage retained at $stage" }
$status='unsigned-not-for-distribution'
if ($signing) {
    $signArgs=@('sign','/fd','SHA256','/sha1',$thumbprint,'/s','My','/tr',$TimestampUrl.AbsoluteUri,'/td','SHA256')
    if ($CertificateStoreLocation -eq 'LocalMachine') { $signArgs+='/sm' }
    $signArgs+=$outputFull
    & $signTool @signArgs
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed with exit code $LASTEXITCODE. Output is not release-ready." }
    & $signTool verify /pa /all /v $outputFull
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed with exit code $LASTEXITCODE. Output is not release-ready." }
    $status='signed-and-verified-on-build-machine'
}
[pscustomobject]@{
    Status=$status;PackagePath=$outputFull;StageDirectory=$stage
    Name=$identity.GetAttribute('Name');Publisher=$identity.GetAttribute('Publisher');Version=$identity.GetAttribute('Version')
    SHA256=(Get-FileHash -LiteralPath $outputFull -Algorithm SHA256).Hash
}
