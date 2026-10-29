$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Parse parameter and pure aggregation/acceptance logic. Never launch a probe.
$samplerPath = Join-Path $PSScriptRoot 'measure-clock-resources.ps1'
$parseErrors = $null
$tokens = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($samplerPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw ($parseErrors | Out-String) }
foreach ($functionName in @('Get-CpuIntervalPercent', 'Get-Sum', 'Get-TreeCpuMeasurement', 'Get-GpuMeasurement')) {
    $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName }, $true)
    if ($null -eq $definition) { throw "Sampler function missing: $functionName" }
    . ([scriptblock]::Create($definition.Extent.Text))
}

function New-Observation([double]$Tick, [double]$Cpu, [string]$Phase = 'manager_open', [int]$Round = 1, [long]$PhaseStart = 1000, [bool]$Stable = $true) {
    [pscustomobject]@{
        tickBeforeMs = $Tick; tickAfterMs = $Tick + 2; cpuSeconds = $Cpu
        phase = $Phase; round = $Round; phaseStartMs = $PhaseStart; stablePhase = $Stable
    }
}
$failures = [Collections.Generic.List[string]]::new()
# Execute the actual parameter validation and Mode assignments without the main workflow.
$modeAssignments = @($ast.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq '$Mode'
} | ForEach-Object { $_.Extent.Text }) -join [Environment]::NewLine
$modeBinding = [scriptblock]::Create($ast.ParamBlock.Extent.Text + [Environment]::NewLine + $modeAssignments + [Environment]::NewLine + '$Mode')
$boundMode = & $modeBinding -BuildDirectory 'unused' -OutputDirectory 'unused' -Mode 'MANAGER-ONLY'
if ($boundMode -cne 'manager-only') {
    $failures.Add("Uppercase mode must normalize to manager-only, got <$boundMode>.")
} else { Write-Output 'PASS uppercase mode normalizes for the native probe' }
$invalidModeRejected = $false
try { $null = & $modeBinding -BuildDirectory 'unused' -OutputDirectory 'unused' -Mode 'invalid-mode' }
catch [Management.Automation.ParameterBindingException] { $invalidModeRejected = $true }
if (-not $invalidModeRejected) { $failures.Add('Unsupported modes must remain rejected by parameter validation.') }
else { Write-Output 'PASS unsupported mode remains rejected' }

# Evaluate only mode selection statements; no file access or process startup.
$productModeAccepted = $false
try {
    $productBoundMode = & $modeBinding -BuildDirectory 'unused' -OutputDirectory 'unused' -Mode 'PRODUCT-COMBINED'
    $productModeAccepted = $productBoundMode -ceq 'product-combined'
} catch [Management.Automation.ParameterBindingException] { }
if (-not $productModeAccepted) { $failures.Add('PRODUCT-COMBINED must bind and normalize to product-combined.') }
else { Write-Output 'PASS product-combined parameter binding and normalization' }
$Mode = 'product-combined'
foreach ($statement in $ast.EndBlock.Statements) {
    if ($statement -is [Management.Automation.Language.AssignmentStatementAst] -and
        $statement.Left.Extent.Text -in @('$widgetMode', '$productMode', '$probeName', '$requiredPhases')) {
        . ([scriptblock]::Create($statement.Extent.Text))
    } elseif ($statement -is [Management.Automation.Language.IfStatementAst] -and
        $statement.Extent.Text -match '\$requiredPhases\s*=') {
        . ([scriptblock]::Create($statement.Extent.Text))
    }
}
if ($probeName -cne 'lume-product-resource-probe.exe' -or $widgetMode) {
    $failures.Add('product-combined must select the isolated product probe without entering widget-only validation.')
} else { Write-Output 'PASS product-combined selects isolated product exe' }
$productPhases = @('components_only_before', 'manager_open', 'manager_minimized', 'manager_restored', 'components_only_after_close', 'component_disabled')
if (($requiredPhases -join ',') -cne ($productPhases -join ',')) {
    $failures.Add("product-combined must require all six ordered phases, got <$($requiredPhases -join ',')>.")
} else { Write-Output 'PASS product-combined requires six ordered phases' }
$steady = @(foreach ($roundNumber in 1..2) {
    foreach ($phaseName in $productPhases) {
        [pscustomobject]@{round=$roundNumber;phase=$phaseName;nonConsoleChildren=0}
    }
})
foreach ($statement in $ast.EndBlock.Statements) {
    if (($statement -is [Management.Automation.Language.AssignmentStatementAst] -and $statement.Left.Extent.Text -eq '$roundEndpoints') -or
        ($statement -is [Management.Automation.Language.IfStatementAst] -and $statement.Extent.Text -match '\$roundEndpoints\s*=')) {
        . ([scriptblock]::Create($statement.Extent.Text))
    }
}
if ($roundEndpoints.Count -ne 4 -or @($roundEndpoints | Where-Object phase -eq 'components_only_after_close').Count -ne 2 -or
    @($roundEndpoints | Where-Object phase -eq 'component_disabled').Count -ne 2) {
    $failures.Add('product-combined round endpoints must include components_only_after_close and component_disabled for every round.')
} else { Write-Output 'PASS product-combined closed and disabled round endpoints' }
$Rounds = 2; $CooldownSeconds = 30
$snapshotAssignment = $ast.Find({param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and
    $node.Left.Extent.Text -eq '$expectedSnapshots'}, $true)
