param(
    # Which "show desktop" to test: WinD (keyboard), Corner (click the taskbar's
    # bottom-right corner), or Both.
    [ValidateSet('WinD', 'Corner', 'Both')][string]$Method = 'Both',
    # Seconds to wait after showing the desktop before checking.
    [double]$Settle = 1.5
)
# Checks that Lume's Dock and desktop widgets stay visible on top of the
# desktop after Windows "show desktop" (Win+D, or a click on the taskbar's
# bottom-right corner). It really sends the input: open windows are minimized
# and then restored the same way. Writes show-desktop-report.txt and
# screenshots to the desktop. Nothing else is changed.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type -TypeDefinition @'
using System; using System.Collections.Generic; using System.Runtime.InteropServices; using System.Text;
public static class SD {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern IntPtr GetProp(IntPtr h, string n);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint f);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  public static string Class(IntPtr h) { var s = new StringBuilder(128); GetClassName(h, s, 128); return s.ToString(); }
  public static uint Pid(IntPtr h) { uint p; GetWindowThreadProcessId(h, out p); return p; }
  public static List<IntPtr> Top() { var l = new List<IntPtr>(); EnumWindows((h, x) => { l.Add(h); return true; }, IntPtr.Zero); return l; }
  public static void WinD() { keybd_event(0x5B, 0, 0, UIntPtr.Zero); keybd_event(0x44, 0, 0, UIntPtr.Zero);
    keybd_event(0x44, 0, 2, UIntPtr.Zero); keybd_event(0x5B, 0, 2, UIntPtr.Zero); }
  public static void Click(int x, int y) { SetCursorPos(x, y); System.Threading.Thread.Sleep(80);
    mouse_event(2, 0, 0, 0, UIntPtr.Zero); mouse_event(4, 0, 0, 0, UIntPtr.Zero); }
}
'@
[void][SD]::SetProcessDpiAwarenessContext([IntPtr](-4))  # per-monitor v2: real pixels

$report = New-Object System.Collections.Generic.List[string]
function Say([string]$t) { $report.Add($t); Write-Host $t }
$desktop = [Environment]::GetFolderPath('Desktop')

$shell = Get-Process LumeShell -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $shell) { Write-Host '请先启动 Lume，并打开 Dock 和至少一个桌面组件后再运行。'; exit 1 }
Say ("时间: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Say ("Lume: " + $shell.Path + "  版本 " + $shell.MainModule.FileVersionInfo.ProductVersion)
$os = Get-CimInstance Win32_OperatingSystem
Say ("系统: $($os.Caption) build $($os.BuildNumber)")
$corner = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced' -Name TaskbarSd -ErrorAction SilentlyContinue).TaskbarSd
Say ("任务栏角落显示桌面开关(TaskbarSd): " + $(if ($null -eq $corner) { '默认(开)' } elseif ($corner -eq 1) { '开' } else { '关' }))

# Lume desktop windows: the Dock icon layer and every desktop widget.
function LumeWindows {
    $list = @()
    foreach ($h in [SD]::Top()) {
        if ([SD]::Pid($h) -ne $shell.Id -or -not [SD]::IsWindowVisible($h) -or [SD]::IsIconic($h)) { continue }
        $cls = [SD]::Class($h)
        $isDock = $cls -eq 'LumeNativeDesktopDock'
        $isWidget = ([SD]::GetProp($h, 'LumeDesktopWidget') -ne [IntPtr]::Zero) -or ($cls -like 'LumeNativeDesktop*' -and $cls -notlike 'LumeNativeDesktopDock*')
        if (-not ($isDock -or $isWidget)) { continue }
        $r = New-Object SD+RECT; [void][SD]::GetWindowRect($h, [ref]$r)
        if ($r.R - $r.L -lt 20 -or $r.B - $r.T -lt 20) { continue }
        $list += [pscustomobject]@{ Handle = $h; Class = $cls; Rect = $r; Name = $(if ($isDock) { 'Dock' } else { $cls }) }
    }
    $list
}
# A window is really visible when points inside it hit Lume, not the desktop or another app.
function Covered($w) {
    $r = $w.Rect
    $points = if ($w.Name -eq 'Dock') {
        # The icon row sits in the lower part of the Dock window.
        foreach ($fx in 0.3, 0.5, 0.7) { ,@([int]($r.L + ($r.R - $r.L) * $fx), [int]($r.B - ($r.B - $r.T) * 0.35)) }
    } else {
        foreach ($f in @(0.5, 0.5), @(0.25, 0.3), @(0.75, 0.7)) { ,@([int]($r.L + ($r.R - $r.L) * $f[0]), [int]($r.T + ($r.B - $r.T) * $f[1])) }
    }
    $mine = 0; $owners = @()
    foreach ($p in $points) {
        $pt = New-Object SD+POINT; $pt.X = $p[0]; $pt.Y = $p[1]
        $hit = [SD]::GetAncestor([SD]::WindowFromPoint($pt), 2)
        if ([SD]::Pid($hit) -eq $shell.Id) { $mine++ } else {
            $proc = (Get-Process -Id ([SD]::Pid($hit)) -ErrorAction SilentlyContinue).ProcessName
            $owners += "$proc/$([SD]::Class($hit))"
        }
    }
    [pscustomobject]@{ Visible = ($mine -gt 0); By = ($owners | Select-Object -Unique) -join ', ' }
}
function Shot([string]$name) {
    $b = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.X, $b.Y, 0, 0, $bmp.Size)
    $path = Join-Path $desktop $name; $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose(); $path
}
function ShowDesktop([string]$how) {
    if ($how -eq 'WinD') { [SD]::WinD(); return }
    # The last pixel of the primary taskbar, at the screen's bottom-right.
    $s = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $old = New-Object SD+POINT; [void][SD]::GetCursorPos([ref]$old)
    [SD]::Click($s.Right - 1, $s.Bottom - 1)
    Start-Sleep -Milliseconds 100; [void][SD]::SetCursorPos($old.X, $old.Y)
}

