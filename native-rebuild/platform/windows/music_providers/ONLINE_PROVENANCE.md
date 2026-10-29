# 在线音乐协议来源

本目录的在线目录适配器以历史项目已经存在的请求路径和字段为基础。
QQ 公开逐行歌词另外参考下述有固定版本的成熟开源实现：

- NetEase：`@neteasecloudmusicapienhanced/api` 4.40.1，历史目录模块位于
  `third_party/folia-major/upstream/node_modules/@neteasecloudmusicapienhanced/api/module`。
  该包声明 MIT；网易云加密请求沿用本仓库已有 `netease_account.cpp` 实现。
- QQ：`third_party/folia-major/upstream/electron/qqMusicBridge.cjs` 及
  `qqMusicPlayback.cjs`。该历史项目整体协议代码按 AGPL-3.0-only 处理。
- QQ 逐行歌词：`yt-dlp/yt-dlp` 标签 `2025.09.26` 的
  `yt_dlp/extractor/qqmusic.py`，`QQMusicIE` 调用 `GetPlayLyricInfo` 时
  `param` 仅含 `songMID`，随后对响应 `lyric` Base64 解码并作为 LRC 字幕使用。
  源码地址：`https://raw.githubusercontent.com/yt-dlp/yt-dlp/2025.09.26/yt_dlp/extractor/qqmusic.py`。
  同标签根 `LICENSE` 声明 Unlicense / public domain；
  本批只参考请求字段及公开测试歌曲 ID，不移植其播放器或下载功能，
  不将原有 Folia AGPL 部分重新许可为 Unlicense。
- KuGou：`kugoumusicapi` 1.5.1 模块及
  `third_party/folia-major/upstream/electron/kugouApiBridge.cjs`。该包声明
  MIT；Folia bridge 代码按 AGPL-3.0-only 处理。

本批不实现解锁、代理、订阅绕过或付费播放；`resolve` 只返回供应商本身授予当前账号的 HTTPS 媒体地址。

## 对应关系

历史树只读根目录：`H:/refactor/history/lume1.0.1/third_party/folia-major/upstream`。
Folia 历史版本：`fa057895f916d0c2482785a150dce51ad6fc928c`。

| 平台 | 实际参考模块 |
| --- | --- |
| 网易云 | `personalized`, `user_playlist`, `top_playlist`, `playlist_track_all`, `song_detail`, `cloudsearch`, `likelist`, `record_recent_song`, `like`, `playlist_create`, `playlist_tracks`, `song_url`, `lyric_new` |
| QQ | `electron/qqMusicBridge.cjs` 的 catalog/mutation/lyrics，包含 `user-playlists` 的自建与收藏双来源；`electron/qqMusicPlayback.cjs` 的授权设备会话与标准音质 Vkey；`src/services/onlineMusic/qqProtocol.ts` 的字段归一化 |
| 酷狗 | `search`, `playlist_track_all`, `playlist_track_all_new`, `user_playlist`, `user_history`, `album_songs`, `top_card_youth`, `song_url`, `playlist_add`, `playlist_tracks_add`, `playlist_tracks_del`, `search_lyric`, `lyric`，以及 `util/request.js`、`util/helper.js`；匿名搜索来自 `src/services/onlineMusic/kugouTransport.ts` |

Lume 修改：移植到 C++ 原生传输；固定 HTTPS 主机/路径；拒绝 UI URL 和任意协议参数；
限制页面/响应/元数据缓存大小；凭证只读；播放仅标准音质，不移植解锁及跨源替换；
仅保留供应商授权地址，不获取、解密或绕过付费保护。

`song_url_v1` 的 xeapi 不在本批移植范围。网易云使用同一历史包中独立存在的
`song_url` 普通接口和 eapi 请求构造，不声称实现 xeapi。

## 原生接线

