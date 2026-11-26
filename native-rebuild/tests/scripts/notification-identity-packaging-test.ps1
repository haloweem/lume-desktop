param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path,
    [switch]$FixtureWorker,
    [string]$SignedIdentityPackagePath = '',
    [string]$SignedIdentityConfigPath = ''
)
$ErrorActionPreference = 'Stop'
$identityRoot = Join-Path $RepositoryRoot 'native-rebuild\packaging\notification-identity'
. (Join-Path $identityRoot 'Identity-Common.ps1')
function Check([bool]$Value, [string]$Reason) { if (-not $Value) { throw $Reason } }
function Reject([scriptblock]$Action, [string]$Reason) {
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    Check $failed $Reason
}
function New-FixtureConfig {
    return [pscustomobject]@{name='Lume.Desktop.NotificationIdentity';publisher='CN=Lume & Partners';applicationId='LumeShell';version='1.0.9.0'}
}
function New-FixtureManifest($Config) {
    $xml = Get-Content -LiteralPath (Join-Path $identityRoot 'AppxManifest.xml.in') -Raw
    return [xml]$xml.Replace('@LUME_NOTIFICATION_IDENTITY_NAME@', $Config.name).
        Replace('@LUME_NOTIFICATION_IDENTITY_PUBLISHER_XML@', [Security.SecurityElement]::Escape($Config.publisher)).
        Replace('@LUME_NOTIFICATION_IDENTITY_APP_ID@', $Config.applicationId).
        Replace('@LUME_NOTIFICATION_IDENTITY_VERSION@', $Config.version)
}
function New-FixturePackage([string]$Path, [Xml.XmlDocument]$Manifest) {
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Create)
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $entry = $archive.CreateEntry('AppxManifest.xml')
        $writer = [IO.StreamWriter]::new($entry.Open(), [Text.UTF8Encoding]::new($false))
        try { $writer.Write($Manifest.OuterXml) } finally { $writer.Dispose() }
    } finally { $archive.Dispose(); $stream.Dispose() }
}
function Invoke-FixtureRegistration {
    # This worker runs in a separate PowerShell process. It replaces deployment
    # commands and package-path lookup before executing the real entry scripts.
    # No package registration, certificate or developer-mode state is changed.
    Add-Type -TypeDefinition @'
namespace LumeNotificationIdentity {
 public static class NativePackagePath {
  public static string ExternalPath(string name) {return name.StartsWith("fixture:")?name.Substring(8):null;}
 }
}
'@
    $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('LumeIdentityDeploymentTest-' + [guid]::NewGuid().ToString('N'))
    $fixtureIdentity = Join-Path $fixtureRoot 'notification-identity'
    New-Item -ItemType Directory -Force -Path $fixtureIdentity | Out-Null
    $global:FixtureConfig = New-FixtureConfig
    $global:FixturePackages = @(); $global:FixtureAdds = 0; $global:FixtureRemoves = 0; $global:FixtureSignature = 'Valid'
    $global:FixtureBusy = $false; $global:FixtureForced = 0; $global:FixtureResultStatus = 'Ok'
    function global:Get-AppxPackage { [CmdletBinding()] param([string]$Name) return @($global:FixturePackages | Where-Object Name -eq $Name) }
    function global:Get-AppxPackageManifest { [CmdletBinding()] param([string]$Package) return ,$global:FixtureInstalledManifest }
    function global:Get-AuthenticodeSignature {
        [CmdletBinding()] param([string]$LiteralPath)
        return [pscustomobject]@{Status=$global:FixtureSignature;SignerCertificate=[pscustomobject]@{Subject=$global:FixtureConfig.publisher}}
    }
    function global:Add-AppxPackage {
        [CmdletBinding()] param([string]$Path,[string]$Register,[string]$ExternalLocation,[switch]$ForceTargetApplicationShutdown)
        if ($global:FixtureBusy -and -not $ForceTargetApplicationShutdown) { throw 'Deployment failed with HRESULT: 0x80073D02; target package is in use.' }
        if ($ForceTargetApplicationShutdown) { ++$global:FixtureForced; $global:FixtureBusy=$false }
        ++$global:FixtureAdds
        $global:FixturePackages = @([pscustomobject]@{Name=$global:FixtureConfig.name;Publisher=$global:FixtureConfig.publisher;
            Version=$global:FixtureConfig.version;PackageFullName=('fixture:' + $ExternalLocation);IsDevelopmentMode=[bool]$Register;Status=$global:FixtureResultStatus})
    }
    function global:Remove-AppxPackage { [CmdletBinding()] param([string]$Package) ++$global:FixtureRemoves; $global:FixturePackages=@() }
    function global:Get-ItemProperty { [CmdletBinding()] param([string]$LiteralPath,[string]$Name) return $null }
    try {
        foreach ($name in @('Identity-Common.ps1','Register-NotificationIdentity.ps1','Remove-NotificationIdentity.ps1')) {
            Copy-Item -LiteralPath (Join-Path $identityRoot $name) -Destination (Join-Path $fixtureIdentity $name)
        }
        [IO.File]::WriteAllBytes((Join-Path $fixtureRoot 'LumeShell.exe'), [byte[]]@(0))
        $global:FixtureConfig | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixtureIdentity 'identity-config.json') -Encoding utf8
        $manifest = New-FixtureManifest $global:FixtureConfig
        $manifest.Save((Join-Path $fixtureIdentity 'AppxManifest.xml'))
        $fixtureMsix = Join-Path $fixtureRoot 'fixture.msix'
        New-FixturePackage $fixtureMsix $manifest
        $register = Join-Path $fixtureIdentity 'Register-NotificationIdentity.ps1'
        $remove = Join-Path $fixtureIdentity 'Remove-NotificationIdentity.ps1'
        Reject { & $register -InstallDirectory $fixtureRoot } 'Registration without an explicit mode must do nothing.'
        Check ($global:FixtureAdds -eq 0) 'Default script invocation unexpectedly deployed a package.'
        & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot | Out-Null
        Check ($global:FixtureAdds -eq 1) 'A validated signed-package plan must register for the exact installation.'
        & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot | Out-Null
        Check ($global:FixtureAdds -eq 1) 'Matching signed identity must be idempotent.'
        $global:FixturePackages[0].Status='Modified, NeedsRemediation'
        & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot | Out-Null
        Check ($global:FixtureAdds -eq 2 -and $global:FixturePackages[0].Status -eq 'Ok') 'Matching version with damaged registration must be repaired, not reported ready.'
        $global:FixturePackages[0].Status='Modified'; $global:FixtureResultStatus='Modified'
        Reject { & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot } 'A completed deployment with unhealthy package status must not report success.'
        $global:FixturePackages[0].Status='Ok'; $global:FixtureResultStatus='Ok'
        $global:FixturePackages[0].Version='1.0.8.0'
        & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot | Out-Null
        Check ($global:FixtureAdds -eq 4) 'An older identity from this same publisher/install must upgrade.'
        $global:FixturePackages[0].Version='1.0.10.0'
        $global:FixtureInstalledManifest = New-FixtureManifest $global:FixtureConfig
        $global:FixtureInstalledManifest.Package.Identity.Version = '1.0.10.0'
        & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot | Out-Null
        Check ($global:FixtureAdds -eq 4 -and $global:FixtureRemoves -eq 0) 'Rollback must reuse a healthy compatible newer identity in place.'
        $global:FixturePackages[0].IsDevelopmentMode = $true
        Reject { & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot } 'A newer development registration must not satisfy production.'
        $global:FixturePackages[0].IsDevelopmentMode = $false
        $global:FixturePackages[0].Status = 'Modified'
        Reject { & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot } 'A damaged newer registration must not be reused or downgraded.'
        $global:FixturePackages[0].Status = 'Ok'
        $capability = $global:FixtureInstalledManifest.SelectSingleNode('//*[local-name()="Capability" and @Name="userNotificationListener"]')
        [void]$capability.ParentNode.RemoveChild($capability)
        Reject { & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot } 'A newer registration missing a required capability must not be reused.'
        $global:FixturePackages[0].Version='1.0.9.0';$global:FixtureSignature='HashMismatch'
        Reject { & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot } 'Even an existing matching identity must not bypass signed-input validation.'
        $global:FixtureSignature='Valid';$global:FixturePackages[0].PackageFullName='fixture:C:\OtherLume'
        Reject { & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot } 'Another installation must not be taken over.'
        & $remove -InstallDirectory $fixtureRoot | Out-Null
        Check ($global:FixtureRemoves -eq 0) 'Uninstall must preserve another external installation.'
        $global:FixturePackages[0].PackageFullName='fixture:' + $fixtureRoot
        $global:FixturePackages[0].Publisher='CN=Other Publisher'
        & $remove -InstallDirectory $fixtureRoot | Out-Null
        Check ($global:FixtureRemoves -eq 0) 'Uninstall must preserve another publisher.'
        $global:FixturePackages[0].Publisher=$global:FixtureConfig.publisher
        $global:FixturePackages[0].Version='1.0.8.0'; $global:FixtureBusy=$true
        Reject { & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot } 'Busy package must not close apps without installer authorization.'
        Check ($global:FixtureForced -eq 0) 'Default registration force-closed a package.'
        & $register -IdentityPackagePath $fixtureMsix -InstallDirectory $fixtureRoot -CloseRunningLume | Out-Null
        Check ($global:FixtureAdds -eq 5 -and $global:FixtureForced -eq 1) 'Authorized upgrade must retry a busy verified target package once.'
        & $remove -InstallDirectory $fixtureRoot | Out-Null
        Check ($global:FixtureRemoves -eq 1) 'Uninstall must remove only the exactly owned identity.'
        Reject { & $register -DevelopmentRegistration -InstallDirectory $fixtureRoot } 'Development registration must not enable a disabled Developer Mode.'
        Check ($global:FixtureAdds -eq 5) 'Negative deployment paths unexpectedly registered a package.'
        Write-Output 'PASS: identity deployment entry scripts on isolated mocked registration/upgrade/uninstall commands; no real registration or trust changes.'
    } finally {
        $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
        $expectedPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\LumeIdentityDeploymentTest-'
        if ($resolvedFixture.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolvedFixture -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
if ($FixtureWorker) { Invoke-FixtureRegistration; return }

$config = New-FixtureConfig
Assert-LumeIdentityConfig $config
$validSignature = [pscustomobject]@{Status='Valid';SignerCertificate=[pscustomobject]@{Subject=$config.publisher}}
Assert-LumeIdentitySignature $validSignature $config
foreach ($status in @('NotSigned','UnknownError','NotTrusted','HashMismatch')) {
    Reject { Assert-LumeIdentitySignature ([pscustomobject]@{Status=$status;SignerCertificate=$validSignature.SignerCertificate}) $config } 'Untrusted, unsigned and damaged signatures must fail.'
}
Reject { Assert-LumeIdentitySignature ([pscustomobject]@{Status='Valid';SignerCertificate=[pscustomobject]@{Subject='CN=Other'}}) $config } 'A different trusted signer must fail exact publisher matching.'
Reject { Assert-LumeIdentityConfig ([pscustomobject]@{name=$config.name;publisher=' ';applicationId=$config.applicationId;version=$config.version}) } 'An empty publisher is invalid.'
$manifest = New-FixtureManifest $config
Assert-LumeIdentityManifest $manifest $config
$assetFixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('LumeIdentityExternalAssetsTest-' + [guid]::NewGuid().ToString('N'))
try {
    foreach ($layout in @('Assets\', 'notification-identity\Assets\')) {
        $externalRoot = Join-Path $assetFixtureRoot ([guid]::NewGuid().ToString('N'))
        $assetManifest = [xml]$manifest.OuterXml
        $assetManifest.Package.Properties.Logo = $layout + 'StoreLogo.png'
        $visual = $assetManifest.Package.Applications.Application.VisualElements
        $visual.SetAttribute('Square44x44Logo', $layout + 'Square44x44Logo.png')
        $visual.SetAttribute('Square150x150Logo', $layout + 'Square150x150Logo.png')
        # Reproduce the old installer: metadata-local images alone do not
        # satisfy Assets-relative application lookups at ExternalLocation.
        $metadataAssets = Join-Path $externalRoot 'notification-identity\Assets'
        New-Item -ItemType Directory -Path $metadataAssets -Force | Out-Null
        foreach ($name in @('StoreLogo.png', 'Square44x44Logo.png', 'Square150x150Logo.png')) {
            Copy-Item -LiteralPath (Join-Path $identityRoot ('Assets\' + $name)) -Destination $metadataAssets
        }
        if ($layout -ceq 'Assets\') {
            Check (-not (Test-Path -LiteralPath (Join-Path $externalRoot $visual.GetAttribute('Square44x44Logo')))) 'The missing taskbar asset fixture must reproduce the metadata-only layout.'
        }
        Copy-LumeIdentityExternalAssets $assetManifest $identityRoot $externalRoot
        foreach ($name in @('StoreLogo.png', 'Square44x44Logo.png', 'Square150x150Logo.png')) {
            $resolvedAsset = Join-Path $externalRoot ($layout + $name)
            Check ((Get-FileHash -LiteralPath $resolvedAsset -Algorithm SHA256).Hash -eq
                (Get-FileHash -LiteralPath (Join-Path $identityRoot ('Assets\' + $name)) -Algorithm SHA256).Hash) 'Every application logo must resolve from the installation root with the original pixels.'
        }
    }
    $invalidManifest = [xml]$manifest.OuterXml
    $invalidManifest.Package.Applications.Application.VisualElements.SetAttribute('Square44x44Logo', '..\Square44x44Logo.png')
    $rejectedRoot = Join-Path $assetFixtureRoot 'rejected'
    Reject { Copy-LumeIdentityExternalAssets $invalidManifest $identityRoot $rejectedRoot } 'A visual asset path must not escape the installation.'
    Check (-not (Test-Path -LiteralPath $rejectedRoot)) 'Invalid visual asset references must be rejected before any copy.'
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($assetFixtureRoot)
    $expectedPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\LumeIdentityExternalAssetsTest-'
    if ($resolvedFixture.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force -ErrorAction SilentlyContinue
    }
}
foreach ($mutation in @('publisher','version','app','capability','borderless-capability','borderless-namespace','virtualization')) {
    $broken = [xml]$manifest.OuterXml
    $ns = [Xml.XmlNamespaceManager]::new($broken.NameTable);$ns.AddNamespace('p','http://schemas.microsoft.com/appx/manifest/foundation/windows10')
    switch ($mutation) {
        'publisher' {$broken.Package.Identity.SetAttribute('Publisher','CN=Other')}
        'version' {$broken.Package.Identity.SetAttribute('Version','1.0.10.0')}
        'app' {$broken.Package.Applications.Application.SetAttribute('Id','AnotherApp')}
        'capability' {$node=$broken.SelectSingleNode('//*[local-name()="Capability" and @Name="userNotificationListener"]');[void]$node.ParentNode.RemoveChild($node)}
        'borderless-capability' {$node=$broken.SelectSingleNode('//*[local-name()="Capability" and @Name="graphicsCaptureWithoutBorder"]');[void]$node.ParentNode.RemoveChild($node)}
        'borderless-namespace' {$node=$broken.SelectSingleNode('//*[local-name()="Capability" and @Name="graphicsCaptureWithoutBorder"]');$replacement=$broken.CreateElement('uap3','Capability','http://schemas.microsoft.com/appx/manifest/uap/windows10/3');$replacement.SetAttribute('Name','graphicsCaptureWithoutBorder');[void]$node.ParentNode.ReplaceChild($replacement,$node)}
        'virtualization' {$node=$broken.SelectSingleNode('//*[local-name()="FileSystemWriteVirtualization"]');$node.InnerText='enabled'}
    }
    Reject { Assert-LumeIdentityManifest $broken $config } "Invalid identity manifest mutation passed: $mutation"
}
$builderFixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('LumeIdentityBuilderValidationTest-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $builderFixtureRoot | Out-Null
    $builder = Join-Path $identityRoot 'Build-NotificationIdentityPackage.ps1'
    $builderOutput = Join-Path $builderFixtureRoot 'Lume.NotificationIdentity.msix'
    foreach ($capabilityName in @('userNotificationListener','graphicsCaptureWithoutBorder')) {
        $broken = [xml]$manifest.OuterXml
        $node = $broken.SelectSingleNode('//*[local-name()="Capability" and @Name="' + $capabilityName + '"]')
        [void]$node.ParentNode.RemoveChild($node)
        $broken.Save((Join-Path $builderFixtureRoot 'AppxManifest.xml'))
        $failure = ''
        try { & $builder -IdentityDirectory $builderFixtureRoot -OutputPath $builderOutput | Out-Null }
        catch { $failure = $_.Exception.Message }
        Check ($failure -ceq 'The generated manifest is not the expected Lume notification identity.') "Identity builder must reject missing $capabilityName before selecting SDK tools or creating a package."
        Check (-not (Test-Path -LiteralPath $builderOutput)) 'Rejected identity validation unexpectedly produced a package.'
    }
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($builderFixtureRoot)
    $expectedPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\LumeIdentityBuilderValidationTest-'
    if ($resolvedFixture.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force -ErrorAction SilentlyContinue
    }
}
foreach ($sid in @('S-1-5-18','S-1-5-19','S-1-5-20')) { Reject { Assert-LumeIdentityUserContext $sid 1 } 'Service identity must not masquerade as the user.' }
Reject { Assert-LumeIdentityUserContext 'S-1-5-21-1-2-3-1001' 0 } 'SYSTEM/session-zero deployment must not report per-user success.'
Assert-LumeIdentityUserContext 'S-1-5-21-1-2-3-1001' 1
Reject { Assert-LumeIdentityUserContext 'S-1-5-21-1-2-3-1002' 1 'S-1-5-21-1-2-3-1001' } 'Another RunAs administrator must not register in the real user session.'
Assert-LumeIdentityUserContext 'S-1-5-21-1-2-3-1001' 1 'S-1-5-21-1-2-3-1001'
Check (Test-LumeIdentityExternalPath 'D:\lume\' 'd:\Lume') 'Exact installation path must permit normal case/trailing-separator differences.'
Check (-not (Test-LumeIdentityExternalPath 'D:\lume-other' 'D:\lume')) 'An adjacent installation must not match.'
Check (-not (Test-LumeIdentityExternalPath '' 'D:\lume')) 'A missing effective external location must never authorize removal.'

$packageSource = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'native-rebuild\tests\scripts\package-original-pages.ps1') -Raw
$installer = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'native-rebuild\packaging\LumeSetup.iss') -Raw
Check ($packageSource.Contains('Copy-LumeIdentityExternalAssets $identityManifest $notificationIdentityRoot $stage')) 'Packaging must stage visual assets at their manifest paths under ExternalLocation.'
foreach ($name in @('StoreLogo.png', 'Square44x44Logo.png', 'Square150x150Logo.png')) {
    Check ($installer.Contains(('Source: "{#SourceDir}\Assets\' + $name + '"; DestDir: "{app}\Assets"; Flags: ignoreversion'))) 'The installer must deliver every root-relative identity logo.'
}
Check ($packageSource -match 'Assert-LumeSignedIdentityPackage \$NotificationIdentityPackage \$compiledIdentityConfig') 'Packaging must validate signed material against the compiled identity.'
Check ($packageSource -match 'notification-identity=\$notificationIdentityMode' -and $packageSource -match 'notification-identity-package-sha256=\$notificationIdentityPackageHash') 'Delivery evidence must identify signed registration versus metadata-only output and pin the signed input.'
Check ($installer -match 'AfterInstall: PrepareInstalledLume' -and
    $installer -match '(?s)procedure PrepareInstalledLume;\s*begin\s*InstallPreparedForLaunch := False;\s*VerifyAllInstalledUi;\s*RegisterSignedNotificationIdentity;\s*InstallPreparedForLaunch := True;') 'The launch gate must open only after integrity checks and the synchronous identity attempt.'
Check ($installer -match '(?s)procedure RegisterSignedNotificationIdentity;.*?ExecAsOriginalUser.*?ewWaitUntilTerminated' -and
    $installer -match 'Result := UiWasVerified and InstallPreparedForLaunch') 'Shell launch must wait for the identity attempt to finish.'
Check ($installer -match 'Check: IsVerifiedLumeStart' -and $installer -match 'Result := IsLumeUpdate and IsVerifiedLumeStart') 'Normal and silent Shell starts must both honor the launch gate.'
$registrationBody = [regex]::Match($installer, '(?s)procedure RegisterSignedNotificationIdentity;(.+?)procedure PrepareInstalledLume;').Groups[1].Value
Check ($registrationBody -match '(?s)GetSHA256OfFile\(PackageFile\).*?RaiseException' -and
    $registrationBody -match '(?s)ewWaitUntilTerminated, ResultCode\) then\s*begin\s*Log\([^;]+;\s*Exit;' -and
    $registrationBody -match '(?s)if ResultCode <> 0 then\s*begin\s*Log\([^;]+;\s*Exit;') 'A damaged payload must stop setup, while host or registration errors must allow desktop fallback.'
foreach ($file in @('Register-NotificationIdentity.ps1','Remove-NotificationIdentity.ps1','Identity-Common.ps1','Build-NotificationIdentityPackage.ps1')) {
    $source = Get-Content -LiteralPath (Join-Path $identityRoot $file) -Raw
    Check ($source -notmatch '(?i)Import-Certificate|Import-PfxCertificate|Set-ItemProperty|New-ItemProperty|Remove-ItemProperty|RequestAccessAsync|Get-AppxPackage[^\r\n]*-AllUsers') 'Deployment scripts must not add trust, change Developer Mode, request private access or affect all users.'
    $tokens=$null;$parseErrors=$null
    [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $identityRoot $file),[ref]$tokens,[ref]$parseErrors)
    Check (-not $parseErrors) "Invalid PowerShell deployment source: $file"
}
$engine = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
& $engine -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $PSCommandPath -RepositoryRoot $RepositoryRoot -FixtureWorker
Check ($LASTEXITCODE -eq 0) 'Isolated deployment command fixtures failed.'
$moduleProbeRoot=Join-Path $RepositoryRoot ('build-artifacts\identity-module-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $moduleProbeRoot | Out-Null
$moduleProbe=Join-Path $moduleProbeRoot 'host-module-probe.ps1'
@'
param([string]$CommonPath,[string]$PollutedModulePath,[string]$SignatureTarget,[string]$IdentityPackage,[string]$IdentityConfig,[string]$OutputPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
# Restrict the inherited search path to another host's module directory. The
# fixture is a new Windows PowerShell process and changes no registry/trust.
$env:PSModulePath=$PollutedModulePath
Remove-Module Microsoft.PowerShell.Security -ErrorAction SilentlyContinue
$moduleOutput=@(. $CommonPath)
if ($moduleOutput.Count -ne 0) { throw 'Identity initialization leaked objects into its callers.' }
$expectedPrefix=[IO.Path]::GetFullPath((Join-Path $PSHOME 'Modules')).TrimEnd('\')+'\'
$loaded=@()
foreach($name in @('Microsoft.PowerShell.Management','Microsoft.PowerShell.Utility','Microsoft.PowerShell.Security')) {
    $matchingModules=@(Get-Module $name -ErrorAction Stop)
    if ($matchingModules.Count -eq 0) { throw "Identity did not load its host module: $name" }
    foreach($module in $matchingModules) {
        if (-not $module.Path.StartsWith($expectedPrefix,[StringComparison]::OrdinalIgnoreCase)) {
            throw "Identity loaded an incompatible module from another host: $name"
        }
    }
    $loaded+=$name
}
$signature=Get-AuthenticodeSignature -LiteralPath $SignatureTarget
if ($signature.Status -ne 'Valid') { throw 'Actual Get-AuthenticodeSignature failed on the host signed executable.' }
$validatedIdentity=$false
if ($IdentityPackage) {
    $config=Get-Content -LiteralPath $IdentityConfig -Raw | ConvertFrom-Json
    $validated=@(Assert-LumeSignedIdentityPackage $IdentityPackage $config)
    if ($validated.Count -ne 1 -or $validated[0] -cne (Resolve-Path -LiteralPath $IdentityPackage).Path) {
        throw 'Actual signed identity validation did not return exactly its approved path.'
    }
    $validatedIdentity=$true
}
[ordered]@{host=$PSVersionTable.PSVersion.ToString();modules=$loaded;signatureStatus=[string]$signature.Status;
    signedIdentityValidated=$validatedIdentity;registrationAttempted=$false;trustChanged=$false} |
    ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $OutputPath -Encoding utf8
'@ | Set-Content -LiteralPath $moduleProbe -Encoding utf8
$pollutedModulePath=Join-Path $PSHOME 'Modules'
# Even when this test is invoked by Windows PowerShell, discover the bundled
# pwsh used by the calling environment if present; otherwise a missing module
# directory still proves that the explicit host paths are sufficient.
$pwsh=Get-Command pwsh.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($pwsh) { $pollutedModulePath=Join-Path (Split-Path $pwsh.Source -Parent) 'Modules' }
if ($pollutedModulePath -ieq (Join-Path (Split-Path $engine -Parent) 'Modules')) {
    $pollutedModulePath=Join-Path $moduleProbeRoot 'foreign-host-modules'
}
if ([bool]$SignedIdentityPackagePath -ne [bool]$SignedIdentityConfigPath) { throw 'The read-only real identity probe requires both signed package and matching config paths.' }
$moduleProbeOutput=Join-Path $moduleProbeRoot 'result.json'
$moduleProbeArguments=@('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$moduleProbe,
    '-CommonPath',(Join-Path $identityRoot 'Identity-Common.ps1'),'-PollutedModulePath',$pollutedModulePath,
    '-SignatureTarget',$engine,'-OutputPath',$moduleProbeOutput)
if ($SignedIdentityPackagePath) {
    $moduleProbeArguments+=@('-IdentityPackage',(Resolve-Path -LiteralPath $SignedIdentityPackagePath).Path,
        '-IdentityConfig',(Resolve-Path -LiteralPath $SignedIdentityConfigPath).Path)
}
& $engine @moduleProbeArguments *> (Join-Path $moduleProbeRoot 'probe.log')
Check ($LASTEXITCODE -eq 0) 'The clean Windows PowerShell host must initialize and validate signatures with a polluted inherited module search path.'
$moduleProbeResult=Get-Content -LiteralPath $moduleProbeOutput -Raw | ConvertFrom-Json
Check ($moduleProbeResult.modules.Count -eq 3 -and $moduleProbeResult.signatureStatus -ceq 'Valid') 'The identity entry point must load all three host modules and execute the actual Authenticode cmdlet.'
if ($SignedIdentityPackagePath) { Check $moduleProbeResult.signedIdentityValidated 'The read-only actual signed MSIX verification did not run.' }
Write-Output ('PASS: clean Windows PowerShell host modules and actual signature validation with foreign PSModulePath; signed MSIX=' + $moduleProbeResult.signedIdentityValidated + '; no registration or trust changes.')
Write-Output ('MODULE_EVIDENCE=' + $moduleProbeRoot)
Write-Output 'PASS: signed identity validation, publisher/capability/virtualization policy, upgrade ownership, package health and installer source wiring; actual launch ordering requires real installation.'
