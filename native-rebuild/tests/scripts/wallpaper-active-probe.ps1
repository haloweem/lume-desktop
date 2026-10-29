param(
    [string]$InstallDirectory = 'D:\lume',
    [string]$VideoPath,
    [string]$OutputDirectory = [Environment]::GetFolderPath('Desktop')
)
$ErrorActionPreference = 'Stop'
# Does not edit product/system settings or send Explorer creation messages.
# Creates a hidden temporary window, reparents only that window, destroys it.
# Runs only the specified installed mpv; only probe-owned processes may be killed.
# No network access or upload. Does not replace the user's wallpaper.
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw 'Output directory does not exist.' }
$nativeSource = @'
using System;
using System.Runtime.InteropServices;
public static class LumeWallpaperActiveProbe {
    public delegate bool EnumProc(IntPtr w,IntPtr d);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
    public class Result {
        public string stage="discovery",candidate,host,error;
        public bool candidateFound,setParentOk,setPositionOk,boundsMatch,hostDestroyed;
        public int setParentError,setPositionError,boundsError;
        public uint hostDpi,parentDpi;
        public Rect expected,actual;
    }
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string c,string n);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr p,IntPtr a,string c,string n);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f,IntPtr d);
    [DllImport("user32.dll",EntryPoint="GetWindowLongW")] static extern int Long32(IntPtr w,int i);
    [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] static extern IntPtr Long64(IntPtr w,int i);
    [DllImport("user32.dll",EntryPoint="SetWindowLongW")] static extern int SetLong32(IntPtr w,int i,int v);
    [DllImport("user32.dll",EntryPoint="SetWindowLongPtrW")] static extern IntPtr SetLong64(IntPtr w,int i,IntPtr v);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateWindowEx(uint ex,string c,string n,uint style,int x,int y,int width,int height,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr data);
    [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SetParent(IntPtr w,IntPtr p);
    [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr w);
    [DllImport("kernel32.dll",EntryPoint="SetLastError")] static extern void ClearError(uint error);
    [DllImport("user32.dll",SetLastError=true)] static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int width,int height,uint flags);
    [DllImport("user32.dll",SetLastError=true)] static extern bool GetWindowRect(IntPtr w,out Rect r);
    [DllImport("user32.dll")] static extern int MapWindowPoints(IntPtr from,IntPtr to,ref Rect r,uint count);
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr w);
    [DllImport("user32.dll")] static extern bool DestroyWindow(IntPtr w);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    static long Style(IntPtr w) { return IntPtr.Size==8?Long64(w,-20).ToInt64():Long32(w,-20); }
    static string Hex(IntPtr w) { return "0x"+w.ToInt64().ToString("X"); }
    public static Result Run() {
        var r=new Result();IntPtr host=IntPtr.Zero,old=IntPtr.Zero;
        try {
            old=SetThreadDpiAwarenessContext(new IntPtr(-4));
            if(old==IntPtr.Zero){r.error="THREAD_DPI_CONTEXT_FAILED";return r;}
            var progman=FindWindow("Progman",null);
            if(progman==IntPtr.Zero){r.error="NO_PROGMAN";return r;}
            var shell=FindWindowEx(progman,IntPtr.Zero,"SHELLDLL_DefView",null);IntPtr worker=IntPtr.Zero;
            EnumWindows(delegate(IntPtr w,IntPtr d){var view=FindWindowEx(w,IntPtr.Zero,"SHELLDLL_DefView",null);
                if(view!=IntPtr.Zero){shell=view;worker=FindWindowEx(IntPtr.Zero,w,"WorkerW",null);}return true;},IntPtr.Zero);
            bool raised=(Style(progman)&0x00200000)!=0&&shell!=IntPtr.Zero&&FindWindowEx(progman,IntPtr.Zero,"WorkerW",null)!=IntPtr.Zero;
            var parent=raised?progman:worker;
            if(parent==IntPtr.Zero){r.error="NO_PASSIVE_CANDIDATE";return r;}
            r.candidateFound=true;r.candidate=Hex(parent);r.parentDpi=GetDpiForWindow(parent);
            Rect area;if(!GetWindowRect(parent,out area)){r.error="PARENT_RECT_FAILED";return r;}
            if(area.right<=area.left||area.bottom<=area.top){r.error="EMPTY_PARENT_BOUNDS";return r;}
            r.stage="create-hidden-window";
            // Hidden WS_CHILD plus the same clipping and tool/noactivate styles.
            // Unlike the product, no SWP_SHOWWINDOW is used: existing wallpaper remains visible.
            host=CreateWindowEx(0x08000080,"STATIC","Lume temporary attachment probe",0x46000000,
                0,0,1,1,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero);
            if(host==IntPtr.Zero){
                // WS_CHILD requires a parent at creation. Product starts as WS_POPUP.
                host=CreateWindowEx(0x08000080,"STATIC","Lume temporary attachment probe",0x86000000,
                    -32000,-32000,1,1,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero);
            }
            if(host==IntPtr.Zero){r.error="HOST_CREATE_FAILED:"+Marshal.GetLastWin32Error();return r;}
            r.host=Hex(host);r.hostDpi=GetDpiForWindow(host);r.stage="set-parent";
            if(IntPtr.Size==8)SetLong64(host,-16,new IntPtr(0x46000000));else SetLong32(host,-16,0x46000000);
            ClearError(0);SetParent(host,parent);r.setParentError=Marshal.GetLastWin32Error();
            r.setParentOk=GetParent(host)==parent;
            if(!r.setParentOk){r.error="SET_PARENT_FAILED";return r;}
            r.expected=new Rect{left=0,top=0,right=area.right-area.left,bottom=area.bottom-area.top};
            r.stage="set-position";
            r.setPositionOk=SetWindowPos(host,IntPtr.Zero,0,0,r.expected.right,r.expected.bottom,0x0034);
            r.setPositionError=r.setPositionOk?0:Marshal.GetLastWin32Error();
            if(!r.setPositionOk){r.error="SET_POSITION_FAILED";return r;}
            Rect actual;if(!GetWindowRect(host,out actual)){r.boundsError=Marshal.GetLastWin32Error();r.error="HOST_RECT_FAILED";return r;}
            MapWindowPoints(IntPtr.Zero,parent,ref actual,2);r.actual=actual;
            r.boundsMatch=actual.left==0&&actual.top==0&&actual.right==r.expected.right&&actual.bottom==r.expected.bottom;
            r.stage="complete";if(!r.boundsMatch)r.error="BOUNDS_MISMATCH";
        } catch(Exception e){r.error=e.GetType().Name+":"+e.Message;}
        finally {if(host!=IntPtr.Zero)r.hostDestroyed=DestroyWindow(host);if(old!=IntPtr.Zero)SetThreadDpiAwarenessContext(old);}
        return r;
    }
}
'@
if (-not ('LumeWallpaperActiveProbe' -as [type])) { Add-Type -TypeDefinition $nativeSource }
$stamp=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6)
$prefix=Join-Path $OutputDirectory ('Lume-Wallpaper-Active-'+$stamp)
function Invoke-PlayerProbe([string]$Name,[string[]]$Arguments) {
    $result=[ordered]@{name=$Name;started=$false;timedOut=$false;exitCode=$null;error=$null;log=$null}
    $process=$null
    try {
        $info=New-Object System.Diagnostics.ProcessStartInfo
        $info.FileName=$mpv;$info.WorkingDirectory=Split-Path -Parent $mpv
        $info.UseShellExecute=$false;$info.CreateNoWindow=$true
        $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
        $info.Arguments=($Arguments -join ' ')
        $process=[Diagnostics.Process]::Start($info);$result.started=$true
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit(20000)){$result.timedOut=$true;$process.Kill();[void]$process.WaitForExit(5000)}
        if($process.HasExited){$result.exitCode=$process.ExitCode}
        if($stdout.IsCompleted -and $stderr.IsCompleted){
            $log=$stdout.Result+[Environment]::NewLine+$stderr.Result
            if($VideoPath){$log=$log.Replace($VideoPath,'[VIDEO]')}
            if($env:USERPROFILE){$log=$log.Replace($env:USERPROFILE,'[USERPROFILE]')}
            $result.log=$prefix+'-'+$Name+'.txt'
            $log | Set-Content -LiteralPath $result.log -Encoding UTF8
        }
    } catch {$result.error=$_.Exception.Message}
    finally {
        if($process){try{if(-not $process.HasExited){$process.Kill();[void]$process.WaitForExit(5000)}}finally{$process.Dispose()}}
    }
    return [pscustomobject]$result
}
$mpv=Join-Path $InstallDirectory 'wallpaper-runtime\mpv.exe'
$attachment=[LumeWallpaperActiveProbe]::Run()
$checks=@()
if(Test-Path -LiteralPath $mpv -PathType Leaf){
    $checks+=Invoke-PlayerProbe 'version' @('--no-config','--version')
    if($VideoPath){
        if(-not(Test-Path -LiteralPath $VideoPath -PathType Leaf)){throw 'VideoPath does not exist.'}
        $VideoPath=(Resolve-Path -LiteralPath $VideoPath).Path
        if($VideoPath.Contains('"')){throw 'Invalid quote in video path.'}
        # Decode a few frames offscreen. Does NOT test D3D11 presentation or protected loopback media.
        $checks+=Invoke-PlayerProbe 'software-decode' @('--no-config','--load-scripts=no','--ytdl=no','--audio=no','--vo=null','--hwdec=no','--frames=3','--',('"'+$VideoPath+'"'))
    }
}
$report=[ordered]@{schema='lume-wallpaper-active-v1';createdUtc=[DateTime]::UtcNow.ToString('o');
    attachment=$attachment;playerExists=(Test-Path -LiteralPath $mpv -PathType Leaf);playerChecks=$checks;
    limitations=@('Hidden temporary window: no visible desktop rendering tested.','No Explorer creation message or z-order changes.','Software decode is not embedded GPU rendering.','No protected-media loopback or Lume Job/IPC test.','A passing probe does not prove the entire wallpaper path works.')}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath ($prefix+'.json') -Encoding UTF8
Write-Host ('REPORT='+$prefix+'.json')
Write-Host ('ATTACH_STAGE='+$attachment.stage+' ERROR='+$attachment.error+' CLEANUP='+$attachment.hostDestroyed)
Write-Host 'Send the JSON and probe TXT logs after reviewing them. No product settings changed.'
