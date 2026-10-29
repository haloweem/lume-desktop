$ErrorActionPreference = 'Stop'

# Read-only taskbar transparency diagnostic for Windows 10/11.
# It never calls SetWindowCompositionAttribute, changes Explorer, or writes
# settings. The only output file is the JSON report beside this script.

$nativeSource = @'
using System;
using System.Runtime.InteropServices;

public static class LumeTaskbarDiagnosticNative20261018 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct APPBARDATA {
        public uint cbSize; public IntPtr hWnd; public uint uCallbackMessage; public uint uEdge;
        public RECT rc; public int lParam;
    }
    [StructLayout(LayoutKind.Sequential)] public struct ACCENT_POLICY {
        public int AccentState; public int AccentFlags; public int GradientColor; public int AnimationId;
    }
    [StructLayout(LayoutKind.Sequential)] public struct WINDOWCOMPOSITIONATTRIBDATA {
        public int Attrib; public IntPtr Data; public UIntPtr Size;
    }
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr FindWindow(string className, string windowName);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr childAfter, string className, string windowName);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll", SetLastError=true)]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", EntryPoint="GetWindowCompositionAttribute", SetLastError=true)]
    public static extern bool GetWindowCompositionAttribute(IntPtr hwnd, ref WINDOWCOMPOSITIONATTRIBDATA data);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll", CharSet=CharSet.Ansi, SetLastError=true)]
    public static extern IntPtr GetProcAddress(IntPtr module, string name);
    [DllImport("dwmapi.dll", SetLastError=true)]
    public static extern int DwmIsCompositionEnabled(out bool enabled);
    [DllImport("shell32.dll", SetLastError=true)]
    public static extern uint SHAppBarMessage(uint message, ref APPBARDATA data);
}
'@

try {
    Add-Type -TypeDefinition $nativeSource -Language CSharp
} catch [System.Management.Automation.AddTypeAlreadyExistsException] {
    # The script is allowed to be run more than once in the same PowerShell.
}

