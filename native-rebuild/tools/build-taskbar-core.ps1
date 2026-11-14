param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$VcpkgRoot,
    [Parameter(Mandatory = $true)][string]$VcpkgInstalledRoot,
    [Parameter(Mandatory = $true)][string]$VCTargetsPath,
    [Parameter(Mandatory = $true)][string]$MSBuildExtensionsPath,
    [Parameter(Mandatory = $true)][string]$SdkReferenceDirectoryRoot,
    [string]$MSBuildPath = 'C:\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
    [ValidateSet('x64', 'arm64', 'all')][string]$Architecture = 'all',
    [string]$StageRoot,
    [string]$LogDirectory
)
$ErrorActionPreference = 'Stop'

function BuildPath([string]$Value) {
    ((Resolve-Path -LiteralPath $Value).Path -replace '\\', '/').TrimEnd('/') + '/'
}
$source = BuildPath $SourceRoot
$vcpkg = BuildPath $VcpkgRoot
$installed = BuildPath $VcpkgInstalledRoot
$vcTargets = BuildPath $VCTargetsPath
$extensions = BuildPath $MSBuildExtensionsPath
$sdkReferences = BuildPath $SdkReferenceDirectoryRoot
if (-not $LogDirectory) { $LogDirectory = Join-Path $SourceRoot 'lume-build-logs' }
New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
$projectionDirectory = Join-Path $LogDirectory 'system-projection'
$cppWinRT = Join-Path $SourceRoot 'packages\Microsoft.Windows.CppWinRT.2.0.250303.1\bin\cppwinrt.exe'
$platformMetadata = Join-Path $SourceRoot 'packages\Microsoft.Windows.SDK.CPP.10.0.26100.7463\c\UnionMetadata\10.0.26100.0\Windows.winmd'
& $cppWinRT -in $platformMetadata -out $projectionDirectory
if ($LASTEXITCODE -ne 0) { throw 'System C++/WinRT metadata projection failed.' }
if (-not (Test-Path -LiteralPath $MSBuildPath)) { throw "Missing MSBuild: $MSBuildPath" }
if ((Get-Content -LiteralPath (Join-Path $SourceRoot 'Common\constants.hpp') -Raw) -notmatch 'TAP_API_VERSION = 5') {
    throw 'Apply the complete Lume diagnostics API 5 patch before building.'
}
if (-not (Test-Path -LiteralPath (Join-Path $SourceRoot 'Common\lumestatus.hpp'))) { throw 'Missing Lume status protocol source.' }
$targets = if ($Architecture -eq 'all') { @('x64', 'arm64') } else { @($Architecture) }
foreach ($arch in $targets) {
    $platform = if ($arch -eq 'arm64') { 'ARM64' } else { 'x64' }
    foreach ($triplet in @("$arch-windows", "$arch-windows-static")) {
        if (-not (Test-Path -LiteralPath (Join-Path $VcpkgInstalledRoot $triplet))) { throw "Missing dependency triplet: $triplet" }
    }
    $arguments = @(
        (Join-Path $SourceRoot 'TranslucentTB\TranslucentTB.vcxproj'), '/m', '/t:Build',
        '/p:Configuration=Release', "/p:Platform=$platform", '/p:VisualStudioVersion=17.0',
        '/p:PlatformToolset=v143', '/p:LumeTaskbarCore=true', '/p:BuildType=Release',
        '/p:SpectreMitigation=false', "/p:GeneratedFilesDir=$platform/Release/Generated Files/",
        "/p:VCTargetsPath=$vcTargets\\", "/p:MSBuildExtensionsPath=$extensions", "/p:MSBuildExtensionsPath32=$extensions",
        "/p:SDKReferenceDirectoryRoot=$sdkReferences", "/p:ForceImportBeforeCppProps=${vcpkg}scripts/buildsystems/msbuild/vcpkg.props",
        "/p:ForceImportAfterCppTargets=${vcpkg}scripts/buildsystems/msbuild/vcpkg.targets",
        "/p:VcpkgInstalledDir=$installed", '/p:VcpkgManifestInstall=false', '/v:minimal'
        '/p:VcpkgEnableManifest=false', "/p:LumeSystemProjectionDir=$projectionDirectory"
    )
    if ($LogDirectory) {
        New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
        & $MSBuildPath @arguments *> (Join-Path $LogDirectory "core-$arch-build.log")
    } else { & $MSBuildPath @arguments }
    if ($LASTEXITCODE -ne 0) { throw "Taskbar core $arch build failed with exit code $LASTEXITCODE" }
    $core = Join-Path $SourceRoot "TranslucentTB\$platform\Release\TranslucentTB.exe"
    if ([Diagnostics.FileVersionInfo]::GetVersionInfo($core).ProductName -ne 'Lume Taskbar Core') { throw "Incorrect product branding in $core" }
    if ($StageRoot) {
        $destination = Join-Path $StageRoot $arch
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        $outputs = @(
            @{ From="TranslucentTB\$platform\Release\TranslucentTB.exe"; To='LumeTaskbarCore.exe' },
            @{ From="ExplorerHooks\$platform\Release\ExplorerHooks.dll"; To='ExplorerHooks.dll' },
            @{ From="ExplorerTAP\$platform\Release\ExplorerTAP.dll"; To='ExplorerTAP.dll' },
            @{ From="ProgramLog\$platform\Release\ProgramLog.dll"; To='ProgramLog.dll' },
            @{ From="Xaml\$platform\Release\Xaml\Xaml.dll"; To='Xaml.dll' }
        )
        foreach ($output in $outputs) {
            Copy-Item -LiteralPath (Join-Path $SourceRoot $output.From) -Destination (Join-Path $destination $output.To) -Force
        }
    }
    Write-Output "LUME_TASKBAR_CORE_BUILD_OK $arch"
}
