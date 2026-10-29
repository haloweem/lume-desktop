# 最小化动画来源记录

核对日期：2026-09-26。

只读来源：
`H:\refactor\history\lume1.0.1\src\vscode-windhawk\bundled-mods\macos-minimize-animation.wh.cpp`

本目录的 `upstream/macos-minimize-animation.wh.cpp` 是原样快照，仅供审查和许可
追溯，不参与编译，也不包含 Windhawk 引擎。两份文件的实测属性相同：

- 文件长度：91,217 bytes。
- SHA-256：`D25AD2313446C8A7AC58158EB8DD6897B4E9E9F09FCBEA99D110E4B43A19C320`。
- 顶部原始声明：`@id macos-minimize-animation`，`@version 3.1.2`，
  `@author Abdullah Masood`，`@github https://github.com/Abdullah-Masood-05`，
  `@license MIT`。
- 原文件引擎注释：`Ported from Potassiumuncher's genie engine`。

直接移植的算法是 `CalculateLampVertexMacOS`（现代 20×20 网格）与
`MacGenieAnimThreadClassic` 的逐扫描行收口／尾段淡出。原版 Direct2D 图像画刷、
单元变换、网格边缘膨胀和圆角掩模也作为实现基线。

本次未复用旧 Wh_SetFunctionHook 运行时、MinHook、Electron、Node、VS Code
或旧安装器；Windows CBT/CallWndProcRet 适配、IPC、进程租约、配置持久化、
失败回滚和会话都是独立实现。具体差异与完整 MIT 许可文字见 `NOTICE.md`。

分发应保留 `NOTICE.md` 和本来源记录。`build-minimize-hosts.ps1` 会复制为
`LumeMinimize.NOTICE.md` 与 `LumeMinimize.SOURCES.md`。
