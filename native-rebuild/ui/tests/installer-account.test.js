import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';

const installer=fs.readFileSync(new URL('../../packaging/LumeSetup.iss',import.meta.url),'utf8');
const cmake=fs.readFileSync(new URL('../../tests/unit/CMakeLists.txt',import.meta.url),'utf8');

test('管理员安装的登录协议对普通使用者可见',()=>{
  assert.match(installer,/^PrivilegesRequired=admin$/m);
  const registrations=installer.split(/\r?\n/).filter(line=>line.startsWith('Root:')&&line.includes('Software\\Classes\\lume'));
  assert.equal(registrations.length,4);
  for(const registration of registrations) {
    assert.match(registration,/^Root: HKLM;/);
    assert.doesNotMatch(registration,/uninsdeletekey/i);
  }
  assert.match(installer,/^ChangesAssociations=yes$/m);
});

test('卸载仅清理仍属于当前安装目录的登录协议',()=>{
  assert.match(installer,/RegQueryStringValue\(RootKey,\s*'Software\\Classes\\lume\\shell\\open\\command',\s*'',\s*ProtocolCommand\)/);
  assert.match(installer,/if CompareText\(ProtocolCommand,\s*ExpectedProtocolCommand\) = 0 then\s+RegDeleteKeyIncludingSubkeys\(RootKey,\s*'Software\\Classes\\lume'\)/);
  assert.ok(installer.includes(`ExpectedProtocolCommand := '"' + ExpandConstant('{app}\\LumeShell.exe') + '" --login-callback "%1"';`));
  assert.match(installer,/RemoveOwnedLoginProtocol\(HKLM\)/);
  assert.match(installer,/RemoveOwnedLoginProtocol\(HKCU\)/);
});

test('壁纸会话测试显式构建所需的播放器夹具',()=>{
  assert.match(cmake,/add_dependencies\(lume-wallpaper-session-test\s+lume-mpv-process-controller-test\)/);
});

test('安装前执行有界关闭工具，不使用可能长时间等待的自动关闭页面',()=>{
  assert.match(installer,/^CloseApplications=no$/m);
  assert.match(installer,/LumeSetupShutdown\.exe.*Flags: dontcopy/);
  assert.match(installer,/#include "installer_shutdown\.iss"/);
  const shutdown=fs.readFileSync(new URL('../../packaging/installer_shutdown.iss',import.meta.url),'utf8');
  assert.match(shutdown,/function PrepareToInstall\(var NeedsRestart: Boolean\): String/);
  assert.match(shutdown,/ExtractTemporaryFile\('LumeSetupShutdown\.exe'\)/);
  assert.match(shutdown,/--directory/);
  assert.match(shutdown,/ExpandConstant\('\{app\}\\\.'\)/);
  assert.match(shutdown,/ResultCode <> 0.*ResultCode <> 10/);
  assert.match(shutdown,/else if ResultCode = 2 then\s+Result := '无法验证此安装目录/);
  assert.doesNotMatch(shutdown,/taskkill|powershell|cmd\.exe/i);
});

test('关闭工作进程创建时即加入退出保护，避免创建与挂接之间遗留进程',()=>{
  const worker=fs.readFileSync(new URL('../../platform/windows/installer_shutdown.cpp',import.meta.url),'utf8');
  assert.match(worker,/PROC_THREAD_ATTRIBUTE_JOB_LIST/);
  assert.match(worker,/EXTENDED_STARTUPINFO_PRESENT/);
  assert.doesNotMatch(worker,/AssignProcessToJobObject/);
  assert.ok(worker.indexOf('PROC_THREAD_ATTRIBUTE_JOB_LIST')<worker.indexOf('CreateProcessW'));
});

test('验收包固定执行设置账号探针，壁纸模式不跳过新功能',()=>{
  const packageScript=fs.readFileSync(new URL('../../tests/scripts/package-original-pages.ps1',import.meta.url),'utf8');
  const mainCmake=fs.readFileSync(new URL('../../CMakeLists.txt',import.meta.url),'utf8');
  for(const target of ['lume-account-security-test','lume-account-session-test','lume-account-rotation-test',
    'lume-account-background-test','lume-application-settings-service-test',
    'lume-application-settings-file-test','lume-desktop-settings-account-test','lume-webview-application-commands-test','lume-shell-activation-test']) {
    assert.ok(packageScript.includes(`'${target}'`),`打包门禁缺少 ${target}`);
    assert.ok(mainCmake.includes(target),`CMake 载荷探针清单缺少 ${target}`);
  }
  assert.match(packageScript,/\$acceptanceRuns = @\(\$settingsAccountProbes \|/);
  assert.match(packageScript,/clean-path-settings-account-isolated-and-lifecycle=pass/);
  assert.doesNotMatch(packageScript,/local-only-no-cloud/);
});
