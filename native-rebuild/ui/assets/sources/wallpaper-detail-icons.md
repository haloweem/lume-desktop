# Wallpaper detail icons

The wallpaper detail controls in `pages.js` reuse the icon paths from the
historical Lume UI dependency packages. The paths and view boxes are unchanged;
CSS size, color and fill presentation are adapted to the original Lume controls.
This file is included in the staged UI assets.

## Font Awesome Free 7.1.0

Icons: `faArrowLeft`, `faDownload`, `faTrashCan`, `faHeart`, `faCropSimple`,
`faHardDrive`, `faClock`.

Author: Fonticons, Inc. <https://fontawesome.com>

Source: `@fortawesome/free-solid-svg-icons` version 7.1.0.

License: Creative Commons Attribution 4.0 International (CC BY 4.0).

- License text: <https://creativecommons.org/licenses/by/4.0/>
- Font Awesome Free license: <https://fontawesome.com/license/free>
- Original dependency: `node_modules/@fortawesome/free-solid-svg-icons/LICENSE.txt`

The original package states:

> The Font Awesome Free download is licensed under a Creative Commons
> Attribution 4.0 International License and applies to all icons packaged
> as SVG and JS file types.

Only the listed SVG icon data is included here. No Font Awesome font files or
library runtime code are redistributed by this change.

## Lucide Settings2 / Plus 1.37.0

Source: `lucide-react/dist/esm/icons/settings-2.mjs`, version 1.37.0.
The original four path/circle nodes are unchanged.

添加壁纸卡片的加号来自同版本 `lucide-react/dist/esm/icons/plus.mjs`。
保留原始两条路径 `M5 12h14` 与 `M12 5v14`，只调整显示尺寸和描边粗细；
不依赖字体基线，也不引入 React 或 Lucide 运行时。

ISC License

Copyright (c) 2026 Lucide Icons and Contributors

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

### Plus 的上游 Feather 声明

同版本 `lucide-react/LICENSE` 将 `plus` 列为来自 Feather 项目的图标。
以下保留该图标对应的原始版权与 MIT 许可：

The MIT License (MIT) (for the icons listed above)

Copyright (c) 2013-present Cole Bemis

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
