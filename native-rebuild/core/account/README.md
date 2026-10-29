# 独立原生账号后端

## 接入接口

`apps/shell/account_session.h` 定义 `lume::shell::AccountSession`。构造、公开方法、
销毁和 `Notify(const std::wstring&)` 均属于创建该对象的 UI 线程。

- `AccountSessionOptions` 保留 `storageDirectory`、`verification=false`。
- 可注入 `transport`、`credentials`、`clock`、`openBrowser`、`chooseAvatarFile`、
  `ensureLoginProtocol`。后台端口使用 shared ownership。
- `bool Restore()` 只启动异步恢复，不等待网络。重复调用不重复恢复。
- `std::wstring HandlePage(const ui::AccountPageCommand&, HWND owner=nullptr)` 返回公开快照；
  状态变化另通过 `Notify` 通知，composition 可以忽略其返回值。
- `bool HandleCallback(std::wstring_view)` 只表示回调匹配并已入队，不表示登录成功。
- `std::wstring State() const` 不包含 token、state、nonce、verifier 或 code。
- `HandleWallpaper` 接收受限 `stats`、`endStats` 和 `like` 命令；
  `WallpaperState()` 返回独立的公开统计快照，复用通知通道但不改变账号快照结构。
- `RecordWallpaperDownload(id, sourceUrl)` 只由成功下载入库后的原生回调调用，
  不接受页面传入下载地址，不改变壁纸 CDN 下载链路。
- `void Shutdown()` 断开消息窗口并取消在途任务；普通关闭保留已验证刷新凭据，生产最多等候 16 秒以完成已消费的刷新响应落盘；注入验证端口保持 200ms 上限。明确退出账号/取消登录仍立即清除凭据。
- `bool HasWork() const` 只在恢复、授权、任务或已登录会话存在时为 true。
  `verification` 缺少显式测试端口时不创建生产端口，空恢复不延长零组件关闭。

后台线程拥有 `account::Backend`；通过 message-only window 将结果送回 UI。
任务带 generation 和取消标记。generation 表示账号生命周期，普通 FIFO 入队不推进世代；
退出、取消登录、设备失效及关闭才使旧世代失效。
分钟设备检查使用独立的 `devicePending`，不设置页面 `busy`，不重复入队，
成功且公开资料未变时不发布快照。检查期间可以编辑昵称；用户保存、刷新或头像操作
在同一 worker 后排队，并立即以 `busy=true` 确认接受。设备检查完成不能提前清除用户任务的 busy，
也不能覆盖用户操作结果；设备失效则取消队列并保留 `device-inactive` 原因。
退出会立即清除公开账号与本地凭据，再排队清理可能竞争的写入。
取消登录也覆盖“凭据已提交但 UI 尚未消费结果”的窗口期。
正常 WinHTTP 请求每 25ms 检查取消，最迟 15 秒超时；异常注入端口不合作时，关闭超时后
仅保留该 worker 自己持有的共享状态和端口，绝不保留 session/UI 指针。
注入端口及其函数捕获的数据也必须具有相应的共享生命周期。

主线程只需 include `cmake/Account.cmake`，将 `lume-account-session` 链接给 composition，
在启动调用 Restore，路由受限页面命令和激活回调，在退出调用 Shutdown。
账号目标不依赖桌面应用、WebView、页面或安装器。

## 壁纸公开统计

壁纸统计、点赞和下载记录与账号操作共用串行 worker，但不占用账号前台 `busy`
或设备检查的 `devicePending`。选中壁纸使用单独的 generation 和取消标记；
换壁纸、离开详情、换号和退出均不能接受旧选中项的结果。客户端不读取或上传旧
`favorites.json`，不把设备收藏归属给某个账号。
统计/点赞触发的令牌续期使用账号生命周期取消标记，不使用选图取消标记；
切换壁纸不会打断已消费旧刷新令牌的续期提交。账号世代变化即使仍是游客身份，
也会重新请求当前选中壁纸的统计，避免授权超时后永久停在加载态。

`GET /api/wallpapers/stats?id=...` 允许游客；`POST /api/wallpapers/like` 使用
原生保存的 Bearer，发送显式目标状态。计数仅接受 JavaScript 安全范围内的非负整数。
页面只收到公开的 likes/downloads/liked/canLike/busy/error，不接收凭据。

