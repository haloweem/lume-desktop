[CmdletBinding()]
param(
    [string]$TestExecutable,
    [string[]]$TestArguments = @(),
    [string]$MpvExecutable,
    [string]$Fixture,
    [string]$OutputDirectory,
    [string]$PhaseFile,
    [ValidateSet('Auto', 'Json', 'JsonLines')][string]$PhaseFormat = 'Auto',
    [ValidateRange(3, 30)][int]$PhaseSeconds = 6,
    [ValidateRange(250, 5000)][int]$IntervalMilliseconds = 1000,
    [ValidateRange(30, 1800)][int]$MaximumSeconds = 300,
    [string[]]$SteadyPhases = @(),
    [ValidateRange(0, 60000)][int]$PhaseWindowMilliseconds = 0,
    [ValidateRange(0, 30000)][int]$WarmupMilliseconds = 2000,
    [switch]$SkipGpu,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$scriptHome = [IO.Path]::GetFullPath($PSScriptRoot)
if (Test-Path -LiteralPath (Join-Path $scriptHome '..\unit\wallpaper_session_playback_test.cpp') -PathType Leaf) {
    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $scriptHome '..\..\..'))
    $runtimeRoot = Join-Path $repositoryRoot 'build-artifacts\native-rebuild\wallpaper-runtime'
} else {
    $runtimeRoot = $scriptHome
}
$nativeRoot = Split-Path $runtimeRoot -Parent
if (-not $MpvExecutable) { $MpvExecutable = Join-Path $runtimeRoot 'mpv.exe' }
if (-not $Fixture) { $Fixture = Join-Path $runtimeRoot 'test-pattern.mp4' }
if (-not $TestExecutable) {
    $TestExecutable = Join-Path $nativeRoot 'release\tests\unit\lume-wallpaper-session-playback-test.exe'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $runtimeRoot ('resource-run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $OutputDirectory.StartsWith($runtimeRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must remain inside this verification-only wallpaper-runtime directory.'
}
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
if (-not $PhaseFile) { $PhaseFile = Join-Path $OutputDirectory 'wallpaper-session-phase.json' }
$PhaseFile = [IO.Path]::GetFullPath($PhaseFile)

if (-not ('LumeWallpaperResourceProbe.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
namespace LumeWallpaperResourceProbe {
 public sealed class Entry { public int Id; public int Parent; public string Name; }
 public sealed class Memory {
  public bool Complete; public int Error; public long PrivateCommitBytes;
  public long WorkingSetBytes; public long PrivateWorkingSetBytes; public long ProcessorTicks;
 }
 public static class Native {
  [StructLayout(LayoutKind.Sequential)] struct SECURITY_ATTRIBUTES { public int Length; public IntPtr Descriptor; public int Inherit; }
  [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct STARTUPINFO {
   public int Size;public string Reserved,Desktop,Title;
   public uint X,Y,XSize,YSize,XCountChars,YCountChars,FillAttribute,Flags;
   public short ShowWindow,ReservedBytes;public IntPtr ReservedPointer,Input,Output,Error;
  }
  [StructLayout(LayoutKind.Sequential)] struct PROCESS_INFORMATION { public IntPtr Process,Thread;public uint ProcessId,ThreadId; }
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
  static extern IntPtr CreateFileW(string path,uint access,uint sharing,ref SECURITY_ATTRIBUTES security,uint disposition,uint flags,IntPtr template);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
  static extern bool CreateProcessW(string executable,StringBuilder arguments,IntPtr processSecurity,IntPtr threadSecurity,bool inherit,uint flags,IntPtr environment,string directory,ref STARTUPINFO startup,out PROCESS_INFORMATION process);
  public static Process Launch(string executable,string arguments,string directory,string stdout,string stderr) {
   var security=new SECURITY_ATTRIBUTES {Length=Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES)),Inherit=1};
   IntPtr output=CreateFileW(stdout,0x40000000,3,ref security,2,0x80,IntPtr.Zero);
   IntPtr error=CreateFileW(stderr,0x40000000,3,ref security,2,0x80,IntPtr.Zero);
   IntPtr input=CreateFileW("NUL",0x80000000,3,ref security,3,0,IntPtr.Zero);
   try {
    if(output==new IntPtr(-1)||error==new IntPtr(-1)||input==new IntPtr(-1))throw new System.ComponentModel.Win32Exception();
    var startup=new STARTUPINFO {Size=Marshal.SizeOf(typeof(STARTUPINFO)),Flags=0x100,Input=input,Output=output,Error=error};
    PROCESS_INFORMATION created;
    // DETACHED_PROCESS: no console or conhost belonging to the harness.
    if(!CreateProcessW(executable,new StringBuilder("\""+executable+"\" "+arguments),IntPtr.Zero,IntPtr.Zero,true,8|0x400,IntPtr.Zero,directory,ref startup,out created))
     throw new System.ComponentModel.Win32Exception();
    try {var process=Process.GetProcessById((int)created.ProcessId);IntPtr pinned=process.Handle;return process;}
    finally{CloseHandle(created.Thread);CloseHandle(created.Process);}
   }finally {
    if(output!=new IntPtr(-1))CloseHandle(output);if(error!=new IntPtr(-1))CloseHandle(error);if(input!=new IntPtr(-1))CloseHandle(input);
   }
  }
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  struct PROCESSENTRY32 {
   public uint Size, Usage, ProcessId; public UIntPtr DefaultHeapId;
   public uint ModuleId, Threads, ParentProcessId; public int BasePriority; public uint Flags;
   [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string ExeFile;
  }
  [StructLayout(LayoutKind.Sequential)]
  struct MEMORY_COUNTERS {
   public uint Size, PageFaults; public UIntPtr PeakWorkingSetSize, WorkingSetSize;
   public UIntPtr QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
   public UIntPtr QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage;
   public UIntPtr PagefileUsage, PeakPagefileUsage, PrivateUsage;
  }
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr CreateToolhelp32Snapshot(uint flags,uint pid);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool Process32FirstW(IntPtr snapshot,ref PROCESSENTRY32 entry);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool Process32NextW(IntPtr snapshot,ref PROCESSENTRY32 entry);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  [DllImport("psapi.dll", SetLastError=true)] static extern bool GetProcessMemoryInfo(IntPtr process,ref MEMORY_COUNTERS counters,uint size);
  [DllImport("psapi.dll", SetLastError=true)] static extern bool QueryWorkingSet(IntPtr process,IntPtr buffer,int bytes);
  public static Entry[] Snapshot() {
   var result=new List<Entry>(); IntPtr snapshot=CreateToolhelp32Snapshot(2,0);
   if(snapshot==new IntPtr(-1))throw new System.ComponentModel.Win32Exception();
   try {
    var entry=new PROCESSENTRY32(); entry.Size=(uint)Marshal.SizeOf(typeof(PROCESSENTRY32));
    if(Process32FirstW(snapshot,ref entry))do {
     result.Add(new Entry {Id=(int)entry.ProcessId,Parent=(int)entry.ParentProcessId,Name=entry.ExeFile});
    }while(Process32NextW(snapshot,ref entry));
    else throw new System.ComponentModel.Win32Exception();
   }finally{CloseHandle(snapshot);} return result.ToArray();
  }
  public static Memory Read(Process process) {
   var result=new Memory(); var counters=new MEMORY_COUNTERS();
   counters.Size=(uint)Marshal.SizeOf(typeof(MEMORY_COUNTERS));
   if(!GetProcessMemoryInfo(process.Handle,ref counters,counters.Size)) {
    result.Error=Marshal.GetLastWin32Error();return result;
   }
   result.PrivateCommitBytes=(long)counters.PrivateUsage.ToUInt64();
   result.WorkingSetBytes=(long)counters.WorkingSetSize.ToUInt64();
   result.ProcessorTicks=process.TotalProcessorTime.Ticks;
   int bytes=1024*1024;
   for(int attempt=0;attempt<4;attempt++) {
    IntPtr buffer=Marshal.AllocHGlobal(bytes);
    try {
     Marshal.WriteIntPtr(buffer,IntPtr.Zero);
     if(QueryWorkingSet(process.Handle,buffer,bytes)) {
      long count=Marshal.ReadIntPtr(buffer).ToInt64(), privatePages=0;
      if(count<0||count>(bytes-IntPtr.Size)/IntPtr.Size){result.Error=13;return result;}
      for(int index=0;index<count;index++) {
       // PSAPI_WORKING_SET_BLOCK: Protection:5, ShareCount:3, Shared:1.
       long page=Marshal.ReadIntPtr(buffer,(index+1)*IntPtr.Size).ToInt64();
       if((page&(1L<<8))==0)privatePages++;
      }
      result.PrivateWorkingSetBytes=privatePages*Environment.SystemPageSize;
      result.Complete=true;return result;
     }
     result.Error=Marshal.GetLastWin32Error();
     long requested=Marshal.ReadIntPtr(buffer).ToInt64();
     if(result.Error!=24||requested<=0||requested>32*1024*1024)return result;
     bytes=checked((int)((requested+1024)*IntPtr.Size));
    }finally{Marshal.FreeHGlobal(buffer);}
   }return result;
  }
 }
 public sealed class GpuValue { public int Pid; public string Kind, Instance; public double Value; public uint Status; }
 public sealed class GpuRead { public uint CollectStatus; public string Status; public GpuValue[] Values; public string[] CounterErrors; }
 public sealed class Gpu : IDisposable {
  [StructLayout(LayoutKind.Sequential)] struct FORMAT_VALUE { public uint Status; public double Value; }
  [StructLayout(LayoutKind.Sequential)] struct FORMAT_ITEM { public IntPtr Name; public FORMAT_VALUE Value; }
  [DllImport("pdh.dll",CharSet=CharSet.Unicode)] static extern uint PdhOpenQueryW(string source,IntPtr data,out IntPtr query);
  [DllImport("pdh.dll",CharSet=CharSet.Unicode)] static extern uint PdhAddEnglishCounterW(IntPtr query,string path,IntPtr data,out IntPtr counter);
  [DllImport("pdh.dll")] static extern uint PdhCollectQueryData(IntPtr query);
  [DllImport("pdh.dll",CharSet=CharSet.Unicode)] static extern uint PdhGetFormattedCounterArrayW(IntPtr counter,uint format,ref uint size,ref uint count,IntPtr buffer);
  [DllImport("pdh.dll")] static extern uint PdhCloseQuery(IntPtr query);
  IntPtr query;
  readonly Dictionary<string,IntPtr> counters=new Dictionary<string,IntPtr>();
  readonly List<string> errors=new List<string>();
  public Gpu() {
   uint status=PdhOpenQueryW(null,IntPtr.Zero,out query);
   if(status!=0){errors.Add("query:"+status);return;}
   Add("engine_pct",@"\GPU Engine(*)\Utilization Percentage");
   Add("dedicated_bytes",@"\GPU Process Memory(*)\Dedicated Usage");
   Add("shared_bytes",@"\GPU Process Memory(*)\Shared Usage");
   PdhCollectQueryData(query);
  }
  void Add(string kind,string path) {
   IntPtr counter;uint status=PdhAddEnglishCounterW(query,path,IntPtr.Zero,out counter);
   if(status==0)counters[kind]=counter;else errors.Add(kind+":"+status);
  }
  public GpuRead Read(int[] pids) {
   var wanted=new HashSet<int>(pids);var values=new List<GpuValue>();var sampleErrors=new List<string>(errors);
   uint status=query==IntPtr.Zero?0xFFFFFFFF:PdhCollectQueryData(query);
   foreach(var pair in counters) {
    uint bytes=0,count=0;
    uint code=PdhGetFormattedCounterArrayW(pair.Value,0x200|0x8000,ref bytes,ref count,IntPtr.Zero);
    if(code!=0x800007D2||bytes==0){sampleErrors.Add(pair.Key+":"+code);continue;}
    IntPtr buffer=Marshal.AllocHGlobal((int)bytes);
    try {
     code=PdhGetFormattedCounterArrayW(pair.Value,0x200|0x8000,ref bytes,ref count,buffer);
     if(code!=0){sampleErrors.Add(pair.Key+":"+code);continue;}
     int stride=Marshal.SizeOf(typeof(FORMAT_ITEM));
     for(int index=0;index<count;index++) {
      var item=(FORMAT_ITEM)Marshal.PtrToStructure(IntPtr.Add(buffer,index*stride),typeof(FORMAT_ITEM));
      string name=Marshal.PtrToStringUni(item.Name)??"";var match=Regex.Match(name,@"(?:^|_)pid_(\d+)(?:_|$)");
      int pid;if(!match.Success||!int.TryParse(match.Groups[1].Value,out pid)||!wanted.Contains(pid))continue;
      values.Add(new GpuValue {Pid=pid,Kind=pair.Key,Instance=name,Value=item.Value.Value,Status=item.Value.Status});
     }
    }finally{Marshal.FreeHGlobal(buffer);}
   }
   return new GpuRead {CollectStatus=status,Status=status!=0?"unavailable":values.Count==0?"no-matching-instances":"observed",
    Values=values.ToArray(),CounterErrors=sampleErrors.ToArray()};
  }
  public void Dispose(){if(query!=IntPtr.Zero){PdhCloseQuery(query);query=IntPtr.Zero;}}
 }
}
'@
}

function Get-Phase {
    if (-not (Test-Path -LiteralPath $PhaseFile -PathType Leaf)) {
        return [pscustomobject]@{ Name = 'starting'; TickMs = 0L; Complete = $false; Error = 'phase-file-not-yet-present' }
    }
    try {
        $stream = [IO.File]::Open($PhaseFile, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $reader = [IO.StreamReader]::new($stream)
        try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $object = $null
        if ($PhaseFormat -ne 'JsonLines') {
            try { $object = ConvertFrom-Json -InputObject $text -ErrorAction Stop } catch { }
        }
        if (-not $object -and $PhaseFormat -ne 'Json') {
            $lines = @($text -split '\r?\n' | Where-Object { $_.Trim() })
            for ($index = $lines.Length - 1; $index -ge 0 -and -not $object; $index--) {
                try { $object = ConvertFrom-Json -InputObject $lines[$index] -ErrorAction Stop } catch { }
            }
        }
        if ($object -and $object.phase) {
            $tick = 0L
            if ($null -ne $object.tickMs) { $tick = [long]$object.tickMs }
            elseif ($null -ne $object.tick_ms) { $tick = [long]$object.tick_ms }
            return [pscustomobject]@{ Name = [string]$object.phase; TickMs = $tick; Complete = $true; Error = '' }
        }
        return [pscustomobject]@{ Name = 'phase-unavailable'; TickMs = 0L; Complete = $false; Error = 'no-complete-phase-object' }
    } catch {
        return [pscustomobject]@{ Name = 'phase-unavailable'; TickMs = 0L; Complete = $false; Error = $_.Exception.Message }
    }
}

if ($ValidateOnly) {
    $self = Get-Process -Id $PID
    $memory = [LumeWallpaperResourceProbe.Native]::Read($self)
    $gpu = if (-not $SkipGpu) { [LumeWallpaperResourceProbe.Gpu]::new() } else { $null }
    try {
        Start-Sleep -Milliseconds 300
        $validation = [ordered]@{
            memory = $memory
            snapshotCount = @([LumeWallpaperResourceProbe.Native]::Snapshot()).Count
            gpu = if ($gpu) { $gpu.Read(@($PID)) } else { $null }
            noPlayerLaunched = $true
        }
        $validation | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'probe-validation.json') -Encoding utf8
        $validation | ConvertTo-Json -Depth 10
        if (-not $memory.Complete) { throw "Native memory query failed: $($memory.Error)" }
    } finally { if ($gpu) { $gpu.Dispose() } }
    return
}

$TestExecutable = (Resolve-Path -LiteralPath $TestExecutable).ProviderPath
$MpvExecutable = (Resolve-Path -LiteralPath $MpvExecutable).ProviderPath
$Fixture = (Resolve-Path -LiteralPath $Fixture).ProviderPath
if (-not $TestExecutable.StartsWith($nativeRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Only a test executable inside build-artifacts/native-rebuild may be launched.'
}
if (-not $MpvExecutable.StartsWith($runtimeRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'MpvExecutable must be the isolated verification copy.'
}
if ($TestArguments.Count -eq 0) {
    $TestArguments = @($MpvExecutable, $Fixture, $OutputDirectory, [string]$PhaseSeconds)
} else {
    $TestArguments = @($TestArguments | ForEach-Object {
        $_.Replace('{output}', $OutputDirectory).Replace('{mpv}', $MpvExecutable).
            Replace('{fixture}', $Fixture).Replace('{seconds}', [string]$PhaseSeconds).
            Replace('{phases}', $PhaseFile)
    })
}
foreach ($argument in $TestArguments) {
    if ($argument.Contains('"')) { throw 'An argument contains a double quote; supply a literal Windows path or ordinary test flag.' }
}
$quotedArguments = @($TestArguments | ForEach-Object {
    '"' + ($_ -replace '(\\+)$', '$1$1') + '"'
})
$stdout = Join-Path $OutputDirectory 'test.stdout.log'
$stderr = Join-Path $OutputDirectory 'test.stderr.log'
$samplePath = Join-Path $OutputDirectory 'resource-samples.jsonl'
$fixtureFiles = if (Test-Path -LiteralPath $Fixture -PathType Leaf) {
    @([pscustomobject]@{ relativePath = [IO.Path]::GetFileName($Fixture); sha256 = (Get-FileHash -LiteralPath $Fixture -Algorithm SHA256).Hash })
} else {
    @(Get-ChildItem -LiteralPath $Fixture -Recurse -File | ForEach-Object {
        [pscustomobject]@{ relativePath = $_.FullName.Substring($Fixture.TrimEnd('\').Length + 1); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
}
$manifest = [ordered]@{
    verificationOnly = $true
    executable = $TestExecutable
    executableSha256 = (Get-FileHash -LiteralPath $TestExecutable -Algorithm SHA256).Hash
    mpv = $MpvExecutable
    mpvSha256 = (Get-FileHash -LiteralPath $MpvExecutable -Algorithm SHA256).Hash
    fixture = $Fixture
    fixtureKind = if (Test-Path -LiteralPath $Fixture -PathType Leaf) { 'file' } else { 'directory' }
    fixtureFiles = $fixtureFiles
    arguments = $TestArguments
    phaseFile = $PhaseFile
    intervalMilliseconds = $IntervalMilliseconds
    steadyPhases = $SteadyPhases
    phaseWindowMilliseconds = $PhaseWindowMilliseconds
    warmupMilliseconds = $WarmupMilliseconds
    logicalProcessors = [Environment]::ProcessorCount
    startedAt = (Get-Date).ToUniversalTime().ToString('o')
    metricDefinitions = @{
        privateCommitBytes = 'GetProcessMemoryInfo PROCESS_MEMORY_COUNTERS_EX.PrivateUsage'
        privateWorkingSetBytes = 'QueryWorkingSet resident entries whose Shared bit is zero, multiplied by system page size'
        cpuMachinePct = 'delta process kernel+user CPU ticks / delta wall time / logical processor count * 100'
        gpu = 'PDH English GPU Engine and GPU Process Memory counters; raw instances retained; unavailable values remain null'
    }
    limitations = @(
        'Only this newly launched test process and recursively observed descendants are included; installed applications are excluded.',
        'Short lived descendants entirely between snapshots can be missed; test evidence independently checks its owned job/container cleanup.',
        'Memory snapshots are sequential and do not trim working sets. Sampler overhead is in a separate PowerShell process.',
        'GPU engine percentages are retained per instance; do not sum different physical engines as one GPU percentage.',
        'GPU shared/dedicated bytes are reported separately and must not be added blindly to private Commit.',
        'An off-screen parent verifies playback and lifecycle but is not desktop composition or fullscreen rendering acceptance.'
    )
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'run-manifest.json') -Encoding utf8
$known = @{}
$identities = @{}
$previousCpu = @{}
$samples = [Collections.Generic.List[object]]::new()
$gpu = if (-not $SkipGpu) { [LumeWallpaperResourceProbe.Gpu]::new() } else { $null }
$timer = [Diagnostics.Stopwatch]::StartNew()
$launched = $null
$timedOut = $false
$lastPhase = ''
$phaseStarted = 0.0
$finalResidual = @()
try {
    $launched = [LumeWallpaperResourceProbe.Native]::Launch($TestExecutable, ($quotedArguments -join ' '), $OutputDirectory, $stdout, $stderr)
    $known[$launched.Id] = $launched
    do {
        $sampleStart = $timer.Elapsed.TotalMilliseconds
        $snapshot = @([LumeWallpaperResourceProbe.Native]::Snapshot())
        $live = [Collections.Generic.HashSet[int]]::new()
        foreach ($entry in @($known.GetEnumerator())) {
            if (-not $entry.Value.HasExited) { $null = $live.Add([int]$entry.Key) }
        }
        $changed = $true
        while ($changed) {
            $changed = $false
            foreach ($entry in $snapshot) {
                if ($live.Contains($entry.Parent) -and -not $live.Contains($entry.Id)) {
                    try {
                        $observed = Get-Process -Id $entry.Id -ErrorAction Stop
                        $null = $observed.Handle
                        $known[$entry.Id] = $observed
                        $null = $live.Add($entry.Id)
                        $changed = $true
                    } catch { }
                }
            }
        }
        $phaseMarker = Get-Phase
        $phase = $phaseMarker.Name
        if ($phase -ne $lastPhase) { $lastPhase = $phase; $phaseStarted = $sampleStart }
        $phaseElapsed = if ($phaseMarker.TickMs -gt 0) { [Environment]::TickCount64 - $phaseMarker.TickMs } else { $sampleStart - $phaseStarted }
        $phaseAccepted = $phaseMarker.Complete -and ($SteadyPhases.Count -eq 0 -or $phase -in $SteadyPhases) -and
            $phase -notin @('starting', 'phase-unavailable', 'transition', 'tray-exit', 'all-exited')
        $steadyWindow = $phaseAccepted -and $phaseElapsed -ge $WarmupMilliseconds -and
            ($PhaseWindowMilliseconds -eq 0 -or $phaseElapsed -lt $PhaseWindowMilliseconds)
        $processSamples = [Collections.Generic.List[object]]::new()
        foreach ($processId in $live) {
            $process = $known[$processId]
            try {
                $memory = [LumeWallpaperResourceProbe.Native]::Read($process)
                $identity = '{0}:{1}' -f $processId, $process.StartTime.ToUniversalTime().Ticks
                $cpu = $null
                if ($previousCpu.ContainsKey($identity)) {
                    $previous = $previousCpu[$identity]
                    $elapsed = $sampleStart - $previous.wallMs
                    if ($elapsed -gt 0) { $cpu = 100.0 * ($memory.ProcessorTicks - $previous.ticks) / ($elapsed * 10000.0 * [Environment]::ProcessorCount) }
                }
                $previousCpu[$identity] = @{ ticks = $memory.ProcessorTicks; wallMs = $sampleStart }
                $identities[$identity] = @{ pid = $processId; createdAt = $process.StartTime.ToUniversalTime().ToString('o'); name = $process.ProcessName }
                $processSamples.Add([pscustomobject][ordered]@{
                    pid = $processId; identity = $identity; name = $process.ProcessName
                    isRoot = $processId -eq $launched.Id; complete = $memory.Complete; error = $memory.Error
                    privateCommitBytes = $memory.PrivateCommitBytes
                    privateWorkingSetBytes = if ($memory.Complete) { $memory.PrivateWorkingSetBytes } else { $null }
                    workingSetBytes = $memory.WorkingSetBytes
                    cpuMachinePct = $cpu
                })
            } catch {
                if (-not $process.HasExited) { $processSamples.Add([pscustomobject]@{ pid = $processId; complete = $false; error = $_.Exception.Message }) }
            }
        }
        $gpuSample = if ($gpu) { $gpu.Read([int[]]@($live)) } else { $null }
        $gpuEngines = if ($gpuSample) { @($gpuSample.Values | Where-Object { $_.Kind -eq 'engine_pct' }) } else { @() }
        $gpuEngineValid = $gpuEngines.Count -gt 0 -and @($gpuEngines | Where-Object { $_.Status -gt 1 }).Count -eq 0
        $gpuMemory = if ($gpuSample) { @($gpuSample.Values | Where-Object { $_.Kind -ne 'engine_pct' }) } else { @() }
        $gpuMemoryValid = $gpuMemory.Count -gt 0 -and @($gpuMemory | Where-Object { $_.Status -gt 1 }).Count -eq 0
        $physicalEngines = @{}
        if ($gpuEngineValid) {
            foreach ($value in $gpuEngines) {
                $engine = $value.Instance -replace '^pid_\d+_', ''
                if (-not $physicalEngines.ContainsKey($engine)) { $physicalEngines[$engine] = 0.0 }
                $physicalEngines[$engine] += $value.Value
            }
        }
        $complete = @($processSamples | Where-Object { -not $_.complete }).Count -eq 0
        $cpuComplete = $complete -and @($processSamples | Where-Object { $null -eq $_.cpuMachinePct }).Count -eq 0
        $row = [pscustomobject][ordered]@{
            utc = (Get-Date).ToUniversalTime().ToString('o'); elapsedMs = $sampleStart
            phase = $phase; phaseElapsedMs = $phaseElapsed
            phaseMarkerTickMs = $phaseMarker.TickMs; phaseReadComplete = $phaseMarker.Complete; phaseReadError = $phaseMarker.Error
            acceptedSteadyWindow = $steadyWindow
            collectionMs = $timer.Elapsed.TotalMilliseconds - $sampleStart
            processCount = $processSamples.Count; complete = $complete; cpuComplete = $cpuComplete
            privateCommitBytes = ($processSamples | Measure-Object -Property privateCommitBytes -Sum).Sum
            privateWorkingSetBytes = if ($complete) { ($processSamples | Measure-Object -Property privateWorkingSetBytes -Sum).Sum } else { $null }
            cpuMachinePct = if ($cpuComplete) { ($processSamples | Measure-Object -Property cpuMachinePct -Sum).Sum } else { $null }
            gpuEngineCountersComplete = $gpuEngineValid
            gpuBusiestPhysicalEnginePct = if ($gpuEngineValid) { ($physicalEngines.Values | Measure-Object -Maximum).Maximum } else { $null }
            gpuDedicatedBytes = if ($gpuMemoryValid) { ($gpuMemory | Where-Object { $_.Kind -eq 'dedicated_bytes' } | Measure-Object -Property Value -Sum).Sum } else { $null }
            gpuSharedBytes = if ($gpuMemoryValid) { ($gpuMemory | Where-Object { $_.Kind -eq 'shared_bytes' } | Measure-Object -Property Value -Sum).Sum } else { $null }
            processes = @($processSamples.ToArray()); gpu = $gpuSample
        }
        $samples.Add($row)
        $row | ConvertTo-Json -Depth 10 -Compress | Add-Content -LiteralPath $samplePath -Encoding utf8
        if ($timer.Elapsed.TotalSeconds -gt $MaximumSeconds -and -not $launched.HasExited) { $timedOut = $true; break }
        $remaining = $IntervalMilliseconds - ($timer.Elapsed.TotalMilliseconds - $sampleStart)
        if ($remaining -gt 0 -and -not $launched.HasExited) { Start-Sleep -Milliseconds ([int]$remaining) }
    } while (-not $launched.HasExited)
    if (-not $launched.HasExited) { $launched.Kill(); $null = $launched.WaitForExit(3000) }
    foreach ($entry in @($known.GetEnumerator())) {
        if (-not $entry.Value.HasExited -and $entry.Key -ne $launched.Id) { $finalResidual += $identities.Values | Where-Object { $_.pid -eq $entry.Key } }
    }
    $phaseSummary = @()
    foreach ($group in ($samples | Group-Object -Property phase)) {
        $steady = @($group.Group | Where-Object { $_.acceptedSteadyWindow -and $_.complete })
        $cpuSteady = @($steady | Where-Object { $_.cpuComplete })
        $gpuSteady = @($steady | Where-Object { $_.gpuEngineCountersComplete })
        $gpuMemorySteady = @($steady | Where-Object { $null -ne $_.gpuDedicatedBytes })
        $roles = @()
        foreach ($role in (@($steady | ForEach-Object { $_.processes }) | Group-Object -Property name)) {
            $roles += [pscustomobject][ordered]@{
                processName = $role.Name
                observedPids = @($role.Group.pid | Sort-Object -Unique)
                meanPerProcessPrivateCommitMiB = [math]::Round(($role.Group | Measure-Object -Property privateCommitBytes -Average).Average / 1MB, 3)
                meanPerProcessPrivateWorkingSetMiB = [math]::Round(($role.Group | Measure-Object -Property privateWorkingSetBytes -Average).Average / 1MB, 3)
            }
        }
        $phaseSummary += [pscustomobject][ordered]@{
            phase = $group.Name; totalSamples = $group.Count; steadySamples = $steady.Count
            phaseMarkerReadFailures = @($group.Group | Where-Object { -not $_.phaseReadComplete }).Count
            privateCommitMiB = if ($steady.Count) { [math]::Round(($steady | Measure-Object -Property privateCommitBytes -Average).Average / 1MB, 3) } else { $null }
            privateWorkingSetMiB = if ($steady.Count) { [math]::Round(($steady | Measure-Object -Property privateWorkingSetBytes -Average).Average / 1MB, 3) } else { $null }
            cpuMachinePct = if ($cpuSteady.Count) { [math]::Round(($cpuSteady | Measure-Object -Property cpuMachinePct -Average).Average, 4) } else { $null }
            gpuBusiestPhysicalEnginePct = if ($gpuSteady.Count) { [math]::Round(($gpuSteady | Measure-Object -Property gpuBusiestPhysicalEnginePct -Average).Average, 4) } else { $null }
            gpuDedicatedMiB = if ($gpuMemorySteady.Count) { [math]::Round(($gpuMemorySteady | Measure-Object -Property gpuDedicatedBytes -Average).Average / 1MB, 3) } else { $null }
            gpuSharedMiB = if ($gpuMemorySteady.Count) { [math]::Round(($gpuMemorySteady | Measure-Object -Property gpuSharedBytes -Average).Average / 1MB, 3) } else { $null }
            minimumProcessCount = if ($steady.Count) { ($steady | Measure-Object -Property processCount -Minimum).Minimum } else { $null }
            maximumProcessCount = if ($steady.Count) { ($steady | Measure-Object -Property processCount -Maximum).Maximum } else { $null }
            processRoles = $roles
        }
    }
    $summary = [ordered]@{
        verificationOnly = $true; rootPid = $launched.Id; timedOut = $timedOut
        testExitCode = if ($launched.HasExited) { $launched.ExitCode } else { $null }
        durationSeconds = $timer.Elapsed.TotalSeconds
        warmupMilliseconds = $WarmupMilliseconds
        phaseAverages = $phaseSummary
        phaseAveragesAfterTwoSeconds = if ($WarmupMilliseconds -eq 2000) { $phaseSummary } else { $null }
        observedProcessIdentities = @($identities.Values)
        residualBeforeCleanup = $finalResidual
        rawSamples = $samplePath
    }
    $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'resource-summary.json') -Encoding utf8
    $phaseSummary | Format-Table -AutoSize
    Write-Output ('Evidence: ' + $OutputDirectory)
    if ($timedOut -or $finalResidual.Count -gt 0 -or $launched.ExitCode -ne 0) { throw "Isolated test failed, timed out, or left descendants; inspect resource-summary.json and test.stdout.log." }
} finally {
    # Process objects retain handles/identity; these are only children discovered
    # below this newly launched test. Never kill by executable name.
    foreach ($entry in @($known.GetEnumerator())) {
        try { if (-not $entry.Value.HasExited) { $entry.Value.Kill(); $null = $entry.Value.WaitForExit(2000) } } catch { }
        $entry.Value.Dispose()
    }
    if ($gpu) { $gpu.Dispose() }
}
