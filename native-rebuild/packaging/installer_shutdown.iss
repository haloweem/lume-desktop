// After Lume unhooks it, Windows keeps the minimize hook DLL loaded in idle
// processes (for example the Now Playing service) until they next run a
// message loop, so it cannot be replaced. A loaded DLL can still be renamed:
// move it aside so the new file can be written; leftovers go next time.
procedure MoveAsideLockedFile(const Name: String);
var
  Path: String;
  FindRec: TFindRec;
begin
  Path := ExpandConstant('{app}\' + Name);
  if FindFirst(Path + '.*.old', FindRec) then
  begin
    try
      repeat
        DeleteFile(ExpandConstant('{app}\') + FindRec.Name);
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
  if FileExists(Path) and not DeleteFile(Path) then
  begin
    if RenameFile(Path, Path + '.' + GetDateTimeString('yyyymmddhhnnss', #0, #0) + '.old') then
      Log('已移开仍被占用的文件：' + Path)
    else
      Log('无法移开仍被占用的文件：' + Path);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  Log('正在关闭此安装目录中的旧版 Lume。');
  try
    ExtractTemporaryFile('LumeSetupShutdown.exe');
    if not Exec(ExpandConstant('{tmp}\LumeSetupShutdown.exe'),
      '--directory "' + ExpandConstant('{app}\.') + '"',
      ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      Result := '无法启动 Lume 退出工具，请重试安装。'
    else if ResultCode = 2 then
      Result := '无法验证此安装目录，请选择有效的安装文件夹后重试。安装文件尚未更改。'
    else if (ResultCode <> 0) and (ResultCode <> 10) then
      Result := '未能关闭此安装目录中的 Lume，请退出旧版后重试。安装文件尚未更改。';
    Log('Lume 退出工具结果：' + IntToStr(ResultCode));
    if Result = '' then
    begin
      MoveAsideLockedFile('LumeMinimizeHook64.dll');
      MoveAsideLockedFile('LumeMinimizeHook32.dll');
    end;
  except
    Log(GetExceptionMessage);
    Result := '无法运行 Lume 退出工具，请重试安装。';
  end;
end;
