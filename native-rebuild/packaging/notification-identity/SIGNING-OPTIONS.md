# 面向普通用户的通知身份签名

核对日期：2026-11-07。此说明和打包脚本不创建证书、导出私钥、导入信任、注册身份包、开启开发者模式或发起购买。

现有实时路径需要注册 Windows 包身份。供普通用户从官网安装时，外部位置身份 MSIX 必须具备目标电脑信任的签名。自签证书加手工导入信任、开发模式 loose 注册以及 Windows 11 `-AllowUnsigned` 都是开发/受管理环境路径，不能作为普通用户默认安装方案。微软 unsigned-package 文档明确说明不要将该功能用于广泛分发。

目前没有证书时，可先生成待签 MSIX 并保留为构建产物；它不是可交付的正式通知身份包。已有普通桌面轮询路径可以继续工作，实际实时事件能力以系统运行状态为准。

## 正式可选路径

- **继续官网/现有安装器**：使用目标 Windows 信任的代码签名证书，或者符合资格的公有信任云签名服务。签名证书的完整 Subject 必须和包 Publisher 完全一致；exe 内嵌 `msix` Publisher、生成配置和注册验证也必须同步，不能只改 MSIX 清单。当前 `CN=LumeDesktop` 是开发元数据，不能假定将来颁发的证书 Subject 恰好相同。
- **本机已有证书或硬件令牌**：使用证书存储中的 thumbprint，由 SignTool 调用对应密钥提供程序；脚本不接收 PFX 密码或明文私钥。公有 CA 发行有身份验证与密钥保存要求；微软当前指南说明 OV 私钥应使用 HSM/硬件令牌或提供商的云 HSM，不能假定会拿到可导出的 PFX。自签证书即使本机已信任，也不等于普通用户电脑默认信任。
- **Microsoft Artifact Signing**：需要 Azure/Entra 资源、权限、通过身份验证及公有信任证书配置；服务与身份地区资格需要按当前官方 Quickstart 确认。当前 Quickstart 的组织地区名单比通用签名概览更广，个人公有信任验证仍限美国/加拿大；不能仅凭中文界面或当前电脑地区判断用户资格。此脚本不添加云提供商依赖或代为申请。
- **Microsoft Store 的 MSIX 提交**：需要 Partner Center 开发者账户、应用身份与商店审核；通过审核后商店为 MSIX 重新签名，不要求自行购买 CA 证书。但这是商店 MSIX 分发方案；把现有 EXE 安装器上架并不会让商店代签该 EXE，也不能据此把未签 sparse MSIX继续放官网交付。可行性需结合 Lume 的安装/更新与商店能力审核另做验证。

## 构建脚本

输入必须是与待交付 Shell 同一次构建生成的 `notification-identity` 目录，含 `AppxManifest.xml` 及 `Assets`，不能直接用源码的 `.in` 模板。

仅生成待签包：

```powershell
& .\Build-NotificationIdentityPackage.ps1 `
  -IdentityDirectory 'H:\path\to\matching-build\notification-identity' `
  -OutputPath 'H:\releases\Lume.NotificationIdentity-unsigned.msix'
```

使用已经存在、匹配 Publisher 且本机信任的代码签名证书：

```powershell
& .\Build-NotificationIdentityPackage.ps1 `
  -IdentityDirectory 'H:\path\to\matching-build\notification-identity' `
  -OutputPath 'H:\releases\Lume.NotificationIdentity-signed.msix' `
  -CertificateThumbprint '<证书完整指纹>' `
  -CertificateStoreLocation CurrentUser `
  -TimestampUrl '<所选签名提供商的 RFC 3161 HTTP(S) 时间戳地址>'
```

脚本使用 Windows SDK `MakeAppx /nv /h SHA256`，暂存目录只包含 manifest 与三张既有 logo；logo 按生成清单中的准确相对路径放置，包含 `notification-identity\Assets` 布局，并在打包前检查资源存在，避免 `/nv` 掩盖路径失配。指定证书时检查有效期、EKU/KU、Subject 和现有信任，再用 `SignTool /sha1` 签名、RFC 3161 时间戳及 `/pa` 验证。私钥由本机/令牌提供程序使用，不导出。输出已有同名文件时拒绝覆盖。阶段目录保留以便核对。脚本不安装 MSIX，也不证明整条生产安装/升级流程已经验收；目标干净机器的信任与安装仍由发布验证负责。

## 官方依据

- https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/grant-identity-to-nonpackaged-apps
- https://learn.microsoft.com/en-us/windows/msix/package/unsigned-package
- https://learn.microsoft.com/en-us/windows/win32/appxpkg/how-to-sign-a-package-using-signtool
- https://learn.microsoft.com/en-us/windows/msix/package/sign-app-package-using-signtool
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart
- https://learn.microsoft.com/en-us/windows/apps/publish/get-started
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/choose-distribution-path
