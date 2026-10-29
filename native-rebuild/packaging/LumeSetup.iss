#ifndef SourceDir
#error SourceDir must be supplied by package-original-pages.ps1
#endif
#ifndef OutputDir
#error OutputDir must be supplied by package-original-pages.ps1
#endif
#ifndef OutputBaseFilename
#define OutputBaseFilename "LumeSetup-1.0.8"
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

[Setup]
AppId={{8B29B016-65A4-4F35-8F0A-F6D7B9C10101}
AppName=Lume
AppVersion=1.0.8
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
VersionInfoVersion=1.0.8
VersionInfoCompany=Lume
VersionInfoDescription=Lume Setup
VersionInfoProductName=Lume
VersionInfoProductVersion=1.0.8

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
Filename: "{app}\LumeShell.exe"; Description: "运行 Lume"; Flags: postinstall nowait skipifsilent
Filename: "{app}\LumeShell.exe"; Flags: nowait skipifnotsilent runasoriginaluser; Check: IsLumeUpdate

[Code]
function IsLumeUpdate: Boolean;
begin
  Result := ExpandConstant('{param:LUMEUPDATE|0}') = '1';
end;

#include "installer_shutdown.iss"

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

[event('PrepareToInstall')]
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
