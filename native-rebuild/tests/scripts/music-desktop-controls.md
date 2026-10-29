# T13 独立组件接线与验收

## 所有权与编译

生产实现只有 `platform/windows/music_desktop_controls.h/.cpp`。
没有修改 app、CMake、session/types 或共享 UI；这些文件的并行改动不属于本分工。
没有第二个播放器、队列、账号服务、歌曲文件读取、网络访问或全局键盘钩子。

主会话新增目标时使用：

```cmake
add_library(lume-music-desktop-controls STATIC platform/windows/music_desktop_controls.cpp)
target_include_directories(lume-music-desktop-controls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lume-music-desktop-controls PUBLIC cxx_std_17)
target_compile_definitions(lume-music-desktop-controls PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-music-desktop-controls PUBLIC lume-widget-window
  PRIVATE runtimeobject shcore crypt32 windowscodecs ole32 uuid comctl32)
lume_enable_native_warnings(lume-music-desktop-controls)
```

复用 `NativeWidgetWindow`、`WidgetCanvas` 和原生材质；其传递依赖包括现有
`lume-widget-ui`、`lume-widget-material`、`lume-window-surface`、
`lume-core` 及 Direct2D/DirectWrite 等系统库。显示组件时仍需现有 `LumeGlass.dll`。

已用当前 WinLibs GCC 16.1.0、MinGW WIDL 11.5 头文件、C++17、
`-Wall -Wextra -Wpedantic -Werror` 编译。MinGW 的 `IReference<BYTE>` /
`IReference<boolean>` 重复特化仅在本翻译单元局部规避，未修改 SDK。
Windows SDK 10.0.26100.0 也存在所用接口；未宣称已做 MSVC 全量构建。

## 主会话接线

命名空间为 `lume::platform::windows`。

- 构造 `MusicDesktopControls(HINSTANCE, CommandHandler, WidgetHandler)`；
  构造和 `Update(const music::MusicSnapshot&)` 不创建窗口或启用 SMTC。
- 只输入现有 session 的当前快照。组件只存储有界当前元数据，不复制播放队列或歌曲 source。
  当前 `MusicSession::WidgetState()` 是 JSON 字符串，不是原生快照；
  原生快照出口由主会话负责。
- `CommandHandler` 收到 `const ui::MusicPageCommand&`；复制并排入主会话命令队列。
  执行前核对当前 `session` 和 `current->id`，再交给 `MusicSession::HandlePage`。
  不要重新填成新的 session/id，也不要让过期命令绕过身份检查。
- 所有公开调用与析构在同一 UI 线程，调用方先初始化 STA COM。
  回调可以 Update/Hide/Shutdown，但应延迟销毁组件对象本身。
- `Configure(Preferences)` 只设置几何/锁定/材质，**不根据 enabled 自动显示**。
  主会话根据菜单、设置与持久化偏好明确调用 `Show()` 或 `Hide()`。
  默认 320x152 DIP；最大 720x720 DIP；中/大两档对应历史 CSS 阈值。
- `WidgetHandler(action, preferences)` 接收关闭、设置、锁定、材质、重置、位置事件。
  组件已本地应用关闭/锁定/材质/重置；主会话持久化回调值。
  `Settings` 仅通知主会话打开设置；`Bounds()` 返回当前位置。
- 推荐调用无参 `EnableSystemMediaControls()`。它懒建一个不显示、不激活、
  无任务栏入口的轻量顶层宿主；独立于管理窗口和桌面组件。
  不得把组件 Show/Hide、管理页 visibility 与 SMTC 启停绑定。
- 显式 HWND 重载仍保留：必须是当前线程、本进程有效顶层 HWND，
  且应比注册活得久；不支持 HWND_MESSAGE。同一 HWND 不允许本模块重复注册。
  外部 owner 销毁前应 Disable，或把其 `WM_NCDESTROY` 交给 ProcessMessage。
- SMTC 激活后，本模块自有宿主处理事件唤醒；只需标准 TranslateMessage/DispatchMessage，
  无需主会话保留额外 WM_APP 消息号或定时器。
- 可以在主窗口 WndProc 的 DefWindowProc 前调用
  `ProcessMessage(hwnd, message, wparam, lparam)`；有返回值则直接返回。
  它仅在 SMTC 未注册时处理该进程 HWND 的 WM_APPCOMMAND，避免双重媒体键操作。
