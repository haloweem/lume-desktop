[CmdletBinding()]
param([string]$InstallDirectory = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\Identity-Common.ps1"
$identityConfig = Get-LumeIdentityConfig
$installRoot = Get-LumeIdentityPath $InstallDirectory
$removed = 0
foreach ($package in @(Get-AppxPackage -Name $identityConfig.name | Where-Object { $_.Name -ceq $identityConfig.name })) {
    $externalPath = Get-LumePackageExternalPath $package.PackageFullName
    if (-not (Test-LumeIdentityOwner $package $identityConfig $externalPath $installRoot)) {
        Write-Output 'Preserved an identity whose publisher or external installation location does not match.'
        continue
    }
    Remove-AppxPackage -Package $package.PackageFullName -ErrorAction Stop
    ++$removed
}
Write-Output "Removed matching current-user Lume notification identities: $removed"
