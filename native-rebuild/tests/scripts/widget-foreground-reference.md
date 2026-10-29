# 黄历、性能与收纳的固定前景对照

状态（2026-09-27）：已完成 96 DPI 九场景修前对照、三类生产前景窄修及
96/120/144/192 DPI 共 36 场景复拍和图片检查；随后完成部分行裁切修复，
新增网格/列表中间滚动，最终四档共44场景复拍。结果与剩余边界见
`docs/verification/2026-09-27-widget-foreground.md`。截图、哈希和日志保存在
`build-artifacts/native-rebuild/verification/widget-foreground-20260927`。
资源矩阵独占期间不要运行以下命令。这里不使用旧的
`minimize-animation-verification` 目录。

沿用 `capture-weather-reference.mjs` 的方法：生产 `WidgetCanvas` 输出透明
BGRA；Edge 加载只读原版 `index.html` 及其真实样式、脚本；双方使用同一固定
数据、DIP 尺寸和 DPI，在 `#202020` 背景上输出原版、原生、并排和差异图。

本流程新增：

- `tests/unit/widget_foreground_capture.cpp`：仅调用三个生产绘制入口，不创建
  桌面窗口，不启动性能采样器、收纳图标 Host 或用户配置会话。
- `capture-widget-foreground-reference.mjs`：固定时间和桥接输入，验证实际
  DOM 文本、过滤顺序与原生快照一致，再导出对照和几何数据。
- `capture-widget-foreground-reference.ps1`：串联既有探针与 Node；不自动
  构建或安装依赖。

## 默认尺寸和状态

尺寸直接来自只读原版 `widgets/widget-layout.js:4`，折叠尺寸来自
`widgets/runtime.js:25`。Node 每次执行都会读回这些声明进行校验。

| 场景 ID | DIP 尺寸 | 固定输入 |
| --- | --- | --- |
| almanac-default | 320 × 152 | 上海时区 2026-09-26 12:00；原版 lunar 与原生 Lookup 各自计算，文本必须完全相同 |
| performance-default | 340 × 216 | CPU 33.6%、GPU 18.2%、内存 10/16 GiB、下载 2.5 MiB/s、上传 80 KiB/s |
| performance-unavailable | 340 × 216 | 同一 error 状态；全部数值 unavailable |
| organizer-empty | 420 × 392 | 原版默认名称、空集合 |
| organizer-icons | 420 × 392 | 六个固定项目、图标布局 |
| organizer-collapsed | 180 × 216 | 同一集合、折叠、前四项 |
| organizer-search | 420 × 392 | 同一集合，通过原版真实搜索按钮及 input 事件输入“项目” |
| organizer-search-empty | 420 × 392 | 同一集合，搜索“不存在” |
| organizer-list | 420 × 392 | 同一集合、列表布局 |
| organizer-icons-scrolled | 420 × 392 | 20个固定项目、网格滚动47 DIP，首尾行部分可见 |
| organizer-list-scrolled | 420 × 392 | 同一20项集合、列表滚动17 DIP，首尾行部分可见 |

默认 96 DPI；另支持 120/144/192 DPI，分别对应 125%/150%/200%。
每个 DPI 使用单独目录。原生输出像素尺寸
必须等于 `DIP × DPI / 96`，浏览器 `deviceScaleFactor` 必须相同，输出 PNG
还会再次校验物理像素尺寸。支持这些参数不代表已完成对应 DPI 验收。

收纳两端使用同一份 48×48 合成固定 BGRA 位图，原版 renderer 仍负责图标
尺寸、滤镜、阴影及排版。它用于隔离布局差异，不代替真实 Shell 图标提取
验收。测试文件路径不会被打开；原版桥接层的写入、打开及删除操作均拒绝。

## 父代理接线片段

已由主代理加入共享 `tests/unit/CMakeLists.txt`，获准图形工作后才构建。视觉复核仅
修改黄历、性能、收纳三个生产 view 和测试工具；最终滚动修复另在 WidgetCanvas
新增直接 D2D 裁切栈，没有额外线程/缓冲。没有修改共享材质、宿主或 CMake。
该工具不注册到常规 ctest，避免争用图形
验收资源或向未指定目录输出图片。

```cmake
add_executable(lume-widget-foreground-capture EXCLUDE_FROM_ALL widget_foreground_capture.cpp)
target_compile_definitions(lume-widget-foreground-capture PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_libraries(lume-widget-foreground-capture PRIVATE
  lume-almanac-view lume-performance-view lume-organizer-view ole32)
set_target_properties(lume-widget-foreground-capture PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MINGW)
  target_link_options(lume-widget-foreground-capture PRIVATE -static)
endif()
if(MSVC)
  target_compile_options(lume-widget-foreground-capture PRIVATE /utf-8)
endif()
lume_enable_native_warnings(lume-widget-foreground-capture)
```

## 允许图形工作后的执行方式

