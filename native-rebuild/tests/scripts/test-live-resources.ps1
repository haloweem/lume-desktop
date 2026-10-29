$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$samplerPath = Join-Path $PSScriptRoot 'measure-live-resources.ps1'
$parseErrors = $null
$tokens = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($samplerPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw ($parseErrors | Out-String) }

foreach ($functionName in @(
        'Get-CpuIntervalPercent', 'Get-IdentityKey', 'Get-ProcessTreeRows', 'Get-ProcessGroup',
        'Get-NullableSum', 'Get-GroupedResourceAggregates', 'Get-GpuMeasurement',
        'Get-SummaryStats')) {
    $definition = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName
    }, $true)
    if ($null -eq $definition) { throw "Sampler function missing: $functionName" }
    . ([scriptblock]::Create($definition.Extent.Text))
}

$failures = [Collections.Generic.List[string]]::new()
function Assert-Equal([string]$Name, $Actual, $Expected) {
    $same = if ($null -eq $Expected) { $null -eq $Actual } else { $null -ne $Actual -and $Actual -eq $Expected }
    if (-not $same) { $failures.Add("${Name}: expected <$Expected>, got <$Actual>") }
    else { Write-Output "PASS $Name" }
}
function Assert-True([string]$Name, [bool]$Value) {
    if (-not $Value) { $failures.Add("${Name}: expected true") }
    else { Write-Output "PASS $Name" }
}

function New-ProcessRow([int]$ProcessId, [int]$ParentPid, [datetime]$Created, [string]$Name, [string]$Key = $null) {
    [pscustomobject]@{
        ProcessId = $ProcessId; ParentProcessId = $ParentPid; ParentIdentityKey = $null; CreationTimeUtc = $Created.ToUniversalTime()
        IdentityKey = if ($Key) { $Key } else { "$ProcessId|$($Created.ToUniversalTime().Ticks)" }
        Name = $Name
    }
}
$rootCreated = [datetime]'2026-01-01T00:00:00Z'
$childCreated = $rootCreated.AddSeconds(1)
$grandchildCreated = $rootCreated.AddSeconds(2)
$root = New-ProcessRow 100 1 $rootCreated 'Lume.exe'
$child = New-ProcessRow 200 100 $childCreated 'worker.exe'
$grandchild = New-ProcessRow 300 200 $grandchildCreated 'worker.exe'
$unrelated = New-ProcessRow 400 1 $childCreated 'unrelated.exe'
$tree = Get-ProcessTreeRows $root.IdentityKey @($root, $child, $grandchild, $unrelated) @($root)
Assert-True 'recursive tree includes root and descendants only' ($tree.rootPresent -and $tree.rows.Count -eq 3 -and @($tree.rows | Where-Object ProcessId -eq 400).Count -eq 0)

$reusedRoot = New-ProcessRow 100 1 $rootCreated.AddMinutes(1) 'reused.exe'
$reuseResult = Get-ProcessTreeRows $root.IdentityKey @($reusedRoot, $child) @($root)
Assert-True 'reused root pid is rejected' (-not $reuseResult.rootPresent -and $reuseResult.status -eq 'root_missing')
$oldParent = New-ProcessRow 500 100 $rootCreated.AddSeconds(1) 'old-parent.exe'
$oldChild = New-ProcessRow 600 500 $rootCreated.AddSeconds(2) 'child.exe'
$oldChild.ParentIdentityKey = $oldParent.IdentityKey
$retained = Get-ProcessTreeRows $root.IdentityKey @($root, $oldChild) @($root, $oldParent)
Assert-Equal 'retained unrelated lineage is excluded' @($retained.rows | ForEach-Object ProcessId).Count 1
$rootGone = Get-ProcessTreeRows $root.IdentityKey @($child, $grandchild) @($root)
Assert-True 'root disappearance is explicit' (-not $rootGone.rootPresent -and $rootGone.status -eq 'root_missing')

$duplicateIdentity = Get-ProcessTreeRows $root.IdentityKey @($root, $root, $child) @($root)
Assert-True 'duplicate identity is rejected from tree' ($duplicateIdentity.status -eq 'identity_ambiguous')

$validPrevious = [pscustomobject]@{ identityKey = '1|a'; sampleTimeMs = 1000; cpuSeconds = 2.0 }
$validCurrent = [pscustomobject]@{ identityKey = '1|a'; sampleTimeMs = 3000; cpuSeconds = 2.2 }
Assert-Equal 'cpu uses actual elapsed time' (Get-CpuIntervalPercent $validPrevious $validCurrent) 10
Assert-Equal 'first cpu observation is null' (Get-CpuIntervalPercent $null $validCurrent) $null
Assert-Equal 'cpu pid reuse is null' (Get-CpuIntervalPercent $validPrevious ([pscustomobject]@{ identityKey = '1|b'; sampleTimeMs = 3000; cpuSeconds = 2.2 })) $null
Assert-Equal 'cpu negative delta is null' (Get-CpuIntervalPercent $validPrevious ([pscustomobject]@{ identityKey = '1|a'; sampleTimeMs = 3000; cpuSeconds = 1.9 })) $null
Assert-Equal 'cpu nonpositive elapsed is null' (Get-CpuIntervalPercent $validPrevious ([pscustomobject]@{ identityKey = '1|a'; sampleTimeMs = 1000; cpuSeconds = 2.2 })) $null

