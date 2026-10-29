# 来源与改动

动画曲线与逐行漏斗的来源是 Lume 历史源码
`src/vscode-windhawk/bundled-mods/macos-minimize-animation.wh.cpp`，版本 3.1.2，
作者 Abdullah Masood，原文件声明 MIT 许可证；原文件注明现代 genie 引擎移植自
Potassiumuncher。窗口捕获、默认选项及任务栏定位语义同样以该文件为依据。

保留原作者版权与许可声明：本模块涉及的原版移植部分按 MIT 许可提供。

MIT License

Copyright (c) Abdullah Masood and Potassiumuncher

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

本次适配以 Windows CBT 事件替代原来的六个函数跳板，不带入 Windhawk 引擎。
现代引擎保留 20×20 网格曲线、图像插值与圆角掩模，使用按需加载的 Direct2D
软件 DC 渲染，避免每个应用创建独立 D3D 设备。经典引擎保留逐行漏斗与尾段淡出。
修正退化曲线除零、有界快照分配、仅拥有的 DWM 属性恢复、停止与父进程退出
的取消路径；目标 GUI 线程完成 PrintWindow，避免绘制线程等待该线程自锁。

不保留原版无限时长的最小化快照缓存，还原在目标窗口恢复后、DWM cloak
期间重新捕获。UIA 查找有事务超时和枚举上限。窗口不支持捕获、首帧准备超时、
进程位数/完整性级别不匹配时，系统默认最小化/还原继续执行。
