var UiFilesVerified: Boolean;

function UiWasVerified: Boolean;
begin
  Log('Lume UI launch check: ' + IntToStr(Ord(UiFilesVerified)));
  Result := UiFilesVerified;
end;

function UiOpenFile(FileName: String; Access, Share: LongWord; Security: Integer;
  Creation, Flags: LongWord; Template: Integer): THandle;
  external 'CreateFileW@kernel32.dll stdcall';
function UiFlushFile(FileHandle: THandle): Boolean;
  external 'FlushFileBuffers@kernel32.dll stdcall';
function UiCloseFile(FileHandle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

procedure VerifyInstalledUiFile(const RelativeName, ExpectedHash: String);
var
  FileName: String;
  FileHandle: THandle;
  Flushed: Boolean;
begin
  FileName := ExpandConstant('{app}\') + RelativeName;
  FileHandle := UiOpenFile(FileName, $40000000, 1, 0, 3, $80000000, 0);
  if FileHandle = THandle(-1) then
    RaiseException('无法确认界面文件已写入磁盘：' + RelativeName);
  try
    Flushed := UiFlushFile(FileHandle);
  finally
    UiCloseFile(FileHandle);
  end;
  if not Flushed then
    RaiseException('界面文件写入磁盘失败：' + RelativeName);
  if CompareText(GetSHA256OfFile(FileName), ExpectedHash) <> 0 then
    RaiseException('界面文件校验失败，请重新安装：' + RelativeName);
end;

procedure VerifyAllInstalledUi;
begin
  UiFilesVerified := False;
    #include UiIntegrityChecksFile
  UiFilesVerified := True;
  Log('Lume UI and recovery files flushed and SHA-256 verified');
end;

<event('CurStepChanged')>
procedure VerifyUiAfterInstall(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and (not UiFilesVerified) then
    VerifyAllInstalledUi;
end;

function GetCustomSetupExitCode: Integer;
begin
  Result := 0;
  if not UiFilesVerified then
    Result := 14;
end;

<event('CurPageChanged')>
procedure UiIntegrityFinishPage(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and (not UiFilesVerified) then
  begin
    WizardForm.FinishedLabel.Caption := '界面文件未能通过校验，Lume 尚未启动。请重新运行安装包修复。';
    WizardForm.RunList.Visible := False;
  end;
end;
