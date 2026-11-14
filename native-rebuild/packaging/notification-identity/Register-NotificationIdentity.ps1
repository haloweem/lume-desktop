[CmdletBinding()]
param(
    [switch]$DevelopmentRegistration,
    [string]$IdentityPackagePath = '',
    [string]$InstallDirectory = (Split-Path $PSScriptRoot -Parent),
    [switch]$CloseRunningLume
)
$ErrorActionPreference = 'Stop'
$registrationLog = [IO.Path]::Combine([Environment]::GetFolderPath('LocalApplicationData'), 'Lume', 'Logs', 'notification-identity-install.log')
function Write-RegistrationLog([string]$Message) {
    try {
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($registrationLog))
        [IO.File]::AppendAllText($registrationLog, ([DateTime]::Now.ToString('o') + ' ' + $Message + [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
    } catch { }
}
$phase = 'initialization'
try {
Write-RegistrationLog ('Starting registration; close-running-lume=' + [bool]$CloseRunningLume)
. "$PSScriptRoot\Identity-Common.ps1"
if ([int][bool]$DevelopmentRegistration + [int][bool]$IdentityPackagePath -ne 1) {
    throw 'Choose explicit -DevelopmentRegistration or -IdentityPackagePath <trusted-signed.msix>. Nothing was registered.'
}
if ([Environment]::OSVersion.Version.Build -lt 19041) { throw 'External-location identity requires Windows 10 build 19041 or later.' }
$registrationSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$registrationSession = [Diagnostics.Process]::GetCurrentProcess().SessionId
Assert-LumeIdentityUserContext $registrationSid $registrationSession
Assert-LumeIdentityUserContext $registrationSid $registrationSession (Get-LumeInteractiveUserSid $registrationSession)
$identityConfig = Get-LumeIdentityConfig
$installRoot = Get-LumeIdentityPath $InstallDirectory
if (-not (Test-Path -LiteralPath (Join-Path $installRoot 'LumeShell.exe') -PathType Leaf)) { throw 'LumeShell.exe was not found in InstallDirectory.' }
$signedPackage = $null
$phase = 'signature-and-manifest'
if (-not $DevelopmentRegistration) { $signedPackage = Assert-LumeSignedIdentityPackage $IdentityPackagePath $identityConfig }
$phase = 'existing-identity'
foreach ($existing in @(Get-AppxPackage -Name $identityConfig.name | Where-Object { $_.Name -ceq $identityConfig.name })) {
    $existingExternalPath = Get-LumePackageExternalPath $existing.PackageFullName
    $action = Get-LumeIdentityRegistrationAction $existing $identityConfig $existingExternalPath $installRoot (-not $DevelopmentRegistration)
    if ($action -eq 'current' -and [string]$existing.Status -in @('Ok', '0')) {
        Write-RegistrationLog 'Matching identity is already registered.'
        Write-Output 'The matching identity is already registered for this user and installation. Restart Lume to use it.'
        return
    }
    if ($action -eq 'current') { Write-RegistrationLog ('Matching identity needs repair; status=' + $existing.Status) }
}
if ($DevelopmentRegistration) {
    $developerMode = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -Name AllowDevelopmentWithoutDevLicense -ErrorAction SilentlyContinue
    if (-not $developerMode -or $developerMode.AllowDevelopmentWithoutDevLicense -ne 1) {
        throw 'Developer Mode is not enabled. This script never changes that setting or installs certificate trust.'
    }
    $manifestPath = Join-Path $PSScriptRoot 'AppxManifest.xml'
    $manifestStream = [IO.File]::OpenRead($manifestPath)
    try { $manifest = Read-LumeIdentityXml $manifestStream } finally { $manifestStream.Dispose() }
    Assert-LumeIdentityManifest $manifest $identityConfig
    Add-AppxPackage -Register $manifestPath -ExternalLocation $installRoot -ErrorAction Stop
} else {
    $phase = 'signed-package-deployment'
    try {
        Add-AppxPackage -Path $signedPackage -ExternalLocation $installRoot -ErrorAction Stop
    } catch {
        # The installer has already requested a graceful shutdown. Windows may
        # still retain package resources or an app may relaunch during copying.
        # Only retry resource-in-use for this verified, owned target package.
        $busy = $_.Exception.HResult -eq -2147009278 -or $_.ToString() -match '(?i)0x80073D02'
        if (-not $CloseRunningLume -or -not $busy) { throw }
        Write-RegistrationLog '0x80073D02: retrying after Windows closes only the target Lume package.'
        Add-AppxPackage -Path $signedPackage -ExternalLocation $installRoot -ForceTargetApplicationShutdown -ErrorAction Stop
    }
}
$phase = 'verify-registration'
$registered = @(Get-AppxPackage -Name $identityConfig.name | Where-Object {
    $_.Name -ceq $identityConfig.name -and $_.Publisher -ceq $identityConfig.publisher -and
    [version]$_.Version -eq [version]$identityConfig.version -and ($DevelopmentRegistration -or -not $_.IsDevelopmentMode) -and
    [string]$_.Status -in @('Ok', '0') -and
    (Test-LumeIdentityLocation $_ $installRoot)
})
if ($registered.Count -ne 1) { throw 'Registration finished without one matching current-user external-location identity.' }
Write-RegistrationLog ('Registered and verified ' + $identityConfig.version + ' at ' + $installRoot)
Write-Output 'Lume notification identity registered for the current user. Restart Lume; notification access remains controlled by the user.'
} catch {
    Write-RegistrationLog ('FAILED phase=' + $phase + '; HRESULT=0x' + $_.Exception.HResult.ToString('X8') + '; ' + ($_ | Out-String))
    throw
}
