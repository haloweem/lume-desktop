# 第一批 UI 还原来源与范围

本目录是静态 HTML、CSS、ES modules；不包含 Electron、React、Ant Design、Node 或 VS Code 运行时。`tools/extract-reference.mjs` 是一次性构建工具，读取历史源码、写入本目录；页面不加载它。页面无文件系统 API，无远程请求，`connect-src 'none'`。

## 参考根目录

历史根目录 `H:/refactor/history/lume1.0.1`；下文 `panel/` 指 `src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/panel/`。

用户参考截图：

- `C:/Users/admin/Desktop/53934d57daf51ff92cfebe119d57c719.png`：首页。
- `C:/Users/admin/Desktop/c64d97800ec13f8ad3f2650eb32ebe3e.png`：官方壁纸。
- `C:/Users/admin/Desktop/0a0139221ae2283f731fcda561a1bd68.png`：桌面美化四卡。
- `C:/Users/admin/Desktop/dc2206164f093f17686db76f1a30fe88.png`：桌面组件七卡。

所有历史文件只读。截图用于视觉核对，没有作为网页整图背景。

## 原布局参数

|来源|保留的结构/参数|
|---|---|
|`panel/DesktopBeautyExperience.tsx`、`.styles.ts`|首页全幅背景，中央四项胶囊、右侧搜索、左右箭头、圆点、左下信息与“前往”按钮。源码 `HomeHoverOverlay` 实际常显 `opacity:1`，并非鼠标悬停才显示。|
|`panel/DesktopBeautyHomeLayout.spec.ts`|全幅轮播、不额外添加内容分区；四项导航顺序；列表无外层玻璃框；组件页不显示主导航。|
|`ModuleToolbar`|`top:clamp(54px,7vh,72px)`；左右 `clamp(36px,6.5vw,120px)`；三列 `minmax(0,1fr) auto minmax(0,1fr)`；间距 `clamp(18px,2.4vw,44px)`。小于1180px左右24px、间距16px。|
|`HomeModuleTabs`|高度48px，padding5px，圆角999px；按钮最小92px，高36px，14px/760字重；白色高亮随4项切换。|
|`src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/capsuleMaterial.ts`|背景 `rgba(26,36,52,.46)`，边框白色.14，`blur(22px) saturate(1.2)`；原阴影。|
|`ModuleSearch`|高42px，宽不超过480px，padding0 14px，圆角50px；背景 `rgba(17,20,27,.62)`，`blur(24px) saturate(1.18)`；原阴影。|
|`HomeNavButton` / `HomeDots`|箭头56×56px，距左右56px、垂直居中；圆点距底 `clamp(88px,10vh,112px)`，8px/选中11px，间距10px。|
|`HomeHoverOverlay`|左76px，top `calc(50% + 72px)`；宽 `min(520px,calc(100% - 160px))`；标题42px/1.12/850。|
|`DesktopBeautyExperience.tsx` 轮播 effect|10,000ms换图，手动切换后重新计时；前一图留在底层，新图0.72秒淡入。减少动态效果时不自动换图。|
|`ExperienceRoot` / `DesktopBeautyLayout`|外padding `clamp(50px,5.2vh,64px) clamp(24px,4vw,72px) clamp(4px,.8vh,10px)`；内部顶部 `clamp(76px,8vh,92px)`，左右 `clamp(20px,1.8vw,32px)`；原760px高度响应规则。|
|`panel/ModuleCard.tsx` / `mediaCardGridModel.ts`|四列flex，16:9，最小宽 `clamp(260px,18vw,380px)`；行间距 `clamp(34px,2.35vw,54px)`、列间距 `clamp(22px,1.7vw,38px)`；16px圆角；标题16px/600，说明14px/1.6；纹理700%×100%，七种裁切位置。|
|`panel/DesktopWidgetDetails.tsx` / `widgetManagerModel.ts`|独立七组件列表及返回箭头；时钟设置与详情双标签；启用开关、240px样式选择、恢复默认。时间默认关，样式welfare。|
|`panel/ModDetailsFrame.tsx` / `modDetailsFrameModel.ts`|设置页无边框/无圆角；横padding `clamp(24px,4vw,72px)`；顶部 `clamp(50px,5.2vh,64px) - 8px`。|
|`src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/components/NavigationBackButton.ts`|48×48px返回按钮，背景黑.2，`blur(12px)`，无边框，白.6图标。|
|`panel/wallpaper/wallpaperGlassMaterial.ts`|详情底部胶囊：背景 `rgba(20,24,30,.44)`，边框白.22，`blur(18px) saturate(1.25)`；原阴影。|
|`panel/WindowTitleBar.tsx` → `third_party/folia-major/upstream/shared/lume-titlebar/titlebar.ts`|标题栏高35px，窗口按钮宽46px；原登录/设置/音乐SVG和Segoe窗口字形；透明背景、原hover色；全屏隐藏按钮，顶栏悬停恢复。|
|`panel/AppHeader.tsx`|已阅读。`location.pathname === '/'` 时返回null。首页截图显示的是上述WindowTitleBar，不应再插入80px Logo导航。|

