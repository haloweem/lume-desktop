# 应用设置后端

本模块不依赖账号、WebView、壁纸模块或 mpv。与壁纸的唯一连接是调用方注入的
`bool(bool)` 均衡模式端口。宿主提供 UTF-8 编码的绝对路径 `preferencesPath`；
本模块不导入或覆盖原版 Electron / VS Code 的设置。

## 接线与生命周期

在 UI 线程构造会话。构造阶段仅同步读取小型配置文件，不调用运行时端口、
不读取 Run 注册表值、不发送 UI 通知。宿主可立即通过 `BalancedMode()` 初始化
壁纸选项。在异步 `Restore()` 确认 Run 值之前，`autostart` 仅表示配置意图，
新用户默认为 true；恢复完成后的快照才表示系统读回状态。

设置成员声明在壁纸成员之前时，必须按以下顺序接线：

1. 先构造设置会话，再用 `settings.BalancedMode()` 构造壁纸选项及壁纸会话。
2. 先调用 `wallpaper.Restore()`，再调用 `settings.Restore()`。
3. 退出时先调用 `settings.Shutdown()`，再调用 `wallpaper.Shutdown()`，
   唤醒可能正在等待壁纸 actor 的端口，最后调用 `settings.JoinShutdown()`。
4. 必须等 Join 完成后才能析构壁纸对象，不能仅依赖成员的逆序析构。

注入的均衡模式端口只在设置模块自己的 worker 上执行。端口必须有界等待，
或能被壁纸关闭流程唤醒，UI 线程不等待播放器。`Shutdown()` 请求取消、
禁止后续通知并丢弃未执行任务；取消后，只要 worker 尚未结束，`HasWork()` 就保持 true。
`JoinShutdown()` 实际等待线程退出，析构函数也执行取消和 Join，绝不 detach。
命令调用与析构属于会话所属的 UI 线程；快照 getter 使用同步保护。

UI 通知通过私有消息窗口分发。`Restore()` 的返回值表示是否接受入队，
不是运行时应用成功；页面命令返回快照，也不代表保存已经完成。
完成通知更新已确认快照，并递增 `revision`。重复的在途检查会合并，
并发修改返回忙状态快照，不越过正在执行的事务。

## 持久化与启动项

配置格式版本 1 包含 `language`、`balancedMode`，以及上次写入的 `autostart` 意图。
文件不存在时默认语言为 `zh-CN`、均衡模式与自启开启。损坏、重复键或未知版本的文件
保持原样，并禁止设置修改。每次保存前重新验证现有文件，再通过同目录临时文件
原子替换。保存失败不提交新的配置值。

启动项快照以实际 Run 值为准，保存的自启意图独立保留，语言和均衡模式保存不会
把系统读回的开启状态覆盖到用户已保存的 false。`Load()` 始终只读。
正常会话的异步 `Restore()` 重读配置；仅当配置确实不存在时，才注册默认自启，
读回确认后保存 true。已有配置中的 false 不会因新默认值而变为 true。
已有 true 但系统入口缺失时，也不主动重建用户可能从系统侧移除的入口。

原生适配器只处理 64 位视图
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 的 `Lume` 值，不使用计划任务。
允许管理的入口是带引号的当前可执行文件绝对路径加 ` --tray-only`，以及同安装目录
下明确的 `lume.exe` 加 ` -tray-only` 旧命令。比较允许路径分隔符和 ASCII 大小写差异，
不猜测其他参数、注册表类型或目录别名。超过 260 字符限制的 Run 命令会被拒绝。

同目录旧入口在只读快照中也报告开启。正常恢复时，保存的 false 会删除本实例入口，
保存的 true 或首次默认开启会将同目录旧入口迁移为当前原生命令。
其他目录的明确 Lume 旧命令或 LumeShell 命令仍报告开启，同时给出
`autostart-conflict`；未知命令也报告冲突，不能把它当作成功关闭。
上述冲突不会被无关设置保存或更新检查清除。跨目录及未知入口始终保持原样，
必须由调用方核实归属后另行处理，不能静默覆盖或删除。

均衡模式变更先调用运行时端口，再保存配置；运行时失败或保存失败都会尝试恢复
原运行时值。启动项变更保留原注册表类型和原始字节，在写入验证或配置保存失败时
尝试恢复。服务层和 Windows 适配器均在修改前验证归属，回滚前出现的无关程序值
也不允许覆盖。回滚失败有独立错误码，不冒充成功；注册表能够重读时，
快照报告实际残留的启动项状态。自动初始化或迁移期间发现配置新建、移除或
语言、均衡模式或自启意图变化，会拒绝保存并尝试回滚。系统启动项已经被外部修改时，
回滚保留实际读回值，不重新建立外部刚删除的入口。