生产下载现在先经过 `AuthorizeWallpaperDownload`：游客每日 3 张，普通登录用户由服务端统一限制为每日合计 10 张，`2503676207@qq.com` 通过服务端白名单享受无限额度；动态与静态共用额度，退出账号后立刻回到游客上限。以北京时间自然日累计设备总量，重启/注销/换号不重置；本地 `%LOCALAPPDATA%/Lume/Account/wallpaper-quota.dat` 对游客做本地失败关闭保护，登录用户的最终额度交给服务端判断，以支持白名单账号。保留服务端 grant/ticket 约束：先预留本地额度，再申请并消费票据，确认后才允许 CDN 媒体字节；票据仍仅允许固定账号域名、无 Bearer 的 GET 302，不跟随重定向、不转发媒体。生产缺少授权端口时拒绝下载，静态/动态及应用触发下载走同一路径。下载完成不再重复申请票据。

游客额度弹窗的副文案只显示“登录后享受更多额度”，界面不公开每日具体张数；已登录时不显示登录引导。中文与英文错误提示均只保留“今日下载额度已用尽”的含义。

Windows DPAPI 账本接受登录预留上限 `SignedInWallpaperReservationLimit`，并兼容旧的 10 张有界调用；读取和退款支持超过 10 次的历史计数和超过 10 个账号条目，游客仍只接受固定 3 张上限。账本保留 v1 格式与已有计数，序列化后不超过读取器的 32768 字节边界；不因升级或退出而清空额度。回归同时覆盖真实 Backend + DPAPI + 假服务端授权链，避免内存端口通过而生产存储拒绝登录预留的遗漏。

授权失败时撤回本地预留；已获授权后的媒体传输失败/用户取消仍按一次授权计数，沿用服务器票据不可退还的语义，不能把本地退款声称为服务端退款。当前计数是下载授权而非成功入库流量审计。客户端不能代替服务器实现跨设备总额度、抵御用户删除本地状态或旧客户端绕行；服务端规则及发布证据统一见 `docs/current/README.md`。

## 文件职责

- `core/account/security.*`：严格 JSON/Unicode、昵称、PKCE、一次性事务、目标限制、短错误码。
- `core/account/jwt.*`：RS256/RS512 完整验签及 OIDC 声明验证。
- `core/account/ports.h`：HTTP、凭据、取消和图片窄接口。
- `core/account/backend.*`：发现、换令牌、恢复、资料、头像及设备租约调用链。
- `core/ui/account_page_protocol.*`：页面 JSON 命令解析。
- `platform/windows/account/crypto.cpp`：CNG 随机、SHA、RSA 验签。
- `platform/windows/account/credentials.*`：当前用户 DPAPI、原子写入、独立 UUID-v4 设备 ID。
- `platform/windows/account/http.*`：WinHTTP 异步请求、TLS、禁止自动重定向和代理失败安全回退。
- `platform/windows/account/avatar.*`：原生选择文件、大小检查和 WIC 完整解码。
- `platform/windows/account/protocol_registration.*`：明确点击登录时修复当前用户协议归属。
- `apps/shell/account_session.*`：UI 线程生命周期、worker、结果消息、分钟设备检查。
- `tests/unit/account_*_test.cpp` 和 `tests/unit/support/account_*`：隔离测试和真实签名 fixture。
  fixture 不在生产目标源列表中，生产源也不包含测试头。

## 身份与兼容性依据

旧源码 `auth-controller.js:418-420` 在资料更新前明确要求
`account.id === state.profile.subject`；`enrichPublicProfile` 也仅在两者匹配时合并资料。

Casdoor 官方 `object/token_jwt.go` 的 `generateJwtToken` 设置
`RegisteredClaims.Subject = user.Id`，ID token 继承该身份。
查阅版本：`96d9cc0ad49072971ca0c51d22618154398dddd4`。
源码位置：
`https://github.com/casdoor/casdoor/blob/96d9cc0ad49072971ca0c51d22618154398dddd4/object/token_jwt.go`

