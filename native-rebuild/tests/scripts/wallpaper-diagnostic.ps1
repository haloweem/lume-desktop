param(
    [string]$InstallDirectory,
    [string]$OutputDirectory = [Environment]::GetFolderPath("Desktop"),
    [ValidateRange(1,10)][int]$Samples = 3
)
$ErrorActionPreference = "Stop"
# Passive only. No Explorer messages, SetParent, playback, settings changes,
# process termination, network requests or automatic uploads. Writes one report.
$source = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class LumeWallpaperPassiveProbe {
    public delegate bool EnumProc(IntPtr w, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
    public class Window {
        public string handle,parent,className; public uint processId,dpi;
        public bool visible,boundsRead; public long extendedStyle; public Rect bounds;
    }
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f,IntPtr d);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr w,EnumProc f,IntPtr d);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w,StringBuilder b,int n);
    [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr w);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr w,out Rect r);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint p);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr w);
    [DllImport("user32.dll",EntryPoint="GetWindowLongW")] static extern int GetLong32(IntPtr w,int i);
    [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] static extern IntPtr GetLong64(IntPtr w,int i);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string c,string n);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr p,IntPtr a,string c,string n);
    static long Style(IntPtr w) { return IntPtr.Size==8?GetLong64(w,-20).ToInt64():GetLong32(w,-20); }
    static string Hex(IntPtr w) { return "0x"+w.ToInt64().ToString("X"); }
    static string Class(IntPtr w) { var b=new StringBuilder(256);GetClassName(w,b,256);return b.ToString(); }
    public static Window[] Snapshot() {
        var rows=new List<Window>();var seen=new HashSet<IntPtr>();IntPtr old=IntPtr.Zero;
        try {
            try { old=SetThreadDpiAwarenessContext(new IntPtr(-4)); } catch(EntryPointNotFoundException) {}
            EnumProc collect=delegate(IntPtr w,IntPtr d) {
                var c=Class(w);
                if(c!="Progman"&&c!="WorkerW"&&c!="SHELLDLL_DefView"&&c!="Lume.Native.WallpaperHost"&&c!="mpv")return true;
                if(!seen.Add(w))return true;
                uint pid;Rect rect;GetWindowThreadProcessId(w,out pid);bool ok=GetWindowRect(w,out rect);uint dpi=0;
                try { dpi=GetDpiForWindow(w); } catch(EntryPointNotFoundException) {}
                rows.Add(new Window{handle=Hex(w),parent=Hex(GetParent(w)),className=c,processId=pid,
                    visible=IsWindowVisible(w),boundsRead=ok,bounds=rect,dpi=dpi,extendedStyle=Style(w)});return true;
            };
            EnumWindows(delegate(IntPtr w,IntPtr d){collect(w,d);
                if(Class(w)=="Progman"||Class(w)=="WorkerW"||Class(w)=="Lume.Native.WallpaperHost")EnumChildWindows(w,collect,d);return true;},IntPtr.Zero);
            return rows.ToArray();
        } finally { if(old!=IntPtr.Zero)SetThreadDpiAwarenessContext(old); }
    }
    public static string Candidate() {
        var p=FindWindow("Progman",null);if(p==IntPtr.Zero)return "NO_PROGMAN";
        var shell=FindWindowEx(p,IntPtr.Zero,"SHELLDLL_DefView",null);IntPtr worker=IntPtr.Zero;
        EnumWindows(delegate(IntPtr w,IntPtr d){var v=FindWindowEx(w,IntPtr.Zero,"SHELLDLL_DefView",null);
            if(v!=IntPtr.Zero){shell=v;worker=FindWindowEx(IntPtr.Zero,w,"WorkerW",null);}return true;},IntPtr.Zero);
        if((Style(p)&0x00200000)!=0&&shell!=IntPtr.Zero&&FindWindowEx(p,IntPtr.Zero,"WorkerW",null)!=IntPtr.Zero)return "PROGMAN:"+Hex(p);
        return worker==IntPtr.Zero?"NO_PASSIVE_CANDIDATE":"WORKERW:"+Hex(worker);
    }
}
'@
if(-not ("LumeWallpaperPassiveProbe" -as [type])) { Add-Type -TypeDefinition $source }
$warnings=New-Object System.Collections.Generic.List[string]
function Read-Cim([string]$Class,[string[]]$Fields) {
    try { Get-CimInstance -ClassName $Class | Select-Object -Property $Fields }
    catch { $warnings.Add("CIM_UNAVAILABLE:"+$Class) }
}
$processes=@(Get-Process -Name LumeShell,mpv,explorer -ErrorAction SilentlyContinue | ForEach-Object {
    $directory=$null;$version=$null
    try { $directory=Split-Path -Parent $_.Path;$version=$_.MainModule.FileVersionInfo.FileVersion } catch {}
    [pscustomobject]@{name=$_.ProcessName;id=$_.Id;session=$_.SessionId;version=$version;directory=$directory}
})
$folders=@($InstallDirectory)+@($processes | Where-Object name -eq "LumeShell" | ForEach-Object directory)
foreach($key in @("HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*","HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*","HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*")) {
    $folders+=@(Get-ItemProperty $key -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -match "^Lume(?:\s|$)" } | ForEach-Object InstallLocation)
}
$folders=@($folders | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } | Select-Object -Unique)
$runtime=@(foreach($folder in $folders) {
    foreach($name in @("LumeShell.exe","wallpaper-runtime\mpv.exe","wallpaper-runtime\d3dcompiler_43.dll")) {
        $path=Join-Path $folder $name;$exists=Test-Path -LiteralPath $path -PathType Leaf;$size=$null;$hash=$null
        if($exists){try{$size=(Get-Item -LiteralPath $path).Length;$hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}catch{$warnings.Add("FILE_READ_FAILED:"+$name)}}
        [pscustomobject]@{directory=$folder;file=$name;exists=$exists;bytes=$size;sha256=$hash}
    }
})
$desktop=@(for($i=0;$i -lt $Samples;$i++) {
    [pscustomobject]@{at=[DateTime]::UtcNow.ToString("o");passiveCandidate=[LumeWallpaperPassiveProbe]::Candidate();windows=@([LumeWallpaperPassiveProbe]::Snapshot())}
    if($i+1 -lt $Samples){Start-Sleep -Milliseconds 600}
})
$codes=@();$readStatus="missing";$diagnostic=Join-Path $env:LOCALAPPDATA "Lume\diagnostics.json"
if(Test-Path -LiteralPath $diagnostic -PathType Leaf) {
    try {
        if((Get-Item -LiteralPath $diagnostic).Length -gt 4MB){$readStatus="too-large"}
        else {
            $raw=[IO.File]::ReadAllText($diagnostic)
            $codes=@([regex]::Matches($raw,"\b(?:MPV_[A-Z0-9_]+|WALLPAPER_(?:PLAYER|MEDIA|PROTECTION|PROTECTED_FILE|SYSTEM_EFFECTS)[A-Z0-9_]*)\b") | ForEach-Object Value | Select-Object -Unique)
            $readStatus="read"
        }
    } catch {$readStatus="unreadable"}
}
$report=[ordered]@{
    schema="lume-wallpaper-readonly-v1";createdUtc=[DateTime]::UtcNow.ToString("o");
    limitations=@("Passive observation only; does not test SetParent or playback.","NO_PASSIVE_CANDIDATE is inconclusive: no desktop creation message is sent.","Error codes are historical, not necessarily from the latest attempt.","Window existence does not prove pixels are rendering.");
    os=@(Read-Cim "Win32_OperatingSystem" @("Caption","Version","BuildNumber","OSArchitecture"));
    computer=@(Read-Cim "Win32_ComputerSystem" @("Manufacturer","Model"));
    gpu=@(Read-Cim "Win32_VideoController" @("Name","DriverVersion","DriverDate","VideoModeDescription","Status"));
    processArchitecture=([IntPtr]::Size*8);processes=@($processes | Select-Object name,id,session,version);
    runtime=$runtime;desktop=$desktop;diagnosticRead=$readStatus;historicalErrorCodes=$codes;warnings=@($warnings);
    privacy="No titles, command lines, credentials or raw logs. Installation paths may contain a username; review before sharing."
}
if(-not(Test-Path -LiteralPath $OutputDirectory -PathType Container)){throw "OutputDirectory must already exist."}
$file=Join-Path $OutputDirectory ("Lume-Wallpaper-Diagnostic-"+(Get-Date -Format "yyyyMMdd-HHmmss")+"-"+[Guid]::NewGuid().ToString("N").Substring(0,6)+".json")
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $file -Encoding UTF8
Write-Host ("REPORT="+$file)
Write-Host "Read-only collection completed. No system settings changed. Review JSON before sharing."
