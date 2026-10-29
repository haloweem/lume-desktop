# macOS 最小化动画原生模块

状态：2026-09-27 Host 生命周期修复与 x86/x64 受限子进程测试已通过；本文件同步模块边界，
不代表系统版本、真实 Explorer 任务栏、多屏和视觉矩阵已验收。

原版 `macos-minimize-animation.wh.cpp` 3.1.2 的入口是六个 User32 函数钩子，
Windhawk 负责把它装入每个 GUI 进程。可复用边界是窗口快照、现代 20×20 网格
曲线、经典逐行漏斗、DWM 隐藏与恢复、任务栏定位；不是 Electron 或 Node。

采用 Windows CBT 的 `HCBT_MINMAX` 在目标进程里捕获最小化/还原，另用
`WH_CALLWNDPROCRET` 观察实验性首次显示。相同位数的独立 Host+Hook DLL 按需
启停；不修改任意应用磁盘文件，不使用远程线程，也不移植 Windhawk 框架。
必须用独立子进程验证 ShowWindow、ShowWindowAsync、CloseWindow、
SetWindowPlacement 和 WM_SYSCOMMAND 的覆盖，不能仅据 API 文档宣称覆盖。

接口已提供，v1 状态保留原字段，并补充 `entries` / `pendingCleanup`：

- `core/minimize_animation/minimize_animation.h`：Preferences、Valid、
  LampVertex、ClassicRow。
- `core/minimize_animation/minimize_animation_store.h`：严格配置解码及原子存储。
- `platform/windows/minimize_animation/minimize_animation_runtime.h`：
  `MinimizeAnimationRuntime::Start/Configure/Stop/Snapshot`，Scope 可限制测试线程。
- `apps/shell/minimize_animation_session.h`：`Restore/Apply/State/Running/Shutdown`，
  宿主可将自己的枚举映射为 Preferences 后调用 Apply；另有
  `HandleMessage(std::wstring_view)` 解析独立测试使用的专属规范协议。
- 本目录 `CMakeLists.txt`：可 `add_subdirectory`，也可独立配置验证。产物为
  `LumeMinimizeHost64.exe` 与 `LumeMinimizeHook64.dll`（32 位构建对应 32 后缀）。

默认值以 `core/minimize_animation/minimize_animation.h` 为准：450ms，restore=true，
launch=true（"展开动画"，1.0.6 起默认开启；已有配置文件保持原值），multiMonitor=false，
unhideTaskbar=true，unhideDuration=450ms，engine=modern；功能启用默认 false。
设置范围分别 50–2000 与 0–5000。测试作用域禁止触碰真实任务栏。

停机先关闭共享启用位并发出停止事件，撤销系统钩子以阻止新任务。Host 最多等待
2000ms 让目标回调／工作线程排空；它的退出不代表目标已恢复。运行时只有同时
观察到该 Host 的进程句柄已发出退出信号、目标 `entries/workers` 都为零时，
才释放所有权并报告停止成功。未启用不创建 Host、钩子或绘制线程；不保留窗口快照缓存，
还原时在窗口恢复后短暂 cloak 并重新捕获，减少跨应用常驻内存。

Host 使用 `STARTUPINFOEX + PROC_THREAD_ATTRIBUTE_JOB_LIST` 在内核创建阶段归入
独立的 kill-on-close Job；没有“先创建、后分配”的崩溃空档，DLL 初始化卡住时
父进程退出也不会遗留 Host。Job 只包含 Lume 自有 Host，绝不分配或终止被注入的
用户进程。Hook 临时线程独立持有目标 DLL、共享区、停止事件以及父进程／Host
句柄，因而 Host 退出后仍能恢复自己的 cloak。对于 transitions，Hook 只有成功
读取原值后才修改并恢复；当前系统该设置属性不可读时保持原样，不宣称读回验收。

