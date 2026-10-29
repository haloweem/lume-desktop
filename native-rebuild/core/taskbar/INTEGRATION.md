# 任务栏透明模块接线说明

当前实现范围为独立模块，宿主、桥接、UI、打包接线由主代理负责。

- `taskbar::Preferences` 默认关闭组件，透明化默认开启，自动隐藏默认关闭；首次启用与恢复默认均使用此设置，已有保存值保持用户选择。
- `taskbar::Controller` 是单线程状态机，依赖 `ITaskbarPort` 和 `RecordStore`。
- `platform::windows::TaskbarRuntime` 负责独立工作线程、请求合并、停止取消、状态快照，禁用并恢复后释放线程。
- `shell::TaskbarSession` 接口不依赖 `PageCommand`，宿主将页面命令映射到 `Apply(Preferences)`，通过消息窗口转发工作线程状态到 UI 所属线程。
- `TaskbarPaths` 需要宿主明确提供 `stateFile`（UTF-8 字符串）、`payloadRoot`（含 SHA256SUMS.txt、settings.json、x64/arm64 子目录的只读核心目录）、`cacheRoot`（独立可写缓存目录）。测试不得指向用户配置。
- Windows 10 先读取原始任务栏属性，再使用原版 Accent=2；无法读取原始属性时失败，不猜测原始样式。
- Windows 11 按原版使用 x64/arm64 的独立任务栏核心，不引入 Electron、Node、C# 守护进程。载荷哈希验证后，通过 `PROC_THREAD_ATTRIBUTE_JOB_LIST` 在进程创建时原子加入 Job，再恢复挂起线程；创建与 Job 所有权之间不存在父进程崩溃遗留子进程的间隙。检查专属托盘窗口及存活状态。
- 原始自动隐藏状态和各任务栏属性在修改前写入原子替换的恢复日志。没有启用且没有待恢复记录时不访问任务栏、不启动线程、不创建系统所有权互斥量。
- 关闭 Windows 11 核心先请求其 `TrayWindow` 正常退出，退出超时保留恢复标志并上报，不能将强制结束声称为恢复成功。
- 核心进程生命周期由 `TaskbarCoreProcess` 独立管理。进程存活和就绪分开判断，重试仍检查所属就绪窗口；窗口类名不区分大小写。已请求正常退出但超过首轮期限的进程，后续确认退出码为零后直接完成恢复，不重新启动核心。
- 各项恢复独立提交。核心退出失败、载荷缺失或单个旧任务栏恢复失败，不阻止其余任务栏及自动隐藏恢复；只保留未恢复项目。监听器初始化失败也必须先执行恢复，失败时由同一工作线程保留系统所有权，最终停止如实返回失败。
- 每个运行实例独占一把当前登录会话中的任务栏所有权互斥量。主/副屏任务栏分别记录进程创建时间与句柄，避免 Explorer 重启后的句柄复用误恢复。
- 偏好设置写入前也取得所有权并重新加载恢复日志，防止另一个实例用旧内存快照覆盖正在生效的原状态。
- 静止时工作线程等待系统事件；监听前台、窗口状态、任务栏创建/位置和主题/设置广播，合并短期事件，再做一次 120ms 延后重设。没有永久 100ms/1s 维护轮询。失败最多以 1/2/4/8/16/30 秒重试，随后等相关事件或用户操作；核心退出由进程句柄唤醒。

真实系统效果、Windows 10/11 双系统、多屏和 Explorer 重启仍须单独验收；私有端口测试不能替代实际效果验证。

## 主代理接线

