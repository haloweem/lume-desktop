# Lume 桌面客户端源码

这是 Lume Windows 桌面客户端（壁纸、桌面组件、任务栏、Dock、灵动岛、音乐等）的完整源码，
对应同版本号的官方安装包 `LumeSetup-<版本>.exe`。本次导出对应的版本和源码提交写在
[`SOURCE-REVISION.txt`](SOURCE-REVISION.txt)。

## 许可

Copyright (C) 2026 Lume 作者

本程序（包括 Lume 作者自己编写的全部代码）按 **GNU Affero General Public License 第 3 版（AGPL-3.0）**
发布，全文见 [`LICENSE`](LICENSE)。第三方组件保留其原有许可，见下文。

你可以按 AGPL-3.0 的条款使用、研究、修改和再分发本程序。本程序不提供任何担保。
AGPL-3.0 第 7 条允许保留商标条款：本授权不包括使用 “Lume” 名称、标志和图标的权利。
分发修改版时，请使用你自己的名称和图标。

程序中包含的第三方组件按各自的许可使用，清单和对应义务见 [`THIRD-PARTY.md`](THIRD-PARTY.md)，
许可原文随代码放在对应目录里（`licenses/`、`*/licenses/`、`*NOTICE*`、`*SOURCES*`）。主要包括：

- Folia Major（AGPL-3.0）：音乐界面的可视化、歌词特效、背景与材质，以及在线音乐协议。
- Mineradio（GPL-3.0）：星河粒子特效（经 Folia 移植）。
- TranslucentTB 2026.1（GPL-3.0）：Windows 11 任务栏核心，源码与 Lume 补丁见
  `native-rebuild/vendor/taskbar-runtime/2026.1/`。
- 以及 Three.js、PixiJS、React、Tiptap、KaTeX、lunar-javascript、OpenCC、nlohmann/json 等宽松许可的库。

不在本仓库里的部分：

- **服务端**（账号、壁纸目录与下载、更新信息）不随客户端分发，不在本源码范围内。客户端连接这些
  服务的地址写在源码里，你可以改成自己的服务。
- **壁纸播放器 mpv**：安装包里的 `wallpaper-runtime/mpv.exe` 是第三方预编译程序（独立进程运行），
  见 `THIRD-PARTY.md` 的 mpv 一节；Lume 自己写的 mpv 插件 `lume-mpv-stream.dll` 的源码在
  `native-rebuild/platform/windows/wallpaper/lume_mpv_stream.cpp`。

## 构建

见 [`BUILDING.md`](BUILDING.md)。
