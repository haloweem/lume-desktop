# 灵动岛网页预览

源码演示页 `native-rebuild/ui/dynamic-island-preview.html` 不进入安装包。先用 MSVC 构建 `lume-island-preview-demo`，再在仓库根目录运行：

```powershell
node native-rebuild/tools/island-preview-server.cjs
```

默认地址为 `http://127.0.0.1:8876/dynamic-island-preview.html`；可用 `LUME_ISLAND_PREVIEW_PORT` 改端口。服务只绑定本机回环地址，图片和动画帧由原生测试程序按需生成，缓存在被 Git 忽略的 `build-artifacts/island-preview-server/`。页面使用随包 Forest 示例壁纸和示例音乐，不反映桌面正在播放的内容。
