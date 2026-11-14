param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Console]::InputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Text.UTF8Encoding]::new($false)
try {
    . (Join-Path $PSScriptRoot 'Signing-Common.ps1')
    Initialize-LumeSigningPowerShellModules
    # Inno inherits this public configuration from the packaging process. It
    # contains only selectors, URLs and tool paths, never a key or password.
    $configurationPath = [Environment]::GetEnvironmentVariable('LUME_SIGNING_CONFIGURATION', 'Process')
    if ([string]::IsNullOrWhiteSpace($configurationPath)) { throw 'LUME_SIGNING_CONFIGURATION was not provided by packaging.' }
    $configurationFile = Get-Item -LiteralPath $configurationPath -ErrorAction Stop
    if ($configurationFile.Length -gt 16384) { throw 'The signing configuration is too large.' }
    $configuration = Get-Content -LiteralPath $configurationFile.FullName -Raw | ConvertFrom-Json
    $context = New-LumeCodeSigningContext -CertificateThumbprint $configuration.Thumbprint `
        -CertificateStoreLocation $configuration.CertificateStoreLocation -TimestampUrl $configuration.TimestampUrl `
        -SignToolPath $configuration.SignToolPath
    if ($context.Subject -cne $configuration.Subject) { throw 'The selected certificate Subject changed after packaging preflight.' }
    $record = Invoke-LumeCodeSignFile -Path $Path -Context $context
    $evidenceDirectory = (Resolve-Path -LiteralPath $configuration.EvidenceDirectory -ErrorAction Stop).Path
    # Inno deletes its signed uninst.e32.tmp after embedding it. Preserve and
    # verify the actual bytes before returning, independently of Inno's cache.
    $evidenceId = [guid]::NewGuid().ToString('N')
    $retainedPath = Join-Path $evidenceDirectory ($evidenceId + '.exe')
    [IO.File]::Copy($record.path, $retainedPath, $false)
    $retainedRecord = Assert-LumeSignedFile -Path $retainedPath -Context $context
    if ($retainedRecord.sha256 -cne $record.sha256) { throw 'The signed compiler input changed while retaining its evidence bytes.' }
    Add-Member -InputObject $record -NotePropertyName retainedPath -NotePropertyValue $retainedPath
    Add-Member -InputObject $record -NotePropertyName retainedSha256 -NotePropertyValue $retainedRecord.sha256
    $evidencePath = Join-Path $evidenceDirectory ($evidenceId + '.json')
    $record | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $evidencePath -Encoding utf8
    $record | ConvertTo-Json -Compress | Write-Output
    exit 0
} catch {
    [Console]::Error.WriteLine('Lume signing failed: ' + $_.Exception.Message)
    exit 1
}