`reference.css`、`titlebar.css`、`icons.js` 来自原文件提取；`app.css` 中动态styled属性改成class/CSS变量，未引入新的布局系统。窗口默认尺寸、1536×864参考画布缩放与全屏切换由宿主管理；34 CSS px 圆角由页面根节点单次裁剪保留合成透明度，UI不额外整体scale。

2026-09-27 试装修复：`startup.js` 等待已存在的首页图片、字体和两帧布局后发送 ready，结束首次壁纸淡入，宿主才显示主窗口；官方目录未到达时不等待或替换内置封面。后续轮播动画保留。失败由宿主有期限的错误页处理。任务栏控件立即显示请求方向，确认状态独立保留，失败回滚；不把请求方向称为系统已应用。当前验证见[试装反馈报告](../../docs/verification/2026-09-27-trial-feedback.md)。

## 资产和时钟

- 官方首页封面来自原版 `wallpaper-library/manifest.json` 的 `carouselCoverOrder`，卡片缩略图和首页/详情高分辨率封面分别使用 `thumbnailUrl` 与 `heroThumbnailUrl`；旧目录或缓存缺少扩展字段时按实际目录顺序回退，不打包内置壁纸图片。
- `assets/black-texture.webp` 来自 `panel/assets/mod-covers/black-texture.webp`，保留原始二进制。
- 六个字体来自 `src/vscode-windhawk/widget-skins/{Welfare,Mond,Inside Dream}/@Resources/Fonts/`：GeosansLight、Ruthligos、Anurati、Quicksand、Allura、Futurist Fixed-width。原来源说明复制到 `assets/sources/`。
- 时钟预览依据各皮肤 `Clock/Clock.ini` 和 `@Resources/Variables.inc`：Welfare中心x180，星期y0/70pt、时间y50/40pt、日期y105/20pt；Mond比例1.06，中心x340×1.06，星期0/40pt×1.06、日期75×1.06/14pt×1.06、时间120×1.06/14pt×1.06；Inside Dream比例.2，中心x1300×.2，星期0/480pt×.2、日期800×.2/60pt×.2、12小时时间910×.2/80pt×.2。
- 原生时钟通过 DirectWrite 私有字体集合加载六份字体；D2D/WIC 绘制透明位图，不依赖字体系统安装。生产设置页已移除新增的折叠预览，恢复旧版控件顺序。保留的浏览器预览工具仅用于历史测试。

## 首批范围与后续接入

下面首批页面说明中的“暂未接入”属于历史阶段。天气、黄历、性能、图标收纳、任务栏、桌面图标和最小化动画现已有独立原生会话与管理页接线，不再使用占位开关；实际验证及剩余项原记录在七项计划 `docs/superpowers/plans/2026-09-26-seven-features.md`（已于提交 `4b2342e` 删除，当前状态见 `docs/current/README.md`）。音乐模块现已完成独立播放宿主、Shell 会话、页面协议、高保真音乐页、本地导入、缓存边界和 provider 接口；在线目录、真实歌词、频谱和 3D 仍未接入。Inside Dream 音频频谱仍未接入；壁纸目录、封面、下载、应用和预览已在当前实现中接通。