$before = LumeWindows
if (-not $before) { Say '没有找到可见的 Dock 或桌面组件：请先在 Lume 中开启 Dock 和至少一个组件。'; exit 1 }
Say ("检查的窗口: " + (($before | ForEach-Object { $_.Name }) -join ', '))
Write-Host ''
Write-Host '接下来会模拟“显示桌面”，所有窗口会最小化，检查后自动恢复。5 秒后开始，期间请不要动鼠标键盘。'
Start-Sleep -Seconds 5

$methods = if ($Method -eq 'Both') { 'WinD', 'Corner' } else { , $Method }
$allOk = $true
foreach ($m in $methods) {
    $label = if ($m -eq 'WinD') { 'Win+D' } else { '单击任务栏右下角' }
    Say ''; Say "== $label =="
    ShowDesktop $m
    Start-Sleep -Seconds $Settle
    $fg = [SD]::GetForegroundWindow()
    Say ("前台窗口: " + [SD]::Class($fg) + " (" + (Get-Process -Id ([SD]::Pid($fg)) -ErrorAction SilentlyContinue).ProcessName + ")")
    $after = LumeWindows
    foreach ($w in $before) {
        $now = $after | Where-Object { $_.Handle -eq $w.Handle } | Select-Object -First 1
        if (-not $now) { Say ("  失败  $($w.Name): 窗口被隐藏或最小化"); $allOk = $false; continue }
        $c = Covered $now
        if ($c.Visible) { Say ("  通过  $($w.Name): 在桌面上可见") }
        else { Say ("  失败  $($w.Name): 被挡住 ($($c.By))"); $allOk = $false }
    }
    Say ("截图: " + (Shot "show-desktop-$m.png"))
    # Restore the windows the same way.
    ShowDesktop $m
    Start-Sleep -Seconds $Settle
}
Say ''
Say $(if ($allOk) { '结论: 通过，两种方式显示桌面后 Dock 和组件都留在桌面上。' } else { '结论: 有失败项，请把本报告和截图发给开发者。' })
$out = Join-Path $desktop 'show-desktop-report.txt'
[IO.File]::WriteAllLines($out, $report, (New-Object Text.UTF8Encoding $true))
Write-Host ''; Write-Host "报告已保存到桌面: $out"
