function Initialize-LumeIdentityPowerShellModules {
    # Installer children may inherit pwsh's module search path. Load the current
    # Windows PowerShell host's own modules before any JSON, signature or
    # Add-Type use, avoiding incompatible format/type data from another host.
    foreach ($name in @('Microsoft.PowerShell.Management','Microsoft.PowerShell.Utility','Microsoft.PowerShell.Security')) {
        # No Join-Path/Test-Path before the first import: those commands can
        # themselves auto-load the incompatible Management module being avoided.
        $manifest = $PSHOME + '\Modules\' + $name + '\' + $name + '.psd1'
        if (-not [IO.File]::Exists($manifest)) { throw "The current PowerShell host has no built-in $name module." }
        $null = Import-Module -Name $manifest -Global -ErrorAction Stop
    }
}
Initialize-LumeIdentityPowerShellModules

function Get-LumeIdentityConfig {
    $identityConfig = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'identity-config.json') -Raw | ConvertFrom-Json
    Assert-LumeIdentityConfig $identityConfig
    return $identityConfig
}
function Assert-LumeIdentityConfig($Config) {
    if ($Config.name -cne 'Lume.Desktop.NotificationIdentity' -or $Config.applicationId -cne 'LumeShell' -or
        $Config.publisher -isnot [string] -or [string]::IsNullOrWhiteSpace($Config.publisher) -or
        $Config.publisher.Length -gt 2048 -or $Config.publisher -match '[\x00-\x1f]' -or
        $Config.version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Invalid Lume notification identity metadata.' }
    $parsedVersion = [version]$Config.version
    if ($parsedVersion.Major -gt 65535 -or $parsedVersion.Minor -gt 65535 -or $parsedVersion.Build -gt 65535 -or
        $parsedVersion.Revision -gt 65535) { throw 'Identity version exceeds the MSIX version field bounds.' }
    [void][Security.Cryptography.X509Certificates.X500DistinguishedName]::new($Config.publisher)
}
function Get-LumeIdentityPath([string]$Path) {
    return [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path).TrimEnd('\')
}
function Get-LumePackageExternalPath([string]$PackageFullName) {
    if (-not ('LumeNotificationIdentity.NativePackagePath' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace LumeNotificationIdentity {
 public static class NativePackagePath {
  [DllImport("kernelbase.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
  private static extern int GetPackagePathByFullName2(string fullName, int pathType, ref uint length, StringBuilder path);
  public static string ExternalPath(string fullName) {
   uint length=0;
   int result=GetPackagePathByFullName2(fullName,5,ref length,null);
   if(result!=122 || length==0 || length>32768) return null;
   var path=new StringBuilder((int)length);
   return GetPackagePathByFullName2(fullName,5,ref length,path)==0 ? path.ToString() : null;
  }
 }
}
'@
    }
    return [LumeNotificationIdentity.NativePackagePath]::ExternalPath($PackageFullName)
}
function Test-LumeIdentityLocation($Package, [string]$InstallDirectory) {
    $externalPath = Get-LumePackageExternalPath $Package.PackageFullName
    return Test-LumeIdentityExternalPath $externalPath $InstallDirectory
}
function Test-LumeIdentityExternalPath([string]$ExternalPath, [string]$InstallDirectory) {
    if (-not $ExternalPath -or -not [IO.Path]::IsPathRooted($ExternalPath)) { return $false }
    return [string]::Equals([IO.Path]::GetFullPath($ExternalPath).TrimEnd('\'), [IO.Path]::GetFullPath($InstallDirectory).TrimEnd('\'),
        [StringComparison]::OrdinalIgnoreCase)
}
function Test-LumeIdentityOwner($Package, $Config, [string]$ExternalPath, [string]$InstallDirectory) {
    return $Package.Name -ceq $Config.name -and $Package.Publisher -ceq $Config.publisher -and
        (Test-LumeIdentityExternalPath $ExternalPath $InstallDirectory)
}
function Get-LumeIdentityRegistrationAction($Package, $Config, [string]$ExternalPath, [string]$InstallDirectory, [bool]$Production) {
    if (-not (Test-LumeIdentityOwner $Package $Config $ExternalPath $InstallDirectory)) {
        throw 'An identity for another publisher or installation already exists and was preserved.'
    }
    if ([version]$Package.Version -gt [version]$Config.version) {
        if (-not $Production -or $Package.IsDevelopmentMode -or [string]$Package.Status -notin @('Ok', '0')) {
            throw 'The newer identity cannot be reused and was preserved.'
        }
        $manifest = Get-AppxPackageManifest -Package $Package.PackageFullName -ErrorAction Stop
        Assert-LumeIdentityManifest $manifest $Config ([string]$Package.Version)
        return 'current'
    }
    if ([version]$Package.Version -eq [version]$Config.version -and (-not $Production -or -not $Package.IsDevelopmentMode)) {
        return 'current'
    }
    return 'install'
}
function Assert-LumeIdentitySignature($Signature, $Config) {
    if ($Signature.Status -ne 'Valid' -or -not $Signature.SignerCertificate -or
        $Signature.SignerCertificate.Subject -cne $Config.publisher) {
        throw 'The identity MSIX requires an already trusted, valid signature with the exact configured publisher. No trust was imported.'
    }
}
function Assert-LumeIdentityUserContext([string]$UserSid, [int]$SessionId, [string]$InteractiveUserSid = '') {
    if ($UserSid -in @('S-1-5-18', 'S-1-5-19', 'S-1-5-20') -or $SessionId -le 0) {
        throw 'Notification identity requires the intended interactive user. SYSTEM/service/session-zero deployment cannot register it for that user.'
    }
    if ($InteractiveUserSid -and $UserSid -cne $InteractiveUserSid) {
        throw 'The registration account is not the signed-in user of this session. Registration for a different RunAs administrator was refused.'
    }
}
function Get-LumeInteractiveUserSid([int]$SessionId) {
    if (-not ('LumeNotificationIdentity.NativeSessionIdentity' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace LumeNotificationIdentity {
 public static class NativeSessionIdentity {
  [DllImport("wtsapi32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
  private static extern bool WTSQuerySessionInformationW(IntPtr server, uint session, int info, out IntPtr buffer, out uint bytes);
  [DllImport("wtsapi32.dll", ExactSpelling=true)] private static extern void WTSFreeMemory(IntPtr memory);
  public static string Value(int session, int info) {
   IntPtr buffer; uint bytes;
   if(!WTSQuerySessionInformationW(IntPtr.Zero,(uint)session,info,out buffer,out bytes)) return null;
   try {return bytes>2 && bytes<=65536 ? Marshal.PtrToStringUni(buffer) : null;}
   finally {WTSFreeMemory(buffer);}
  }
 }
}
'@
    }
    $userName = [LumeNotificationIdentity.NativeSessionIdentity]::Value($SessionId, 5)
    $domainName = [LumeNotificationIdentity.NativeSessionIdentity]::Value($SessionId, 7)
    if ([string]::IsNullOrWhiteSpace($userName)) { throw 'Unable to verify the signed-in user of this session; identity registration was not attempted.' }
    $account = if ($domainName) { [Security.Principal.NTAccount]::new($domainName, $userName) } else { [Security.Principal.NTAccount]::new($userName) }
    return $account.Translate([Security.Principal.SecurityIdentifier]).Value
}
function Read-LumeIdentityXml([IO.Stream]$Stream) {
    $settings = [Xml.XmlReaderSettings]::new()
    $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
    $settings.XmlResolver = $null
    $settings.MaxCharactersInDocument = 65536
    $reader = [Xml.XmlReader]::Create($Stream, $settings)
    try { $document = [Xml.XmlDocument]::new(); $document.XmlResolver = $null; $document.Load($reader); return ,$document }
    finally { $reader.Dispose() }
}
function Assert-LumeIdentityManifest([Xml.XmlDocument]$Manifest, $Config, [string]$ExpectedVersion = $Config.version) {
    $namespaces = [Xml.XmlNamespaceManager]::new($Manifest.NameTable)
    $namespaces.AddNamespace('p', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
    $namespaces.AddNamespace('uap10', 'http://schemas.microsoft.com/appx/manifest/uap/windows10/10')
    $namespaces.AddNamespace('uap11', 'http://schemas.microsoft.com/appx/manifest/uap/windows10/11')
    $namespaces.AddNamespace('uap3', 'http://schemas.microsoft.com/appx/manifest/uap/windows10/3')
    $namespaces.AddNamespace('rescap', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities')
    $namespaces.AddNamespace('desktop6', 'http://schemas.microsoft.com/appx/manifest/desktop/windows10/6')
    $identity = $Manifest.SelectSingleNode('/p:Package/p:Identity', $namespaces)
    $app = $Manifest.SelectSingleNode('/p:Package/p:Applications/p:Application', $namespaces)
    if ($Manifest.SelectNodes('/p:Package/p:Identity', $namespaces).Count -ne 1 -or
        $Manifest.SelectNodes('/p:Package/p:Applications/p:Application', $namespaces).Count -ne 1 -or
        -not $identity -or -not $app -or $identity.Name -cne $Config.name -or
        $identity.Publisher -cne $Config.publisher -or $identity.Version -cne $ExpectedVersion -or
        $app.Id -cne $Config.applicationId -or $app.Executable -cne 'LumeShell.exe') {
        throw 'The identity package does not match this Lume build.'
    }
    foreach ($required in @(
        '/p:Package/p:Properties/uap10:AllowExternalContent[text()="true"]',
        '/p:Package/p:Properties/desktop6:RegistryWriteVirtualization[text()="disabled"]',
        '/p:Package/p:Properties/desktop6:FileSystemWriteVirtualization[text()="disabled"]',
        '/p:Package/p:Capabilities/rescap:Capability[@Name="runFullTrust"]',
        '/p:Package/p:Capabilities/rescap:Capability[@Name="unvirtualizedResources"]',
        '/p:Package/p:Capabilities/uap3:Capability[@Name="userNotificationListener"]',
        '/p:Package/p:Capabilities/uap11:Capability[@Name="graphicsCaptureWithoutBorder"]')) {
        if (-not $Manifest.SelectSingleNode($required, $namespaces)) { throw 'Identity manifest is missing its required desktop capabilities or virtualization policy.' }
    }
}
function Copy-LumeIdentityExternalAssets([Xml.XmlDocument]$Manifest, [string]$IdentityDirectory, [string]$ExternalLocation) {
    $namespaces = [Xml.XmlNamespaceManager]::new($Manifest.NameTable)
    $namespaces.AddNamespace('p', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
    $namespaces.AddNamespace('uap', 'http://schemas.microsoft.com/appx/manifest/uap/windows10')
    $logo = $Manifest.SelectSingleNode('/p:Package/p:Properties/p:Logo', $namespaces)
    $visual = $Manifest.SelectSingleNode('/p:Package/p:Applications/p:Application/uap:VisualElements', $namespaces)
    if (-not $logo -or -not $visual) { throw 'The identity manifest is missing its visual asset references.' }
    $assets = @(
        @{Name='StoreLogo.png';Relative=$logo.InnerText},
        @{Name='Square44x44Logo.png';Relative=$visual.GetAttribute('Square44x44Logo')},
        @{Name='Square150x150Logo.png';Relative=$visual.GetAttribute('Square150x150Logo')}
    )
    # Validate the entire plan before copying anything. Only the two supported
    # manifest layouts are allowed, including for externally supplied packages.
    foreach ($asset in $assets) {
        $asset.Relative = $asset.Relative.Replace('/', '\')
        if ($asset.Relative -cne ('Assets\' + $asset.Name) -and
            $asset.Relative -cne ('notification-identity\Assets\' + $asset.Name)) {
            throw 'Unexpected identity visual asset path.'
        }
        if (-not (Test-Path -LiteralPath (Join-Path $IdentityDirectory ('Assets\' + $asset.Name)) -PathType Leaf)) {
            throw ('Missing identity visual asset: ' + $asset.Name)
        }
    }
    foreach ($asset in $assets) {
        $source = Join-Path $IdentityDirectory ('Assets\' + $asset.Name)
        $destination = Join-Path $ExternalLocation $asset.Relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {
            throw ('Identity visual asset payload mismatch: ' + $asset.Name)
        }
    }
}
function Assert-LumeSignedIdentityPackage([string]$Path, $Config) {
    Assert-LumeIdentityConfig $Config
    $signedPackage = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    if ([IO.Path]::GetExtension($signedPackage) -ine '.msix') { throw 'Identity package must be a signed .msix file.' }
    Assert-LumeIdentitySignature (Get-AuthenticodeSignature -LiteralPath $signedPackage) $Config
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($signedPackage)
    try {
        $entries = @($archive.Entries | Where-Object { $_.FullName -ceq 'AppxManifest.xml' })
        if ($entries.Count -ne 1 -or $entries[0].Length -gt 65536) { throw 'Identity MSIX must contain one bounded package manifest.' }
        $manifestStream = $entries[0].Open()
        try { $manifest = Read-LumeIdentityXml $manifestStream } finally { $manifestStream.Dispose() }
        Assert-LumeIdentityManifest $manifest $Config
    } finally { $archive.Dispose() }
    return $signedPackage
}
