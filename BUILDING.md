# 构建说明

只支持在 Windows x64 上构建，产物是 x64 程序。以下版本是官方构建实际使用的版本；更新的版本通常也可以，
但不保证与官方安装包逐字节一致。

## 1. 环境

| 工具 | 官方构建版本 | 说明 |
|---|---|---|
| Windows | 10 / 11 x64 | |
| Visual Studio 2022 Build Tools | MSVC 14.44（cl 19.44.35227） | 安装“使用 C++ 的桌面开发”工作负载，包括 MSVC x64/x86 生成工具、Windows SDK、C++ CMake 工具 |
| Windows SDK | 10.0.26100.0 | 需包含 C++/WinRT 头文件（`cppwinrt/winrt/Windows.Graphics.Capture.h`），材质模块要用 |
| CMake | 3.25 或更高（官方用 4.3.2） | |
| Ninja | 1.13.2 | 或使用 Build Tools 自带的 Ninja |
| PowerShell | Windows PowerShell 5.1 或 PowerShell 7 | 构建材质 DLL、打包脚本 |
| Node.js | 24.x（官方用 24.15.0） | 只用于运行界面测试和重新生成音乐特效包（可选） |
| Inno Setup | 6.x | 只用于生成安装程序（可选） |
| innounp | 任意版本 | 打包脚本用它解开安装包做校验（可选） |

首次配置时 CMake 会从 nuget.org 下载固定版本的 WebView2 SDK（1.0.2903.40，校验 SHA-256），
需要能访问网络；下载后缓存在 `build-artifacts/dependencies/`。如果代理或防火墙导致下载失败
（例如报 `SSL peer certificate ... was not OK`），可以手动下载
`https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.2903.40/microsoft.web.webview2.1.0.2903.40.nupkg`，
改名为 `sdk.zip` 放到 `build-artifacts/dependencies/webview2-1.0.2903.40/`，CMake 会校验它的 SHA-256。
除此之外不会联网下载依赖，所有第三方代码都已随源码提供。

## 2. 编译

在 “x64 Native Tools Command Prompt for VS 2022”（或先运行 `vcvars64.bat`）里，进入本仓库根目录：

```bat
chcp 65001
cd native-rebuild
cmake --preset windows-release
cmake --build --preset windows-release
```

`chcp 65001` 让 Ninja 正确识别中文系统下编译器的依赖输出，建议保留。

产物在 `build-artifacts/native-rebuild/release/`（与 `native-rebuild/` 同级）。主要文件：

- `LumeShell.exe`：主程序；`LumeLauncher.exe`：启动器
- `LumeMusicHost.exe`、`LumeIconHost.exe`、`LumeMinimizeHost*.exe`：辅助进程
- `LumeGlass.dll`：材质；`lume-mpv-stream.dll`：mpv 插件
- `ui/`：界面网页资源；`taskbar-runtime/`：任务栏核心

直接运行 `LumeShell.exe` 即可启动。壁纸播放还需要 mpv：把 `mpv.exe`（及其 `d3dcompiler_43.dll`）
放到 `build-artifacts/native-rebuild/wallpaper-runtime/`，或从已安装的 Lume 目录的 `wallpaper-runtime/` 复制。

## 3. 测试

```bat
ctest --preset windows-release
```

界面测试（在 `native-rebuild/ui/` 下）：

```bat
node --test tests/
```

部分测试依赖桌面环境（真实窗口、显卡、Edge），在无桌面的环境中会失败或跳过。

## 4. 生成安装程序（可选）

需要 Inno Setup 6 和 innounp。默认在 `%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe` 和 WinGet 的 innounp 位置查找，
也可以用环境变量 `LUME_ISCC`、`LUME_INNOUNP` 指定。还需要上一节提到的 `wallpaper-runtime/mpv.exe`。

打包脚本要求源码在一个干净的 git 工作区里（它记录源码提交号）。在仓库根目录先 `git init` 并提交一次，然后：

```bat
cd native-rebuild
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\scripts\package-original-pages.ps1 ^
  -IncludeWallpaperValidationRuntime -WallpaperValidationOnly -VerificationMode Quick -OutputDirectory <输出目录>
```

## 5. 重新生成音乐特效包（可选）

`native-rebuild/ui/music-visualizers/vendor/`、`ui/music-effects/reference/` 等目录里的 `.js`
是由同目录 `source/` 下的原始源码用 esbuild 0.25.12 生成的，生成脚本在 `native-rebuild/ui/tools/`。
直接使用已生成的文件即可构建；要修改特效时：

```bat
set LUME_ESBUILD_PATH=<esbuild 0.25.12 的 node_modules/esbuild 目录>
node tools/extract-music-3d.mjs
node tools/extract-music-3d.mjs --check
```

各目录的 `manifest.json` 记录了所有输入和输出的 SHA-256，`--check` 会逐字节核对。

## 6. 目录结构

| 目录 | 内容 |
|---|---|
| `native-rebuild/apps/` | 各程序入口：shell（主程序）、launcher、music-host 等 |
| `native-rebuild/core/` | 与平台无关的模型与逻辑 |
| `native-rebuild/platform/windows/` | Windows 实现：窗口、壁纸、任务栏、组件、材质、音乐协议 |
| `native-rebuild/ui/` | 设置与音乐界面（WebView2 页面，原生 ES 模块，无需打包） |
| `native-rebuild/vendor/` | 随附的第三方代码与运行时 |
| `native-rebuild/tests/` | 单元测试与打包脚本 |
| `native-rebuild/packaging/` | Inno Setup 安装脚本 |
| `native-rebuild/cmake/` | CMake 模块 |