$native = [LumeTaskbarDiagnosticNative20261018]
$reportPath = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) ("taskbar-diagnostic-{0}.json" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

function Hex([IntPtr] $value) { return ('0x{0:X}' -f $value.ToInt64()) }

function Get-WindowInfo([IntPtr] $hwnd, [string] $className) {
    if ($hwnd -eq [IntPtr]::Zero) { return $null }
    $rect = [Activator]::CreateInstance($native.GetNestedType('RECT'))
    $processId = [uint32]0
    [void]$native::GetWindowRect($hwnd, [ref]$rect)
    [void]$native::GetWindowThreadProcessId($hwnd, [ref]$processId)
    [pscustomobject]@{
        class = $className
        hwnd = (Hex $hwnd)
        pid = $processId
        visible = [bool]$native::IsWindowVisible($hwnd)
        rect = @{ left=$rect.left; top=$rect.top; right=$rect.right; bottom=$rect.bottom }
    }
}

function Get-TaskbarWindows {
    $result = @()
    $primary = $native::FindWindow('Shell_TrayWnd', $null)
    if ($primary -ne [IntPtr]::Zero) { $result += Get-WindowInfo $primary 'Shell_TrayWnd' }
    $after = [IntPtr]::Zero
    while ($true) {
        $secondary = $native::FindWindowEx([IntPtr]::Zero, $after, 'Shell_SecondaryTrayWnd', $null)
        if ($secondary -eq [IntPtr]::Zero) { break }
        $result += Get-WindowInfo $secondary 'Shell_SecondaryTrayWnd'
        $after = $secondary
    }
    return @($result)
}

function Read-Accent([IntPtr] $hwnd) {
    $result = [ordered]@{ apiCallable=$false; getError=$null; accent=$null }
    try {
        $policy = [Activator]::CreateInstance($native.GetNestedType('ACCENT_POLICY'))
        $size = [Runtime.InteropServices.Marshal]::SizeOf($policy)
        $memory = [Runtime.InteropServices.Marshal]::AllocHGlobal($size)
        try {
            [Runtime.InteropServices.Marshal]::StructureToPtr($policy, $memory, $false)
            $data = [Activator]::CreateInstance($native.GetNestedType('WINDOWCOMPOSITIONATTRIBDATA'))
            $data.Attrib = 19 # WCA_ACCENT_POLICY
            $data.Data = $memory
            $data.Size = [UIntPtr]::new([uint64]$size)
            $ok = $native::GetWindowCompositionAttribute($hwnd, [ref]$data)
            $result.apiCallable = $true
            if ($ok) {
                $actual = [Runtime.InteropServices.Marshal]::PtrToStructure($memory, $native.GetNestedType('ACCENT_POLICY'))
                $result.accent = [ordered]@{
                    state=$actual.AccentState; flags=$actual.AccentFlags
                    gradientColor=('0x{0:X8}' -f ([uint32]$actual.GradientColor)); animation=$actual.AnimationId
                }
            } else { $result.getError = [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
        } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($memory) }
    } catch [EntryPointNotFoundException] { $result.getError = 'EntryPointNotFound' }
      catch { $result.getError = $_.Exception.Message }
    return [pscustomobject]$result
}

$taskbars = @(Get-TaskbarWindows)
$composition = [ordered]@{ dwmEnabled=$null; hresult=$null }
$dwmEnabled = $false
$composition.hresult = $native::DwmIsCompositionEnabled([ref]$dwmEnabled)
$composition.dwmEnabled = $dwmEnabled

 $appbar = [Activator]::CreateInstance($native.GetNestedType('APPBARDATA'))
$appbar.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($appbar)
$appbarState = $native::SHAppBarMessage(4, [ref]$appbar) # ABM_GETSTATE

$user32 = $native::GetModuleHandle('user32.dll')
$api = [ordered]@{
    getWindowCompositionAttribute = (Hex ($native::GetProcAddress($user32, 'GetWindowCompositionAttribute')))
    setWindowCompositionAttribute = (Hex ($native::GetProcAddress($user32, 'SetWindowCompositionAttribute')))
}

$conflicts = @(@('TranslucentTB','ExplorerPatcher','StartAllBack','LumeTaskbarCore') | ForEach-Object {
    Get-Process -Name $_ -ErrorAction SilentlyContinue | ForEach-Object {
        [pscustomobject]@{ name=$_.ProcessName; id=$_.Id; path=$_.Path }
    }
})

$explorer = @(Get-Process explorer -ErrorAction SilentlyContinue | ForEach-Object {
    [pscustomobject]@{ id=$_.Id; responding=$_.Responding; path=$_.Path }
})
$gpu = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | ForEach-Object {
    [pscustomobject]@{ name=$_.Name; driverVersion=$_.DriverVersion; status=$_.Status }
})
$lumeStatePath = Join-Path $env:LOCALAPPDATA 'Lume	askbar-state.json'
$lumeState = $null
$lumeStatePath = Join-Path (Join-Path $env:LOCALAPPDATA 'Lume') 'taskbar-state.json'
if (Test-Path -LiteralPath $lumeStatePath) {
    try {
        $raw = Get-Content -LiteralPath $lumeStatePath -Raw | ConvertFrom-Json
        $lumeState = [ordered]@{
            exists=$true; lastWrite=(Get-Item -LiteralPath $lumeStatePath).LastWriteTime.ToString('o')
            enabled=$raw.enabled; transparentTaskbar=$raw.transparentTaskbar
            autoHideTaskbar=$raw.autoHideTaskbar; ownsCore=$raw.ownsCore
            appearances=if($raw.appearances){$raw.appearances.Count}else{0}; pendingRecovery=($raw.ownsCore -or $raw.appearances.Count -gt 0)
        }
    } catch { $lumeState = [ordered]@{ exists=$true; parseError=$_.Exception.Message } }
} else { $lumeState = [ordered]@{ exists=$false } }

$accent = @()
foreach ($taskbar in $taskbars) {
    $hwndText = $taskbar.hwnd
    $hwnd = [IntPtr]::new([Convert]::ToInt64($hwndText.Substring(2),16))
    $accent += [pscustomobject]@{ class=$taskbar.class; hwnd=$taskbar.hwnd; result=(Read-Accent $hwnd) }
}

$report = [ordered]@{
    generatedAt=(Get-Date).ToUniversalTime().ToString('o')
    powershell=$PSVersionTable.PSVersion.ToString(); os=(Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber,OSArchitecture)
    remoteSession=([bool]([Environment]::GetEnvironmentVariable('SESSIONNAME') -like 'RDP-*'))
    sessionName=[Environment]::GetEnvironmentVariable('SESSIONNAME')
    isAdministrator=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    composition=$composition; api=$api; taskbars=$taskbars; accent=$accent
    appbarState=$appbarState; explorer=$explorer; gpu=$gpu; conflicts=$conflicts; lumeState=$lumeState
    likelyCauses=@(
        if (!$taskbars.Count) { 'taskbar-window-not-found' }
        if ($composition.dwmEnabled -eq $false) { 'dwm-disabled' }
        if ($conflicts.Count) { 'other-taskbar-tool-or-core-conflict' }
        if ($accent | Where-Object {$_.result.getError -eq 87}) { 'win10-classic-accent-read-rejected-error-87-compatible-fallback' }
        elseif ($accent | Where-Object {$_.result.apiCallable -eq $false -or $null -ne $_.result.getError}) { 'composition-api-unavailable-or-read-failed' }
        if ($remoteSession) { 'remote-session-rendering-may-change-composition' }
    )
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Output "REPORT=$reportPath"
Write-Output ("TASKBARS={0} DWM={1} API_GET={2} CONFLICTS={3}" -f $taskbars.Count,$composition.dwmEnabled,$api.getWindowCompositionAttribute,$conflicts.Count)