主会话需将 `platform/windows/music_online_provider.cpp` 加入它负责的构建目标；
本批不修改 CMake、UI、music_session、账号 session 或核心类型。
账号文件只新增 helper 和 `catalog_requests.h` 引用，沿用现有加密及设备构造。

- 调用：`lume::music::online::Execute(provider, action, args, credentials, cancelled, exchange)`。
- `args._userId` 仅由原生凭证租约注入。输入凭证不会被修改或落盘。
- 页面返回 `tracks`、`playlists`、`recommendations`、`more`；各数组最多 100。
  曲目包含 `id/title/artist/album/artwork/duration/source`，时长单位秒。
  集合包含 `providerId/type/id/title/artwork/owned/description`。
- `like` 返回 `{liked: bool}`；其他写操作返回 `{ok: true}` 或新建歌单页面。
- `resolve` 仅返回 `{url}`，仅供原生播放器使用；`lyrics` 返回原始供应商载荷，
  由主会话调用 `ParseOnlineLyrics(provider, raw, duration)`。
- 错误均为 `runtime_error` 的稳定字符串：`service/expired/unsupported/unavailable/invalid-response`；
  取消保持 `cancelled`。
- QQ 的 mediaMid/songId、酷狗的 albumAudioId/listId 等保存在进程内的 2048 项 FIFO 缓存，
  以供应商和凭证 SHA-256 指纹隔离。缓存不包含凭证原文和解析后的播放 URL。
  新会话或缓存淘汰后，添加到歌单需重新读取 `home`，否则明确报 `unavailable`。
- 取消酷狗喜欢会重新读取喜欢歌单的文件 ID，最多扫描 1000 首，避免误用搜索 fileId；
  超出扫描边界报 `unavailable`。

## 本轮补齐

- QQ 首页合并自建及收藏歌单，使用历史 `fcg_user_created_diss` 与
  `fcg_get_profile_order_asset.fcg`。先按自建优先去重，再对合并结果分页，
  不采用两个来源各截一页后直接截断的做法。两个来源独立处理：某一来源
  因登录过期、服务错误或畸形响应不可用时，保留另一来源及公开推荐；只有
  `unsupported` 或 `cancelled` 等调用边界错误继续向上返回。
- 酷狗 `recent` 使用历史依赖 `module/user_history.js` 的
  `POST /playhistory/v1/get_songs`，保留 `source_classify=app`、
  `to_subdivide_sr=1` 和原生账号参数。历史依赖的 `docs/README.md` 中
  “获取用户最近听歌历史”明确解释 `/user/history` 与 `bp` 续页参数。
  先前仅依据 Folia bridge 得出的“没有最近播放接口”结论已纠正；
  不使用听歌排行或历史日推冒充最近播放。
  续页值只从响应 `data.bp` / `bp` 读取并原样传回，不接受 UI 游标，
  不推算游标。响应曲目使用已有历史字段归一化，缺失列表或重复游标报错。
- 酷狗首页订阅专辑保存原生类型元数据，点击仍调用 `playlist(rawId)`，
  原生分派至历史 `POST /v1/album_audio/lite`。
  `x-router=openapi.kugou.com`、`kg-tid=255` 仅开放给该路由；
  `is_buy` 保持历史默认空字符串，绝不伪造已购买。
  上游每页 30 首，按需拼接最多五页以满足 UI 默认 50、最大 100 的切片。
- 酷狗推荐沿用历史 `providers/kugou/provider.ts` 的六张卡片：
  3006、3001、3004、3014、3101、3005。卡片请求来自 MIT 包
  `module/top_card_youth.js`，虚拟集合及缓存分页逻辑参考 Folia AGPL 源码。
  每卡最多 30 首，原生缓存只保存归一化公开字段，不存响应内任意 URL 或凭证；
  点击卡片按缓存快照分页。供应商拒绝推荐时仍报错，不忽略失败。

## 歌词真实格式

适配器没有实现 QRC/KRC 解密或解压。逐行降级调整后，`lyrics` 返回规则如下：

