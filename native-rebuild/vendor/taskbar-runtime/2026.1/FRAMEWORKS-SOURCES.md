# Windows AppX framework dependencies

These are unmodified Microsoft-signed framework packages used by the pinned
TranslucentTB/Lume Taskbar Core portable startup path. Application-local
MSVC DLLs do not satisfy its dynamic package dependencies.

## Microsoft.VCLibs.140.00

Version: 14.0.33519.0. Publisher ID: 8wekyb3d8bbwe.
The package identity is Microsoft.VCLibs.140.00, not the separately named
Microsoft.VCLibs.140.00.UWPDesktop framework.

Official source: Visual Studio 2022 17.14 package catalog, package version
14.44.35208. Only the release AppX was extracted from each original CAB;
no MSI was installed and no package was repacked.

- x64 SDK component: Microsoft.VisualCpp.CRT.Appx.Msi
- x64 CAB: https://download.visualstudio.microsoft.com/download/pr/d2fa077f-a814-4fb2-b903-1fca7658d17e/dd4dbe036d235dd902ef41f2cb3ec7ce7ab1ebf93e6cc26fb6d7283301188e5a/cab1.cab
- x64 CAB SHA-256: DD4DBE036D235DD902EF41F2CB3EC7CE7AB1EBF93E6CC26FB6D7283301188E5A
- ARM64 SDK component: Microsoft.VisualCpp.CRT.Appx.ARM64.Msi
- ARM64 CAB: https://download.visualstudio.microsoft.com/download/pr/d2fa077f-a814-4fb2-b903-1fca7658d17e/7c0c4411e978befc4f2e577ddebdae154798a82214d14b47c63c2c2dc17c2bbd/cab1.cab
- ARM64 CAB SHA-256: 7C0C4411E978BEFC4F2E577DDEBDAE154798A82214D14B47C63C2C2DC17C2BBD

The Visual Studio 2022 Distributable Code list includes the release AppX at
Microsoft SDKs/Windows Kits/10/ExtensionSDKs/Microsoft.VCLibs/14.0/Appx/Retail/[arch]/Microsoft.VCLibs.[arch].14.00.appx:
https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution

These Microsoft framework binaries retain Microsoft's distribution terms;
they are not relicensed as Lume AGPL or taskbar-core GPL code. See the
accompanying MSVC-RUNTIME-LICENSE.docx and the official redistribution list.

## Microsoft.UI.Xaml.2.8

Version: 8.2501.31001.0. Publisher ID: 8wekyb3d8bbwe.
Source: Microsoft.UI.Xaml NuGet package version 2.8.7, published by Microsoft.

- NuGet: https://www.nuget.org/packages/Microsoft.UI.Xaml/2.8.7
- Download: https://api.nuget.org/v3-flatcontainer/microsoft.ui.xaml/2.8.7/microsoft.ui.xaml.2.8.7.nupkg
- Package paths: tools/AppX/x64/Release/Microsoft.UI.Xaml.2.8.appx and tools/AppX/arm64/Release/Microsoft.UI.Xaml.2.8.appx
- License: WINUI-LICENSE.txt, copied unmodified from license.txt in that NuGet package.

## Verification and hashes

All four AppX files have Framework=true, the expected architecture and identity,
and an Authenticode status of Valid with signer Microsoft Corporation. Both CAB
hashes were compared to the Visual Studio package catalog before extraction.
The AppX files and their signatures are retained byte-for-byte.

| Architecture | File | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| x64 | Microsoft.VCLibs.x64.14.00.appx | 896581 | 9C17B521F9D690A1F504DA5108ED6EEC5669EB3A8FD1331EEF43E40D84E74283 |
| arm64 | Microsoft.VCLibs.arm64.14.00.appx | 1570406 | C73D0F55DDA331F9DCBEFC99FF5A420B62120773D2917387639382AA478533EE |
| x64 | Microsoft.UI.Xaml.2.8.appx | 4932225 | 1421DBA0B7F1DA87E6AD52E17C25D38BCD2159BD7F91C85034CFA837238ABAAA |
| arm64 | Microsoft.UI.Xaml.2.8.appx | 5051718 | 052AB1507191902467340A3856BE4A77C3A6AD51A7A33CAE54CC1F5C813E500A |

Total framework payload: 12,450,930 bytes before installer compression.