已实现：首页10张轮播和详情入口；四项导航；本地搜索；美化四卡；组件七卡；时间与日历设置/详情/恢复默认页面；原生窗口动作；宿主回执控制保存状态；系统/页面隐藏时停止时钟和轮播计时器。

时间组件已接入真实原生桌面窗口、三样式独立外观、旧版右键菜单、颜色/坐标编辑器、拖动锁定与托盘生命周期。Inside Dream 独立音频频谱尚未接入。日历通过独立 CalendarSession 连接原生窗口、RGBA颜色编辑器及version 1偏好存储；时钟schema 3文件不包含日历。DesktopApplication组装时钟、日历、Todo会话与独立提醒、路由命令及统一保活，共用一个托盘；主界面关闭时释放 WebView2，启用的原生组件或待提醒任务可继续保活。日历农历与本地日程已接通，新建、清单、原生编辑、颜色备注、日期时间及提醒字段可用；2026-09-26已接入本地提醒以及日历、编辑器、月份/日期/提醒下拉的共享真实材质。弹层等待首帧，失败或3秒后使用可编辑降级；到点通知窗仍纯色。账号与云同步已取消，新本地操作不追加同步队列，旧数据文件自动导入尚未实现。其余模块与组件保留原卡片、说明和入口，开关禁用，点击进入“暂未接入”。登录、应用设置和音乐本地播放已接通；音乐在线目录、真实歌词、频谱和3D仍未接入。

官方壁纸页读取原生官方目录，恢复原网格及标题/元信息位置；首页按原版顺序显示官方记录，封面进入详情后沿用同一条官方记录的正常下载入口。我的壁纸读取本地库，官方下载完成后可进入详情并应用。图片优先使用官方高分辨率封面，公共静态域名失败时回退到受信任的 COS 源站；列表不创建视频解码器，详情预览按需租用并在隐藏时释放。旧版缓存缺少封面扩展字段时只显示实际目录记录，不伪造内置预览。

Ant Design不随程序分发；开关用同尺寸CSS实现，时间样式使用共用自绘下拉，保留深色弹层与选中标记。`settings-frame.js` 提供返回/标题/说明/标签/内容框架及选择器，组件只提供内容和动作；不承担服务或持久化。原生菜单/编辑器独立为 `platform/windows/widget_ui`，不依赖时钟核心。未接入开关仍禁用。图标使用原标题栏SVG/Segoe字形与内联返回SVG。

## 音乐页面来源与边界

音乐页面只提取历史源码中的布局、材质、图标和交互时序，运行时不加载旧版 Electron、Node、React 或在线服务。页面文件为 `music.html`、`music.js`、`music.css` 和 `music-drop.js`；页面通过 `bridge.js` 的音乐动作白名单与原生 Shell 通信。

原生侧按职责拆分为 `MusicModel`、`MusicSession`、`MusicHostRuntime`、`MusicHostClient`、`WindowsAudioEngine`、`MusicLocalFiles` 和 `MusicProviderGateway`。本地导入使用 WebView2 `AdditionalObjects`，不接受页面直接提交的任意路径；缓存使用固定容量 LRU，最多保留 16 项元数据，单张封面不超过 256 KiB，歌词最多 2000 行，单次拖入最多 32 个音频文件。

未登录状态仍显示本地导入和拖放入口；在线推荐、账户同步、真实歌词、频谱和 3D 仍属于后续范围。完整测试和资源测量口径见 `docs/verification/2026-10-01-music-rebuild.md`。

## 边界协议

`bridge.js` 唯一出站通道为 `window.chrome.webview.postMessage(string)`，严格允许：

