# 可选的 Windows 通知身份

这个目录提供稀疏包元数据和身份管理脚本。生产安装器校验随包签名 MSIX 的哈希后，在启动 Lume 前为原登录用户尝试注册。系统注册失败时，安装器记录原因并继续启动，依赖系统身份的通知功能可能受限。下次覆盖安装会再次检查。Windows 通知访问仍由用户在产品中主动选择。

身份名称和 ApplicationId 固定在 `cmake/LayerApps.cmake`；`LUME_NOTIFICATION_IDENTITY_PUBLISHER` 是 CMake CACHE STRING（默认 `CN=LumeDesktop`），必须配置为真实签名证书的精确 Subject。Publisher 的 XML 和 JSON 分别转义；版本从 CMake `PROJECT_VERSION` 生成。包清单、exe 内嵌 `msix` 元数据和 `identity-config.json` 使用同一组值。可用 `-DLUME_NOTIFICATION_IDENTITY=OFF` 关闭构建此可选元数据，但这样的构建不能携带生产身份包。

## 当前发行边界

生产身份包必须由可信代码签名证书签署，并与 Shell 内嵌身份和配置中的 Publisher 完全一致。现有本机开发身份的低延迟实测不等于普通用户部署已完成；只有元数据的验收包以 `notification-identity=metadata-only-not-user-ready` 标明边界。仅签名包经过完整验证并随安装器交付时，记录 `notification-identity=signed-auto-registration` 及 `notification-identity-package-sha256=`。这些字段描述安装载荷和注册链，不能替代实机安装、升级、卸载和隐私授权验收。

正式打包把已签名身份包传入 `tests/scripts/package-original-pages.ps1 -NotificationIdentityPackage <trusted-signed.msix>`。入口检查系统签名有效、证书 Subject、清单身份/版本、所需能力及虚拟化策略，再复制为固定名 `notification-identity/Lume.NotificationIdentity.msix`。未提供签名材料时只允许本地验收模式（`-WallpaperValidationOnly`），不自动注册，现有桌面接收路径保留为后备。

也可以通过同一打包脚本的 `-CertificateThumbprint` 和 `-TimestampUrl` 自动重建匹配 Publisher、签署自有程序、制作身份包并签署安装器及卸载器，见 [签名流程](../../tools/signing/README.md)。私钥由已登记的本地或云端提供程序持有，项目不保存私钥、PIN 或手机验证码。

签名注册不会替用户开启通知读取：每位用户仍需在灵动岛设置中开启消息及 QQ，并主动允许 Windows 通知访问。当前 QQ 有系统通知事件通道；微信仍是无正文的任务栏注意提示，签名不使其自动成为逐条聊天通知。旧开发身份若仍使用 `CN=LumeDesktop`，会与真实证书 Publisher 不同；现有注册脚本保留并拒绝接管该身份，需要单独安排迁移，不能将开发机原有授权当作新身份已获准。

本机调试必须由用户明确选择，且系统已经开启开发者模式。以将使用 Lume 的用户身份运行：

```powershell
& 'D:\lume\notification-identity\Register-NotificationIdentity.ps1' -DevelopmentRegistration -InstallDirectory 'D:\lume'
```

脚本只检查开发者模式，绝不修改它。禁止把开发模式注册当作生产部署。生产安装器会自动调用以下受验证路径，也可以人工重试；生产身份包必须由目标电脑已信任且 Subject 等于配置 Publisher 的证书签名：

```powershell
& 'D:\lume\notification-identity\Register-NotificationIdentity.ps1' -IdentityPackagePath 'C:\releases\Lume.NotificationIdentity.msix' -InstallDirectory 'D:\lume'
```

制作 MSIX 时，使用构建输出 `notification-identity/AppxManifest.xml`，并将 `Assets` 保持在清单旁的包内 `Assets` 路径；使用独立身份包制作工具和生产签名流程。此目录不附带证书、私钥、未签名 MSIX，也不自动信任任何签名。清单声明 `runFullTrust`、`userNotificationListener`、`unvirtualizedResources` 和 `graphicsCaptureWithoutBorder`；HKCU/AppData 写入虚拟化被禁用，保持现有设置位置。现有同名包必须 Publisher 和实际外部位置匹配。回装旧版时，若已有更高版本的正式身份，脚本会检查包状态和已注册清单中的应用入口及所需能力，检查通过后直接复用。

`graphicsCaptureWithoutBorder` 用于现有玻璃材质的 Windows Graphics Capture 无边框权限，与 `userNotificationListener` 消息读取权限彼此独立。能力声明只描述应用需要的权限，不代表用户已授权；身份构建、注册和安装脚本均不调用 `RequestAccessAsync` 或修改系统权限。运行时只有系统已允许无边框捕获才启动玻璃采样，未授权、撤权或系统不支持时使用非采样材质回退，避免因添加通知身份而显示捕获边框。制作和部署验证均拒绝缺少任一所需能力的身份包。

自动注册范围是标准交互安装，以及保留原用户令牌的同用户静默升级。安装器同步执行注册，成功后才执行Shell的`[Run]`项；失败会明确记录／提示并阻止自动启动。SYSTEM、服务账户和session 0会被拒绝；注册账户SID还必须与WTS返回的本会话登录用户SID一致，防止RunAs其他管理员冒充原用户。同一用户的UAC提权不会仅因提权被拒绝。其他Windows账户首次启动、企业系统部署等仍需另行安排每用户注册，不能宣称全机账户均已完成。2026-11-10本机已备份并移除旧开发身份、隔离客户端数据，使用真实签名安装器完成原用户自动注册与未登录首启，用户确认QQ自然消息正常；全新Windows账户／系统、升级、卸载及首次授权矩阵仍未完成。

身份脚本在任何JSON、验签和Add-Type前，明确从当前`$PSHOME`加载Management、Utility和Security模块；入口直接引用Common，避免先由Join-Path触发错误自动加载。该修复来自本机真实安装失败的PowerShell事件，污染模块搜索路径的独立Windows PowerShell5.1回归、实际已签MSIX只读校验及最终安装器自动注册均通过。

移除仅影响当前用户、名称/Publisher 和实际 `EffectiveExternal` 路径均匹配当前安装目录的身份：

```powershell
& 'D:\lume\notification-identity\Remove-NotificationIdentity.ps1' -InstallDirectory 'D:\lume'
```

卸载器在删除文件前执行相同检查，不操作其他安装目录或其他用户。若卸载器使用另一管理员账户，原用户需自行执行移除脚本。不删除用户设置、通知或聊天数据。

PNG 直接从现有 `packaging/lume.ico` 提取/缩放，44、150、50 像素；未引入新品牌资源。