| 平台 | 实际返回 |
| --- | --- |
| 网易云 | JSON 中的明文 LRC / YRC 字符串，未在本适配器转换成统一歌词模型 |
| QQ | 使用 `GetPlayLyricInfo` 公开逐行请求，仅传 `songMID`，`comm={cv:0,ct:24,format:"json",uin:0}`，不传 cookie/authst 或 crypt/qrc 参数。返回 Base64 LRC，须通过现有 parser 后才交付；意外响应 `crypt=1` 仍抛 `unsupported` |
| 酷狗 | 搜索候选后请求 `/download?fmt=lrc`，此参数与 Base64 明文处理明确存在于历史 `module/lyric.js`。原样保留 `content`，复用现有解析器检查真实逐行时序，不生成逐字时间、不新增 `decodeContent` |

返回前调用现有 `ParseOnlineLyrics`，不修改 core parser：
`unsupported-qrc-encryption` / `unsupported-krc-compression` 映射为接口稳定错误
`unsupported`；其他解析失败为 `invalid-response`；没有可用时序行为 `unavailable`。
酷狗即使无视 `fmt=lrc` 返回 `krc1`，也不会伪装成功。
仅校验后保留原始载荷，由 session 再调用同一个解析器，接口不变。

Folia 历史源码全文检索只找到加密请求及
`src/utils/lyrics/providers/qrcDecrypt.ts`；本轮额外核验上述 yt-dlp 固定版本，
找到真实公开 LRC 请求并通过原生匿名实测。这里不是猜改 `crypt=0/qrc=0`，
而是改用原版实现明确使用的 `songMID` 请求分支。
公开歌词不再依赖歌曲详情请求、私有数值 songId、设备会话或真实账号。
返回原生 JSON 仍为 `request.data`，Base64 明文 LRC 原样保留给现有 parser，
行时间与翻译不重写，不生成逐字时间。
原生 QRC 解密、KRC 逐字解码仍未实现；逐行已可用，故本轮不新增解码器或压缩依赖。
未来如单列逐字需求，解密接入点仍是 provider 的 `LyricsPayload` 之前，
只有完整解密成功才清除 `crypt`。

## 明确限制

- QQ：在指定历史 QQ provider/bridge 中未找到最近播放协议，`recent` 仍明确返回
  `unsupported`，不声称平台本身绝无此能力。
- QQ 合并歌单每次从头扫描，各来源最多 10 次、每次 100 条；无法在此上限内完成时
  报 `unsupported`，不返回被截断的“完整”列表。合并后最多 2000 条，单个返回页面最多 100。
- 酷狗最近播放每次从头按原生 `bp` 扫描，最多 10 批、1000 条；
  `offset + limit > 1000` 或扫描仍不足以取得目标页面时报 `unsupported`。
  未取得真实登录响应样本，当前验证是历史请求构造与假传输分页，不等同实账号验收。
- 酷狗专辑类型依赖此前首页登记的原生元数据；新会话或缓存淘汰后应先重新读取首页。
- 酷狗推荐仅覆盖这六个历史卡片及其每卡 30 首快照；未知卡片 ID 报 `unsupported`。
- 酷狗搜索及首页账号歌单分页要求 offset 与 limit 对齐；所有 offset 最大 10000，
  limit 默认 50、上限 100。
- 网易云最近播放仅覆盖上游最近 100 首；大歌单仍受原生 256 KiB 响应硬上限约束。
- 供应商失效、拒绝、畸形响应不伪装为空列表；有明确成功且零结果时才返回空数组。
- 匿名探测只能证明当次匿名请求；真实账号写操作、订阅播放和完整 UI 链路仍需独立验收。

## 验证