## 更新验收边界

当前开发版本为 `1.0.3.1`。独立更新通知模块安排每日后台检查，
用户也可手动检查；不自动下载或安装。生产地址固定为 `https://lumedesktop.com/updates.json`，
保留 HTTPS 证书验证，拒绝所有重定向。独立异步 WinHTTP 请求具有 15 秒整体截止时间、
64 KiB 响应大小上限和取消机制，不复用 AccountHTTP，也不占用壁纸下载 worker。

`RequestUpdateCheck(manual)` 仅在实际入队时返回 true；忙时不污染设置错误。
`SetUpdateObserver` 在 UI 线程通知原始更新结果，不受均衡模式或自启错误干扰。
完成消息丢失时，由仅在任务执行期间运行的 250 毫秒定时器补发一次；
退出和同步销毁后不会再调用观察者。自然日去重、弹窗和材质由独立通知模块负责。

版本按 2 至 4 段非负整数比较，保留第 4 段。清单版本不高于当前版本时返回 `current`，
不要求旧版 Electron 清单提供原生下载字段；例如 `1.0.1` 不会被误报为新版。
新版缺少明确的字符串标识 `runtime: "native-win32"` 或
`channel: "native-verification"` 时，返回 `incompatible / update-incompatible`。

标识匹配后，还须验证官方 HTTPS `.exe` 下载地址 `downloadUrl`、
64 个十六进制字符组成的 `sha256`，以及可选的整数 `size`，范围为 2 字节至 1 GiB。
这些字段不合法时返回 `error / update-manifest`。

即使原生元数据完全合法，首包也只返回 `incompatible / update-disabled`：
当前验收包没有可用的原生下载安装通道，不发布 `available`、`downloading`、
`ready` 或 `installing`，避免出现可点击却无法兑现的下载入口。
下载及安装命令不会联网、启动安装进程或虚报成功。官方原生发布契约与完整原生更新器
仍属于后续工作，不能用旧 Electron 安装包替代。

## 测试与安全范围

`verification=true` 禁止默认启动项适配器访问真实 Run，也禁止默认更新适配器联网。
会话同时禁止恢复阶段的默认注册、旧入口迁移和自动删除，即使测试注入可写端口
也不自动执行这些操作。只有正常模式的隔离夹具才验收自动恢复副作用。
该开关不会自动隔离配置文件路径；测试必须注入文件 fake，或将 `preferencesPath`
放在项目 `build-artifacts` 内。显式注入的测试端口也由测试负责隔离。
所有文件夹具和测试日志均位于项目 `build-artifacts`。生产启动项适配器的单测
先创建唯一临时注册表根，再通过 `RegOverridePredefKey` 仅重定向当前测试进程；
重定向失败即停止，退出时删除临时根，绝不访问真实 Run。

`cmake/ApplicationSettings.cmake` 定义独立库 `lume-application-settings-session`
及以下五组测试：

- 协议：重复键、额外字段、非法枚举、错误值类型及输入长度限制。
- 事务服务：配置往返、损坏及未知版本保护、保存与运行时失败回滚、
  首次默认真实开启、已保存关闭、旧入口识别与迁移、未知入口保护、
  自动恢复期间配置冲突、原始注册表数据恢复、取消及回滚失败的真实错误。
- 更新策略：第 4 段版本比较、旧版清单、运行时和渠道不兼容、
  非法元数据，以及禁止暴露不可用的下载状态。
- Windows 文件适配：无副作用读取、原子替换、失败时保留原文件、
  临时文件清理、中文路径及验证模式隔离。
- 会话：构造阶段只读文件、均衡端口仅后台执行、UI 线程通知、
  并发检查合并、离线错误、取消后工作状态、Join 及无迟到通知。

离线和并发网络行为通过注入传输 fake 验证；这些单测不等同于生产 WinHTTP
真实联网验收，也不代表真实注册表启停或安装包整机验收已经完成。
本机 CIM 与原生注册表读取视图差异仍须单独核实；本模块不推断看不到的入口、
其他启动机制或系统侧启动审批状态。配置文件与注册表之间也没有跨资源原子事务，
不能保证进程崩溃及外部进程在最后检查之后抢写时的原子性。

本轮限定验证命令（仓库根目录执行，不构建应用或运行整体套件）：

```powershell
cmake -S native-rebuild -B build-artifacts/native-rebuild/autostart-validation -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-artifacts/native-rebuild/autostart-validation --target lume-application-settings-service-test lume-application-settings-file-test lume-application-settings-session-test lume-application-settings-protocol-test lume-application-settings-update-test --parallel 2
ctest --test-dir build-artifacts/native-rebuild/autostart-validation -R '^lume-application-settings-(service|file|session|protocol|update)-test$' --output-on-failure
```