因此同时保留 `userinfo.sub == id_token.sub` 和 `get-account.id == 已验证sub`。
测试明确区分 UUID `id` 与 `owner/name`，不会把后者当作等价身份。
这是旧客户端及官方默认实现的依据，不代表已验证部署实例的真实授权结果；
自定义 JWT 属性如果改写 sub，将被拒绝而不是静默放宽身份绑定。

设备 ID 使用 CNG 随机的标准 UUID-v4，读取兼容旧客户端
`^[a-f0-9-]{32,80}$`（忽略大小写），不是自定义 base64url 格式。
刷新可不返回 ID token；此时依赖 DPAPI 内保留的已验证 subject，并重新绑定 userinfo。
刷新 ID token 可以省略 nonce；若包含 nonce，必须等于最初授权时保存的 nonce。

## 刷新轮换与故障边界

R0 表示原刷新令牌，R1 表示服务端轮换后的刷新令牌。以下保存顺序适用于刷新及凭据恢复；
首次授权登录仍需完成身份、设备注册和资料校验后才保存凭据并报告登录成功。

1. 固定来源发现校验和 JWKS 预取先于 token POST；发现结果可在同一 worker 内缓存，
   JWKS 每次换令牌前重新获取，包括最终未返回 ID token 的刷新请求。
   公共端点网络失败、401、403 或 503 不代表账号失效，不消耗 R0；
   恢复服务后可继续使用原凭据。
2. token 响应只依赖已取回的 JWKS 在本地执行完整 JWT 验签与声明检查，
   不在消耗旧凭据之后再引入 JWKS 网络请求。刷新无 ID token 时保留此前验证过的 subject。
3. 刷新响应验证后立即原子持久化 R1，并更新 worker 私有令牌，
   然后才请求 userinfo、设备和资料。上述后段临时失败保留 R1 及最后确认的公开资料，
   同 worker 重试或重启恢复均使用 R1；失败的首次恢复不发布 signed-in 或伪造 profile。
4. 身份不匹配、失效设备、令牌失效和凭据保存失败仍清除本地账号，不报告成功。
   取消前后检查、退出时立即清理及 FIFO 后续清理共同覆盖 R1 提交与注销竞争。
5. 受保护业务 GET/额度授权首次 401 时进行一次受校验的令牌刷新再重试；第二次 401 仍失效。业务 403 仅表示请求权限失败，不当作删除账号凭据的证据；设备明确 `active=false` 仍立即退出。首次恢复遇临时网络/服务错误时按 5–300 秒退避自动重试，成功恢复前不伪造已登录。普通关闭的续期 POST 独立于 UI 取消并在 JWT 验证后保存 R1；明确退出账号的取消不可被普通关闭覆盖。

JWT 签名/声明错误，或服务端在 JWKS 预取与 token 响应之间切换为预取结果中不存在的 key，
均拒绝该响应，不以未验证 JWT 保存账号。响应丢失、进程在收到 R1 与持久化之间崩溃、
损坏的 token 响应及磁盘故障，无法仅由客户端保证挽回已经撤销的 R0；
可能需要重新授权。没有服务端幂等协议时不重放结果不确定的 token POST。
这些边界不同于资料/设备临时 503，后者不会再丢失已验证的轮换凭据。

## 安全边界

- 原版 issuer、clientId、scope 和 callback 固定，不接收页面指定的网络地址或路径。
- 5 分钟事务使用 CNG state/nonce 和 PKCE S256。重复参数、错误来源、过期及重放均拒绝。
- JWT 使用 CNG PKCS#1 验签，检查 issuer/aud/azp/exp/nbf/iat/sub/nonce 和出现的 hash claim。
  不使用未验签 JWT 建立身份。
- 发现和 JWKS 为公开 GET；Bearer 仅到固定 auth/account 主机及固定业务端点。
- WinHTTP 禁止自动重定向、cookie 和自动认证；不关闭 TLS 证书校验、不使用 HTTP。
  仅连接/解析/自动代理配置失败可直连重试；TLS 错误和不确定的 POST 超时不自动重放。
- 昵称 2 至 32 个 Unicode 标量，拒绝控制字符。公开资料同时遵守页面字符串上限。
- 图片最大 2 MiB，JPG/PNG/WebP 均检查内容并完整原生解码；
  同时限制单帧、4096×4096 和最大像素数。平台缺少所需解码器时拒绝该格式。
