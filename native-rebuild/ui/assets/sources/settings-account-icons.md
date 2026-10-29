# 设置与账号图标来源

本声明覆盖 `application-settings.js`、`account.js` 和 `titlebar.js` 使用的图标，
包括从 `icons.js` 复用的静态路径。核验日期：2026-09-29。
以下历史相对路径均以只读目录 `H:/refactor/history/lume1.0.1/` 为根。

## Font Awesome Free 7.1.0

- 来源包：`@fortawesome/free-solid-svg-icons`，实装版本 **7.1.0**，不是仅依据依赖版本范围推断。
- 作者：The Font Awesome Team / Fonticons, Inc.。
- 包内版权声明：Copyright 2025 Fonticons, Inc.。
- 官方项目：<https://github.com/FortAwesome/Font-Awesome>。
- 官方许可说明：<https://fontawesome.com/license/free>。
- 版本核验文件：`src/vscode-windhawk-ui/node_modules/@fortawesome/free-solid-svg-icons/package.json`。
- 许可核验文件：`src/vscode-windhawk-ui/node_modules/@fortawesome/free-solid-svg-icons/LICENSE.txt`。

包的许可表达式是 `(CC-BY-4.0 AND MIT)`。其 `LICENSE.txt` 按资源类型区分：

| 资源类型 | 许可 | 本模块使用情况 |
| --- | --- | --- |
| SVG 图标及 JS 文件中的图标数据 | Creative Commons Attribution 4.0 International，简称 **CC BY 4.0** | 仅使用下表列出的静态 SVG 路径 |
| 非字体、非图标的库代码 | MIT | 未引入 Font Awesome 运行时 |
| 字体文件 | SIL OFL 1.1 | 未引入 Font Awesome 字体 |

图标许可全文：<https://creativecommons.org/licenses/by/4.0/>。
MIT 许可说明：<https://opensource.org/licenses/MIT>。
SVG 路径即使来自 JS 包，仍属于图标数据，不能因此改记为仅受 MIT 许可覆盖。

### 使用清单

已逐一核对下面 10 个图标，当前路径和 viewBox 尺寸与历史 7.1.0 包一致。
只调整了显示尺寸、CSS 颜色、`currentColor` 填充和无障碍 SVG 包装，
没有重绘图标，也没有复制 Font Awesome React 组件或运行时。

| 包导出名 | 标准图标名 | 当前用途 | viewBox 尺寸 |
| --- | --- | --- | --- |
| `faSliders` | `sliders` | 设置侧栏：通用 | 512×512 |
| `faUserGear` | `user-gear` | 设置侧栏：用户设置 | 640×512 |
| `faRocket` | `rocket` | 设置侧栏：开机启动 | 512×512 |
| `faInfoCircle`，别名对应 `faCircleInfo` | `circle-info` | 设置侧栏：关于 | 512×512 |
| `faCamera` | `camera` | 账号：更换头像 | 512×512 |
| `faRotate` | `rotate` | 账号：刷新；关于：检查更新 | 512×512 |
| `faRightFromBracket` | `right-from-bracket` | 账号：退出登录 | 512×512 |
| `faCircleUser` | `circle-user` | 用户设置中的未登录账号图标，复用 `icons.js` | 512×512 |
| `faGear` | `gear` | 标题栏：设置，复用 `icons.js` | 512×512 |
| `faMusic` | `music` | 标题栏：音乐，复用 `icons.js` | 512×512 |

原版实际入口：

- `src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/settings/SettingsWindow.tsx`。
- `src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/settings/UserSettings.tsx`。
- `third_party/folia-major/upstream/shared/lume-titlebar/titlebar.ts`。

上述图标的署名为 **Font Awesome Free 7.1.0 by Fonticons, Inc.**，
按 **CC BY 4.0** 使用；来源、许可链接及本模块的展示适配已在本文件列明。

## Lume 原版图片

### VIP 徽章

本次使用的原文件是：

`third_party/folia-major/upstream/shared/lume-titlebar/assets/vip-badge.webp`

共享标题栏的实际入口 `third_party/folia-major/upstream/shared/lume-titlebar/titlebar.ts`
通过 `./assets/vip-badge.webp` 引用该图。原账号页 `UserSettings.tsx` 引用另一份文件：

`src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/panel/assets/vip-badge.webp`

两份历史文件逐字节一致，均为 480×480 WebP，29,006 字节，SHA-256：

`bff5a94c7f7426c68aafce96a8f1e5ce9736d3da482e5f1e8d00570898f6e517`

当前 `account.js` 的 `VIP_BADGE_URL` 将原图经
`sharp(original).resize(48, 48).webp({quality: 90})` 缩放、重新编码后内嵌为
`data:image/webp;base64`。已复核重新转换结果与内嵌字节完全一致；
没有替换或重新设计图案。内嵌文件为 48×48、1,454 字节，SHA-256：

`062a7ad7eb2f98eb6ff23dc80c80ca1f20eeec845561f7a590548c12a17464e2`

账号页按 24×24 显示，标题栏按 12×12 显示。图片通过现有 JS 随包，
运行时不从历史目录或网络读取。

### Lume 标志

`account.js` 的 `LUME_LOGO` 来自：

`src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/panel/assets/logo-white.svg`

保留原 `path` 和 512×512 viewBox，将白色填充改为 `currentColor`，
用于账号默认头像、标题栏默认头像和关于页标志。标题栏复用同一份本地标志，
没有设置头像或头像加载失败时显示；不新增远程图片请求。

### 图片授权边界

VIP 徽章和 Lume 标志不是 Font Awesome 图标，不适用上述 Font Awesome 许可。
在已核对的历史图片及相邻文件中未找到资产级的独立许可声明；
本文件只记录原版来源与适配，不授予新的图片授权，具体授权范围仍需项目权利方确认。
也不能仅凭 VIP 文件位于 `folia-major` 目录，就将该图片认定为受 Folia 代码许可覆盖。

原生窗口按钮的系统字体字形和既有返回箭头不在本 Font Awesome 清单内。
本声明不修改或取代壁纸页面的既有来源文档。

## 随包要求

本文件须保留在安装目录的 `ui/assets/sources/settings-account-icons.md`。
现有 `native-rebuild/cmake/StageUi.cmake` 与
`native-rebuild/tests/scripts/package-original-pages.ps1` 均允许复制 `assets/sources/*.md`，
`native-rebuild/packaging/LumeSetup.iss` 递归携带暂存的 `ui` 目录。
不需要另改构建规则；分发上述静态图标时须同时保留本来源声明。