Assert-Equal 'root is shell group' (Get-ProcessGroup $root $root.IdentityKey) 'shell'
Assert-Equal 'webview group is exact process family' (Get-ProcessGroup (New-ProcessRow 2 100 $childCreated 'msedgewebview2.exe') $root.IdentityKey) 'webview'
Assert-Equal 'wallpaper group is exact process family' (Get-ProcessGroup (New-ProcessRow 3 100 $childCreated 'mpv.exe') $root.IdentityKey) 'wallpaper'
Assert-Equal 'other group is fallback' (Get-ProcessGroup (New-ProcessRow 4 100 $childCreated 'chrome.exe') $root.IdentityKey) 'other'

Assert-Equal 'nullable sum propagates missing values' (Get-NullableSum @([pscustomobject]@{ value = 2 }, [pscustomobject]@{ value = $null }) 'value') $null
Assert-Equal 'nullable sum keeps complete values' (Get-NullableSum @([pscustomobject]@{ value = 2 }, [pscustomobject]@{ value = 3 }) 'value') 5
Assert-Equal 'empty nullable sum is null' (Get-NullableSum @() 'value') $null

function New-ResourceRow([string]$Group, $Ws, $PrivateWs, $Commit, $GpuDedicated, $GpuShared, $Cpu) {
    [pscustomobject]@{
        group = $Group; workingSetBytes = $Ws; privateWorkingSetBytes = $PrivateWs
        privateBytes = $Commit; gpuDedicatedBytes = $GpuDedicated; gpuSharedBytes = $GpuShared
        cpuPercent = $Cpu
    }
}
$resourceRows = @(
    (New-ResourceRow shell 10 5 7 1 2 3)
    (New-ResourceRow wallpaper 20 10 14 4 8 6)
    (New-ResourceRow other 30 $null 21 $null $null $null)
)
$aggregates = Get-GroupedResourceAggregates $resourceRows
Assert-Equal 'shell aggregate' $aggregates.shell.workingSetBytes 10
Assert-Equal 'wallpaper aggregate' $aggregates.wallpaper.workingSetBytes 20
Assert-Equal 'total aggregate includes all groups' $aggregates.total.workingSetBytes 60
Assert-Equal 'nonWallpaper aggregate excludes wallpaper' $aggregates.nonWallpaper.workingSetBytes 40
Assert-Equal 'aggregate missing metric is null' $aggregates.total.privateWorkingSetBytes $null
Assert-Equal 'aggregate missing metric reports incomplete coverage' $aggregates.total.coverage.status 'partial'

$gpuRows = @(
    [pscustomobject]@{ Name = 'pid_12_luid_a_phys_0'; DedicatedUsage = 100; SharedUsage = 200 }
    [pscustomobject]@{ Name = 'pid_1_luid_a_phys_0'; DedicatedUsage = 999; SharedUsage = 999 }
    [pscustomobject]@{ Name = 'pid_120_luid_a_phys_0'; DedicatedUsage = 888; SharedUsage = 888 }
)
$gpu = Get-GpuMeasurement $gpuRows 12
Assert-Equal 'gpu exact pid dedicated usage' $gpu.dedicatedBytes 100
Assert-Equal 'gpu exact pid shared usage' $gpu.sharedBytes 200
Assert-Equal 'gpu exact pid match count' $gpu.matchCount 1
$gpuMissing = Get-GpuMeasurement $gpuRows 99
Assert-Equal 'gpu missing row is null' $gpuMissing.dedicatedBytes $null
Assert-Equal 'gpu missing row reports coverage' $gpuMissing.status 'missing_row'
$gpuExtent = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-GpuMeasurement'
}, $true).Extent.Text
Assert-True 'gpu function does not request IDProcess property' (-not ($gpuExtent -match 'IDProcess'))

$summary = Get-SummaryStats @(
    [pscustomobject]@{ totalWorkingSetBytes = 10; totalCpuPercent = 4 }
    [pscustomobject]@{ totalWorkingSetBytes = 30; totalCpuPercent = $null }
)
Assert-Equal 'summary count ignores null values' $summary.totalCpuPercent.count 1
Assert-Equal 'summary mean is calculated from values' $summary.totalWorkingSetBytes.mean 20
Assert-Equal 'summary min is calculated from values' $summary.totalWorkingSetBytes.min 10
Assert-Equal 'summary max is calculated from values' $summary.totalWorkingSetBytes.max 30

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}
Write-Output "All live-resource pure-function tests passed: $($failures.Count) failures"