- 头像上传使用 account 端点取 ticket、无 Bearer 的签名 PUT、再更新 Casdoor avatar；
  目前签名 PUT 只允许腾讯 COS 区域主机，显示下载允许固定 Lume 主机和 COS。
  未知存储主机拒绝，不借机扩大页面 CSP。
  COS PUT 的 401/403/服务错误映射为 `avatar-upload-failed`，不清除账号凭据，
  不更新 Casdoor avatar，保留旧头像并允许重新申请 ticket 后重试。
- 页面头像只得到空串或不超过输入图片上限生成的 png/jpeg/webp data URL。
  远程头像不可用时留空。
- DPAPI 当前用户加密刷新凭据，额外绑定存储目录；临时文件原子替换失败不报告登录成功。
- 本地退出标记 `local-logout`，不声称服务端撤销成功。删除失败为 `storage-failed`；
  必须修复磁盘权限后重试，不能把仍有凭据的磁盘状态称为成功退出。
- 登录按钮才调用 `EnsureLoginProtocol`，固定当前 exe、HKCU `Software\Classes\lume`、
  `--login-callback "%1"`，去掉旧 DelegateExecute 并读回核对。失败不打开浏览器。
  生产构造、恢复和快照均不调用注册入口。
  注册表适配器使用第二次 `RegGetValueW` 返回的实际字节数，不把容量查询的额外空间计入字符串。
- 安装/卸载、跨用户协议清理仍属于主线程/安装器，不由账号后端操作。

## 隔离验证

在项目根目录运行：

```powershell
cmake -S native-rebuild/core/account/isolated -B build-artifacts/native-rebuild/account-isolated/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-artifacts/native-rebuild/account-isolated/debug -j 3
ctest --test-dir build-artifacts/native-rebuild/account-isolated/debug --output-on-failure
ctest --test-dir build-artifacts/native-rebuild/account-isolated/debug -R "^lume-account-(rotation|session|background)$" --repeat until-fail:5 --output-on-failure
```

单测共 10 项：security、jwt、storage、avatar、http、backend、session、protocol、rotation、background。
由 `cmake/Account.cmake` 在 `BUILD_TESTING` 下注册，CTest 名称为 `lume-account-<名称>`。
按测试先行记录了缺少实现的红灯，以及 UUID、公开字段限制、选择器异常、
取消结果投递竞态和协议注册失败的回归红灯，再实现并运行绿灯。
审查回归另覆盖 R0 真失效的轮换 fixture、userinfo/设备/资料 503 后的 R1 重试与重启、
JWKS 故障发生在轮换前及轮换响应后的请求次序、COS 401/403/503 不丢登录、
慢设备检查不发布 busy、用户操作实际排队执行、背景结果不解锁前景 busy、
R1 已保存后的注销竞争及设备失效取消队列。

JWT fixture 由测试专用 CNG RSA 密钥实际签名；DPAPI 使用 build-artifacts 下独立目录；
协议单测同时使用内存端口与真实 Windows 适配器。真实适配器测试通过
`RegOverridePredefKey` 将该测试进程的 HKCU 指向唯一隔离键，并在结束时撤销重定向、删除自建键。
覆盖空协议标记、旧命令替换、DelegateExecute 清除、重复注册及登录入口放行。
测试不真实登录、不改用户实际协议/启动项/资料，不启动系统浏览器。
HTTP 单测在请求前验证目标和取消，没有发送外网请求。
手工只读获取过公开发现及 Casdoor 官方源码；原生公开发现/JWKS 网络验证由主线程另行执行。

测试二进制、初期红灯 build 和失败测试数据均位于
`build-artifacts/native-rebuild/account-isolated`，不在源目录。
隔离测试使用上述独立构建目录；共享 Release 整合回归、提交及打包由主线程统一执行。

## 尚未宣称通过

真实账号授权、部署实例刷新轮转、服务器资料修改、实际头像 signed PUT、
设备替换行为及真实登录点击后的 HKCU 注册归属，仍需用户授权的人工验收。
上述调用链有生产实现及隔离 fixture 覆盖，不能将 fixture 成功写成真实授权成功。
