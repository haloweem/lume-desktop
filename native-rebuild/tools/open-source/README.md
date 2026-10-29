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

## 免责声明

- 本仓库源代码仅供学习、研究与技术交流使用。
- 在线音乐功能仅在用户以**本人的第三方音乐平台账号**登录后，于该账号已有的权限范围内播放内容、读取歌单与歌词，
  仅供个人学习、欣赏使用，不涉及任何商业用途，Lume 亦不以该功能谋取商业利益。
  本项目与相关平台不存在任何合作、授权或从属关系，相关名称与商标归其各自所有者所有。
- 本项目**不提供、也不接受**任何破解、解锁会员、绕过付费、下载或导出受保护内容的功能或代码贡献。
  歌曲、歌词、封面等内容的著作权归其权利人所有。
- 使用第三方服务须遵守该平台的用户协议。因使用本项目导致的账号限制、封禁或其他后果，由使用者自行承担。
- 请勿将本项目用于任何违反法律法规或侵害他人合法权益的用途。基于本项目的修改版本及其使用，由修改者和使用者自行负责。
- 如权利人认为本项目的内容侵犯了其合法权益，请通过 GitHub Issues 联系，我们会在核实后及时处理。
- 本程序按“现状”提供，不提供任何明示或默示的担保，详见 `LICENSE` 第 15、16 条。

## 构建

见 [`BUILDING.md`](BUILDING.md)。
