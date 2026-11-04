param([string]$OutputDirectory,[string]$Iscc='')
$ErrorActionPreference='Stop'
if(-not $OutputDirectory){throw 'Supply a dedicated verification output directory'}
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
if(-not $Iscc){$Iscc=Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'}
$repository=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$include=(Join-Path $repository 'native-rebuild\packaging\installer_ui_integrity.iss').Replace('\','/')
$good=[Text.Encoding]::UTF8.GetBytes('verified UI fixture')
$hash=[Security.Cryptography.SHA256]::Create()
try{$digest=([BitConverter]::ToString($hash.ComputeHash($good))).Replace('-','').ToLowerInvariant()}finally{$hash.Dispose()}
foreach($case in @('healthy','corrupt')){
    $caseRoot=Join-Path $output $case
    New-Item -ItemType Directory -Path $caseRoot -Force | Out-Null
    $source=Join-Path $caseRoot 'source.js'
    [IO.File]::WriteAllBytes($source,$(if($case -eq 'healthy'){$good}else{[byte[]]::new($good.Length)}))
    $checks=Join-Path $caseRoot 'checks.iss'
    "VerifyInstalledUiFile('ui\localization.js', '$digest');" | Set-Content -LiteralPath $checks -Encoding UTF8
    $script=Join-Path $caseRoot 'fixture.iss'
    @"
#define UiIntegrityChecksFile `"$($checks.Replace('\','/'))`"
[Setup]
AppName=Lume UI integrity verification
AppVersion=1
DefaultDirName=$caseRoot\installed
PrivilegesRequired=lowest
Uninstallable=no
CreateAppDir=yes
OutputDir=$caseRoot
OutputBaseFilename=verify-ui
DisableDirPage=yes
DisableProgramGroupPage=yes
[Files]
Source: `"$source`"; DestDir: `"{app}\ui`"; DestName: `"localization.js`"
Source: `"$checks`"; DestDir: `"{app}`"; DestName: `"ui-integrity.stamp`"; Flags: ignoreversion; AfterInstall: VerifyAllInstalledUi
[Run]
Filename: `"{cmd}`"; Parameters: `"/c exit 0`"; Flags: runhidden; Check: UiWasVerified
[Code]
#include `"$include`"
"@ | Set-Content -LiteralPath $script -Encoding UTF8
    & $Iscc '/Q' $script *> (Join-Path $caseRoot 'compile.log')
    if($LASTEXITCODE -ne 0){Get-Content -LiteralPath (Join-Path $caseRoot 'compile.log') -Tail 15;throw ('Installer fixture compile failed: '+$case)}
    $setup=Join-Path $caseRoot 'verify-ui.exe'
    $log=Join-Path $caseRoot 'install.log'
    $process=Start-Process -FilePath $setup -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+$log+'"')) -PassThru -WindowStyle Hidden
    if(-not $process.WaitForExit(20000)){throw 'Isolated installer verification timed out'}
    if($case -eq 'healthy' -and $process.ExitCode -ne 0){throw 'Verified UI fixture failed installation'}
    if($case -eq 'corrupt' -and $process.ExitCode -eq 0){throw 'Corrupt UI fixture was accepted by the installer'}
    $text=Get-Content -LiteralPath $log -Raw
    if($case -eq 'healthy' -and $text -notmatch 'flushed and SHA-256 verified'){throw 'Installer did not perform the integrity phase'}
    if($case -eq 'corrupt' -and $text -notmatch 'localization.js'){throw 'Installer failed without identifying the corrupt file'}
    if($case -eq 'corrupt' -and $text -match '-- Run entry --'){throw 'Corrupt UI was allowed to launch a program'}
    if($case -eq 'healthy' -and $text -notmatch '-- Run entry --'){throw 'Healthy UI was not allowed to launch'}
    Write-Output ("PASS isolated installer UI $case exit="+$process.ExitCode)
}
