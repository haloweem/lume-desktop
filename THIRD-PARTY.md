# 第三方组件

Lume 客户端整体按 AGPL-3.0 发布（见 `LICENSE`）。其中包含的第三方组件按各自许可使用，
许可原文和来源记录随代码放在对应目录（`licenses/`、`*/licenses/`、`*NOTICE*`、`*SOURCES*`、`manifest.json`）。

## 著佐权（copyleft）组件

| 项目 | 许可 | 位置 | 说明 |
|---|---|---|---|
| Folia Major（chthollyphile/folia-major） | AGPL-3.0 | `native-rebuild/ui/music-visualizers/`、`ui/music-effects/reference/`、`ui/music-background/`、`ui/music-material/`；在线音乐协议 `platform/windows/music_online_provider.cpp`、`platform/windows/music_providers/` | 原始源码保存在各目录的 `source/` 下，`manifest.json` 记录哈希；Lume 的改动见各目录 `README.md`/`SOURCES.md` |
| Mineradio（XxHuberrr/Mineradio） | GPL-3.0 | 星河粒子着色器与几何：`ui/music-effects/reference/`、`ui/music-visualizers/vendor/` | 经 Folia 移植 |
| TranslucentTB 2026.1 | GPL-3.0 | `native-rebuild/vendor/taskbar-runtime/2026.1/` | Windows 11 任务栏核心，独立进程。上游源码提交 `322e2b7395a51975150126276308b415970e080b`，Lume 补丁 `lume-taskbar-core.patch`，详见同目录 `NOTICE.md` |
| mpv（含 FFmpeg、libplacebo） | GPL-2.0+ / LGPL | 安装包 `wallpaper-runtime/mpv.exe`（不在本仓库） | 壁纸播放器，独立进程。版本：mpv v0.41.0-724-g71ebd0840，FFmpeg N-124864-gf8af47b64，libplacebo v7.365.0。源码见 https://github.com/mpv-player/mpv 、https://ffmpeg.org 、https://code.videolan.org/videolan/libplacebo |
| 组件菜单、颜色选择器、位置编辑器 | GPL（见目录 `LICENSE`） | `native-rebuild/platform/windows/widget_ui/` | 来源见同目录 `SOURCES.md` |

## 宽松许可组件

| 项目 | 许可 | 位置 |
|---|---|---|
| Three.js | MIT | `ui/music-visualizers/vendor/`、`motif-vendor/` |
| PixiJS 及依赖（@pixi/colord、earcut、eventemitter3、ismobilejs、parse-svg-path、tiny-lru、@xmldom/xmldom） | MIT / ISC / BSD-3 | `ui/music-visualizers/lumiere/`、`reference*/` |
| React、react-dom、scheduler、use-sync-external-store、@react-three/fiber、its-fine、suspend-react、react-use-measure、zustand | MIT | `ui/music-visualizers/reference*/` |
| framer-motion、motion-dom、motion-utils、@chenglou/pretext | MIT | `ui/music-visualizers/` |
| tailwindcss、@tailwindcss/oxide、postcss、@babel/parser | MIT | `ui/music-visualizers/reference/` |
| esbuild | MIT | 构建工具 |
| lucide / lucide-react、Feather | ISC / MIT | 界面图标 |
| qrcode | MIT | `ui/music-provider-qr/` |
| Tiptap、ProseMirror、marked、linkifyjs、orderedmap、rope-sequence、w3c-keyname、commander | MIT | `ui/vendor/notes-editor/` |
| KaTeX | MIT | `ui/vendor/notes-math/` |
| lunar-javascript 1.7.7 | MIT | 农历与黄历数据 |
| OpenCC 数据、opencc-js | Apache-2.0 / MIT | 简繁转换 |
| nlohmann/json | MIT | `vendor/nlohmann/` |
| dr_flac | Public Domain / MIT-0 | `vendor/dr-flac/` |
| ybouane/liquidglass、iyinchao/liquid-glass-studio | MIT | 材质 `platform/windows/material/` |
| macos-minimize-animation（Windhawk 模组） | MIT | 最小化动画 `platform/windows/minimize_animation/` |

## 图标、字体与皮肤资源

| 资源 | 许可 |
|---|---|
| Font Awesome Free 7.1.0 图标 | CC BY 4.0，署名见 `ui/assets/sources/` |
| 字体 Allura、Quicksand | SIL OFL 1.1 |
| 字体 GeosansLight、Ruthligos、Anurati、Futurist Fixed-width；时钟皮肤 Welfare、Mond、Inside Dream 的布局 | 归原作者所有，来源见 `ui/assets/sources/`；不在 AGPL 授权范围内，再分发前请自行确认其许可 |

## 微软组件

| 组件 | 条件 |
|---|---|
| WebView2 SDK（构建时从 nuget.org 下载） | Microsoft WebView2 SDK 许可 |
| Visual C++ 运行库（`vendor/taskbar-runtime/`） | Visual C++ 可再分发组件许可，见 `MSVC-RUNTIME-LICENSE.docx` |
