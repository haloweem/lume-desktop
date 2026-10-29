import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mountApplicationSettings} from '../application-settings.js';
import {mountTitlebar} from '../titlebar.js';
const {JSDOM}=createRequire(import.meta.url)(process.argv[2]);
const dom=new JSDOM('<header></header><main></main>',{url:'https://lume.local/',pretendToBeVisual:true});
const {document,Event}=dom.window,host=document.querySelector('main'),commands=[],languages=[];
let sent=true,backs=0;
const bridge={applicationSettings:(action,fields={})=>{commands.push([action,fields]);return sent;},
  account:(action,fields={})=>{commands.push(['account',action,fields]);return sent;},send:()=>true};
const titlebar=mountTitlebar(document.querySelector('header'),bridge,()=>{});
const controller=mountApplicationSettings(host,{bridge,onBack:()=>backs++,onLanguage:language=>{
  languages.push(language);titlebar.setLanguage(language);
}});
const find=selector=>{const e=host.querySelector(selector);assert.ok(e,selector);return e;};
const snapshot=(revision,fields={})=>({version:1,type:'app-settings-state',revision,language:'zh-CN',
  balancedMode:true,autostart:false,busy:false,error:'',update:{status:'idle',version:'',progress:0},...fields});
const change=(selector,value)=>{
  const e=find(selector);
  if(e.type==='checkbox')e.checked=value;else e.value=value;
  e.dispatchEvent(new Event('change',{bubbles:true}));
  return e;
};
try {
  controller.show();
  assert.deepEqual([...host.querySelectorAll('button[data-settings-section] svg')].map(e=>e.dataset.icon),
    ['sliders','user-gear','rocket','circle-info'],'本版设置导航隐藏诊断与维护');
  assert.equal(host.firstElementChild.dataset.settingsSection,'general');
  assert.equal(find('[data-setting="balancedMode"]').checked,true);
  assert.equal(find('[data-setting="balancedMode"]').disabled,true,'初次确认前不允许保存');
  assert.equal(find('[data-setting="autostart"]').checked,true,'新用户自启默认展示开启');
  assert.equal(find('[data-setting="autostart"]').disabled,true,'真实系统确认前不能操作自启');
  controller.handleMessage(snapshot(5));
  assert.equal(find('[data-setting="autostart"]').checked,false,'已有关闭快照覆盖新用户默认展示');
  assert.equal(controller.handleMessage({type:'wallpaper-state',revision:99}),false);
  const root=host.firstElementChild,balanced=find('[data-setting="balancedMode"]');
  balanced.focus();
  change('[data-setting="balancedMode"]',false);
  assert.deepEqual(commands.at(-1),['balancedMode',{value:false}]);
  assert.equal(balanced.checked,false);
  controller.handleMessage(snapshot(5));
  assert.equal(balanced.checked,false,'同 revision 不能结束 pending');
  controller.handleMessage(snapshot(6,{busy:true,balancedMode:false}));
  assert.equal(balanced.checked,false);
  controller.handleMessage(snapshot(7,{error:'preferences-save',balancedMode:true}));
  assert.equal(balanced.checked,true,'失败清除 pending，回到 native 快照中的已确认开关');
  assert.equal(document.activeElement,balanced);
  assert.equal(host.firstElementChild,root);
  assert.equal(find('[data-setting="balancedMode"]'),balanced);
  assert.match(find('[data-settings-message]').textContent,/保存失败/);
  change('[data-setting="balancedMode"]',false);
  controller.handleMessage(snapshot(8,{balancedMode:false}));
  assert.equal(balanced.checked,false);
  controller.show('autostart');
  const autostart=change('[data-setting="autostart"]',true);
  assert.deepEqual(commands.at(-1),['autostart',{value:true}]);
  controller.handleMessage(snapshot(9,{error:'startup-write',autostart:false,balancedMode:false}));
  assert.equal(autostart.checked,false);
  controller.show('general');
  change('[data-setting="language"]','en');
  assert.deepEqual(commands.at(-1),['language',{value:'en'}]);
  assert.equal(find('[data-settings-heading]').textContent,'General');
  assert.equal(document.querySelector('[data-titlebar-settings]').title,'Settings');
  controller.handleMessage(snapshot(10,{language:'zh-CN',balancedMode:false,error:'preferences-save'}));
  assert.equal(find('[data-settings-heading]').textContent,'通用');
  assert.equal(document.querySelector('[data-titlebar-settings]').title,'设置');
  change('[data-setting="language"]','en');
  controller.handleMessage(snapshot(11,{language:'en',balancedMode:false}));
  assert.equal(find('[data-settings-heading]').textContent,'General');
  assert.deepEqual(languages.slice(-3),['en','zh-CN','en']);
  controller.handleMessage({version:1,type:'account-state',revision:1,status:'signed-in',busy:false,error:'',
    profile:{id:'1',displayName:'原昵称',avatarUrl:'',vip:false,vipExpiresAt:''}});
  controller.show('user');
  const input=find('[data-account-name-input]'),panel=find('[data-settings-panel="user"]');
  input.value='未保存草稿';input.dispatchEvent(new Event('input',{bubbles:true}));
  panel.scrollTop=117;panel.dispatchEvent(new Event('scroll'));
  find('[data-settings-back]').click();
  assert.equal(backs,1);
  host.replaceChildren(document.createElement('div'));
  controller.show('user');
  assert.equal(host.firstElementChild,root,'主线程替换页面后复用原节点');
  assert.equal(find('[data-account-name-input]'),input);
  assert.equal(input.value,'未保存草稿');
  assert.equal(panel.scrollTop,117);
  controller.render();
  assert.equal(input.value,'未保存草稿');
  controller.show('about');
  find('[data-settings-action="checkUpdate"]').click();
  assert.deepEqual(commands.at(-1),['checkUpdate',{}]);
  controller.handleMessage(snapshot(12,{language:'en',update:{status:'incompatible',version:'2.0',progress:0}}));
  assert.match(find('[data-update-status]').textContent,/compatible/i);
  assert.equal(find('[data-settings-action="downloadUpdate"]').hidden,true);
  assert.equal(find('[data-settings-action="installUpdate"]').hidden,true);
  for(const [revision,status,action] of [[13,'available','downloadUpdate'],[14,'ready','installUpdate']]) {
    controller.handleMessage(snapshot(revision,{update:{status,version:'2.0',progress:0}}));
    assert.equal(find(`[data-settings-action="${action}"]`).hidden,false);
    find(`[data-settings-action="${action}"]`).click();
    assert.deepEqual(commands.at(-1),[action,{}]);
  }
  controller.handleMessage(snapshot(15,{busy:true,update:{status:'downloading',version:'2.0',progress:54}}));
  assert.equal(find('[data-update-progress]').value,54);
  controller.handleMessage(snapshot(16,{error:'network',update:{status:'error',version:'2.0',progress:54}}));
  assert.equal(find('[data-settings-action="checkUpdate"]').disabled,false);
  assert.match(find('[data-settings-message]').textContent,/网络/);
  controller.show('diagnostics');
  assert.equal(find('[data-settings-heading]').textContent,'通用');
  assert.equal(host.querySelector('[data-settings-section="diagnostics"]'),null,'诊断入口本版隐藏');
  assert.equal(find('[data-settings-panel="diagnostics"]').hidden,true,'深链接不会打开已禁用诊断页');
  controller.handleMessage(snapshot(15,{language:'en'}));
  assert.match(find('[data-settings-heading]').textContent,/通用/);
  controller.show('general');
  sent=false;
  change('[data-setting="balancedMode"]',false);
  assert.equal(balanced.checked,true);
  assert.match(find('[data-settings-message]').textContent,/宿主未连接/);
  const count=commands.length;
  controller.dispose();titlebar.dispose();
  assert.equal(controller.handleMessage(snapshot(30)),false);
  controller.show('general');
  assert.equal(host.children.length,0);
  balanced.dispatchEvent(new Event('change',{bubbles:true}));
  assert.equal(commands.length,count);
  const recovered=mountApplicationSettings(host,{bridge});
  recovered.handleMessage(snapshot(40,{language:'en',balancedMode:false,autostart:true,error:'balanced-runtime'}));
  assert.equal(find('[data-settings-heading]').textContent,'General','初次带错误快照仍包含已确认的语言');
  assert.equal(find('[data-setting="balancedMode"]').checked,false);
  assert.equal(find('[data-setting="autostart"]').checked,true);
  assert.match(find('[data-settings-message]').textContent,/wallpaper.*policy/i);
  for(const [index,[error,pattern]] of [
    ['preferences-save',/保存失败/],['preferences-read',/读取失败/],
    ['autostart-write',/开机启动.*失败/],['balanced-runtime',/壁纸.*失败/],
    ['update-disabled',/不支持.*下载.*安装/],['update-incompatible',/不兼容/],
    ['update-tls',/验证/],['update-timeout',/超时/],
    ['preferences-load',/读取失败/],['startup-read',/开机启动.*失败/],['startup-write',/开机启动.*失败/],
    ['startup-rollback',/开机启动.*恢复失败/],['update-unavailable',/不支持.*下载.*安装/],
  ].entries()) {
    recovered.handleMessage(snapshot(41+index,{error}));
    assert.match(find('[data-settings-message]').textContent,pattern,error);
  }
  sent=true;
  recovered.show('autostart');
  change('[data-setting="autostart"]',true);
  recovered.handleMessage(snapshot(60,{autostart:true,error:'startup-rollback'}));
  assert.equal(find('[data-setting="autostart"]').checked,true,'回滚失败时以 native 实际确认状态为准，不捏造回滚成功');
  recovered.handleMessage(snapshot(61,{autostart:true,error:'autostart-conflict'}));
  assert.equal(find('[data-setting="autostart"]').checked,true,'其他目录旧版命令仍在自启时不能假关闭');
  assert.match(find('[data-settings-message]').textContent,/启动项.*未.*更改|未.*更改.*启动项/);
  recovered.dispose();
  console.log('设置 DOM：四栏、默认值、pending/错误回退、双向语言、独立账号草稿、返回/滚动、兼容更新状态、断桥/dispose 通过。');
} finally {
  controller.dispose();titlebar.dispose();dom.window.close();
}
