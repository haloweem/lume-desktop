param(
    [ValidateSet('debug','release')][string]$Configuration = 'debug',
    [ValidateSet('red','green')][string]$Phase = 'green',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$sourceRoot = Join-Path $repositoryRoot 'native-rebuild'
$buildRoot = Join-Path $repositoryRoot "build-artifacts\native-rebuild\$Configuration"
$diagnosticsRoot = Join-Path $repositoryRoot 'build-artifacts\native-rebuild\diagnostics\organizer-material-feedback-fix'
$runRoot = Join-Path $diagnosticsRoot "$Configuration-$Phase-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
if (-not $SkipBuild) {
    Push-Location $sourceRoot
    try {
        $configureLog = Join-Path $runRoot 'configure.log'
        & cmake --preset "windows-$Configuration" *> $configureLog
        if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $configureLog; throw "Configure failed: $LASTEXITCODE" }
        $buildLog = Join-Path $runRoot 'session-build.log'
        & cmake --build --preset "windows-$Configuration" --target lume-organizer-session-test *> $buildLog
        if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $buildLog; throw "Session build failed: $LASTEXITCODE" }
    } finally { Pop-Location }
}
$compilerLine = Get-Content -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') |
    Where-Object { $_.StartsWith('CMAKE_CXX_COMPILER:FILEPATH=') } |
    Select-Object -First 1
if (-not $compilerLine) { throw 'No configured C++ compiler in CMake cache.' }
$compiler = $compilerLine.Substring('CMAKE_CXX_COMPILER:FILEPATH='.Length)
$missingRoot = Join-Path $runRoot 'missing'
$fixtureRoot = Join-Path $runRoot 'fixture'
New-Item -ItemType Directory -Path $missingRoot,$fixtureRoot -Force | Out-Null
$executable = Join-Path $buildRoot 'lume-organizer-session-test.exe'
Copy-Item -LiteralPath $executable -Destination $missingRoot
Copy-Item -LiteralPath $executable -Destination $fixtureRoot
$fixtureSource = Join-Path $sourceRoot 'tests\unit\organizer_material_fixture.cpp'
if ((Split-Path $compiler -Leaf) -match '^(cl|clang-cl)(\.exe)?$') {
    $compilerArguments = @(
        '/nologo','/std:c++17','/LD','/MT','/EHsc','/W4','/WX',
        '/DUNICODE','/D_UNICODE','/DWIN32_LEAN_AND_MEAN','/DNOMINMAX',
        "/I$sourceRoot", $fixtureSource, "/Fo$fixtureRoot\", "/Fe$fixtureRoot\LumeGlass.dll",
        '/link','user32.lib'
    )
} else {
    $compilerArguments = @(
        '-std=c++17','-shared','-static','-Wall','-Wextra','-Wpedantic','-Werror',
        '-DUNICODE','-D_UNICODE','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX',
        '-I', $sourceRoot, $fixtureSource, '-o', (Join-Path $fixtureRoot 'LumeGlass.dll'), '-luser32'
    )
    $compilerArguments += if ($Configuration -eq 'debug') { '-g' } else { '-O2' }
}
$compileLog = Join-Path $runRoot 'fixture-build.log'
& $compiler @compilerArguments *> $compileLog
if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $compileLog; throw "Fixture compile failed: $LASTEXITCODE" }
$results = @()
foreach ($case in @(
    @{ Directory = $missingRoot; Argument = '--material-unavailable'; Name = 'missing' },
    @{ Directory = $fixtureRoot; Argument = '--material-feedback'; Name = 'feedback' }
)) {
    $log = Join-Path $runRoot "$($case.Name).log"
    & (Join-Path $case.Directory 'lume-organizer-session-test.exe') $case.Argument *> $log
    $result = [pscustomobject]@{ Case = $case.Name; ExitCode = $LASTEXITCODE; Log = $log }
    $results += $result
    Get-Content -LiteralPath $log
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'results.json') -Encoding utf8
$results | Format-Table -AutoSize
if ($results | Where-Object { $_.ExitCode -ne 0 }) { exit 1 }
