# 通知图标来源

日常关怀和天气的六枚图标于 2026-11-09 使用用户指定的兼容 Images API、`gpt-image-2` 重新生成。用户提供的两张截图仅作造型与风格参考：圆润面形、柔和渐变、左上高光；保留水杯、起身座椅、月亮、雷云、雨伞和降温温度计的语义及配色，不增加底板。

实际产品使用生成图经去除黑色底、透明边缘处理和尺寸归一化后的 256×256 RGBA PNG，并非重绘的 SVG。原始生成图、逐图提示词、来源与最终 SHA-256 清单位于 `output/imagegen/island-icons-v2/`，后处理脚本为 `native-rebuild/tools/prepare-island-icons.py`。同名 PNG 同时供设置页和原生通知缓存使用，原生 256px／512KiB 上限保持。访问密钥不写入项目。

`preview.svg/png` 消息摘要未在本次截图范围内，保持原样。`render-notification-icons.mjs` 现在只从 SVG 导出该摘要，不再覆盖六枚生图资源。QQ、微信也保持原图标。

## 下班提醒

`off-work.png` 于 2026-11-10 按用户要求沿用同一兼容 Images API、`gpt-image-2`、`high`、1024×1024，通过现有 URL 响应兼容入口调用未修改的 imagegen 技能 CLI 生成。现有 `water.png`、`stand.png`、`sleep.png` 仅作风格参考，暖橙色公文包与象牙白小钟表示下班，不含文字或底板。

原始图为纯黑背景 RGB，沿用既有去黑底、抗锯齿边缘恢复和居中缩放算法处理，产品为 256×256 RGBA，主体最长边 216px；无重新绘制。原图、实际提示词、参考图和工具 SHA-256、处理脚本以及深／白／灰底小尺寸预览位于 `output/imagegen/off-work-20261110/`。产品 SHA-256：`F2303EF3819DBF8BD1EEF92535E211F33F79AFE9FAF21E60E0ABEFC5400BF82A`。

## 下班提醒章鱼哥样式

2026-11-10 用户提供章鱼哥图片和语音，并进一步指定清晰版 `872f109167ca118b91e4ba1e44ed4239.jpg` 为最终图片来源；本项不是 AI 生成，未调用图片或转录 API。角色及图片的权利属于其各自权利人。

- `off-work-squidward.png`：从清晰原图 1104×806 的 `[318,0,830,512)` 正方形区域裁出脸部和帽子，等比缩为 256×256 RGBA；以 8 倍分辨率圆形蒙版生成抗锯齿透明边缘，不拉伸、不重绘。文件 102,160 字节，SHA-256：`17D67FA370C9D46EB83371B586FF97C33B6CC2111BC73AD5099859E0B6702A88`。
- `off-work-squidward.wav`：按用户最新要求替换为 `C:/Users/admin/Desktop/1.wav` 的原样副本，3,104,846 字节，RIFF/WAVE PCM（formatTag 1）、双声道、44,100Hz、16bit、776,192 帧，时长 17.6007256 秒。新文件的 PCM 样本与此前版本不同；未剪辑、变速或转码，保留原音频节奏，用户要求在下班时间前 5 秒开始播放。SHA-256：`F388DE714B023581CE957B60AB5BF13E160E8F6DFBCDD834BA31230E4D884AB1`。

- 章鱼哥字幕：`platform/windows/dynamic_island/off_work_voice_captions.h` 的13个字幕点绑定上述音频哈希，来自本地 Whisper base／small 交叉识别与原始 PCM 波形核对；音频未外传。五个数字的起点依次为0、480、1367、2220、2824ms，“我下班了，蟹老板”起点3720ms，后续完整台词按短句显示；末句“油污”由用户明确纠正。前段声学起点误差约15–20ms，后半段连续台词时间为本地识别估计（不同窗口／模型分歧约0.2–0.8秒），尚待用户听看验收，不能称为逐字精确对齐。实际显示使用waveOut设备播放位置，读数暂时不可用时保留最后字幕；设备播放完成不从0重播字幕。分析与复核证据在`build-artifacts/squidward-captions-20261110/`，不随产品分发ASR模型或工具。

原图原样副本、处理脚本、完整来源清单及深／白／灰底 24／32／40px 预览在 `output/imagegen/off-work-squidward-20261110/`，该目录名沿用图标来源组织方式，不表示使用生成模型。

## QQ 与微信

本目录图标于 2026-11-07 从本机已安装应用的原始 Windows 图标资源中提取，仅用于在 Lume 中标识对应应用。未重新绘制、改色或放大。QQ、微信名称及图标归其各自权利人所有。

| 文件 | 本机来源 | 应用版本 | 原资源 | PNG 尺寸 |
| --- | --- | --- | --- | --- |
| `qq.png` | `D:\Software\QQ\QQ.exe` | QQ 9.9.36.53644 | 图标组 1 中的 256 × 256 图像 | 256 × 256，保留透明通道 |
| `wechat.png` | `D:\Software\WeChat\WeChat.exe` | WeChat 3.9.12.51 | 图标组 107 中的 256 × 256 图像 | 256 × 256，保留透明通道 |

提取时两个源程序的 Windows Authenticode 签名均验证为 `Valid`，签名发布者均为 Tencent Technology (Shenzhen) Company Limited。

- `qq.png` SHA-256：`C6F10317D639A544C798EB04FBFBB45C7127DCD95F9083BE480697329C8265E7`
- `wechat.png` SHA-256：`604BD4215A07827D139901EC35D39DDCD216C21A5603642E03633C395F2CFE00`