```
v1|ready
v1|window|minimize
v1|window|maximize
v1|window|close
v1|window|drag
v1|clock|enabled|true
v1|clock|enabled|false
v1|clock|style|welfare
v1|clock|style|mond
v1|clock|style|insideDream
v1|clock|reset
v1|calendar|enabled|true
v1|calendar|enabled|false
v1|calendar|theme|spacious
v1|calendar|theme|split
v1|calendar|material|liquid-glass
v1|calendar|material|frosted-glass
v1|calendar|textColor
v1|calendar|highlightColor
v1|calendar|reset
```

入站为WebView的message事件对象：`{version:1,type:'settings',clock:{enabled,style},saved:boolean}`，`{version:1,type:'visibility',visible:boolean}`，`{version:1,type:'window',fullscreen:boolean}`，以及 `{version:1,type:'navigate',page:'clock'|'calendar'}`。全部校验后处理。更改设置时不先宣称成功，直到宿主回执；时钟回执false显示失败；无宿主时两个组件都显示未连接。宿主设置回执只更新所属组件控件，不销毁select及预览区域。

日历独立回执为 `{version:1,type:'calendar-settings',calendar:{enabled,theme,material,textColor,highlightColor,locked,running},saved,error,materialStatus,loadStatus,result}`。`materialStatus` 为 `inactive`、`native-starting`、`native-ready` 或 `transparent-fallback` 之一。只允许列出的字段、固定主题与材质、布尔值及严格 `#rrggbb[aa]` 十六进制颜色（入站大小写均可，页面显示大写）。错误限定为空字符串、`invalid/runtime/save/rollback/load`。`CalendarActionForPage` 仅映射启用、主题、材质、恢复默认；颜色命令不带颜色值，留给 Shell 打开原生 RGBA 编辑器。

`CalendarPageState(controller, loadStatus=Loaded, result=Snapshot)` 总是输出固定 `loadStatus` 与 `result`，不拼接任意错误文本。`loadStatus` 来自 `calendar_preferences_store.h`，只能为 `loaded/fresh/recovered-backup/failed`。`result` 只能为 `snapshot/applied/cancelled`；只有 `applied` 且控制器无错误时 `saved=true`。初读、默认值和取消均不称保存成功；取消回当前快照与旧控制器错误，页面保留之前的错误。读取失败显示默认配置未保存提示；备份恢复提示持续至真正成功保存，由宿主在成功保存后改传 `Loaded`。`failed` 可与 `runtime` 等控制器错误共存。JS 为初定协议兼容允许省略两个扩展字段：缺省 `loadStatus=loaded`，缺省 `result` 按 `saved` 推导为 `applied/snapshot`。时钟和日历的 pending/error 完全独立。

日历设置来源为历史 `panel/DesktopWidgetDetails.tsx` 与 `widgetManagerModel.ts`：原描述、经典/舒展主题、液态/磨砂玻璃名称、240×38px颜色控件及28px圆色块；复用 `settings-frame.js`，不改变共享返回位置及卡片材质。当前说明应区分本地提醒、真实材质准备/就绪/降级和未接功能；不能继续沿用C批仅保存提醒字段的历史描述。原生日程编辑与清单几何取自 `calendar-todos.css` 全加载顺序；原生 Unicode EDIT 保留文本编辑能力，数据经独立 TodoSession 与命令端口保存，窗口与 renderer 不读写 JSON。

宿主Ready分别发送时钟与日历快照，然后处理待打开的设置页面。可信WebView命令先投递至管理窗口消息队列，再打开原生模态编辑器，避免在WebView事件回调栈内嵌套消息循环。颜色取消、管理窗口关闭或应用退出均不保存待提交颜色；真正保存成功才清除备份恢复提示。测试可在verification模式注入一次Ready脚本以走真实 `chrome.webview.postMessage`，产品入口不暴露该能力，也未新增页面测试协议。当前宿主接线的验证范围与剩余缺口见 `docs/verification/2026-09-25-calendar-native-window.md`。

