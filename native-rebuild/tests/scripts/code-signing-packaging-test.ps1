param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path,
    [string]$SigningConfigurationPath = '',
    [string]$SignedFilePath = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $RepositoryRoot 'native-rebuild\tools\signing\Signing-Common.ps1')
$checks = 0
function Check([bool]$Value, [string]$Reason) {
    if (-not $Value) { throw $Reason }
    $script:checks++
}
function Reject([scriptblock]$Action, [string]$Reason) {
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    Check $failed $Reason
}
$root = Join-Path $RepositoryRoot ('build-artifacts\signing-packaging-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$now = Get-Date
foreach ($invalid in @('private-key','future','expired','eku','key-usage')) {
    $candidate = [pscustomobject]@{
        HasPrivateKey=$true; Subject='CN=Lume Signing Fixture'; NotBefore=$now.AddDays(-1); NotAfter=$now.AddDays(1)
        Extensions=@([pscustomobject]@{Oid=[pscustomobject]@{Value='2.5.29.37'};
            EnhancedKeyUsages=@([pscustomobject]@{Value='1.3.6.1.5.5.7.3.3'})})
    }
    switch ($invalid) {
        'private-key' {$candidate.HasPrivateKey=$false}
        'future' {$candidate.NotBefore=$now.AddDays(1)}
        'expired' {$candidate.NotAfter=$now.AddDays(-1)}
        'eku' {$candidate.Extensions[0].EnhancedKeyUsages=@([pscustomobject]@{Value='1.3.6.1.5.5.7.3.1'})}
        'key-usage' {$candidate.Extensions+= [pscustomobject]@{Oid=[pscustomobject]@{Value='2.5.29.15'};
            KeyUsages=[Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment}}
    }
    Reject { Assert-LumeSigningCertificate $candidate } "Invalid certificate must fail before any signing: $invalid"
}
Reject { New-LumeCodeSigningContext -CertificateThumbprint 'not-a-thumbprint' -TimestampUrl 'http://time.certum.pl' } 'A partial certificate selector must be rejected.'
Reject { New-LumeCodeSigningContext -CertificateThumbprint ('A'*40) -TimestampUrl 'http://user:password@example.invalid/' } 'Timestamp URL credentials must be rejected before reading a certificate.'
Reject { & (Join-Path $RepositoryRoot 'native-rebuild\tests\scripts\package-original-pages.ps1') -CertificateThumbprint ' ' } 'An explicitly empty signing selector must not silently generate an unsigned package.'
Reject { & (Join-Path $RepositoryRoot 'native-rebuild\tests\scripts\package-original-pages.ps1') -TimestampUrl 'http://time.certum.pl' } 'A timestamp without a signing selector must fail before any packaging side effects.'

$context = [pscustomobject]@{Thumbprint=('A'*40); Subject=('CN="Lume $ Signing & Partners", O=Exact;Subject]====]' + [char]0x4e2d + [char]0x6587);
    CertificateStoreLocation='CurrentUser';TimestampUrl='http://time.certum.pl';SignToolPath=''}
$cachePath = Join-Path $root 'publisher.cmake'
Write-LumeSigningCMakeCache $context $cachePath
$cmakeProbe = Join-Path $root 'read-publisher.cmake'
[IO.File]::WriteAllText($cmakeProbe, 'include("${CMAKE_CURRENT_LIST_DIR}/publisher.cmake")' + "`n" +
    'file(WRITE "${CMAKE_CURRENT_LIST_DIR}/publisher.txt" "${LUME_NOTIFICATION_IDENTITY_PUBLISHER}")' + "`n", [Text.UTF8Encoding]::new($false))
& cmake -P $cmakeProbe *> (Join-Path $root 'cmake-publisher.log')
Check ($LASTEXITCODE -eq 0) 'The exact Publisher CMake cache must parse.'
Check ([IO.File]::ReadAllText((Join-Path $root 'publisher.txt')) -ceq $context.Subject) 'CMake must preserve quotes, dollar signs, Unicode-safe text and semicolons in the certificate Subject.'
$owned = @(Get-LumeOwnedSigningFiles)
Check ($owned.Count -eq 11 -and @($owned | Sort-Object -Unique).Count -eq 11) 'The owned product allowlist must be complete and unique.'
Check (@($owned | Where-Object {$_ -match 'taskbar|WebView|mpv\.exe|d3dcompiler|test'}).Count -eq 0) 'Pinned third-party binaries and test programs must not be selected for signing.'

# These are explicit mocks: no certificate store, private-key provider or real
# signing process is used by the following control-flow checks.
$global:LumeSigningFixtureReady=$false
$global:LumeSigningFixtureSignCalls=0
$global:LumeSigningFixtureFailure=''
function Get-AuthenticodeSignature {
    param([string]$LiteralPath)
    if (-not $global:LumeSigningFixtureReady) { return [pscustomobject]@{Status='NotSigned';SignerCertificate=$null;TimeStamperCertificate=$null} }
    return [pscustomobject]@{Status='Valid';SignerCertificate=[pscustomobject]@{Thumbprint=('A'*40);Subject=$context.Subject};
        TimeStamperCertificate=[pscustomobject]@{Subject='CN=Mock Timestamp'}}
}
$fakeSignTool = Join-Path $root 'fake-signtool.ps1'
@'
if ($args[0] -eq $global:LumeSigningFixtureFailure) { $global:LASTEXITCODE=7; return }
if ($args[0] -eq 'sign') { $global:LumeSigningFixtureSignCalls++; $global:LumeSigningFixtureReady=$true }
$global:LASTEXITCODE=0
'@ | Set-Content -LiteralPath $fakeSignTool -Encoding utf8
$context.SignToolPath = $fakeSignTool
$mockProduct = Join-Path $root 'mock-product.exe'
'MOCK ONLY; NOT A SIGNED EXECUTABLE' | Set-Content -LiteralPath $mockProduct -Encoding ascii
$record = Invoke-LumeCodeSignFile $mockProduct $context
Check ($record.action -eq 'signed' -and $global:LumeSigningFixtureSignCalls -eq 1) 'An unsigned owned file must request signing and then verification.'
$record = Invoke-LumeCodeSignFile $mockProduct $context
Check ($record.action -eq 'verified-existing' -and $global:LumeSigningFixtureSignCalls -eq 1) 'A valid timestamped signature from the exact same certificate must avoid another cloud signature.'
$global:LumeSigningFixtureFailure='verify'
Reject { Invoke-LumeCodeSignFile $mockProduct $context } 'An existing signature must not bypass an independent SignTool verification failure.'
$global:LumeSigningFixtureFailure='sign';$global:LumeSigningFixtureReady=$false
Reject { Invoke-LumeCodeSignFile $mockProduct $context } 'A SignTool signing failure must terminate packaging.'
Remove-Item -LiteralPath Function:\Get-AuthenticodeSignature

$packageSource = Get-Content -LiteralPath (Join-Path $RepositoryRoot 'native-rebuild\tests\scripts\package-original-pages.ps1') -Raw
Check ($packageSource.IndexOf('Get-LumeOwnedSigningFiles') -lt $packageSource.IndexOf('foreach ($name in $products)')) 'Owned files must be signed before staging.'
Check ($packageSource.IndexOf('Assert-LumeSignedFile -Path $built') -lt $packageSource.IndexOf('& $innounp -x')) 'Final installer signature verification must precede extraction and delivery evidence.'
Check ($packageSource.Contains("[Environment]::SetEnvironmentVariable('LUME_SIGNING_CONFIGURATION', `$previousSigningConfiguration, 'Process')")) 'The parent signing environment must be restored after the compiler returns.'

$powershellExe = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$wrapper = Join-Path $RepositoryRoot 'native-rebuild\tools\signing\Invoke-FileSigning.ps1'
$providerProbe = Join-Path $root 'isolated-certificate-provider.ps1'
$providerProbeSource = @'
param([string]$CommonPath,[string]$OutputPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. $CommonPath
# Confine module removal and the deliberately broken search path to this child.
# This changes no certificate, trust setting or private-key provider.
Remove-Module Microsoft.PowerShell.Security -ErrorAction SilentlyContinue
$env:PSModulePath=Join-Path $PSScriptRoot 'missing-module-search-path'
$oldFailure=''
try { Get-Item -LiteralPath Cert:\CurrentUser\My | Out-Null }
catch { $oldFailure=$_.FullyQualifiedErrorId }
Initialize-LumeSigningSecurityModule
Initialize-LumeSigningSecurityModule
$store=Get-Item -LiteralPath Cert:\CurrentUser\My -ErrorAction Stop
$drive=Get-PSDrive Cert
[ordered]@{oldFailure=$oldFailure;store=$store.Name;provider=$drive.Provider.Name;
    module=(Get-Module Microsoft.PowerShell.Security).Path;
    signatureCommand=(Get-Command Get-AuthenticodeSignature).ModuleName} |
    ConvertTo-Json | Set-Content -LiteralPath $OutputPath -Encoding utf8
'@
$providerProbeSource | Set-Content -LiteralPath $providerProbe -Encoding utf8
$providerProbeOutput=Join-Path $root 'isolated-certificate-provider.json'
& $powershellExe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $providerProbe `
    -CommonPath (Join-Path $RepositoryRoot 'native-rebuild\tools\signing\Signing-Common.ps1') `
    -OutputPath $providerProbeOutput *> (Join-Path $root 'isolated-certificate-provider.log')
Check ($LASTEXITCODE -eq 0) 'A clean Windows PowerShell callback must load its built-in certificate provider despite an inherited incompatible module path.'
$providerResult=Get-Content -LiteralPath $providerProbeOutput -Raw | ConvertFrom-Json
Check ($providerResult.oldFailure -like 'DriveNotFound,*') 'The isolated fixture must reproduce the original missing Cert drive before initialization.'
Check ($providerResult.store -eq 'My' -and $providerResult.provider -eq 'Certificate') 'The initialized child must read CurrentUser My without altering its certificates.'
Check ($providerResult.module -like '*WindowsPowerShell\v1.0\Modules\Microsoft.PowerShell.Security\*' -and
    $providerResult.signatureCommand -eq 'Microsoft.PowerShell.Security') 'The child must use Windows PowerShell security cmdlets rather than an inherited pwsh module.'
if ($SigningConfigurationPath) {
    # Optional read-only preflight against an activated real public certificate.
    # This path never invokes SignTool or exports/changes a key or certificate.
    $contextProbe=Join-Path $root 'real-context-readonly.ps1'
    @'
param([string]$CommonPath,[string]$ConfigurationPath,[string]$OutputPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. $CommonPath
$configuration=Get-Content -LiteralPath $ConfigurationPath -Raw | ConvertFrom-Json
$values=@(New-LumeCodeSigningContext -CertificateThumbprint $configuration.Thumbprint `
    -CertificateStoreLocation $configuration.CertificateStoreLocation -TimestampUrl $configuration.TimestampUrl `
    -SignToolPath $configuration.SignToolPath)
if ($values.Count -ne 1) { throw "Signing context emitted $($values.Count) pipeline objects instead of one." }
$context=$values[0]
[ordered]@{count=$values.Count;type=$context.GetType().FullName;
    subjectMatches=($context.Subject -ceq $configuration.Subject);
    thumbprintMatches=($context.Thumbprint -ceq $configuration.Thumbprint);
    toolExists=(Test-Path -LiteralPath $context.SignToolPath -PathType Leaf)} |
    ConvertTo-Json | Set-Content -LiteralPath $OutputPath -Encoding utf8
'@ | Set-Content -LiteralPath $contextProbe -Encoding utf8
    $contextProbeOutput=Join-Path $root 'real-context-readonly.json'
    & $powershellExe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $contextProbe `
        -CommonPath (Join-Path $RepositoryRoot 'native-rebuild\tools\signing\Signing-Common.ps1') `
        -ConfigurationPath $SigningConfigurationPath -OutputPath $contextProbeOutput *> (Join-Path $root 'real-context-readonly.log')
    Check ($LASTEXITCODE -eq 0) 'A real certificate context must pass in a clean strict Windows PowerShell callback.'
    $contextProbeResult=Get-Content -LiteralPath $contextProbeOutput -Raw | ConvertFrom-Json
    Check ($contextProbeResult.count -eq 1 -and $contextProbeResult.type -eq 'System.Management.Automation.PSCustomObject') 'The real context must emit exactly one structured object without an OID collection index.'
    Check ($contextProbeResult.subjectMatches -and $contextProbeResult.thumbprintMatches) 'The real context must preserve the configured certificate Subject and thumbprint under StrictMode.'
    Check $contextProbeResult.toolExists 'The real context must preserve the exact available Windows SDK signing tool.'
}
if ($SignedFilePath) {
    if (-not $SigningConfigurationPath) { throw 'SignedFilePath requires SigningConfigurationPath for the read-only callback preflight.' }
    $actualConfiguration=Get-Content -LiteralPath $SigningConfigurationPath -Raw | ConvertFrom-Json
    $actualSignedPath=(Resolve-Path -LiteralPath $SignedFilePath -ErrorAction Stop).Path
    $actualBeforeHash=(Get-FileHash -LiteralPath $actualSignedPath -Algorithm SHA256).Hash
    $readOnlyProxy=Join-Path $root 'verify-only-signtool.ps1'
    $verifyVerbLog=Join-Path $root 'read-only-sdk-verbs.txt'
    # This script can only forward `verify` to the real SDK executable. A
    # regression entering the signing branch is rejected before any key use.
    $proxySource=@'
if ($args.Count -eq 0 -or $args[0] -ne 'verify') { throw 'Read-only callback preflight forbids SignTool sign.' }
[IO.File]::AppendAllText('LOG_PLACEHOLDER', [string]$args[0] + "`r`n")
& 'TOOL_PLACEHOLDER' @args
$global:LASTEXITCODE=$LASTEXITCODE
'@
    $proxySource.Replace('LOG_PLACEHOLDER',$verifyVerbLog.Replace("'","''")).
        Replace('TOOL_PLACEHOLDER',([string]$actualConfiguration.SignToolPath).Replace("'","''")) |
        Set-Content -LiteralPath $readOnlyProxy -Encoding utf8
    $readOnlyEvidence=Join-Path $root 'read-only-callback-evidence'
    New-Item -ItemType Directory -Path $readOnlyEvidence | Out-Null
    $actualConfiguration.SignToolPath=$readOnlyProxy
    $actualConfiguration.EvidenceDirectory=$readOnlyEvidence
    $readOnlyConfiguration=Join-Path $root 'read-only-callback-configuration.json'
    $actualConfiguration | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $readOnlyConfiguration -Encoding utf8
    $callbackProbe=Join-Path $root 'read-only-full-callback.ps1'
    @'
param([string]$HelperPath,[string]$ConfigurationPath,[string]$SignedPath)
$ErrorActionPreference='Stop'
# Match the broken module lookup inherited by the real Inno callback. There
# are no inherited test functions in this new Windows PowerShell process.
Remove-Module Microsoft.PowerShell.Security -ErrorAction SilentlyContinue
$env:PSModulePath=Join-Path $PSScriptRoot 'missing-module-search-path'
$env:LUME_SIGNING_CONFIGURATION=$ConfigurationPath
& $HelperPath -Path $SignedPath
'@ | Set-Content -LiteralPath $callbackProbe -Encoding utf8
    & $powershellExe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $callbackProbe `
        -HelperPath $wrapper -ConfigurationPath $readOnlyConfiguration -SignedPath $actualSignedPath *> (Join-Path $root 'read-only-full-callback.log')
    Check ($LASTEXITCODE -eq 0) 'The complete real-certificate callback must succeed with a broken module search path.'
    $actualEvidenceFiles=@(Get-ChildItem -LiteralPath $readOnlyEvidence -Filter '*.json' -File)
    Check ($actualEvidenceFiles.Count -eq 1) 'The callback must write exactly one signature evidence JSON file.'
    $actualEvidence=Get-Content -LiteralPath $actualEvidenceFiles[0].FullName -Raw | ConvertFrom-Json
    Check ($actualEvidence.action -eq 'verified-existing' -and $actualEvidence.status -eq 'signed-and-verified') 'The complete callback must reuse and independently verify the actual existing signature.'
    Check ($actualEvidence.sha256 -ceq $actualBeforeHash -and
        (Get-FileHash -LiteralPath $actualSignedPath -Algorithm SHA256).Hash -ceq $actualBeforeHash) 'The read-only callback evidence must hash the original unchanged signed bytes.'
    Check ($actualEvidence.subject -ceq $actualConfiguration.Subject -and
        $actualEvidence.thumbprint -ceq $actualConfiguration.Thumbprint) 'The callback evidence must name the real configured signer.'
    $actualVerbs=@(Get-Content -LiteralPath $verifyVerbLog)
    Check ($actualVerbs.Count -eq 2 -and @($actualVerbs | Where-Object {$_ -cne 'verify'}).Count -eq 0) 'The callback must verify both its input and retained bytes without ever requesting signing.'
    $uninstallerFixtureDirectory=Join-Path $root 'uninstaller-lifecycle-fixture'
    New-Item -ItemType Directory -Path $uninstallerFixtureDirectory | Out-Null
    $uninstallerFixture=Join-Path $uninstallerFixtureDirectory 'uninst.e32.tmp'
    [IO.File]::Copy($actualSignedPath,$uninstallerFixture,$false)
    & $powershellExe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $callbackProbe `
        -HelperPath $wrapper -ConfigurationPath $readOnlyConfiguration -SignedPath $uninstallerFixture *> (Join-Path $root 'read-only-uninstaller-callback.log')
    Check ($LASTEXITCODE -eq 0) 'The callback must retain an already-signed uninst.e32.tmp without requesting another signature.'
    $uninstallerEvidence=@(Get-ChildItem -LiteralPath $readOnlyEvidence -Filter '*.json' -File | ForEach-Object {
        Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
    } | Where-Object {$_.path -ieq $uninstallerFixture})
    if ($uninstallerEvidence.Count -ne 1) { throw 'Expected exactly one uninstaller callback record.' }
    Remove-Item -LiteralPath $uninstallerFixture
    Check (-not (Test-Path -LiteralPath $uninstallerFixture)) 'The fixture must reproduce Inno removing its original signed temporary uninstaller.'
    $retainedContext=New-LumeCodeSigningContext -CertificateThumbprint $actualConfiguration.Thumbprint `
        -CertificateStoreLocation $actualConfiguration.CertificateStoreLocation -TimestampUrl $actualConfiguration.TimestampUrl `
        -SignToolPath $actualConfiguration.SignToolPath
    $retainedTemplate=Assert-LumeRetainedSigningRecord -Record $uninstallerEvidence[0] -Context $retainedContext -EvidenceDirectory $readOnlyEvidence
    Check ($retainedTemplate.sha256 -ceq $actualBeforeHash -and $retainedTemplate.status -eq 'signed-and-verified') 'The retained uninstaller bytes and signature must remain verifiable after the original temporary file is deleted.'
    $missingRecord=$uninstallerEvidence[0] | ConvertTo-Json -Depth 4 | ConvertFrom-Json
    $missingRecord.retainedPath=Join-Path $readOnlyEvidence 'missing-retained.exe'
    Reject { Assert-LumeRetainedSigningRecord $missingRecord $retainedContext $readOnlyEvidence } 'A missing retained uninstaller must block packaging.'
    $corruptRecord=$uninstallerEvidence[0] | ConvertTo-Json -Depth 4 | ConvertFrom-Json
    $corruptRecord.retainedPath=Join-Path $readOnlyEvidence 'corrupt-retained.exe'
    [IO.File]::WriteAllBytes($corruptRecord.retainedPath,[byte[]](0,1,2,3))
    Reject { Assert-LumeRetainedSigningRecord $corruptRecord $retainedContext $readOnlyEvidence } 'Corrupted retained uninstaller bytes must block packaging.'
    $actualVerbs=@(Get-Content -LiteralPath $verifyVerbLog)
    Check ($actualVerbs.Count -ge 5 -and @($actualVerbs | Where-Object {$_ -cne 'verify'}).Count -eq 0) 'Every real SDK call in the complete callback and uninstaller lifecycle preflight must be verification only.'
}
$previousConfiguration = [Environment]::GetEnvironmentVariable('LUME_SIGNING_CONFIGURATION', 'Process')
try {
    [Environment]::SetEnvironmentVariable('LUME_SIGNING_CONFIGURATION', $null, 'Process')
    $previousErrorPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $powershellExe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $wrapper -Path $mockProduct *> (Join-Path $root 'callback-missing-configuration.log')
        $callbackExitCode=$LASTEXITCODE
    } finally { $ErrorActionPreference=$previousErrorPreference }
    Check ($callbackExitCode -eq 1) 'A compiler callback without packaging configuration must fail in its own process.'
} finally { [Environment]::SetEnvironmentVariable('LUME_SIGNING_CONFIGURATION', $previousConfiguration, 'Process') }

# Compile only a minimal fixture with a fake callback. It never invokes
# SignTool and does not produce a distributable signed Lume installer.
$iscc = if ($env:LUME_ISCC) {$env:LUME_ISCC} else {Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'}
$innoRan = $false
if (Test-Path -LiteralPath $iscc -PathType Leaf) {
    $fixture = Join-Path $root 'callback $files with spaces'
    $fixtureOutput = Join-Path $fixture 'output'
    $fixtureUninstaller = Join-Path $fixture 'signed-uninstallers'
    New-Item -ItemType Directory -Path $fixture, $fixtureOutput, $fixtureUninstaller -Force | Out-Null
    $fakeCallback = Join-Path $fixture 'fake-signing-callback.ps1'
    @'
param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference='Stop'
if ($env:LUME_SIGNING_FIXTURE_FAIL -eq '1') { exit 9 }
if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { exit 8 }
[pscustomobject]@{path=$Path} | ConvertTo-Json -Compress | Add-Content -LiteralPath $env:LUME_SIGNING_FIXTURE_LOG -Encoding utf8
exit 0
'@ | Set-Content -LiteralPath $fakeCallback -Encoding utf8
    $fixtureIss = Join-Path $fixture 'fixture.iss'
    $fixtureText = @'
[Setup]
AppName=Lume Simulated Signing Fixture
AppVersion=1.0.9
DefaultDirName={tmp}\Lume Simulated Signing Fixture
PrivilegesRequired=lowest
OutputDir=OUTPUT_PLACEHOLDER
OutputBaseFilename=fixture
Compression=none
Uninstallable=yes
SignTool=LumeSign
SignedUninstaller=yes
SignedUninstallerDir=UNINSTALL_PLACEHOLDER
SignToolRetryCount=0
[Files]
Source: "PAYLOAD_PLACEHOLDER"; DestDir: "{app}"
'@
    $fixtureText.Replace('OUTPUT_PLACEHOLDER',$fixtureOutput).Replace('UNINSTALL_PLACEHOLDER',$fixtureUninstaller).
        Replace('PAYLOAD_PLACEHOLDER',$mockProduct) | Set-Content -LiteralPath $fixtureIss -Encoding utf8
    $callbackLog = Join-Path $root 'inno-callbacks.jsonl'
    $command = New-LumeInnoSigningCommand $powershellExe $fakeCallback
    function Invoke-InnoFixture([string]$Log) {
        $info=[Diagnostics.ProcessStartInfo]::new()
        $info.FileName=$iscc
        # These two arguments have no trailing backslashes. Escape embedded
        # command-template quotes for the native argv parser, without a shell.
        $info.Arguments='"' + ('/SLumeSign=' + $command).Replace('"','\"') + '" "' + $fixtureIss + '"'
        $info.UseShellExecute=$false;$info.CreateNoWindow=$true
        $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
        $process=[Diagnostics.Process]::Start($info)
        try {
            $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
            if (-not $process.WaitForExit(30000)) { $process.Kill();$process.WaitForExit();throw 'The minimal Inno fixture timed out.' }
            [IO.File]::WriteAllText($Log,$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
            return $process.ExitCode
        } finally { $process.Dispose() }
    }
    $previousFixtureLog=$env:LUME_SIGNING_FIXTURE_LOG;$previousFixtureFail=$env:LUME_SIGNING_FIXTURE_FAIL
    try {
        $env:LUME_SIGNING_FIXTURE_LOG=$callbackLog;$env:LUME_SIGNING_FIXTURE_FAIL='0'
        $uninstallerLog=Join-Path $root 'inno-uninstaller-fixture.log'
        $fixtureExitCode=Invoke-InnoFixture $uninstallerLog
        Check ($fixtureExitCode -ne 0 -and (Get-Content -LiteralPath $uninstallerLog -Raw).Contains('does not have a digital signature')) 'Inno must reject a callback returning zero without actually signing the uninstaller.'
        $callbackRecords = @(Get-Content -LiteralPath $callbackLog | ForEach-Object {$_ | ConvertFrom-Json})
        Check ($callbackRecords.Count -ge 1) 'The callback command must reach PowerShell with spaces and literal dollar signs intact.'
        Check (@($callbackRecords | Where-Object {$_.path.StartsWith($fixtureUninstaller,[StringComparison]::OrdinalIgnoreCase)}).Count -ge 1) 'Inno must present its uninstaller in the configured per-package directory.'
        # Disable uninstallation in a second fixture to independently reach the
        # Setup callback without creating or trusting any test certificate.
        $setupFixtureText=$fixtureText.Replace('Uninstallable=yes','Uninstallable=no').Replace('SignedUninstaller=yes','SignedUninstaller=no').
            Replace('OutputBaseFilename=fixture','OutputBaseFilename=fixture-setup').Replace('OUTPUT_PLACEHOLDER',$fixtureOutput).
            Replace('UNINSTALL_PLACEHOLDER',$fixtureUninstaller).Replace('PAYLOAD_PLACEHOLDER',$mockProduct)
        $setupFixtureText | Set-Content -LiteralPath $fixtureIss -Encoding utf8
        $setupLog=Join-Path $root 'inno-setup-fixture.log'
        $fixtureExitCode=Invoke-InnoFixture $setupLog
        Check ($fixtureExitCode -eq 0) 'The minimal Setup fixture must reach its callback and compile without requiring a real signing key.'
        $callbackRecords = @(Get-Content -LiteralPath $callbackLog | ForEach-Object {$_ | ConvertFrom-Json})
        Check (@($callbackRecords | Where-Object {$_.path.StartsWith($fixtureOutput,[StringComparison]::OrdinalIgnoreCase)}).Count -ge 1) 'The Setup callback must receive a real generated file inside the configured output directory.'
        Reject { Assert-LumeSignedFile (Join-Path $fixtureOutput 'fixture-setup.exe') $context } 'Independent final verification must reject an unsigned Setup even when its fake callback and compiler both returned zero.'
        $env:LUME_SIGNING_FIXTURE_FAIL='1'
        $failedCompilerExitCode=Invoke-InnoFixture (Join-Path $root 'inno-failure.log')
        Check ($failedCompilerExitCode -ne 0) 'A signing callback failure must cause Inno compilation to fail.'
        $innoRan=$true
    } finally { $env:LUME_SIGNING_FIXTURE_LOG=$previousFixtureLog;$env:LUME_SIGNING_FIXTURE_FAIL=$previousFixtureFail }
}
[ordered]@{checks=$checks;innoFixture=$innoRan;realSigning=$false;realCertificatesUsed=([bool]$SigningConfigurationPath)} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'summary.json') -Encoding utf8
Write-Output "PASS: $checks signing preflight, exact Publisher, ownership, failure and callback checks; Inno fixture=$innoRan; no real signing."
Write-Output "EVIDENCE=$root"
