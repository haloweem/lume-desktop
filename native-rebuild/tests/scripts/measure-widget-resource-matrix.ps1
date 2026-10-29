param(
    [string]$BuildDirectory=(Join-Path $PSScriptRoot '../../../build-artifacts/native-rebuild/release'),
    [string]$OutputDirectory=(Join-Path $PSScriptRoot ('../../../build-artifacts/native-rebuild/verification/widget-resource-matrix-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [ValidateRange(2,100)][int]$Rounds=6,
    [ValidateRange(4000,30000)][int]$DwellMilliseconds=8000,
    [ValidateRange(250,5000)][int]$SampleMilliseconds=1000,
    [ValidateRange(4,600)][int]$CooldownSeconds=30,
    [ValidateSet('weather-only','almanac-only','performance-only','organizer-only','widgets-combined')]
    [string[]]$Modes=@('weather-only','almanac-only','performance-only','organizer-only','widgets-combined')
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$BuildDirectory=(Resolve-Path -LiteralPath $BuildDirectory).Path
if(Test-Path -LiteralPath $OutputDirectory){throw 'Every widget matrix needs a fresh output directory.'}
$probe=Join-Path $BuildDirectory 'lume-widget-resource-probe.exe'
$material=Join-Path $BuildDirectory 'LumeGlass.dll'
if(-not(Test-Path -LiteralPath $probe)-or -not(Test-Path -LiteralPath $material)) {
    throw 'Build lume-widget-resource-probe and lume-material-dll before running the matrix.'
}
$null=New-Item -ItemType Directory -Path $OutputDirectory
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$runtime=Join-Path $OutputDirectory 'runtime'
$null=New-Item -ItemType Directory -Path $runtime
# All five modes use the same binaries, even if a later build updates release/.
Copy-Item -LiteralPath $probe -Destination $runtime
Get-ChildItem -LiteralPath $BuildDirectory -Filter '*.dll' -File|Copy-Item -Destination $runtime
$iconHost=Join-Path $BuildDirectory 'LumeIconHost.exe'
if(Test-Path -LiteralPath $iconHost){Copy-Item -LiteralPath $iconHost -Destination $runtime}
$hashes=@(Get-ChildItem -LiteralPath $runtime -File|Sort-Object Name|ForEach-Object {
    [pscustomobject]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$hashes|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputDirectory 'runtime-hashes.json') -Encoding utf8
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $OutputDirectory 'measure-widget-resource-matrix.executed.ps1')
$modes=@($Modes|ForEach-Object {$_.ToLowerInvariant()}|Select-Object -Unique)
if(-not $modes.Count){throw 'The matrix requires at least one requested mode.'}
$results=[Collections.Generic.List[object]]::new()
$started=[DateTime]::UtcNow
foreach($mode in $modes) {
    $modeDirectory=Join-Path $OutputDirectory $mode
    $failure=$null
    try {
        & (Join-Path $PSScriptRoot 'measure-clock-resources.ps1') -BuildDirectory $runtime `
            -OutputDirectory $modeDirectory -Mode $mode -Rounds $Rounds `
            -DwellMilliseconds $DwellMilliseconds -SampleMilliseconds $SampleMilliseconds `
            -CooldownSeconds $CooldownSeconds -CollectGpu
    } catch {$failure=$_.Exception.Message}
    $summaryPath=Join-Path $modeDirectory 'summary.json'
    $summary=if(Test-Path -LiteralPath $summaryPath){Get-Content -LiteralPath $summaryPath -Raw|ConvertFrom-Json}else{$null}
    $results.Add([pscustomobject]@{mode=$mode;passed=$null -eq $failure;failure=$failure
        outputDirectory=$modeDirectory;summary=$summary})
    # Persist each completed mode so an interruption does not discard earlier results.
    [ordered]@{
        startedUtc=$started.ToString('o');updatedUtc=[DateTime]::UtcNow.ToString('o')
        rounds=$Rounds;dwellMilliseconds=$DwellMilliseconds;sampleMilliseconds=$SampleMilliseconds
        cooldownSeconds=$CooldownSeconds;expectedModes=$modes;completedModes=$results.Count
        runtimeHashes=$hashes;results=@($results)
        complete=$results.Count -eq $modes.Count
        passed=$results.Count -eq $modes.Count -and @($results|Where-Object {-not $_.passed}).Count -eq 0
    }|ConvertTo-Json -Depth 16|Set-Content -LiteralPath (Join-Path $OutputDirectory 'matrix-summary.json') -Encoding utf8
}
$results|Select-Object mode,passed,failure|Format-Table -AutoSize
Write-Output "Widget matrix artifacts: $OutputDirectory"
if(@($results|Where-Object {-not $_.passed}).Count){throw 'Widget resource matrix has failed modes; inspect matrix-summary.json.'}
