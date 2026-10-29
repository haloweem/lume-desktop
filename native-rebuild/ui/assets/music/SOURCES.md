# 音乐页资源来源

参考根目录：`H:\refactor\history\lume1.0.1\third_party\folia-major\upstream`，只读。

- `forest.webp` 为官方 Forest（forest-2）森林的静态单帧，取自 `https://static.lumedesktop.com/lume/wallpapers/official/hero-thumbnails/forest-2.jpg`，转换为 1920×1078 的静态 WebP（质量 90）。随包本地提供，默认及加载失败回退使用该图，不播放视频；原雪地火车图已移除。
- `../../music-icons.js` 由参考工程已安装的 `lucide-react` 导出 SVG，保留原图标路径、24×24 viewBox、2px 描边及圆端点。许可见 `lucide-LICENSE.txt`。
- 构建时提取脚本：`../../tools/extract-music-reference.mjs`。React、React DOM 和 Lucide 的运行时代码不进入产品。

布局与材质参照 `MusicHomeNextLayout.tsx`、`Grid3D.tsx`、`OnlineProviderConnectPanel.tsx` 与 `src/index.css`。首批只还原首页、列表及本地交互；播放器液态玻璃折射、动态壁纸特效与在线提供商需独立验收，不能由本文件推断已完成。
