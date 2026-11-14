#ifndef SourceDir
#error SourceDir must be supplied by package-original-pages.ps1
#endif
#ifndef OutputDir
#error OutputDir must be supplied by package-original-pages.ps1
#endif
#ifndef OutputBaseFilename
#define OutputBaseFilename "LumeSetup-1.1.3"
#endif
#ifndef IconFile
#error IconFile must be supplied by package-original-pages.ps1
#endif
#ifndef ShutdownHelperFile
#error ShutdownHelperFile must be supplied by package-original-pages.ps1
#endif
#ifndef WebView2RuntimeInstallerFile
#error WebView2RuntimeInstallerFile must be supplied by package-original-pages.ps1
#endif
#ifndef UiIntegrityChecksFile
#error UiIntegrityChecksFile must be supplied by package-original-pages.ps1
#endif
#if FileExists(SourceDir + "\notification-identity\Lume.NotificationIdentity.msix")
  #ifndef NotificationIdentityPackageSha256
    #error Signed notification identity must have its packaging-verified hash
  #endif
  #if Len(NotificationIdentityPackageSha256) != 64
    #error Invalid signed notification identity package hash
  #endif
  #define SignedNotificationIdentity
#endif

[Setup]
#ifdef LumeCodeSigning
SignTool=LumeSign
SignedUninstaller=yes
SignedUninstallerDir={#SignedUninstallerDirectory}
SignToolRetryCount=1
#endif
AppId={{8B29B016-65A4-4F35-8F0A-F6D7B9C10101}
AppName=Lume
AppVersion=1.1.3
AppPublisher=Lume
AppPublisherURL=https://lumedesktop.com/
AppSupportURL=https://lumedesktop.com/
AppUpdatesURL=https://lumedesktop.com/
DefaultDirName=D:\lume
UsePreviousAppDir=yes
UsePreviousTasks=yes
UninstallLogMode=overwrite
DefaultGroupName=Lume
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\LumeShell.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
ChangesAssociations=yes
DisableDirPage=no
DirExistsWarning=no
CloseApplications=no
RestartApplications=no
LanguageDetectionMethod=uilanguage
ShowLanguageDialog=yes
VersionInfoVersion=1.1.3
VersionInfoCompany=Lume
VersionInfoDescription=Lume Setup
VersionInfoProductName=Lume
VersionInfoProductVersion=1.1.3

[Languages]
Name: "chinesesimp"; MessagesFile: "{#SourcePath}\installer-languages\ChineseSimplified.isl"

[Dirs]
; Downloaded wallpapers live beside the program. Lume runs as the signed-in user,
; so that user must be able to write here; uninstalling keeps the library.
Name: "{app}\Wallpapers"; Permissions: users-modify; Flags: uninsneveruninstall
; Account, to-dos, music library and settings live here by default. Same rule:
; the signed-in user writes it and an uninstall keeps it.
Name: "{app}\Data"; Permissions: users-modify; Flags: uninsneveruninstall

[Files]
Source: "{#ShutdownHelperFile}"; DestDir: "{tmp}"; DestName: "LumeSetupShutdown.exe"; Flags: dontcopy
Source: "{#WebView2RuntimeInstallerFile}"; DestDir: "{tmp}"; DestName: "MicrosoftEdgeWebView2Setup.exe"; Flags: dontcopy
Source: "{#SourceDir}\LumeLauncher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LumeShell.exe"; DestDir: "{app}"; Flags: ignoreversion
#if DirExists(SourceDir + "\notification-identity")
; Optional files only: registration is an explicit user action after install.
Source: "{#SourceDir}\notification-identity\*"; DestDir: "{app}\notification-identity"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
#if DirExists(SourceDir + "\Assets")
; Sparse application logos are looked up relative to the installation root.
Source: "{#SourceDir}\Assets\Square44x44Logo.png"; DestDir: "{app}\Assets"; Flags: ignoreversion
Source: "{#SourceDir}\Assets\Square150x150Logo.png"; DestDir: "{app}\Assets"; Flags: ignoreversion
Source: "{#SourceDir}\Assets\StoreLogo.png"; DestDir: "{app}\Assets"; Flags: ignoreversion
#endif
#if FileExists(SourceDir + "\LumeGlass.dll")
Source: "{#SourceDir}\LumeGlass.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LumeGlass.NOTICE"; DestDir: "{app}"; Flags: ignoreversion
#endif
Source: "{#SourceDir}\LumeMusicHost.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LumeIconHost.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LumeMinimizeHost32.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LumeMinimizeHost64.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LumeMinimizeHook32.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LumeMinimizeHook64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LumeMinimize.NOTICE.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LumeMinimize.SOURCES.md"; DestDir: "{app}"; Flags: ignoreversion
#if FileExists(SourceDir + "\wallpaper-runtime\mpv.exe")
Source: "{#SourceDir}\wallpaper-runtime\mpv.exe"; DestDir: "{app}\wallpaper-runtime"; Flags: ignoreversion
Source: "{#SourceDir}\wallpaper-runtime\d3dcompiler_43.dll"; DestDir: "{app}\wallpaper-runtime"; Flags: ignoreversion
Source: "{#SourceDir}\wallpaper-runtime\lume-mpv-stream.dll"; DestDir: "{app}\wallpaper-runtime"; Flags: ignoreversion
Source: "{#SourceDir}\wallpaper-runtime\VALIDATION-ONLY.txt"; DestDir: "{app}\wallpaper-runtime"; Flags: ignoreversion
#endif
Source: "{#SourceDir}\taskbar-runtime\*"; DestDir: "{app}\taskbar-runtime"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\AcceptanceNotes.txt"; DestDir: "{app}"; Flags: ignoreversion
#if FileExists(SourceDir + "\WidgetUI-LICENSE.txt")
Source: "{#SourceDir}\WidgetUI-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\WidgetUI-SOURCES.md"; DestDir: "{app}"; Flags: ignoreversion
#endif
#if DirExists(SourceDir + "\licenses")
Source: "{#SourceDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
#if FileExists(SourceDir + "\WebView2Loader.dll")
Source: "{#SourceDir}\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\WebView2-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\WebView2-NOTICE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\ui\*"; DestDir: "{app}\ui"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\ui-recovery\*"; DestDir: "{app}\ui-recovery"; Flags: ignoreversion recursesubdirs createallsubdirs
; Finish both UI verification and identity registration before ANY [Run] entry.
; ssPostInstall is too late for the silent-update Run entry.
Source: "{#SourceDir}\ui-integrity.stamp"; DestDir: "{app}"; Flags: ignoreversion; AfterInstall: PrepareInstalledLume
#endif

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："

[Registry]
Root: HKLM; Subkey: "Software\Classes\lume"; ValueType: string; ValueName: ""; ValueData: "URL:Lume"
Root: HKLM; Subkey: "Software\Classes\lume"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKLM; Subkey: "Software\Classes\lume\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\LumeShell.exe,0"
Root: HKLM; Subkey: "Software\Classes\lume\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\LumeShell.exe"" --login-callback ""%1"""

[UninstallDelete]
; Hook DLLs moved aside during an update while another process still had them loaded.
Type: files; Name: "{app}\LumeMinimizeHook*.dll.*.old"

[Icons]
Name: "{group}\Lume"; Filename: "{app}\LumeShell.exe"; WorkingDir: "{app}"; IconFilename: "{app}\LumeShell.exe"
Name: "{autodesktop}\Lume"; Filename: "{app}\LumeShell.exe"; WorkingDir: "{app}"; IconFilename: "{app}\LumeShell.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\LumeShell.exe"; Description: "运行 Lume"; Flags: postinstall nowait skipifsilent; Check: IsVerifiedLumeStart
Filename: "{app}\LumeShell.exe"; Flags: nowait skipifnotsilent runasoriginaluser; Check: IsVerifiedLumeUpdate

[Code]
function IsLumeUpdate: Boolean;
begin
  Result := ExpandConstant('{param:LUMEUPDATE|0}') = '1';
end;

function IsVerifiedLumeUpdate: Boolean; forward;
function IsVerifiedLumeStart: Boolean; forward;

#include "installer_shutdown.iss"
#include "installer_ui_integrity.iss"

var NotificationIdentityReady: Boolean;

function IsVerifiedLumeStart: Boolean;
begin
  Result := UiWasVerified and NotificationIdentityReady;
end;

function IsVerifiedLumeUpdate: Boolean;
begin
  Result := IsLumeUpdate and IsVerifiedLumeStart;
end;

procedure RegisterSignedNotificationIdentity;
var
  PackageFile: String;
  ResultCode: Integer;
begin
  if NotificationIdentityReady then
    Exit;
  #ifdef SignedNotificationIdentity
  if not UiWasVerified then
    VerifyAllInstalledUi;
  PackageFile := ExpandConstant('{app}\notification-identity\Lume.NotificationIdentity.msix');
  if CompareText(GetSHA256OfFile(PackageFile), '{#NotificationIdentityPackageSha256}') <> 0 then
    RaiseException('通知身份安装文件校验失败，Lume 尚未启动。请重新下载安装包。');
  Log('Registering the verified signed notification identity as the original user before Shell launch.');
  if not ExecAsOriginalUser(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "' +
    ExpandConstant('{app}\notification-identity\Register-NotificationIdentity.ps1') +
    '" -IdentityPackagePath "' + PackageFile + '" -InstallDirectory "' + ExpandConstant('{app}') + '" -CloseRunningLume',
    ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Log('Signed notification identity registration could not be launched for the original user.');
    RaiseException('无法为原登录用户启动通知身份注册，Lume 尚未自动启动。请查看安装日志并重试。');
  end;
  Log('Signed notification identity registration result: ' + IntToStr(ResultCode));
  if ResultCode <> 0 then
    RaiseException('通知功能注册未完成（退出码 ' + IntToStr(ResultCode) + '），Lume 尚未自动启动。具体原因已记录到当前登录用户的 %LOCALAPPDATA%\Lume\Logs\notification-identity-install.log。请将该日志提供给支持人员。');
  Log('notification-identity=signed-auto-registration; original-user registration completed before launch.');
  #else
  Log('notification-identity=metadata-only-not-user-ready; no signed package, registration skipped, desktop fallback retained.');
  #endif
  NotificationIdentityReady := True;
end;

procedure PrepareInstalledLume;
begin
  VerifyAllInstalledUi;
  RegisterSignedNotificationIdentity;
end;

procedure RemoveOwnedLoginProtocol(RootKey: Integer);
var
  ProtocolCommand: String;
  ExpectedProtocolCommand: String;
begin
  ExpectedProtocolCommand := '"' + ExpandConstant('{app}\LumeShell.exe') + '" --login-callback "%1"';
  if RegQueryStringValue(RootKey, 'Software\Classes\lume\shell\open\command', '', ProtocolCommand) then
  begin
    if CompareText(ProtocolCommand, ExpectedProtocolCommand) = 0 then
      RegDeleteKeyIncludingSubkeys(RootKey, 'Software\Classes\lume');
  end;
end;

function WebView2RuntimeVersion(RootKey: Integer; const Key: String): String;
begin
  if not RegQueryStringValue(RootKey, Key, 'pv', Result) then
    Result := '';
  if Result = '0.0.0.0' then
    Result := '';
end;

function HasWebView2Runtime: Boolean;
begin
  // Setup is elevated: HKCU can belong to a different administrator account.
  // Require the machine-wide runtime that is also available to the launch user.
  Result := (WebView2RuntimeVersion(HKLM,
      'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}') <> '');
end;

procedure EnsureWebView2Runtime;
var
  ResultCode: Integer;
  Attempts: Integer;
begin
  if HasWebView2Runtime then
    exit;
  ExtractTemporaryFile('MicrosoftEdgeWebView2Setup.exe');
  ResultCode := -1;
  if (not Exec(ExpandConstant('{tmp}\MicrosoftEdgeWebView2Setup.exe'),
      '/silent /install', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)) or (ResultCode <> 0) then
    RaiseException('WebView2 Runtime 安装失败（错误码 ' + IntToStr(ResultCode) + '）。请检查网络后重试，或先安装 Microsoft Edge WebView2 Runtime。');
  for Attempts := 1 to 60 do
  begin
    if HasWebView2Runtime then
      exit;
    Sleep(500);
  end;
  RaiseException('WebView2 Runtime 安装失败：未检测到有效版本。请检查网络后重试，或先安装 Microsoft Edge WebView2 Runtime。');
end;

<event('PrepareToInstall')>
function PrepareWebView2Runtime(var NeedsRestart: Boolean): String;
begin
  Result := '';
  try
    EnsureWebView2Runtime;
  except
    Result := GetExceptionMessage;
    Log(Result);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  RunCommand: String;
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    { Remove only the current user's identity with this exact external location.
      The script preserves registrations belonging to other installs/users. }
    if FileExists(ExpandConstant('{app}\notification-identity\Remove-NotificationIdentity.ps1')) then
    begin
      if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
        '-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "' +
        ExpandConstant('{app}\notification-identity\Remove-NotificationIdentity.ps1') +
        '" -InstallDirectory "' + ExpandConstant('{app}') + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
        Log('Unable to launch optional notification identity cleanup.')
      else if ResultCode <> 0 then
        Log('Optional notification identity cleanup did not complete; preserved unmatched registrations.');
    end;
    RemoveOwnedLoginProtocol(HKLM);
    RemoveOwnedLoginProtocol(HKCU);
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Lume', RunCommand) then
    begin
      if CompareText(RunCommand, '"' + ExpandConstant('{app}\LumeShell.exe') + '" --tray-only') = 0 then
        RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Lume');
    end;
    { The logon task Lume keeps beside the Run entry (startup_task.cpp). }
    Exec(ExpandConstant('{sys}\schtasks.exe'),
      '/Delete /TN "Lume Startup (' + GetUserNameString + ')" /F', '', SW_HIDE,
      ewWaitUntilTerminated, ResultCode);
  end;
end;
