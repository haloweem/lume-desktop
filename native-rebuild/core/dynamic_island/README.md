# Windows 灵动岛

入口是桌面美化页的独立卡片，默认关闭，不依赖 Dock。

## 结构

- `core/dynamic_island/island_model.*`：媒体快照与专注计时。
- `core/dynamic_island/compact_widgets.h`：左右小组件稳定 ID。
- `platform/windows/dynamic_island/island_window.*`：独立 HWND、动画、音乐/提醒展开态、输入与材质。
- `platform/windows/dynamic_island/compact_widgets.*`：收缩态真实读取和绘制；当前页面提供 30 个选项，旧文件暂存 ID 18 保留但不再开放。
- `apps/shell/dynamic_island_session.*`：生命周期、保存/失败回退和共享天气数据申请。
- `ui/dynamic-island.*`、`ui/compact-widgets.js`：设置、左右选择与原生预览。

## 交互

- 收缩态左右可独立选择时间、系统或天气信息。文件暂存选项已移除，旧配置读入时按“不显示”处理，下一次保存时写为 `none`。
- 原先的日历、待办、天气、性能展开页已移除。只有媒体播放和待办提醒有展开态；滚轮只在当前可用的展开态之间切换。没有媒体和提醒时，点击或滚轮不会打开空页。
- 点击普通收缩组件切换展开态；管理页通过明确的设置菜单打开。
- 待办提醒保留 9 秒通知及点击打开待办、查看本次提醒列表的行为。

## 资源与验证

- 天气 worker 仅在选择天气收缩组件或设置预览运行时申请共享数据；性能采样仅在选择网速/CPU/内存相关组件或设置预览运行时启用。关闭预览或取消选择后释放。
- 动画只在过渡期间运行；播放频谱 80ms 重绘只在实际可见时运行。关闭释放 HWND、钩子、定时器、位图、画布和玻璃。
- `lume-dynamic-island-test` 覆盖原生点击、滚轮、左右选择、天气和性能资源启停、提醒、保存回退及预览；`lume-desktop-dynamic-island-test` 覆盖真实管理页协议。
