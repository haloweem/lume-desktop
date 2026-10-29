[Setup]
AppId=LumeInstallerShutdownIsolatedVerification
AppName=Lume Installer Shutdown Verification
AppVersion=1
DefaultDirName={tmp}\LumeInstallerShutdownVerification
PrivilegesRequired=lowest
Uninstallable=no
CreateUninstallRegKey=no
DisableProgramGroupPage=yes
CloseApplications=no
RestartApplications=no
OutputDir={#OutputDir}
OutputBaseFilename=installer-shutdown-verification
Compression=lzma2

[Files]
Source: "{#ShutdownHelperFile}"; DestDir: "{tmp}"; DestName: "LumeSetupShutdown.exe"; Flags: dontcopy
Source: "{#ReplacementFile}"; DestDir: "{app}"; DestName: "LumeShell.exe"; Flags: ignoreversion

[Code]
#include ShutdownCode
