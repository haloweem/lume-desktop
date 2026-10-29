param([switch]$OnlyPathGuard)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$build = Join-Path $repo 'build-artifacts\native-rebuild\release'
$helper = Join-Path $build 'LumeSetupShutdown.exe'
$fixture = Join-Path $build 'lume-installer-shutdown-test.exe'
$output = Join-Path $repo ('build-artifacts\installer-shutdown-inno\' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
$owned = [Collections.Generic.List[Diagnostics.Process]]::new()
$records = [Collections.Generic.List[object]]::new()

function Start-Fixture([string]$Executable, [string]$Mode, [string]$Marker) {
    $process = Start-Process -FilePath $Executable -ArgumentList @('--fixture', $Mode, "`"$Marker`"") -WindowStyle Hidden -PassThru
    $null = $process.Handle
    $owned.Add($process)
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (-not (Test-Path -LiteralPath $Marker) -and [DateTime]::UtcNow -lt $deadline -and -not $process.HasExited) {
        Start-Sleep -Milliseconds 20
    }
    if (-not (Test-Path -LiteralPath $Marker)) { throw 'Isolated process did not become ready' }
    return $process
}

try {
    $outside = Join-Path $output 'outside'
    $redirected = Join-Path $output 'redirected'
    New-Item -ItemType Directory -Path $outside, $redirected | Out-Null
    $outsidePlayer = Join-Path $outside 'mpv.exe'
    Copy-Item -LiteralPath $fixture -Destination $outsidePlayer
    $player = Start-Fixture $outsidePlayer 'stubborn' (Join-Path $outside 'ready.txt')
    $null = New-Item -ItemType Junction -Path (Join-Path $redirected 'wallpaper-runtime') -Target $outside
    $probe = Start-Process -FilePath $helper -ArgumentList @('--directory', "`"$redirected\.`"") -WindowStyle Hidden -PassThru
    $null = $probe.Handle
    $owned.Add($probe)
    if (-not $probe.WaitForExit(15000)) { throw 'Directory boundary probe timed out' }
    $probe.Refresh()
    $records.Add([ordered]@{ scenario='directory-junction'; exitCode=$probe.ExitCode; outsideAlive=(-not $player.HasExited) })
    if ($probe.ExitCode -ne 2 -or $player.HasExited) { throw 'Directory junction must not extend shutdown to an external player' }
    if (-not $OnlyPathGuard) {
        $iscc = if ($env:LUME_ISCC) { $env:LUME_ISCC } else { Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe' }
        $replacement = Join-Path $repo 'native-rebuild\tests\fixtures\installer_replacement.txt'
        $shutdownCode = Join-Path $repo 'native-rebuild\packaging\installer_shutdown.iss'
        & $iscc "/DOutputDir=$output" "/DShutdownHelperFile=$helper" "/DReplacementFile=$replacement" "/DShutdownCode=$shutdownCode" `
            (Join-Path $repo 'native-rebuild\tests\fixtures\installer_shutdown.iss') *> (Join-Path $output 'compile.log')
        if ($LASTEXITCODE -ne 0) { throw 'Isolated Inno installer compilation failed' }
        $installer = Join-Path $output 'installer-shutdown-verification.exe'
        foreach ($mode in @('fresh', 'fresh-nested', 'existing-empty')) {
            $target = if ($mode -eq 'fresh-nested') { Join-Path $output 'new parent\Lume test' } else { Join-Path $output $mode }
            if ($mode -eq 'existing-empty') { New-Item -ItemType Directory -Path $target | Out-Null }
            elseif (Test-Path -LiteralPath $target) { throw "Fresh-install target already exists: $target" }
            $setup = Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                "/DIR=`"$target`"", "/LOG=`"$(Join-Path $output ($mode + '-install.log'))`"") -WindowStyle Hidden -PassThru
            $null = $setup.Handle
            $owned.Add($setup)
            if (-not $setup.WaitForExit(25000)) { throw "First installation timed out: $mode" }
            $setup.Refresh()
            $installed = Join-Path $target 'LumeShell.exe'
            $matched = (Test-Path -LiteralPath $installed) -and
                ((Get-FileHash -LiteralPath $installed).Hash -eq (Get-FileHash -LiteralPath $replacement).Hash)
            $records.Add([ordered]@{ scenario=$mode; exitCode=$setup.ExitCode; installed=$matched })
            if ($setup.ExitCode -ne 0 -or -not $matched) { throw "First installation failed: $mode" }
        }
        foreach ($mode in @('normal', 'legacy', 'stubborn')) {
            $target = Join-Path $output $mode
            New-Item -ItemType Directory -Path $target | Out-Null
            $shell = Join-Path $target 'LumeShell.exe'
            Copy-Item -LiteralPath $fixture -Destination $shell
            $marker = Join-Path $target 'ready.txt'
            $process = Start-Fixture $shell $mode $marker
            $watch = [Diagnostics.Stopwatch]::StartNew()
            $setup = Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                "/DIR=`"$target`"", "/LOG=`"$(Join-Path $target 'install.log')`"") -WindowStyle Hidden -PassThru
            $null = $setup.Handle
            $owned.Add($setup)
            if (-not $setup.WaitForExit(25000)) { throw "Isolated installation timed out: $mode" }
            $setup.Refresh()
            $watch.Stop()
            $replaced = (Get-FileHash -LiteralPath $shell).Hash -eq (Get-FileHash -LiteralPath $replacement).Hash
            $records.Add([ordered]@{ scenario=$mode; exitCode=$setup.ExitCode; milliseconds=$watch.ElapsedMilliseconds;
                oldExited=$process.HasExited; replaced=$replaced; cleaned=(Test-Path -LiteralPath ($marker + '.clean')) })
            if ($setup.ExitCode -ne 0 -or -not $process.HasExited -or -not $replaced) { throw "Isolated installation failed: $mode" }
            if ($mode -ne 'stubborn' -and -not (Test-Path -LiteralPath ($marker + '.clean'))) { throw 'Responsive process skipped graceful cleanup' }
        }
        $blockedShell = Join-Path $redirected 'LumeShell.exe'
        Copy-Item -LiteralPath $fixture -Destination $blockedShell
        $blocked = Start-Fixture $blockedShell 'normal' (Join-Path $redirected 'ready.txt')
        $before = (Get-FileHash -LiteralPath $blockedShell).Hash
        $setup = Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
            "/DIR=`"$redirected`"", "/LOG=`"$(Join-Path $redirected 'install.log')`"") -WindowStyle Hidden -PassThru
        $null = $setup.Handle
        $owned.Add($setup)
        if (-not $setup.WaitForExit(15000)) { throw 'Rejected installation did not return promptly' }
        $setup.Refresh()
        $unchanged = (Get-FileHash -LiteralPath $blockedShell).Hash -eq $before
        $records.Add([ordered]@{ scenario='refused-before-copy'; exitCode=$setup.ExitCode;
            oldAlive=(-not $blocked.HasExited); unchanged=$unchanged; outsideAlive=(-not $player.HasExited) })
        if ($setup.ExitCode -eq 0 -or -not $unchanged -or $blocked.HasExited -or $player.HasExited) {
            throw 'Failed pre-install shutdown must preserve the old files and unrelated processes'
        }
    }
    Write-Output 'PASS: isolated path boundary and installer shutdown verification'
} finally {
    foreach ($process in $owned) {
        if (-not $process.HasExited) {
            $process.Kill()
            $null = $process.WaitForExit(3000)
        }
        $process.Dispose()
    }
    $records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'results.json') -Encoding utf8
    Write-Output "EVIDENCE=$output"
}