运行 `native-rebuild/tests/scripts/test-music-online-provider.ps1` 编译并执行独立测试，
使用真实原生加密/路由校验、虚构凭证和假 HTTP 传输。添加 `-LiveProbe` 才会执行三次
匿名只读搜索，每个平台 limit=2；不读取保存凭证，不登录，不调用真实写操作。
`-LiveLyricsProbe` 另执行一次酷狗匿名搜索、歌词候选搜索和（候选成功时）LRC 下载，
只打印响应状态、结构、解析错误与时间统计，不打印歌词、accesskey 或完整请求 URL。
`-LiveQQLyricsProbe` 使用上述 yt-dlp 的两个公开 songMID 做两次原生匿名歌词请求，
不请求歌曲详情或媒体，不读取任何保存凭证，只打印 crypt、行数和时间统计。
独立测试脚本链接已有 `music_lyrics.cpp`、`music_online_lyrics.cpp`，无需改动 CMake。

2026-10-05 本批验证：

- 新增在线适配器假传输测试通过，包含真实 CNG 加密、eapi 解密断言、
  酷狗签名/播放 key、QQ songMid/mediaMid 分离、全部写请求的虚构账号验证、
  schema/100 条上限、错误/取消、账号隔离、试看拒绝和 URL 白名单。
- 本轮新增假传输覆盖：QQ 收藏/自建去重与跨来源分页、收藏源错误传播和扫描上限；
  酷狗六卡片白名单、缓存切片与账号隔离、专辑不对齐分页和固定请求头、
  最近播放游标原样续取/重复游标/末页/扫描边界；
  QQ 与酷狗歌词格式与错误的精确断言。没有执行本轮真实账号请求。
- 逐行降级专项：假传输经 provider 和实际 core parser 解析，断言起始时间
  1.25 秒和 3.75 秒原样保留、逐字数组为空；覆盖 QQ 加密标记、
  酷狗意外 KRC、空内容、无时序内容、畸形 Base64 的稳定错误。
- 2026-10-05 匿名 LRC 验证未通过：原生歌词候选搜索返回 HTTP 200、
  零字节响应，故没有进入 LRC 下载。补齐历史 `util/request.js` 的固定设备请求头
  后结果相同。直接调用只读历史 `search_lyric` 模块（匿名关键词搜索）亦返回 502。
  未取得原版或原生在线 LRC 样本；只能确认协议接线和假传输解析通过，
  不能宣称酷狗真实歌词链路已经完成验收。
- 2026-10-05 QQ 逐行交付验证通过：完整专属假传输测试通过；
  provider→实际 parser 覆盖冷缓存、rawId/qq:rawId、匿名及已登录均不发送凭证、
  单请求、翻译、真实行时间、拒绝加密响应和畸形/无时序内容。
  两首真实公开歌曲分别返回 `crypt=0`，47 行（首行 0、末行 266.78 秒）
  和 37 行（首行 0、末行 188.82 秒），全部逐字数组为空。
  这证明本次 QQ 公开逐行链路可用，不表示逐字解密或全曲库验收完成。
- 本轮重新编译并执行 `music_provider_http_test` 通过，全程无网络；
  初次独立命令遗漏账号实现导致链接失败，补齐链接源后通过，无需修改账号代码。
- 现有 `music_provider_http_test`、`music_netease_account_test`、
  `music_kugou_account_test`、`music_qq_account_test` 独立构建及执行通过。
- 三次匿名只读搜索：网易云返回 2 首、酷狗返回 2 首；
  QQ 返回协议成功及零结果，不能据此证明其真实账号搜索可用。
- 未运行真实账号写操作、保存凭证读取、完整应用构建或 UI 验收。

## MIT 许可文本

以下许可分别适用于网易云参考代码（Copyright (c) 2013-2022 Binaryify）和
酷狗参考代码（Copyright (c) 2023 MakcRe）；不得将 Folia 的 AGPL 部分重标为 MIT。
Folia 完整 AGPL v3 文本已保存在
`native-rebuild/ui/music-visualizers/vendor/licenses/Folia-AGPL-3.0.txt`，
对应 notice 为同目录 `Folia-NOTICE.md`，发布时需随原生移植代码一并保留。

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
