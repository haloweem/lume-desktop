# 共用原生组件界面来源

`lume-widget-ui` 是独立静态库，提供组件菜单、颜色选择器与位置编辑器，
不依赖时钟库或时钟控制器。各组件只传入当前值、接收确认结果，自行负责命令、
保存与生命周期。公开头文件可独立使用；颜色选择器仍要求调用方初始化 GDI+，
菜单使用系统主题与 GDI，不需要 GDI+。

`LumeWidgetMenu.cpp/.h`、`StdAfx.h` 和 `resource.h` 最初来自原 Lume 的
`src/lume-electron-shell/widgets/widget-menu-source/`。
`LumeWidgetColorPicker.cpp/.h` 和 `LumeWidgetPositionEditor.cpp/.h` 提取自
`third_party/lume-widget-host/patches/0001-lume-widget-host.patch`。
原组件宿主的 GPL 许可证保留在同目录 `LICENSE` 中。
移植改动使用有界标准宽字符格式化与 Windows 10 DPI API，不依赖 Rainmeter 进程。

颜色和位置对话框保持原布局。宿主销毁时终止模态循环，保留 `WM_QUIT` 原退出码，
避免对话框关闭后激活桌面时钟。位置编辑器按 DPI 缩放并约束在显示器工作区内。
颜色选择器将 HEX/RGB 输入选择与透明度编辑分离，忽略自身同步通知，
调整透明度滑块前提交待处理颜色；不增加组件业务或保存依赖。

## 2026-10-01 右键菜单（记录已过时，以下为 2026-10 的现状）

> 本节原先记录的"改用原生 `HMENU` + `TrackPopupMenuEx`"**不在当前代码中**：
> `LumeWidgetMenu.cpp` 里没有 `CreatePopupMenu`、`TrackPopupMenuEx`、`WM_DRAWITEM`
> 或 `SetWindowSubclass`。现行实现仍是自绘窗口，勿按原生菜单假设去改。

现状：`LumeWidgetMenu` 自建窗口类 `LumeWidgetMenuWindow`，带自有模态循环；
二级菜单用 `kSubmenuCloseTimer`（3000ms 保留）与 `kParentHoverTimer` 轮询父项悬停，
`kSubmenuOverlap = 10`、`kSubmenuKeepAlivePadding = 48`。
尺寸沿用原逻辑基准：根菜单 250 DIP、普通行 42 DIP；子菜单 170 DIP、普通行 32 DIP；
紧凑行 176/30。命令编号与勾选状态由菜单模型自己维护。

## 2026-09-27 共享组件修复

`widget_snap_controller.h` 和 `core/widget/snap_geometry.h` 复用原
`src/lume-electron-shell/widgets/widget-snap.js` 及 `runtime.js` 的吸附语义：
物理像素间距 8、吸入 8、脱离 12、缩放提示 2，提示短留 260ms。
组件通过通用窗口能力与消息协议协作；纯几何策略不依赖 Win32 或具体组件，
不创建工作线程或空闲轮询。设置和生命周期仍归各组件。

`WidgetCanvas::ImageResource` 使用原 PNG，按实际绘制尺寸和 DPI 预滤波，
避免直接线性大幅缩小导致锯齿。每画布最多缓存 16 个资源、每资源两个尺寸；
解码器不常驻，窗口关闭随画布释放。图标收纳的 Shell 图像提取另由独立宿主管理。
