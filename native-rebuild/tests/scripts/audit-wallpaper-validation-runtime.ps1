$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$sourceRoot = 'H:\refactor\history\lume1.0.1\src\vscode-windhawk\wallpaper-helper\mpv'
$runtimeRoot = Join-Path $repoRoot 'build-artifacts\native-rebuild\wallpaper-runtime'
$expectedHashes = [ordered]@{
    'mpv.exe' = '3BA74E88277E76E830967BEA421E63492A43603F6950858B1217CC57C2D1A4E5'
    'd3dcompiler_43.dll' = '4B074A3976399DC735484F5D43D04B519B7BDEE8AC719D9AB8ED6BD4E6BE0345'
}

function Get-BinaryEvidence([string]$Path) {
    $file = Get-Item -LiteralPath $Path
    $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($file.FullName)
    [ordered]@{
        path = $file.FullName
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        versionInfo = [ordered]@{
            fileVersion = $version.FileVersion
            productVersion = $version.ProductVersion
            productName = $version.ProductName
            companyName = $version.CompanyName
            originalFilename = $version.OriginalFilename
        }
    }
}

$files = @()
$warnings = @()
foreach ($name in $expectedHashes.Keys) {
    $source = Get-BinaryEvidence (Join-Path $sourceRoot $name)
    $runtime = Get-BinaryEvidence (Join-Path $runtimeRoot $name)
    if ($source.sha256 -ne $expectedHashes[$name] -or $runtime.sha256 -ne $expectedHashes[$name]) {
        throw "Pinned source/runtime SHA256 mismatch: $name"
    }
    $files += [ordered]@{ name = $name; expectedSha256 = $expectedHashes[$name]; source = $source; runtime = $runtime }
    if ($runtime.versionInfo.originalFilename -and $runtime.versionInfo.originalFilename -ine $name) {
        $warnings += "$name reports OriginalFilename=$($runtime.versionInfo.originalFilename); naming/origin and redistribution rights remain unverified."
    }
}

$info = [System.Diagnostics.ProcessStartInfo]::new()
$info.FileName = Join-Path $runtimeRoot 'mpv.exe'
$info.Arguments = '--no-config --version'
$info.WorkingDirectory = $runtimeRoot
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
$process = [System.Diagnostics.Process]::Start($info)
try {
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(20000)) { throw 'mpv version probe timed out after 20 seconds' }
    $probe = [ordered]@{
        executable = $info.FileName
        arguments = $info.Arguments
        workingDirectory = $info.WorkingDirectory
        environmentPath = $info.EnvironmentVariables['PATH']
        createNoWindow = $info.CreateNoWindow
        windowStyle = $info.WindowStyle.ToString()
        exitCode = $process.ExitCode
        stdout = $stdoutTask.GetAwaiter().GetResult()
        stderr = $stderrTask.GetAwaiter().GetResult()
    }
    if ($probe.exitCode -ne 0 -or [string]::IsNullOrWhiteSpace($probe.stdout + $probe.stderr)) {
        throw "mpv version probe failed: exit=$($probe.exitCode); stderr=$($probe.stderr)"
    }
} finally {
    if (-not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
    $process.Dispose()
}

$now = [DateTimeOffset]::Now
$auditedAt = $now.ToString('o')
$auditDate = $now.ToString('yyyy-MM-dd')
$unverified = @(
    'Exact upstream mpv/FFmpeg/libplacebo source revisions, complete source trees, build recipes and binary distribution chain have not been independently verified.',
    'Complete dependency licenses and redistribution rights, including those for the d3dcompiler DLL, have not been verified or supplied by this audit.',
    'Playback, Windows/DPI/GPU compatibility, signing, installation and commercial release qualification are not established by this version-only audit.'
)
$provenance = [ordered]@{
    schemaVersion = 1
    recordKind = 'new-local-validation-observations'
    auditDate = $auditDate
    auditedAt = $auditedAt
    computerName = $env:COMPUTERNAME
    validationOnly = $true
    historicalAuditRecovered = $false
    sourceRoot = $sourceRoot
    sourceReadOnly = $true
    runtimeRoot = $runtimeRoot
    files = $files
    mpvVersionProbe = $probe
    upstreamSourceStatus = 'unverified'
    dependencyLicenseStatus = 'unverified'
    commercialReleaseEligibility = 'unverified'
    commercialReleaseAllowed = $false
    unverifiedItems = $unverified
    warnings = $warnings
}
$provenancePath = Join-Path $runtimeRoot 'player-provenance.json'
$provenance | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $provenancePath -Encoding utf8
$audit = [ordered]@{
    schemaVersion = 1
    recordKind = 'new-local-validation-audit'
    auditDate = $auditDate
    auditedAt = $auditedAt
    validationOnly = $true
    historicalAuditRecovered = $false
    auditStatus = 'unverified'
    observedPinnedHashesMatch = $true
    observedVersionProbeExitCode = $probe.exitCode
    playerProvenanceFile = 'player-provenance.json'
    playerProvenanceSha256 = (Get-FileHash -LiteralPath $provenancePath -Algorithm SHA256).Hash
    upstreamSourceStatus = 'unverified'
    dependencyLicenseStatus = 'unverified'
    commercialReleaseEligibility = 'unverified'
    commercialReleaseAllowed = $false
    unverifiedItems = $unverified
    warnings = $warnings
}
$audit | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runtimeRoot 'provenance-audit.json') -Encoding utf8
$hashSummary = ($files | ForEach-Object { "$($_.name) SHA256=$($_.runtime.sha256) (historical source and restored copy match the pinned hash)" }) -join [Environment]::NewLine
@"
New local wallpaper validation audit, not a recovered historical audit.
Audit date: $auditDate
Audited at: $auditedAt
validationOnly=true
auditStatus=unverified
commercialReleaseAllowed=false
Source (read-only): $sourceRoot
Restored runtime: $runtimeRoot

$hashSummary
mpv --no-config --version exit code: $($probe.exitCode); no window; clean PATH.
Exact FileVersionInfo, stdout and stderr are recorded in player-provenance.json.

UNVERIFIED / COMMERCIAL RELEASE PROHIBITED:
$($unverified -join [Environment]::NewLine)
$($warnings -join [Environment]::NewLine)

The packaging entry only checks that these three audit files exist and copies them.
Their presence and the observed hashes/version do not constitute provenance,
license review, commercial release approval or complete installer acceptance.
"@ | Set-Content -LiteralPath (Join-Path $runtimeRoot 'REVIEW-HANDOFF.txt') -Encoding utf8
Write-Output "AUDIT_RECORDED date=$auditDate validationOnly=true auditStatus=unverified commercialReleaseAllowed=false"
Write-Output "RUNTIME=$runtimeRoot"