1. 顶层加入 `add_subdirectory(core/taskbar)` 和 `add_subdirectory(platform/windows/taskbar)`。后者需要在 `lume-platform-windows` 目标定义后加入。
2. 增加 `lume-taskbar-session` 静态库，源文件 `apps/shell/taskbar_session.cpp`；公开包含 `native-rebuild` 根目录，设置项目通用 Windows 定义，链接 `lume-taskbar-windows`。宿主链接该目标。
3. `TaskbarSession(HINSTANCE, TaskbarPaths, Notify, 可选 IFileStore*, 可选 PortFactory)`；`Notify` 始终在创建会话的 UI 线程执行。`Apply` 返回请求是否接受，实际应用结果通过 `State()` 和回调中的 `applying/running/error` 获取，不得将“排队成功”显示为“已应用”。
4. 宿主把页面命令映射到 `session.Preferences()` 的副本再调用 `Apply`，Ready 返回 `State()`。类型为 `taskbar-state`，默认设置字段为 `enabled=false, transparentTaskbar=true, autoHideTaskbar=false`。生命周期为启动 `Restore()`、结束 `Shutdown()`；返回 false 必须保留并报告待恢复状态。
5. 五个测试源分别是 `tests/unit/taskbar_{controller,store,runtime,session,payload}_test.cpp`。Controller/store 链接 `lume-taskbar-core`；runtime/payload 链接 `lume-taskbar-windows`；session 链接 `lume-taskbar-session`；runtime 另链 `psapi`。
6. 原版原生核心只能作为独立载荷打包：完整 SHA256SUMS.txt、settings.json、选定架构的八个依赖及许可证/NOTICE/SOURCE/patch。若承诺在 x64 程序运行于 ARM64 系统时支持此功能，必须同时带 ARM64 载荷。不要只复制 exe。
7. 包内可约定 `taskbar-core/2026.1`；模块不会自行读取旧安装目录，也不会查找并启动用户旧应用。只会启动通过编译内置哈希校验的缓存路径。
8. `TaskbarPaths::allowSystemEffects` 默认 `false`。生产宿主显式设为 `true`；验证宿主保持 `false`，需要功能测试时注入专用假 `PortFactory`。既未显式允许又未传工厂时不会构造生产端口、不会启动线程，`Restore/Apply` 返回 false，错误为 `system-effects-disabled`。直接构造 `WindowsTaskbarPort` 也有同样的默认拒绝保护。
9. 管理窗口关闭后的保活必须用 `TaskbarSession::HasWork()`，覆盖排队/工作线程/待恢复/已运行状态。只检查 `Running()` 会漏掉异步启用阶段。

## 已执行的隔离验证（2026-09-26）

- 所有新增 C++ 文件以 GCC C++17、`-Wall -Wextra -Wpedantic -Werror` 在 `build-artifacts/taskbar-isolated` 独立编译，未使用主构建目录。
- Controller/store：严格字段与重复键拒绝、精确 64 位身份、原子替换失败保留原记录、从未启用零任务栏访问、系统修改前先保存恢复记录、多任务栏、句柄复用、失败恢复、未启用禁止触碰未拥有状态。
- Runtime：慢启动异步且可取消、请求合并、关闭恢复、16 轮启停。四轮预热后与第十六轮均为 131 个句柄、GDI 0、USER 1、4 个线程、Private Commit 1,581,056 字节。该数据只代表测试进程及假系统端口，不包含真实核心进程。
- Session：真实隐藏消息窗口与 UI 消息循环，断言回调在创建线程执行，配置保存后跨会话读取并恢复。
- Payload：读取原版只读路径，验证 x64/ARM64 两套依赖的固定哈希，在独立临时目录验证复制、缓存复用、损坏 DLL 修复、伪造清单拒绝。测试未启动核心，已删除自有临时目录。
- 独立审查回归先复现了未就绪误报成功、延迟退出重复启动、核心失败阻塞自动隐藏恢复、监听器失败跳过恢复四条路径，再修复。Runtime 测试通过实际隔离子进程覆盖前两条：子进程不接触 Explorer，故意不提供就绪窗口或延迟正常关闭；同时验证 Job 回收、关闭中的进程不能重新确认启用。监听器创建失败使用本测试进程的私有窗口类注入，不改变系统配置。
- 后续非图形回归在实际 `CreateProcessW` 返回瞬间终止测试父进程，确认仍挂起的子进程已经属于该父进程的独立 Job，并随 Job 回收。另注入 `WAIT_FAILED`，断言无法确认退出时保留进程句柄、不启动替代实例、不报告停止成功。独立运行入口为 `lume-taskbar-runtime-test --atomic-process-only`，不创建图形窗口或执行真实任务栏效果。

## 尚未验收及边界

- 未运行生产任务栏端口，未改变真实自动隐藏设置，未启动独立核心，未运行安装器。
- Windows 10 的 `GetWindowCompositionAttribute` 和 Accent=2 是原版同类内部 API 路径；若系统拒绝读取原始属性，会明确失败，不能改成猜测默认材质再宣称可恢复。
- Windows 11 正常退出使用原版核心的 `RestoreTaskbarBeforeShutdown`。崩溃恢复通过同一可信核心的正常启动/退出重走其恢复路径；此路径仍须实机验收，不能等同于已经证明任何第三方任务栏样式都能完整恢复。
- 退出超时会保留恢复日志，最终释放 Job 可能终止残留核心；此时不会报告“恢复成功”。损坏恢复日志采取失败关闭，不自动套用可能过期的 `.bak`。
- 原生核心准备阶段验证的是进程存活、所属 `TrayWindow` 和已知载荷完整性，不能据此宣称实际透明像素、多屏或 Explorer 重启已经验收。
- 不同 Windows 构建、实际核心资源占用、实际多屏/Explorer 重启/系统睡眠唤醒、任务栏与其他已实现组件的组合资源矩阵仍由主任务验收。