默认停止预算为 7000ms。仅当目标租约已排空而自有 Host 仍未退出时，才允许终止
该 Host 的 Job，并继续等待实际退出。若目标未恢复或退出等待失败，Stop 返回
`stop-timeout` 并保留 `systemError`、Host 计数及 `pendingCleanup`。析构不能丢掉
这些句柄，而将整条所有权记录转入进程内 pending 表；查询及下次启动会重新检查，
无永久轮询线程。`WAIT_FAILED` 不能当作进程已退出；租约未排空时不能启用替代
Host。恢复后才清记录并允许重新启用。目标自身挂起／卡死时不强行结束用户应用，
未完成的回调仍如实保留在状态中。

Win32 钩子有位数、桌面、完整性级别和受保护应用边界：x64 不等于 x86/ARM64，
非提权 Host 不声称支持提权窗口。32 位配套缺失会让默认生产启动明确返回
`missing-host`，不降级冒充双位数覆盖；安装接线须携带实际构建出的对应位数
产物，不能把未验证平台标作支持。

宿主总入口、顶层 CMake、共享测试 CMake、管理页面和打包文件由主代理接线。

## 可重跑构建

本目录可独立配置：

```powershell
cmake -S native-rebuild/platform/windows/minimize_animation -B <独立验证目录> -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build <独立验证目录> --parallel 3
ctest --test-dir <独立验证目录> --output-on-failure
```