在仓库根目录运行，先由主代理构建上述目标，再执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  native-rebuild/tests/scripts/capture-widget-foreground-reference.ps1 -Dpi 96
```

默认探针：
`build-artifacts/native-rebuild/release/lume-widget-foreground-capture.exe`。
默认输出：
`build-artifacts/native-rebuild/verification/widget-foreground-reference-96dpi`。
本次任务的自定义输出也应放在本仓库 `build-artifacts` 内。后续修正前景时，
将修正前的报告保留在独立目录；不要覆盖唯一的修正前证据。
脚本从已有天气对照
`build-artifacts/native-rebuild/verification/weather-reference-20260926/reference-layout.json`
读取原版 widgets 目录，当前为：
`H:\refactor\history\lume1.0.1\src\lume-electron-shell\widgets`。

当前发现的既有依赖：

- Node：`C:\Program Files\nodejs\node.exe`。
- Playwright：
  `C:\Users\admin\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\node_modules\playwright`。
- 浏览器：既有天气流程使用的 Edge channel `msedge`。

依赖或构建位置不同可以传 `-NativeProbe`、`-OriginalWidgets`、
`-PlaywrightDirectory`、`-OutputDirectory`。已有 BGRA 和
`native-capture-provenance.json` 后只重跑原版对照：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  native-rebuild/tests/scripts/capture-widget-foreground-reference.ps1 `
  -Dpi 96 -ReferenceOnly
```

## 产物与判断

每场景生成 `original-<id>.png`、`native-<id>.png`、
`comparison-<id>.png`、`difference-<id>.png`。保留原始 `<id>.bgra`、
`foreground-manifest.json`、`native-capture-provenance.json` 和
`foreground-reference-layout.json`。

报告保存完整原版源文件哈希、原生 EXE 哈希、BGRA/PNG 哈希、浏览器版本、
固定时钟、DPI、DOM 几何和字体、原生黄历面板与收纳 cell 几何及差值。
PowerShell 在捕获前后核对 EXE 哈希并保存原始清单、全部 BGRA 哈希；
Node 拒绝使用与该次捕获不匹配的 EXE、清单或像素。因此重建探针后必须
重新捕获原生图片，不能仅用 `-ReferenceOnly` 给旧像素标注新 EXE 哈希。
每次成功或异常结束都会重新核对原版文件哈希。原版数据不符、脚本错误、
尺寸或 DPI 不符时拒绝生成“有效对照”报告。

差异图和 RGB 数字是定位工具，没有自动“视觉通过”阈值。逐张检查文字基线、
溢出裁剪、字重、图标形状、列间距和状态布局，记录明确差异后再由主代理决定
是否修改生产绘制。

初版源代码静态比对发现以下问题；2026-09-27 已通过截图确认并修正字重、描边、
名称省略号和搜索框；整行裁切随后也通过红绿测试与四档滚动对照修复，
以下为初版差异的来源记录，详见最终报告：

- 黄历原版 `#almanac-lunar` 为650字重，初版原生 bool 参数走600；
  最终日期测量与绘制已统一为650。
- 性能原版网络速率字重550，初版原生600；最终已恢复550并校正基线和位置。
- 收纳空态原版为48×48描边 SVG，初版原生为40×40填充块；
  最终空态与标题旁四格已恢复原版描边。
- 初版收纳对越出 viewport 的整行直接跳过；最终已修复为裁切部分可见行，
  完全不可见行跳过，可见区域保持命中，视口外区域不命中。
- 收纳原版图标名称有 `text-overflow: ellipsis`，初版只裁剪；
  最终收纳名称已补省略号，固定长名称对照已复核。
- 收纳搜索框原版有1px描边；最终已补边框，并按16px SVG尺寸修正放大镜。

## 截图后的人工记录

已查看 96 DPI 九张 `comparison-*.png`，以及 120/144/192 DPI 每档覆盖九场景的
三张未缩放拼版 `review-group-*.png`；原始并排 PNG 均保留。125%/150%/200%
记录来自各自实际截图目录。

| 场景 | 当前状态 | 需要核对 |
| --- | --- | --- |
| almanac-default | 四档已检查 | 日期拟合、字重、两行干支、默认宜忌裁剪；默认多余滚动条已移除 |
| performance-default | 四档已检查 | 已补边框占位、标签基线与网络 550 字重 |
| performance-unavailable | 四档已检查 | 状态圆点、占位符、空进度条 |
| organizer-empty | 四档已检查 | 已恢复描边空态与三段纵向间距 |
| organizer-icons | 四档已检查 | 已修标题、搜索图标、列间距、图标位置和名称省略号 |
| organizer-collapsed | 四档已检查 | 四图标位置、标题位置、搜索按钮 |
| organizer-search | 四档已检查 | 已修搜索边框、查询文本和结果排列 |
| organizer-search-empty | 四档已检查 | 搜索框和空结果提示的位置 |
| organizer-list | 四档已检查 | 24px 图标、文字位置和默认六行 |
| organizer-icons-scrolled | 四档已检查 | 47 DIP滚动，首尾部分行像素与可见区域命中 |
| organizer-list-scrolled | 四档已检查 | 17 DIP滚动，首尾部分行像素与可见区域命中 |

最终四档证据在 `viewport-after-96/120/144/192`，滚动修前在
`viewport-before-96`。原九场景在最终四档中的原生 BGRA 与上一轮完全同哈希，
详见 `viewport-final-summary.json`。不要用要求11场景的新版脚本重标注旧9场景目录。

边界：原版通过真实 `nativeGlass` 更新切入 `native-glass-ready`，因此黄历
活动面板背景与外层玻璃均由本次对照排除。原生端只画前景，同样不调用共享
材质。不能据此宣称液态玻璃、折射、桌面图层、实时数据、Shell 图标提取、
焦点编辑框/IME 或动画已经验收。