应用应通过WebView2虚拟域名只读映射本目录载入（宿主使用 `https://lume.local/index.html`）；本目录所有资产URL仍为相对路径，不依赖网络或localhost服务器。

当前阶段与待验收项见[实施审计](../../docs/verification/2026-09-26-implementation-status.md)。以上参数来源不等于所有页面已完成实窗视觉对照。

## 验证记录（首批页面历史）

以下首批12项与折叠预览记录仅描述前一批实现。当前增加原子reset、原生设置路由、外观快照及DOM回归；生产页没有预览折叠区。当前批次证据见 `docs/verification/2026-09-24-native-clock.md`，不能用历史预览结果代替桌面组件验收。

在本目录运行 `node --test tests/*.test.js`：12项行为测试全部通过。首批7项以先6失败/1通过的空实现开始，再7项通过；P2回归补充5项，分别先观察到预览生命周期3项失败、反馈状态2项失败，修复后12项通过。覆盖出站allowlist、入站字段校验和解绑、无宿主、两层返回、保存回执、轮播边界和搜索、双重可见性定时器生命周期，以及折叠预览无定时器、展开立即刷新、关闭/详情标签/离开页面停止、隐藏恢复、错误/未连接/已保存偏好的反馈生成。

浏览器验证跨多次自动轮播后，未提交搜索值与焦点仍保留，轮播预览层稳定为2层；搜索“Mond”能定位时间组件。浏览器控制台未见error/warn。

运行 `node --check app.js`、`node --check pages.js`、`node --check clock.js` 通过。

浏览器1536×864画布实际检查：首页、四卡、七卡、时间设置及原字体Welfare预览、两层返回、官方预览网格。轮播更新保留搜索控件DOM；宿主和桌面组件持久化需要独立原生集成验证，不能由浏览器预览替代。

### P2回归验证与实窗边界

时钟计时器只在原生窗口与document均可见、clock页面、settings标签、预览details.open同时满足时存在。原生details toggle由capture监听，展开立即刷新，折叠立即取消。卡片及搜索页没有内联.status时，由共享notice显示保存失败或宿主未连接；保存成功只称已保存偏好，并说明桌面显示尚未接入。初始读取成功不会误弹保存成功提示。

`tests/dom-integration.mjs`使用历史工程已有的jsdom开发依赖，无新增依赖、无产品运行时依赖。执行：

```powershell
node tests/dom-integration.mjs H:/refactor/history/lume1.0.1/src/vscode-windhawk-ui/node_modules/jsdom/lib/api.js
```

已验证实际app.js DOM事件链：组件卡片保存失败提示可见、宿主当前state保留且可继续重试、未连接提示、保存成功准确提示；默认折叠0个预览interval、非冒泡toggle展开后1个并立即显示时间、折叠0个、原生隐藏0个/恢复1个、详情标签和离开页面0个。此处是DOM集成验证，不替代真实WebView2里强制保存失败的视觉验收；该失败场景的实窗检查仍为缺口。首批页面只称还原并待逐页验收，不宣称逐像素一致。

`tests/catalog-feedback.html`与`tests/feedback-host.js`是可选的浏览器回归fixture，包含模拟失败宿主和interval计数，绝不作为产品页面使用。`tools/`和`tests/`由StageUi排除。`tools/extract-reference.mjs`使用fileURLToPath解析自身位置并输出到父目录ui，不依赖工作目录。

## 便签页面（notes.js / notes.css）