- 消息循环可先调用 `PreTranslateMessage(MSG)`，供组件的 Tab 遍历使用。
  组件提供鼠标拖动跳转、方向/Home/End 跳转、空格/回车播放、焦点提示和原生 tooltip。
- `Hide()` 释放组件 HWND、材质、解码封面和 tooltip，不停止音频、不撤销 SMTC。
  空快照禁用系统媒体会话并清空显示信息。SMTC 错误可读
  `SystemMediaResult()`；不会自动循环重试或弹窗。
- 退出时先 `Shutdown()`，再释放 session/callback 目标、COM 和消息循环。
  Shutdown 幂等；撤销事件、关闭隐藏宿主、清空回调及待执行消息。

## Windows API 与边界

使用 `RoInitialize/RoGetActivationFactory`、
`ISystemMediaTransportControlsInterop::GetForWindow`、
`ISystemMediaTransportControls`、`ISystemMediaTransportControls2`、
`ButtonPressed`、`PlaybackPositionChangeRequested`、
`DisplayUpdater`、`MusicDisplayProperties` 和 `UpdateTimelineProperties`。
必须先设置 `DisplayUpdater.Type=Music` 再取 MusicProperties；
真实探测曾确认顺序错误会返回 `0x80070032`，现已修正。

WinRT 事件仅持有共享邮箱，不捕获组件裸指针；邮箱最多 32 条，
每批只唤醒一次，超限丢弃新事件。歌曲身份变化会清空队列并递增代数。
撤销后的迟到回调不能投递；重新注册使用新邮箱。
Play/Pause 使用明确动作，不把重复 Play 错转为 Toggle。
不会调用 GetForCurrentView、媒体会话全局管理器、RegisterHotKey 或键盘钩子。

封面只接受 <=2800000 字节 PNG/JPEG data URI；
base64 解码 <=2 MiB，WIC 原图两边 <=2048，中心裁剪至最多 256x256 BGRA。
只保留当前编码封面和一个解码图。远程 URL、文件路径、不支持的格式显示音符占位。
SMTC 缩略图通过内存 IStream -> IRandomAccessStream -> StreamReference 发布。
组件不会额外下载封面；未来由现有资源层提供合规内存封面即可。

## 验证

在仓库根目录执行：

```powershell
pwsh -NoProfile -File native-rebuild/tests/scripts/test-music-desktop-controls.ps1
```

脚本使用既有构建库，不修改/重配置 CMake；输出到
`build-artifacts/music-desktop-controls`。
默认测试不调用 Show、不注册真实 SMTC、不播放音频、不改变配置。
覆盖状态投影、中文、命令身份、非法参数、幂等动作、媒体键范围、1000 次跨线程事件、
32 条上限/单次唤醒、撤销后迟到事件、图片格式/数据边界、退出拒绝操作，
以及五种状态 x 三档尺寸 x 三档 DPI 的 45 组离屏画布。
原生生产绘制器的离屏 BMP 可供视觉复核；背景仅用于衬托，不代表真实玻璃验收。

真实 SMTC 为**显式**探测，不加入默认 CTest：

```powershell
build-artifacts/music-desktop-controls/music-desktop-controls-test.exe --smtc-smoke
```

会短暂注册测试进程的 SMTC 并发布合成元数据，退出清理；不显示组件或播放音频。
已验证注册 HRESULT=0、标题回读、内存封面、重复 owner 拒绝、撤销/重注册、
原管理 HWND 销毁后独立宿主存活、空态恢复、多次启停与宿主销毁。

测试翻译单元包含生产 .cpp 以测试内部 COM 邮箱。主会话加测试目标时
不要再把同一生产 .cpp 或 `lume-music-desktop-controls` 静态库链接进去，
只链接上面已有窗口库和系统依赖。

## 尚待验收

- 主会话完成 native snapshot、菜单/设置/偏好、命令排队校验、生命周期和 CMake 接线。
- 真实桌面玻璃、层级、拖动/锁定/缩放、跨显示器 DPI、tooltip 与键盘焦点人工验收。
- 物理媒体键、系统媒体面板跳转、与其他播放器竞争路由的端到端验收。
- 屏幕阅读器下自绘控件可访问性尚未验收；目前提供键盘操作、窗口标题和悬停提示，
  未实现每个自绘按钮的独立 MSAA/UIA 子节点。
