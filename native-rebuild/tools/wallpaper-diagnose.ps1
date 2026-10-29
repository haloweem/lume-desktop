param(
    # Optional: a video that should work (for example the MP4 the user dragged in).
    [string]$Video = '',
    # Lume install folder; found from the uninstall registry when omitted.
    [string]$InstallDir = '',
    # Also end leftover wallpaper players started from the Lume folder.
    [switch]$StopLeftovers
)
# Lume dynamic wallpaper diagnosis. Read-only, except that it starts short
# test runs of Lume's own mpv.exe (ended by the script) and, only with
# -StopLeftovers, ends mpv processes started from the Lume folder.
# Writes a report to the desktop: lume-wallpaper-report.txt
$ErrorActionPreference = 'Continue'
$report = New-Object System.Collections.Generic.List[string]
function Say([string]$text) { $report.Add($text); Write-Host $text }
function Section([string]$title) { Say ''; Say "== $title ==" }

Section '基本信息'
Say ("时间: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
$os = Get-CimInstance Win32_OperatingSystem
Say ("系统: $($os.Caption) $($os.Version) build $($os.BuildNumber)")
foreach ($gpu in Get-CimInstance Win32_VideoController) { Say ("显卡: $($gpu.Name) 驱动 $($gpu.DriverVersion)") }

if (-not $InstallDir) {
    foreach ($root in 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall', 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall') {
        $hit = Get-ChildItem $root -ErrorAction SilentlyContinue | Get-ItemProperty -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -like 'Lume*' -and $_.InstallLocation } | Select-Object -First 1
        if ($hit) { $InstallDir = $hit.InstallLocation.TrimEnd('\'); Say ("Lume 版本: $($hit.DisplayVersion)"); break }
    }
}
if (-not $InstallDir) { $InstallDir = 'D:\lume' }
Say "安装目录: $InstallDir"
$mpv = Join-Path $InstallDir 'wallpaper-runtime\mpv.exe'

Section '1. 播放器文件'
if (Test-Path -LiteralPath $mpv) {
    $file = Get-Item -LiteralPath $mpv
    Say ("mpv.exe 存在，大小 {0:N0} 字节，修改时间 {1}" -f $file.Length, $file.LastWriteTime)
    $hash = (Get-FileHash -LiteralPath $mpv -Algorithm SHA256).Hash
    Say "SHA-256: $hash"
    $zone = Get-Content -LiteralPath $mpv -Stream Zone.Identifier -ErrorAction SilentlyContinue
    if ($zone) { Say '注意: mpv.exe 带有“来自网络”标记' }
} else {
    Say '失败: mpv.exe 不存在。很可能被安全软件隔离或删除，请到安全软件的隔离区恢复并加入白名单，或重新安装 Lume。'
}

Section '2. 残留的壁纸播放器进程'
$players = Get-CimInstance Win32_Process -Filter "Name='mpv.exe'" -ErrorAction SilentlyContinue
$ours = @($players | Where-Object { $_.ExecutablePath -and $_.ExecutablePath -like "$InstallDir*" })
Say ("Lume 的 mpv 进程数: " + $ours.Count)
foreach ($p in $ours) {
    $age = [int]((Get-Date) - $p.CreationDate).TotalMinutes
    $media = if ($p.CommandLine -match '127\.0\.0\.1') { '下载壁纸(本机连接)' } elseif ($p.CommandLine) { '本地文件或空闲' } else { '未知' }
    Say ("  PID $($p.ProcessId)，已运行 $age 分钟，$media")
}
if ($ours.Count -gt 1) { Say '注意: 同时存在多个播放器，说明之前的取消/切换没有结束旧播放器。' }
if ($StopLeftovers -and $ours.Count) {
    foreach ($p in $ours) { Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
    Say '已结束上述播放器进程。请重新打开 Lume 后再试一次应用壁纸。'
}

Section '3. 安全软件与网络工具'
$av = Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct -ErrorAction SilentlyContinue
foreach ($a in $av) { Say ("安全软件: $($a.displayName)") }
if (-not $av) { Say '未读到安全中心登记的安全软件' }
$known = '360|ZhuDongFangYu|HipsDaemon|HipsTray|wsctrl|QQPCRTP|QQPCTray|kxetray|kxescore|avp|ksde|Clash|v2ray|xray|sing-box|nekoray|UU|Leigod|xunyou|qiyou|GameViewer'
$tools = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match $known } | Select-Object -ExpandProperty ProcessName -Unique
if ($tools) { Say ("正在运行的安全/加速/代理工具: " + ($tools -join ', ')) } else { Say '未发现常见安全/加速/代理工具进程' }

# Small loopback server like Lume's protected media server (Range support, one thread per connection).
Add-Type -TypeDefinition @'
using System; using System.IO; using System.Net; using System.Net.Sockets; using System.Text; using System.Threading;
public sealed class LumeProbeServer {
    readonly TcpListener listener; readonly string path; public int Port; public int Requests;
    public LumeProbeServer(string file) { path = file; listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        Port = ((IPEndPoint)listener.LocalEndpoint).Port; new Thread(Loop) { IsBackground = true }.Start(); }
    public void Stop() { try { listener.Stop(); } catch {} }
    void Loop() { while (true) { TcpClient c; try { c = listener.AcceptTcpClient(); } catch { return; }
        new Thread(() => Serve(c)) { IsBackground = true }.Start(); } }
    void Serve(TcpClient c) { try { using (c) { var s = c.GetStream(); var head = new StringBuilder(); var b = new byte[1];
        while (!head.ToString().EndsWith("\r\n\r\n") && s.Read(b, 0, 1) == 1) head.Append((char)b[0]);
        Interlocked.Increment(ref Requests);
        long size = new FileInfo(path).Length, begin = 0, end = size - 1; bool range = false;
        foreach (var line in head.ToString().Split(new[] { "\r\n" }, StringSplitOptions.None))
            if (line.StartsWith("Range: bytes=", StringComparison.OrdinalIgnoreCase)) { range = true;
                var v = line.Substring(13).Split('-'); if (v[0] != "") begin = long.Parse(v[0]); if (v.Length > 1 && v[1] != "") end = Math.Min(long.Parse(v[1]), size - 1); }
        var h = (range ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n") + "Connection: close\r\nAccept-Ranges: bytes\r\nContent-Type: video/mp4\r\nContent-Length: " + (end - begin + 1) + "\r\n" +
            (range ? "Content-Range: bytes " + begin + "-" + end + "/" + size + "\r\n" : "") + "\r\n";
        var hb = Encoding.ASCII.GetBytes(h); s.Write(hb, 0, hb.Length);
        using (var f = File.OpenRead(path)) { f.Seek(begin, SeekOrigin.Begin); var buf = new byte[65536]; long left = end - begin + 1;
            while (left > 0) { int n = f.Read(buf, 0, (int)Math.Min(buf.Length, left)); if (n <= 0) break; s.Write(buf, 0, n); left -= n; } } } } catch {} }
}
'@

# Decodes the first frames the way Lume starts mpv, without showing a window.
# mpv keeps its log open for writing: read it without asking for exclusive access.
function Read-Shared([string]$path) {
    try {
        $stream = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite, Delete')
        try { return (New-Object IO.StreamReader($stream)).ReadToEnd() } finally { $stream.Dispose() }
    } catch { return '' }
}
function Test-Play([string]$media, [string]$label, [string]$hwdec = 'auto-safe') {
    $log = Join-Path $env:TEMP ("lume-mpv-" + [guid]::NewGuid().ToString('N') + '.log')
    $mpvArgs = @('--no-config', '--no-terminal', '--audio=no', '--load-scripts=no', '--scripts=', '--ytdl=no', '--autoload-files=no', '--access-references=no', '--vo=null', '--frames=30', "--hwdec=$hwdec",
              '--demuxer-lavf-o=protocol_whitelist=[file,http,tcp],max_redirects=0,http_proxy=[]',
              '--stream-lavf-o=protocol_whitelist=[file,http,tcp],max_redirects=0,http_proxy=[]',
              '--msg-level=all=info', "--log-file=$log", $media)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $proc = Start-Process -FilePath $mpv -ArgumentList ($mpvArgs | ForEach-Object { '"' + $_ + '"' }) -PassThru -WindowStyle Hidden
    # Judge by what mpv logs, not by its exit: on some systems mpv lingers
    # after a successful playback instead of quitting.
    $text = ''; $decoded = $false; $finished = $false
    while ($watch.Elapsed.TotalSeconds -lt 20) {
        Start-Sleep -Milliseconds 200
        $text = if (Test-Path $log) { (Read-Shared $log) } else { '' }
        $decoded = $text -match 'finished playback, success|\[vo/null\] reconfig to'
        $finished = $proc.HasExited -or $decoded
        if ($finished) { break }
    }
    $seconds = [math]::Round($watch.Elapsed.TotalSeconds, 1)
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 300 }
    if (Test-Path $log) { $text = (Read-Shared $log) }
    $codec = if ($text -match '\[vd\] Codec: ([^\r\n]+)') { $Matches[1] } elseif ($text -match 'Video --vid=1[^\r\n]*\(([^)]*)\)') { $Matches[1] } else { '' }
    $hw = if ($text -match 'Using hardware decoding \(([^)]*)\)') { "硬件解码 $($Matches[1])" } elseif ($text) { '软件解码' } else { '' }
    if ($decoded) { Say "${label}: 成功，$seconds 秒内解出画面（$codec $hw）"; $ok = $true }
    elseif ($finished) { Say "${label}: 失败，播放器退出（退出码 $($proc.ExitCode)），用时 $seconds 秒（$codec $hw）"; $ok = $false }
    else { Say "${label}: 失败，20 秒内没有解出画面（$codec $hw）"; $ok = $false }
    if (-not $ok -and $text) {
        # Where it stopped: the last lines mpv wrote.
        $lines = ($text -split "`r?`n") | Where-Object { $_ -and $_ -notmatch 'Configuration:' } | Select-Object -Last 15
        foreach ($l in $lines) { Say "    mpv: $l" }
    }
    Remove-Item $log -ErrorAction SilentlyContinue
    return @{ ok = $ok; seconds = $seconds }
}

Section '4. 播放器能否启动'
$canStart = $false
if (Test-Path -LiteralPath $mpv) {
    $proc = Start-Process -FilePath $mpv -ArgumentList '--version' -PassThru -WindowStyle Hidden
    if ($proc.WaitForExit(10000) -and $proc.ExitCode -eq 0) { Say '成功: mpv.exe 可以启动'; $canStart = $true }
    else { if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }; Say '失败: mpv.exe 无法启动或被拦截（安全软件常见）。请把 Lume 安装目录加入白名单。' }
}

Section '5. 播放测试视频'
if (-not $Video) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = '选择一个 MP4 视频用于测试（比如之前拖进 Lume 的那个）'
    $dialog.Filter = '视频 (*.mp4;*.webm;*.mkv)|*.mp4;*.webm;*.mkv'
    if ($dialog.ShowDialog() -eq 'OK') { $Video = $dialog.FileName }
}
if ($canStart -and $Video -and (Test-Path -LiteralPath $Video)) {
    Say ("测试视频: {0}（{1:N1} MB）" -f (Split-Path $Video -Leaf), ((Get-Item -LiteralPath $Video).Length / 1MB))
    $local = Test-Play $Video '直接读文件（自己拖入壁纸的方式）'
    $software = Test-Play $Video '直接读文件，关闭硬件解码' 'no'
    $server = New-Object LumeProbeServer($Video)
    try { $remote = Test-Play ("http://127.0.0.1:$($server.Port)/media.mp4") '通过本机连接读取（下载壁纸的方式）' }
    finally { $server.Stop() }
    Say ("本机连接收到请求: $($server.Requests) 次")

    Section '结论'
    if (-not $local.ok -and $software.ok) {
        Say '关闭硬件解码后就能播放：显卡硬件解码卡住了。常见原因是虚拟显示器或远程软件（如网易 UU 远程 GameViewer、ToDesk、向日葵）或显卡驱动。请先退出这类软件再试，并更新显卡驱动；把报告发给开发者，Lume 将改为自动回退到软件解码。'
    } elseif ($local.ok -and -not $remote.ok) {
        if ($server.Requests -eq 0) { Say '播放器连不上本机地址 127.0.0.1：安全软件、代理或加速器拦截了 mpv.exe 的网络连接。请把 Lume 安装目录加入白名单，或暂时关闭这些工具后再试。' }
        else { Say '播放器连上了本机地址，但读取中断：可能是网络过滤驱动干扰了本机连接。请暂时关闭安全软件/加速器后再试。' }
    } elseif (-not $local.ok) {
        Say '连本地文件都无法解码：播放器本身被拦截或显卡解码异常。请把 Lume 安装目录加入安全软件白名单，并更新显卡驱动。'
    } elseif ($remote.seconds -gt 8) {
        Say '两种方式都能播放，但较慢：下载的壁纸可能在 Lume 的 10 秒限制内来不及出画面。请把报告发给开发者。'
    } else {
        Say '两种方式都正常：播放器与本机连接没问题，请把报告和 Lume 的报错截图一起发给开发者。'
    }
} elseif ($canStart) {
    Say '未选择测试视频，跳过播放测试。'
}

$out = Join-Path ([Environment]::GetFolderPath('Desktop')) 'lume-wallpaper-report.txt'
[IO.File]::WriteAllLines($out, $report, (New-Object Text.UTF8Encoding $true))
Write-Host ''
Write-Host "报告已保存到桌面: $out"