- 布局与配色参考花笺 Floral Notepaper（https://github.com/Achilng/floral-notepaper，MIT 许可证，Copyright Achilng）：左侧搜索、新建、导入与笔记列表，右侧工具栏、标题与字数/保存状态、格式栏、编辑/分栏/预览，底部状态栏。
- 只参考界面结构与用户提供的截图，没有复制其源码、字体或图标。`notes-markdown.js` 是本项目自写的小型 Markdown 子集解析器，用户 HTML 先转义；链接仅保留不可导航的 `data-href`，图片只展示文字占位。代码和数学表达式独立处理，不会继续经过强调解析；图标为本项目自绘 SVG 路径。
- 行内与块公式使用离线 KaTeX 0.19.0（https://katex.org/，MIT，Copyright Khan Academy and other contributors）。资源来自固定的官方 npm 包 `https://registry.npmjs.org/katex/-/katex-0.19.0.tgz`，保存在 `vendor/notes-math/`，包括官方 `katex.mjs`、仅保留 WOFF2 来源的样式、20 个 WOFF2 字体和 `LICENSE.txt`。这些是 KaTeX 资源，不来自 Floral Notepaper。
- `node tools/vendor-notes-math.mjs` 可重现上述资源：先核验脚本固定的 npm SHA-512 完整性，再提取明确允许的文件并输出 `manifest.json`，记录版本、归档 SHA-256、样式处理规则与每个交付文件的 SHA-256。运行便签不访问 CDN 或 npm。
- `notes-markdown.js` 预览公式使用 `trust:false`、`throwOnError:false`、`maxExpand:1000`、`maxSize:10`，每个表达式创建新的宏表且限制为 8192 个字符。拒绝公式中的外链、图片和任意 HTML；未知命令保留红色可读文本，解析错误显示原文和错误提示。KaTeX 支持其公开的 LaTeX 子集，不代表完整 TeX 引擎。

### 所见即所得编辑器（notes-rich-editor.js）

- `notes-rich-editor.js` 使用固定版本 Tiptap `3.31.4`（MIT，官方 npm 包）和 ProseMirror 依赖，由 `tools/vendor-notes-editor.mjs` 在资源准备阶段通过锁定的 `package-lock.json` 与 esbuild `0.25.12` 打包为 `vendor/notes-editor/editor.mjs`。脚本的 `npm ci` 需要联网取得未缓存的依赖；运行已交付的便签不访问 npm 或 CDN。编辑器通过 Tiptap Markdown 扩展保存 Markdown，不使用手写 `contenteditable` 或 `execCommand` 富文本引擎。
- 引擎包含 StarterKit、任务列表/任务项、表格、硬换行、链接（禁用自动打开/自动链接）和官方数学扩展；数学渲染复用 `vendor/notes-math/` 的 KaTeX `0.19.0` 离线资源。编辑器公式视图使用 `throwOnError:true` 并捕获异常；未知命令或解析错误显示原公式文本与 `.MdMathError` 样式。每个公式使用独立宏表、`trust:false`、`maxExpand:1000`、`maxSize:10` 与 8192 字符上限；不允许公式渲染外链、图片或任意 HTML。
- 原始 Markdown 通过 `setMarkdown()` 加载时不会触发保存回调并清理撤销历史；真实编辑事务通过 `onChange(markdown)` 回传。粘贴只读取 `text/plain`，拖放被拦截，链接不导航，外部图片/HTML不请求或执行。公式节点使用 `.MdMath` / `.MdMathBlock` / `.MdMathError`，可由页面样式统一调整。
- `vendor/notes-editor/manifest.json` 记录上游 npm 归档地址、integrity、依赖版本、许可文件及 bundle SHA-256；所有依赖许可证随包存放于 `vendor/notes-editor/licenses/*.txt`。`node tools/vendor-notes-editor.mjs` 可在干净临时目录重复安装并生成相同 bundle 哈希；本轮复现 SHA-256 为 `47188C7BD62FC54431BA9B4AD1B4DAF9403566A3844F4946A95ADD7D988C3CA9`。
- 最小真实 Edge 回归：`$env:LUME_UI_BROWSER_TESTS='1'; node --test tests/notes-rich-editor-browser.test.js`，覆盖格式按钮事务、撤销/重做、切换便签清理历史、硬换行/任务/表格/数学 Markdown 往返、公式编辑、纯文本粘贴、不可导航链接、禁止外部请求和 HTML/尖括号文本。当前 1/1 通过；未由该用例替代完整 WebView2/Win10/DPI 矩阵验收。
