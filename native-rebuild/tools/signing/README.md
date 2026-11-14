# Signing a local Lume acceptance installer

The optional signing path uses an existing code-signing certificate in the
Windows `My` certificate store. Its private key stays with its registered
provider (for example SimplySign Desktop); these scripts do not accept a PFX,
password, activation code or exported private key.

After the certificate has been activated and the provider is available, run
from the repository root, replacing the public certificate thumbprint:

```powershell
.\build-artifacts\native-rebuild\msvc-run.ps1 -Command ".\tests\scripts\package-original-pages.ps1 -IncludeWallpaperValidationRuntime -WallpaperValidationOnly -AllowDirtySource -VerificationMode Full -CertificateThumbprint '<40-digit certificate thumbprint>' -TimestampUrl 'http://time.certum.pl'"
```

`CertificateStoreLocation` defaults to `CurrentUser`; `LocalMachine` can be
selected explicitly. `SignToolPath` and `MakeAppxPath` can select exact Windows
SDK executables. The timestamp endpoint must be the certificate provider's
RFC 3161 endpoint. The certificate selector's SHA-1 thumbprint is an identifier;
file and timestamp signatures are requested with SHA-256.

The signing path:

1. Validates certificate dates, explicit Code Signing EKU, key usage, private-key
   provider and the existing trusted chain. It imports no trust.
2. Writes the certificate's exact Subject into a CMake initial cache and
   rebuilds. Shell metadata, the MSIX manifest and identity config share that
   Publisher.
3. Signs and verifies the explicit eleven-file Lume ownership allowlist before
   staging. Valid timestamped signatures from the same certificate are verified
   and reused. Microsoft, pinned taskbar and mpv dependencies keep their bytes.
4. Builds and signs the notification MSIX, then uses the normal identity
   validation and installer registration path.
5. Supplies Inno Setup with `SignTool=LumeSign` and `SignedUninstaller=yes`.
   Compilation signs the installer and generated uninstaller. Its public JSON
   configuration is inherited through a temporary process environment variable,
   which is restored after compilation.
6. Retains and verifies the actual signed compiler input bytes before each
   callback returns, including the uninstaller template that Inno deletes after
   embedding. Verifies the installer, retained uninstaller template, extracted
   own products and extracted MSIX. `signing-evidence.json` maps signatures and SHA-256 hashes
   to source and installed paths. Existing extraction, probe and delivery hashes
   bind the final signed bytes.

Without signing parameters, the previous packaging path remains available.
`-NotificationIdentityPackage` still accepts a separately signed identity package
in that path; it cannot be combined with automatic certificate signing.

Signing failures stop the package before replacing the delivered installer.
Version, local acceptance scope, player provenance and release gates retain
their existing meanings. No installation, upload or publication is performed.

The isolated control-flow test uses mock signatures and a minimal Inno fixture;
it never signs with a real certificate or changes a certificate store:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File native-rebuild/tests/scripts/code-signing-packaging-test.ps1
```

With an activated certificate and an already signed own executable, optional
`-SigningConfigurationPath <package/signing-configuration.json>` and
`-SignedFilePath <already-signed.exe>` run a read-only real-certificate preflight.
It exercises the complete Windows PowerShell callback with a broken module
search path, checks the existing signature and evidence JSON, and compares the
file hash before and after. A verify-only proxy rejects every SignTool `sign`
request. The callback explicitly loads its host's built-in Management, Utility
and Security modules; it does not depend on modules inherited from `pwsh`.

Relevant primary documentation:

- https://learn.microsoft.com/windows/win32/seccrypto/signtool
- https://learn.microsoft.com/windows/msix/package/sign-app-package-using-signtool
- https://jrsoftware.org/ishelp/topic_setup_signtool.htm
- https://jrsoftware.org/ishelp/topic_setup_signeduninstaller.htm
- https://jrsoftware.org/ishelp/topic_compilercmdline.htm