. ([scriptblock]::Create($snapshotAssignment.Extent.Text))
if ($expectedSnapshots -ne 14) { $failures.Add("product-combined two rounds plus initial/cooldown must require 14 snapshots, got <$expectedSnapshots>.") }
else { Write-Output 'PASS product-combined retains snapshot count algorithm' }

function Assert-Cpu([string]$Name, $Previous, $Current, $Expected) {
    $actual = Get-CpuIntervalPercent $Previous $Current
    if (($null -eq $Expected -and $null -ne $actual) -or
        ($null -ne $Expected -and ($null -eq $actual -or [Math]::Abs($actual - $Expected) -gt 0.00001))) {
        $failures.Add("${Name}: expected <$Expected>, got <$actual>")
    } else { Write-Output "PASS $Name" }
}

Assert-Cpu 'cross-phase interval' (New-Observation 5000 1 'diagnostic') (New-Observation 6000 1.1) $null
Assert-Cpu 'same phase starts before warm-up ends' (New-Observation 2999 1) (New-Observation 3999 1.1) $null
Assert-Cpu 'valid steady interval' (New-Observation 4000 1) (New-Observation 5000 1.1) 10
Assert-Cpu 'missing intervening observation uses PID elapsed time' (New-Observation 4000 1) (New-Observation 7000 1.3) 10
Assert-Cpu 'cumulative CPU rollback' (New-Observation 4000 2) (New-Observation 5000 1) $null
Assert-Cpu 'first PID observation' $null (New-Observation 4000 1) $null
Assert-Cpu 'round changed' (New-Observation 4000 1) (New-Observation 5000 1.1 'manager_open' 2) $null
Assert-Cpu 'phase restarted with same name and round' (New-Observation 4000 1) (New-Observation 6000 1.1 'manager_open' 1 3500) $null
Assert-Cpu 'previous CPU read straddles phase change' (New-Observation 4000 1 'manager_open' 1 1000 $false) (New-Observation 5000 1.1) $null
Assert-Cpu 'current CPU read straddles phase change' (New-Observation 4000 1) (New-Observation 5000 1.1 'manager_open' 1 1000 $false) $null
Assert-Cpu 'nonpositive elapsed time' (New-Observation 4000 1) (New-Observation 4000 1.1) $null
Assert-Cpu 'idle CPU is valid zero' (New-Observation 4000 1) (New-Observation 5000 1) 0
Assert-Cpu 'missing current observation' (New-Observation 4000 1) $null $null
Assert-Cpu 'diagnostic interval is excluded' (New-Observation 4000 1 'diagnostic') (New-Observation 5000 1.1 'diagnostic') $null
Assert-Cpu 'steady interval begins exactly at two seconds' (New-Observation 3000 1) (New-Observation 4000 1.1) 10

