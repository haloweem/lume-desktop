import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync,readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const moduleUrl=new URL('../account.js',import.meta.url);
test('账号模块提供独立控制器、公开快照验证和 Unicode 昵称验证',async()=>{
  assert.ok(existsSync(moduleUrl),'需要独立 account.js，不能借用壁纸状态');
  const {mountAccount,isAccountSnapshot,normalizeDisplayName,safeAvatarUrl,accountErrorMessage}=await import(moduleUrl);
  assert.equal(typeof mountAccount,'function');
  for(const name of ['用户','😀😀','A👩‍💻','😀'.repeat(32)])
    assert.equal(normalizeDisplayName(` ${name} `),name);
  for(const name of ['A','😀','字'.repeat(33),'a\nb','a\u0000b','a\u0085b','a\u202eb','a\ud800b'])
    assert.equal(normalizeDisplayName(name),null,name);
  assert.equal(safeAvatarUrl('data:image/png;base64,YWJj'),'data:image/png;base64,YWJj');
  for(const url of ['https://example.com/a.png','data:image/svg+xml;base64,YWJj','javascript:alert(1)',
    'data:image/png;base64,%%%','data:image/webp;base64,'+'Y'.repeat(2800000)])
    assert.equal(safeAvatarUrl(url),'');
  const message={version:1,type:'account-state',revision:1,status:'signed-out',busy:false,error:'',profile:null};
  assert.equal(isAccountSnapshot(message),true);
  for(const fields of [{version:2},{revision:-1},{revision:NaN},{revision:1.5},{busy:1},{status:'signedIn'},
    {profile:{}},{error:'<img src=x>'},{accessToken:'secret'}])
    assert.equal(isAccountSnapshot({...message,...fields}),false,JSON.stringify(fields));
  for(const code of ['network','storage','expired','cancelled','invalid-name','unavailable','unknown'])
    assert.match(accountErrorMessage(code,'zh-CN'),/[\u4e00-\u9fff]/);
  assert.doesNotMatch(accountErrorMessage('<script>secret</script>'),/secret|script/);
});

test('账号与标题栏 DOM 行为回归',t=>{
  const jsdom=process.env.LUME_JSDOM_PATH || process.env.LUME_JSDOM;
  if(!jsdom) return t.skip('设置 LUME_JSDOM_PATH 为已有 jsdom/lib/api.js 绝对路径后运行 DOM 回归');
  assert.ok(existsSync(moduleUrl),'需要独立 account.js');
  const result=spawnSync(process.execPath,[fileURLToPath(new URL('./account-dom.mjs',import.meta.url)),jsdom],{encoding:'utf8'});
  assert.equal(result.status,0,result.stdout+result.stderr);
  t.diagnostic(result.stdout.trim());
});

test('原生错误码必须翻译为对应的可读错误，不泄漏服务文本',async()=>{
  const {accountErrorMessage}=await import(moduleUrl);
  for(const [code,pattern] of [
    ['network-unavailable',/网络/],['service-unavailable',/服务/],['storage-failed',/保存/],
    ['credential-corrupt',/损坏/],['device-inactive',/失效/],['invalid-avatar',/JPG.*PNG.*WebP/],
    ['callback-expired',/超时/],['callback-state',/验证/],['authorization-denied',/拒绝/],['local-logout',/退出/],
    ['protocol-registration-failed',/登录回调注册失败.*重试/],
  ]) assert.match(accountErrorMessage(code),pattern,code);
  assert.equal(accountErrorMessage('local-logout'),'已退出此设备的登录。');
  assert.equal(accountErrorMessage('local-logout','en'),'Signed out on this device.');
  assert.doesNotMatch(accountErrorMessage('protocol-registration-failed'),/权限/);
  assert.doesNotMatch(accountErrorMessage('protocol-registration-failed','en'),/permissions/i);
});

test('账号快照包含 HTML 的展示字符串只作为文字，危险头像拒绝',async()=>{
  const {isAccountSnapshot}=await import(moduleUrl);
  const message={version:1,type:'account-state',revision:2,status:'signed-in',busy:false,error:'',
    profile:{id:'id',displayName:'<img src=x onerror=alert(1)>',avatarUrl:'',vip:false,vipExpiresAt:''}};
  assert.equal(isAccountSnapshot(message),true);
  for(const fields of [{avatarUrl:'https://evil.test/p.png'},{avatarUrl:'data:image/svg+xml;base64,YWJj'},
    {refreshToken:'secret'},{vip:'true'},{id:''}])
    assert.equal(isAccountSnapshot({...message,profile:{...message.profile,...fields}}),false);
});

test('设置、账号与标题栏图标来源声明覆盖版本、许可与实际 VIP 资产',async()=>{
  const noticeUrl=new URL('../assets/sources/settings-account-icons.md',import.meta.url);
  assert.ok(existsSync(noticeUrl),'设置与账号图标需要独立的中文随包来源声明');
  const notice=readFileSync(noticeUrl,'utf8');
  assert.match(notice,/[\u4e00-\u9fff]/);
  for(const required of [
    '@fortawesome/free-solid-svg-icons','7.1.0','Fonticons, Inc.','CC BY 4.0',
    '(CC-BY-4.0 AND MIT)','https://fontawesome.com/license/free',
    'https://creativecommons.org/licenses/by/4.0/',
    'node_modules/@fortawesome/free-solid-svg-icons/package.json',
    'node_modules/@fortawesome/free-solid-svg-icons/LICENSE.txt',
    'SettingsWindow.tsx','UserSettings.tsx',
    'faSliders','faUserGear','faRocket','faInfoCircle','faCamera','faRotate',
    'faRightFromBracket','faCircleUser','faGear','faMusic',
    'third_party/folia-major/upstream/shared/lume-titlebar/titlebar.ts',
    'third_party/folia-major/upstream/shared/lume-titlebar/assets/vip-badge.webp',
    'src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/panel/assets/vip-badge.webp',
    'src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/panel/assets/logo-white.svg',
    'ui/assets/sources/settings-account-icons.md',
  ]) assert.ok(notice.includes(required),`来源声明缺少 ${required}`);
  const {VIP_BADGE_URL}=await import(moduleUrl);
  assert.match(VIP_BADGE_URL,/^data:image\/webp;base64,/);
  const badge=Buffer.from(VIP_BADGE_URL.slice(VIP_BADGE_URL.indexOf(',')+1),'base64');
  const hash=createHash('sha256').update(badge).digest('hex');
  assert.ok(notice.includes(hash),'更换内嵌 VIP 图片时必须同步来源声明的 SHA-256');
});