生成双方位数的独立 Host/DLL：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File native-rebuild/platform/windows/minimize_animation/build-minimize-hosts.ps1 -OutputDirectory <产物目录> -Architecture Both -IncludeVerification
```

主项目已自己构建 Host64/Hook64 时，用 `-Architecture 32` 只补 x86，避免并行
覆盖同名 x64 产物。脚本使用已安装的 MSVC x86/x64 工具链，不安装任何运行时。
`-IncludeVerification` 额外输出对应位数的静态运行期测试程序；省略则只构建产品载荷。
主项目需将 `lume-minimize-animation-session` 链入宿主，并让产品目标依赖
`lume-minimize-animation-payload`。本模块已定义这个 `ALL` 目标，并让会话库
依赖它；它包含原生 Host/DLL、另一位数配套和两份许可／来源文件。顶层可以
显式添加 `add_dependencies(LumeShell lume-minimize-animation-payload)`，无须再
定义一份 x86 构建规则。独立子构建不创建这个双位数目标，避免递归构建。

本轮复用了临时 GNU x64 `minimize-animation-verification/mingw` 和 MSVC x86
`minimize-animation-verification/msvc32` 构建目录。最终日志、资源数据、源文件与
二进制 SHA-256、双方位数可执行测试／Host／Hook 已归档至仓库
`build-artifacts/native-rebuild/verification/minimize-lifecycle-20260927`。
临时目录不属于产品依赖；归档 `README.md` 说明重跑方法及验证范围。

## 解包目录的双位数验证

`lume-minimize-animation-runtime-test` 已接受 `--payload-directory <解包目录>`，
直接从该目录启动对应位数的 Host/DLL。测试自身同时充当独立 GUI fixture 和
模拟崩溃的 owner，不需要额外 fixture 程序。每次只装入该测试程序对应位数的
钩子，且严格限定其专用子进程线程；因此需要分别运行 x86、x64 测试程序。
不要同时运行它们或其他最小化运行期测试，因为 IPC 使用每会话共享名称。

仅复制两个测试程序到 verification 目录，不将它们放入产品安装包：

```powershell
Copy-Item <msvc32构建目录>\lume-minimize-animation-runtime-test.exe <verification目录>\minimize-runtime-test32.exe
Copy-Item <mingw构建目录>\lume-minimize-animation-runtime-test.exe <verification目录>\minimize-runtime-test64.exe
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    & <verification目录>\minimize-runtime-test32.exe --payload-directory <解包目录>
    if ($LASTEXITCODE -ne 0) { throw 'x86 解包载荷运行期验证失败' }
    & <verification目录>\minimize-runtime-test64.exe --payload-directory <解包目录>
    if ($LASTEXITCODE -ne 0) { throw 'x64 解包载荷运行期验证失败' }
} finally {
    $env:PATH = $savedPath
}
```

通过标准为 Host 真正启动、两类钩子安装、专用目标进程中实际捕获并绘制帧、
五入口/现代和经典样式/还原/首次显示/停止与崩溃回收全部成功。测试输出明确
打印 `runtime test architecture=32/64 host started; hooks=2`；不以文件存在或
PE machine 字段代替运行验证。构建脚本的编译输出写到
`minimize-host-build-32.log` / `minimize-host-build-64.log`，成功仅打印摘要，
失败显示末尾 50 行及完整日志路径。

当前归档的静态测试程序为上述归档目录下
`payload/minimize-runtime-test32.exe`、`payload/minimize-runtime-test64.exe`，
以及对应位数的 core/session 测试。它们是最终已测文件的原样复制，哈希见
`evidence.json`。本轮完整测试使用独立构建目录，3/3 分别耗时 x64 20.70 秒、
x86 20.82 秒；后续实际安装载荷的 clean-PATH 检查由主项目包装流程执行。
受限作用域证据不代表已全局启用并验收双 Host 或用户桌面所有窗口。

## 已验证与边界

三个测试分别覆盖设置/曲线/文件恢复，会话真实启停/保存失败回滚，跨进程系统
窗口动画。运行期测试创建独立 GUI 子进程，scope 限制到该线程，不触碰用户
运行中的应用或真实任务栏。已验证 ShowWindow、WM_SYSCOMMAND/DefWindowProc、
CloseWindow、SetWindowPlacement、ShowWindowAsync，反向还原，经典样式，
实验性 SetWindowPos 首次显示，8 轮动画中停止/重新启用，以及测试父进程与
目标进程分别被终止后的回收。所有测试子进程归专属 Job 管理，失败也可清理。

停机检查包含父进程句柄回到预热基线、目标进程句柄容差 +2、GDI 不增长、
USER 对象容差 +1（USER32 惰性对象），并读回 DWM cloak=0。资源数字只属于
此测试，不等同实际整机 CPU、工作集或 GPU 验收。

默认运行期测试还包含以下回归，无须另加参数才会执行：

- 初始化卡住时父进程退出，以及内核创建返回的第一刻崩溃：检查精确 Job 成员关系，
  并确认没有悬空自有 Host。测试替代 Host 不绘图、不装钩子。
- Host 有界退出，未完成租约保留，超时后重新启用；析构后的 pending 所有权及
  `WAIT_FAILED` 不清计数。会话测试同步读回 `entries` / `pendingCleanup`。
- 自有目标的最小化与实际 cloak 中的还原动画各自发生 owner 崩溃；等待
  workers/entries 排空，并读回 cloak=0。transitions 只在可查询的平台检查原值，
  输出能力标记；不可查询时不将其列为已验证的读回项。

定位问题时可单跑 `--initialization-regression`，或
`--shutdown-regression --payload-directory <载荷目录>`；双方位数都支持。
这些定向参数不运行真实窗口动画或资源采样。完整测试按现代／经典分别执行
4 轮最小化＋还原，输出实测 `frames/captures/completed`、墙钟耗时，以及测试进程、
专用目标进程、自有 Host 各自 CPU 毫秒、单核百分比、Private Commit 和工作集的
起止值及采样峰值。CPU 使用进程内核＋用户时间，内存约每 10ms 采样，测试进程
包含观察开销；采样峰值不是操作系统生存期峰值。

绘制最多同时取得 4 个跨双方位数共享租约；目标进程退出时，Host 只查询已由
事件登记的 PID/创建时间并清理死租约，不进行系统进程枚举。现代引擎按需加载
Direct2D 软件 DC；没有动画时不保留画面缓存、设备、计时器或绘制线程。
软件 DC 这一架构事实不能推导为比原版 GPU 绘制更省 CPU；本测试没有原版同机
对照，也没有 GPU 计数器，相关优劣不能据此宣称。

尚未覆盖真实 Explorer 自动隐藏与按钮 UIA 命中、多显示器/非 96 DPI、休眠、
所有第三方 GPU 窗口、UWP/受保护应用/提权窗口，以及原版同尺寸实窗视觉和
完整 CPU/内存/GPU 采样。首次捕获或首帧超时会保留系统原生行为；不能将这份
受限作用域证据扩展为“所有系统窗口已验收”。