function Assert-Tree([string]$Name, [object[]]$Rows, [int]$Gaps, [bool]$Stable, $Expected) {
    $actual = Get-TreeCpuMeasurement $Rows $Gaps $Stable 4
    if (($null -eq $Expected -and ($null -ne $actual.cpuPercentOneCore -or $null -ne $actual.cpuPercentMachine)) -or
        ($null -ne $Expected -and ($actual.cpuPercentOneCore -ne $Expected -or $actual.cpuPercentMachine -ne $Expected / 4))) {
        $failures.Add("${Name}: expected <$Expected>, got <$($actual.cpuPercentOneCore)>")
    } else { Write-Output "PASS $Name" }
}
$validRows = @([pscustomobject]@{cpuPercentOneCore = 10}, [pscustomobject]@{cpuPercentOneCore = 20})
Assert-Tree 'complete tree' $validRows 0 $true 30
Assert-Tree 'partial PID CPU coverage' @($validRows[0], [pscustomobject]@{cpuPercentOneCore = $null}) 0 $true $null
Assert-Tree 'missing process query' $validRows 1 $true $null
Assert-Tree 'unstable sample phase' $validRows 0 $false $null
Assert-Tree 'empty process tree' @() 0 $true $null

function New-SteadySample([string]$Phase, [bool]$Complete, $Cpu) {
    [pscustomobject]@{
        phase = $Phase; round = 1; processes = 1; children = 0; privateWsMeasuredProcesses = 1; processQueryGaps = 0
        privateWorkingSetMiB = 5; privateBytesMiB = 7
        cpuComplete = $Complete; cpuPercentOneCore = $Cpu
        cpuPercentMachine = if ($null -ne $Cpu) { $Cpu / 4 } else { $null }
        gpuAvailable = $false
    }
}
$steady = @(
    (New-SteadySample 'manager_open' $true 10)
    (New-SteadySample 'manager_open' $false $null)
    (New-SteadySample 'manager_open' $false 999)
    (New-SteadySample 'component_disabled' $false $null)
)
$Rounds = 1
$requiredPhases = @('manager_open', 'component_disabled')
foreach ($assignmentName in @('$phaseSummary', '$coverage', '$incompleteCoverage')) {
    $assignment = $ast.Find({ param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq $assignmentName }, $true)
    if ($null -eq $assignment) { throw "Sampler assignment missing: $assignmentName" }
    . ([scriptblock]::Create($assignment.Extent.Text))
}
$managerSummary = $phaseSummary | Where-Object phase -eq 'manager_open'
$disabledSummary = $phaseSummary | Where-Object phase -eq 'component_disabled'
if ($managerSummary.samples -ne 3 -or $managerSummary.cpuCompleteSamples -ne 1 -or $managerSummary.cpuIncompleteSamples -ne 2 -or
    $managerSummary.cpuPercentOneCoreMean -ne 10 -or $managerSummary.cpuPercentMachineMean -ne 2.5) {
    $failures.Add('Phase CPU means/counts must use only complete CPU observations, preserving memory sample counts.')
} else { Write-Output 'PASS phase summary uses complete CPU observations and reports counts' }
if ($disabledSummary.cpuCompleteSamples -ne 0 -or $null -ne $disabledSummary.cpuPercentOneCoreMean -or $null -ne $disabledSummary.cpuPercentMachineMean) {
    $failures.Add('Phase without valid CPU observations must have null CPU means and zero complete count.')
} else { Write-Output 'PASS phase without CPU coverage reports null means' }
if ($incompleteCoverage.Count -ne 0 -or ($coverage | Where-Object phase -eq 'component_disabled').cpuIncompleteSamples -ne 1) {
    $failures.Add('CPU gaps must be counted without failing otherwise complete memory coverage.')
} else { Write-Output 'PASS CPU missing coverage does not fail memory coverage' }

