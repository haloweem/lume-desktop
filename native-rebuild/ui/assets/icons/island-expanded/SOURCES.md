# 灵动岛展开组件图标

音乐、日历、待办、时间、性能五枚 PNG 与六枚关怀／天气图标于 2026-11-09 一并生成，使用用户指定的兼容 Images API、gpt-image-2、1024×1024、high。两张用户截图只作为风格和语义参考。保留粉、蓝、绿、橙、青五种分类颜色及圆角底，统一面形、渐变与光照。

生成原图和逐图提示词位于 `output/imagegen/island-icons-v2/`。`native-rebuild/tools/prepare-island-icons.py` 仅去底、处理透明边缘和归一化尺寸，不重绘图形；最终画布 256×256，主体最大 208px，以原有 24／32px CSS 尺寸显示。图片内不含黑色按钮底板，选中态仍由原 CSS 控制。

`expandedWidgetIcon` 是展开选择器、摘要及样式侧栏的共同入口。组件 ID、开关、排序和布局保持。未发布的 Focus 保留原 SVG，不在本次截图和生成范围内。