$gpu = Get-GpuMeasurement @(
    [pscustomobject]@{ Name='pid_1_luid_a_phys_0'; DedicatedUsage=10; SharedUsage=20; TotalCommitted=100 }
    [pscustomobject]@{ Name='pid_2_luid_a_phys_0'; DedicatedUsage=30; SharedUsage=40; TotalCommitted=200 }
    [pscustomobject]@{ Name='pid_12_luid_a_phys_0'; DedicatedUsage=999; SharedUsage=999; TotalCommitted=999 }
) @(
    [pscustomobject]@{ Name='pid_1_luid_a_phys_0_eng_0_engtype_3D'; UtilizationPercentage=20 }
    [pscustomobject]@{ Name='pid_2_luid_a_phys_0_eng_0_engtype_3D'; UtilizationPercentage=25 }
    [pscustomobject]@{ Name='pid_1_luid_a_phys_0_eng_1_engtype_Copy'; UtilizationPercentage=30 }
    [pscustomobject]@{ Name='pid_12_luid_a_phys_0_eng_0_engtype_3D'; UtilizationPercentage=90 }
) @(1,2)
if ($gpu.dedicatedBytes -ne 40 -or $gpu.sharedBytes -ne 60 -or $gpu.committedBytes -ne 300 -or $gpu.busiestEnginePercent -ne 45 -or
    $gpu.memoryInstances -ne 2 -or $gpu.engineInstances -ne 2) {
    $failures.Add('GPU counters must filter exact process identities, combine each physical engine and select the busiest engine.')
} else { Write-Output 'PASS GPU process and physical engine aggregation' }

foreach ($functionName in @('Get-DxgiValidation','Get-WidgetDataValidation','Get-OwnedHelperTraceRecords')) {
    $definition=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName},$true)
    if($null -eq $definition){throw "Sampler function missing: $functionName"}
    . ([scriptblock]::Create($definition.Extent.Text))
}
function New-DxgiRecord([string]$Phase,[int]$Round,[long]$Local=0,[long]$Nonlocal=0) {
    [pscustomobject]@{
        schemaVersion=1;phase=$Phase;round=$Round;pid=42;complete=$true
        source='IDXGIAdapter3::QueryVideoMemoryInfo';adapterIndex=0;nodeIndex=0;adapterLuid='00000000_00001234'
        localHresult=0;nonLocalHresult=0;localCurrentUsageBytes=$Local;nonLocalCurrentUsageBytes=$Nonlocal
    }
}
function New-DxgiRecords {
    @((New-DxgiRecord initial_disabled 0),(New-DxgiRecord components_enabled 1 1048576 262144),
      (New-DxgiRecord component_disabled 1),(New-DxgiRecord cooldown 1))
}
function Assert-Dxgi([string]$Name,$Records,[bool]$Incomplete,[bool]$LifecycleFailed) {
    $actual=Get-DxgiValidation $Records 42 1 30 $true
    if($actual.incomplete -ne $Incomplete -or $actual.lifecycleFailed -ne $LifecycleFailed) {
        $failures.Add("${Name}: incorrect DXGI result $($actual|ConvertTo-Json -Depth 5 -Compress)")
    } else {Write-Output "PASS $Name"}
}
Assert-Dxgi 'DXGI populated enabled and exactly zero inactive phases' (New-DxgiRecords) $false $false
Assert-Dxgi 'DXGI missing file is incomplete, never zero' @() $true $false
$case=New-DxgiRecords;$case[2].complete=$false;$case[2].localHresult=-2147467259;$case[2].localCurrentUsageBytes=$null
Assert-Dxgi 'DXGI failed query does not become zero' $case $true $false
$case=New-DxgiRecords;$case[2].pid=41
Assert-Dxgi 'DXGI foreign process identity rejected' $case $true $false
$case=New-DxgiRecords;$case[2].adapterIndex=1
Assert-Dxgi 'DXGI wrong adapter scope rejected' $case $true $false
$case=New-DxgiRecords;$case[2].adapterLuid='00000000_00005678'
Assert-Dxgi 'DXGI changing adapter identity cannot masquerade as release' $case $true $false
$case=New-DxgiRecords;$case+=New-DxgiRecord component_disabled 1
Assert-Dxgi 'DXGI duplicate phase rejected' $case $true $false
$case=New-DxgiRecords;$case[2].localCurrentUsageBytes=1
Assert-Dxgi 'DXGI one byte retained fails shutdown' $case $false $true
$case=New-DxgiRecords;$case[3].nonLocalCurrentUsageBytes=1
Assert-Dxgi 'DXGI nonlocal cooldown retention fails shutdown' $case $false $true
$case=New-DxgiRecords;$case[1].localCurrentUsageBytes=0;$case[1].nonLocalCurrentUsageBytes=0
Assert-Dxgi 'DXGI never populated enabled phases cannot prove release' $case $true $false
$case=New-DxgiRecords;$case[2].localCurrentUsageBytes=-1
Assert-Dxgi 'DXGI negative usage is invalid' $case $true $false
$case=New-DxgiRecords;$case[2].PSObject.Properties.Remove('nonLocalCurrentUsageBytes')
Assert-Dxgi 'DXGI missing usage property is incomplete' $case $true $false
$disabled=Get-DxgiValidation @() 42 1 0 $false
if($disabled.required -or $disabled.incomplete -or $disabled.lifecycleFailed){$failures.Add('Legacy modes must not require widget DXGI records')}
else{Write-Output 'PASS legacy modes do not require widget DXGI records'}
$reading=[pscustomobject]@{round=1;weatherEnabled=$true;weatherAvailable=$true;weatherError=0;weatherLoading=$false;performanceAvailable=$true}
foreach($case in @(
    @{name='real weather and performance';rows=@($reading);mode='widgets-combined';expected=$false},
    @{name='missing business data';rows=@();mode='widgets-combined';expected=$true},
    @{name='duplicate business round';rows=@($reading,$reading);mode='widgets-combined';expected=$true},
    @{name='organizer requires no weather provider';rows=@([pscustomobject]@{round=1;weatherEnabled=$false;weatherAvailable=$false;weatherError=1;weatherLoading=$false;performanceAvailable=$false});mode='organizer-only';expected=$false},
    @{name='weather provider failure is not synthetic success';rows=@([pscustomobject]@{round=1;weatherEnabled=$true;weatherAvailable=$false;weatherError=1;weatherLoading=$false;performanceAvailable=$true});mode='weather-only';expected=$true}
)) {
    $actual=Get-WidgetDataValidation $case.rows $case.mode 1
    if($actual.incomplete -ne $case.expected){$failures.Add("Widget data: $($case.name)")}
    else{Write-Output "PASS $($case.name)"}
}
function New-HelperEvent([string]$Kind,[int]$Parent=42,[long]$Time=200,[string]$Name='LumeIconHost.exe') {
    [pscustomobject]@{SourceIdentifier="test_$Kind";SourceEventArgs=[pscustomobject]@{NewEvent=[pscustomobject]@{
        ProcessName=$Name;ProcessID=7000;ParentProcessID=$Parent;TIME_CREATED=$Time;ExitStatus=0
    }}}
}
$helperEvents=@((New-HelperEvent start),(New-HelperEvent stop 42 300),
    (New-HelperEvent start 41),(New-HelperEvent start 42 99),(New-HelperEvent start 42 200 other.exe))
$owned=@(Get-OwnedHelperTraceRecords $helperEvents 42 100)
if($owned.Count -ne 2 -or $owned[0].event -ne 'start' -or $owned[1].event -ne 'stop' -or $owned[1].exitStatus -ne 0) {
    $failures.Add('Helper traces must retain own start/stop events, excluding foreign parents, names and pre-probe identities.')
} else {Write-Output 'PASS transient helper trace identity and start/stop filtering'}
$jobDefinition=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-JobResourceValidation'},$true)
if($null -eq $jobDefinition) {
    $failures.Add('Kernel Job validation must reject incomplete transient-process resource coverage.')
} else {
    . ([scriptblock]::Create($jobDefinition.Extent.Text))
    function New-JobRecords {
        $phases=@('initial_disabled','components_enabled','component_disabled','components_enabled','component_disabled','cooldown')
        $rounds=@(0,1,1,2,2,2)
        $totals=@(1,2,2,3,3,3)
        for($i=0;$i -lt $phases.Count;$i++) {
            [pscustomobject]@{
                schemaVersion=1;phase=$phases[$i];round=$rounds[$i];pid=42;tickMs=1000+1000*$i
                source='QueryInformationJobObject'
                coverage='probe_and_all_future_descendants_including_nested_icon_jobs'
                complete=$true;timeError=0;memoryError=0;limitFlags=0
                totalProcesses=$totals[$i];activeProcesses=1;terminatedByLimit=0
                kernelTime100ns=10000+1000*$i;userTime100ns=20000+2000*$i
                peakProcessCommitBytes=1048576;peakJobCommitBytes=2097152
            }
        }
    }
    function Assert-Job([string]$Name,$Records,[bool]$Incomplete,[bool]$LifecycleFailed) {
        $actual=Get-JobResourceValidation $Records 42 2 30 $true $true
        if($actual.incomplete -ne $Incomplete -or $actual.lifecycleFailed -ne $LifecycleFailed) {
            $failures.Add("${Name}: incorrect Job result $($actual|ConvertTo-Json -Depth 5 -Compress)")
        } else {Write-Output "PASS $Name"}
    }
    Assert-Job 'Job includes completed children, cumulative CPU and nonzero commit peaks' (New-JobRecords) $false $false
    Assert-Job 'Job missing file remains incomplete' @() $true $false
    $case=@(New-JobRecords);$case=$case[0..4]
    Assert-Job 'Job missing required cooldown rejected' $case $true $false
    $case=@(New-JobRecords);$case+=($case[2]|Select-Object *)
    Assert-Job 'Job duplicate phase rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].phase='unexpected'
    Assert-Job 'Job unexpected phase rejected' $case $true $false
    $case=@(New-JobRecords);$swap=$case[1];$case[1]=$case[2];$case[2]=$swap
    Assert-Job 'Job reordered stages cannot imply monotonic accounting' $case $true $false
    $case=@(New-JobRecords);$case[2].pid=43
    Assert-Job 'Job foreign probe identity rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].coverage='all_system_processes'
    Assert-Job 'Job wrong coverage rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].source='WMI'
    Assert-Job 'Job wrong measurement source rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].schemaVersion=2
    Assert-Job 'Job unknown schema rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].complete=$false;$case[2].timeError=5;$case[2].kernelTime100ns=$null
    Assert-Job 'Job failed CPU query does not become zero' $case $true $false
    $case=@(New-JobRecords);$case[2].complete=$false;$case[2].memoryError=5;$case[2].peakJobCommitBytes=$null
    Assert-Job 'Job failed memory query does not become zero' $case $true $false
    $case=@(New-JobRecords);$case[2].timeError=5
    Assert-Job 'Job inconsistent successful-query marker rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].complete='true'
    Assert-Job 'Job textual complete marker rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].PSObject.Properties.Remove('limitFlags')
    Assert-Job 'Job missing scope constraint field rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].limitFlags=8192
    Assert-Job 'Job accounting probe must add no job limits' $case $true $false
    foreach($value in @($null,$true,'12',-1,1.5,[double]::NaN,[double]::PositiveInfinity)) {
        $case=@(New-JobRecords);$case[2].kernelTime100ns=$value
        Assert-Job "Job invalid CPU counter <$value> rejected" $case $true $false
    }
    $case=@(New-JobRecords);$case[2].peakJobCommitBytes=0
    Assert-Job 'Job zero aggregate commit peak rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].peakProcessCommitBytes=0
    Assert-Job 'Job zero process commit peak rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].tickMs=$case[1].tickMs
    Assert-Job 'Job equal sample timestamps rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].kernelTime100ns=1
    Assert-Job 'Job kernel CPU rollback rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].userTime100ns=1
    Assert-Job 'Job user CPU rollback rejected' $case $true $false
    $case=@(New-JobRecords);$case|ForEach-Object {$_.kernelTime100ns=0;$_.userTime100ns=0}
    Assert-Job 'Job never populated CPU counters cannot prove coverage' $case $true $false
    $case=@(New-JobRecords);$case[2].totalProcesses=1
    Assert-Job 'Job total process rollback rejected' $case $true $false
    $case=@(New-JobRecords);$case[2].activeProcesses=3
    Assert-Job 'Job active processes cannot exceed lifetime total' $case $true $false
    $case=@(New-JobRecords);$case[2].activeProcesses=0
    Assert-Job 'Job snapshot cannot omit its living probe' $case $true $false
    $case=@(New-JobRecords);$case|ForEach-Object {$_.totalProcesses=1}
    Assert-Job 'Job organizer must actually create children' $case $true $false
    $case=@(New-JobRecords);$case[3..5]|ForEach-Object {$_.totalProcesses=2}
    Assert-Job 'Job children from only the first round do not cover later rounds' $case $true $false
    $case=@(New-JobRecords);$case[2].activeProcesses=2
    Assert-Job 'Job live child at a disabled endpoint fails release' $case $false $true
    $case=@(New-JobRecords);$case[5].activeProcesses=2
    Assert-Job 'Job live child after cooldown fails release' $case $false $true
    $case=@(New-JobRecords);$case[5].terminatedByLimit=1
    Assert-Job 'Job limit termination cannot masquerade as clean shutdown' $case $false $true
    $withoutChildren=@(New-JobRecords);$withoutChildren|ForEach-Object {$_.totalProcesses=1}
    $noHelper=Get-JobResourceValidation $withoutChildren 42 2 30 $true $false
    if($noHelper.incomplete -or $noHelper.lifecycleFailed){$failures.Add('Job accounting for a mode without organizer must not require child creation.')}
    else {Write-Output 'PASS Job non-organizer modes require no helper child'}
    $disabled=Get-JobResourceValidation @() 42 2 0 $false $false
    if($disabled.required -or $disabled.incomplete -or $disabled.lifecycleFailed){$failures.Add('Legacy non-widget modes must not require Job records.')}
    else {Write-Output 'PASS legacy non-widget modes require no Job records'}
    $valid=Get-JobResourceValidation (New-JobRecords) 42 2 30 $true $true
    if($valid.roundChildProcesses.Count -ne 2 -or $valid.roundChildProcesses[0].newProcesses -ne 1 -or
       $valid.roundChildProcesses[1].newProcesses -ne 1 -or $valid.finalTotalProcesses -ne 3 -or
       $valid.finalActiveProcesses -ne 1 -or $valid.peakJobCommitBytes -ne 2097152 -or
       [Math]::Abs($valid.cumulativeCpuSeconds-0.0045) -gt 0.0000001) {
        $failures.Add('Job summary must retain actual cumulative CPU, process deltas and commit bytes without relabelling as private working set.')
    } else {Write-Output 'PASS Job summary preserves cumulative CPU and commit units'}
    $coverageAssignment=$ast.Find({param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left.Extent.Text -eq '$helperCoverageIncomplete'},$true)
    if($null -eq $coverageAssignment) {
        $failures.Add('Helper coverage gate must use validated kernel Job accounting.')
    } else {
        $helperTraceRequired=$true;$helperTraceIncomplete=$true;$jobResourceValidation=$valid
        . ([scriptblock]::Create($coverageAssignment.Extent.Text))
        if($helperCoverageIncomplete){$failures.Add('Valid kernel Job accounting must retain WMI access failure without treating it as missing aggregate child accounting.')}
        else {Write-Output 'PASS Job coverage survives separately reported WMI access failure'}
        $helperTraceIncomplete=$false;$jobResourceValidation=Get-JobResourceValidation @() 42 2 30 $true $true
        . ([scriptblock]::Create($coverageAssignment.Extent.Text))
        if(-not $helperCoverageIncomplete){$failures.Add('Successful lifetime events cannot replace missing kernel child CPU/commit accounting.')}
        else {Write-Output 'PASS lifetime traces do not replace missing Job CPU and commit data'}
        $jobResourceValidation=$noHelper
        . ([scriptblock]::Create($coverageAssignment.Extent.Text))
        if(-not $helperCoverageIncomplete){$failures.Add('Organizer helper coverage requires per-round child creation validation.')}
        else {Write-Output 'PASS helper coverage requires actual per-round child creation'}
        $helperTraceRequired=$false
        . ([scriptblock]::Create($coverageAssignment.Extent.Text))
        if($helperCoverageIncomplete){$failures.Add('Non-organizer modes do not require transient helper coverage.')}
        else {Write-Output 'PASS non-organizer modes do not require helper coverage'}
    }
}
if ($failures.Count -gt 0) { throw ($failures -join [Environment]::NewLine) }
Write-Output 'All mode, CPU interval, tree, WMI, DXGI, Job, business-data, summary and coverage tests passed.'
