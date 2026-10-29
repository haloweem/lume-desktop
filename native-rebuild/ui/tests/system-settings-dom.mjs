// 通过 LUME_JSDOM_PATH 或首个参数加载既有 jsdom 依赖，不随产品发布。
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {installStartupBrowserApis} from './startup-dom.mjs';
const {JSDOM}=createRequire(import.meta.url)(process.env.LUME_JSDOM_PATH || process.argv[2]);
const dom=new JSDOM('<div id="app"><header id="titlebar"></header><main id="content"></main><div id="notice" hidden></div></div>',
  {url:'https://lume.local/',pretendToBeVisual:true});
globalThis.window=dom.window;
globalThis.document=dom.window.document;
const finishStartup=installStartupBrowserApis(dom.window);
globalThis.matchMedia=()=>({matches:true});
const callbacks=new Set(),commands=[];
window.chrome={webview:{
  addEventListener:(_,fn)=>callbacks.add(fn),removeEventListener:(_,fn)=>callbacks.delete(fn),
  postMessage:value=>commands.push(value),
}};
const receive=data=>callbacks.forEach(fn=>fn({data}));
const taskbar=(fields={})=>receive({
  version:1,type:'taskbar-state',enabled:false,running:false,transparentTaskbar:true,autoHideTaskbar:true,
  applying:false,pendingRecovery:false,taskbars:0,mode:'classic',error:'',loadStatus:'loaded',revision:0,...fields,
});
const minimize=(fields={})=>receive({
  version:1,type:'minimize-state',enabled:false,running:false,durationMs:450,restore:true,launch:false,
  multiMonitor:false,unhideTaskbar:true,unhideDurationMs:450,engine:'modern',x64:false,x86:false,
  hosts:0,hooks:0,workers:0,entries:0,pendingCleanup:0,systemError:0,error:'',loadStatus:'loaded',result:'snapshot',saved:false,...fields,
});
const find=selector=>{
  const element=document.querySelector(selector);
  assert.ok(element,`Expected settings control ${selector}`);return element;
};
const click=selector=>find(selector).click();
const status=()=>find('.status').textContent;
try {
  await import('../app.js?system-settings-dom');
  await finishStartup(commands);
  taskbar();minimize();
  click('[data-view="desktopBeauty"]');
  const taskbarToggle=find('.Card [data-action="taskbar-toggle"]');
  taskbarToggle.focus();taskbarToggle.click();
  assert.equal(commands.at(-1),'v1|taskbar|enabled|true');
  assert.equal(taskbarToggle.getAttribute('aria-checked'),'true','requested position responds in the same event');
  assert.equal(taskbarToggle.getAttribute('aria-busy'),'true');
  assert.equal(taskbarToggle.getAttribute('aria-disabled'),'true');
  assert.equal(document.activeElement,taskbarToggle,'pending must retain the focused switch');
  const count=commands.length;taskbarToggle.click();assert.equal(commands.length,count,'pending switch cannot submit twice');
  taskbar({applying:true,enabled:true});
  assert.equal(taskbarToggle.getAttribute('aria-checked'),'true','applying snapshot preserves immediate feedback');
  assert.equal(document.activeElement,taskbarToggle);
  taskbar();
  assert.equal(taskbarToggle.getAttribute('aria-disabled'),'true','old snapshot does not complete the operation');
  taskbar({revision:1,enabled:true,running:true});
  assert.equal(taskbarToggle.getAttribute('aria-checked'),'true');
  assert.equal(taskbarToggle.getAttribute('aria-disabled'),'false');
  assert.equal(document.activeElement,taskbarToggle);
  click('[data-open="taskbar"]');
  assert.ok(find('[data-settings-scope="taskbar"]'));
  assert.equal(document.querySelector('[data-action="taskbar-toggle"]'),null);
  assert.match(status(),/已应用/);
  click('[data-action="taskbar-autoHide"]');
  assert.equal(commands.at(-1),'v1|taskbar|autoHide|false');
  assert.match(status(),/正在应用/);
  assert.doesNotMatch(status(),/已应用/);
  assert.equal(find('[data-action="taskbar-autoHide"]').getAttribute('aria-checked'),'false');
  taskbar({revision:2,enabled:true,error:'apply-auto-hide',pendingRecovery:true});
  assert.equal(find('[data-action="taskbar-autoHide"]').getAttribute('aria-checked'),'true','failure restores the actual preference');
  assert.match(status(),/自动隐藏/);
  assert.match(status(),/恢复/);
  assert.equal(find('.status').getAttribute('role'),'alert');
  assert.doesNotMatch(status(),/已应用|保存成功/);
  click('[data-action="taskbar-transparent"]');
  assert.equal(commands.at(-1),'v1|taskbar|transparent|false');
  taskbar({revision:3,enabled:true,running:true,transparentTaskbar:false});
  click('[data-action="taskbar-reset"]');assert.equal(commands.at(-1),'v1|taskbar|reset');
  taskbar({revision:4,enabled:true,running:true});
  click('[data-tab="details"]');
  assert.match(find('.settings-body').textContent,/关闭/);
  receive({version:1,type:'navigate',page:'taskbar'});
  assert.ok(find('[data-action="taskbar-transparent"]'));
  click('[data-action="back"]');
  assert.ok(find('[data-open="taskbar"]'));

  click('[data-open="minimize"]');
  assert.ok(find('[data-settings-scope="minimize"]'));
  const duration=find('#minimize-durationMs');
  duration.focus();duration.value='900';
  duration.dispatchEvent(new window.Event('change',{bubbles:true}));
  assert.equal(commands.at(-1),'v1|minimize|durationMs|900');
  assert.equal(document.activeElement,duration);
  assert.equal(duration.readOnly,true);
  minimize();assert.equal(duration.value,'900','unrelated snapshot must keep the submitted draft while pending');
  assert.equal(document.activeElement,duration);
  minimize({durationMs:900,saved:true,result:'applied'});
  assert.equal(duration.value,'900');assert.equal(duration.readOnly,false);
  assert.equal(document.activeElement,duration);
  duration.value='901';minimize({durationMs:900});
  assert.equal(duration.value,'901','idle runtime snapshots must preserve an in-progress keyboard draft');
  duration.value='49';duration.dispatchEvent(new window.Event('change',{bubbles:true}));
  assert.equal(duration.value,'900');assert.match(find('#notice').textContent,/50.*2000/);
  for(const [key,value] of [['restore',false],['launch',true],['multiMonitor',true],['unhideTaskbar',false]]) {
    click(`[data-action="minimize-${key}"]`);
    assert.equal(commands.at(-1),`v1|minimize|${key}|${value}`);
    minimize({durationMs:900,[key]:value,saved:true,result:'applied'});
  }
  const unhide=find('#minimize-unhideDurationMs');
  unhide.focus();unhide.value='0';unhide.dispatchEvent(new window.Event('change',{bubbles:true}));
  assert.equal(commands.at(-1),'v1|minimize|unhideDurationMs|0');
  minimize({durationMs:900,unhideDurationMs:0,saved:true,result:'applied'});
  click('#minimize-engine');
  click('[data-select-owner="minimize-engine"][data-select-value="classic"]');
  assert.equal(commands.at(-1),'v1|minimize|engine|classic');
  const engine=find('#minimize-engine');
  assert.equal(document.activeElement,engine,'select retains focus while awaiting native confirmation');
  assert.equal(engine.getAttribute('aria-disabled'),'true');
  assert.equal(find('#minimize-engine-options').hidden,true);
  const beforeBlockedSelect=commands.length;
  engine.dispatchEvent(new window.KeyboardEvent('keydown',{key:'ArrowDown',bubbles:true,cancelable:true}));
  assert.equal(find('#minimize-engine-options').hidden,true);
  find('[data-select-owner="minimize-engine"][data-select-value="modern"]').dispatchEvent(new window.MouseEvent('click',{bubbles:true}));
  assert.equal(commands.length,beforeBlockedSelect);
  const tab=new window.KeyboardEvent('keydown',{key:'Tab',bubbles:true,cancelable:true});engine.dispatchEvent(tab);
  assert.equal(tab.defaultPrevented,false,'pending controls do not trap focus');
  minimize({durationMs:900,engine:'classic',saved:true,result:'applied'});
  assert.match(engine.textContent,/经典样式/);assert.equal(document.activeElement,engine);
  click('[data-action="back"]');
  click('.Card [data-action="minimize-toggle"]');
  assert.equal(commands.at(-1),'v1|minimize|enabled|true');
  minimize({error:'missing-host',systemError:2,result:'applied'});
  assert.equal(find('[data-action="minimize-toggle"]').getAttribute('aria-checked'),'false');
  assert.equal(find('[data-action="minimize-toggle"]').disabled,false,'启动失败仍可从卡片重试');
  click('[data-open="minimize"]');
  assert.equal(document.querySelector('[data-action="minimize-toggle"]'),null);
  assert.match(status(),/32 位.*64 位/);assert.match(status(),/2/);
  assert.doesNotMatch(status(),/已保存|已应用|运行中/);
  taskbar({revision:4,enabled:true,running:true});
  assert.match(status(),/32 位.*64 位/,'unrelated host messages preserve the error');
  click('[data-action="minimize-reset"]');assert.equal(commands.at(-1),'v1|minimize|reset');
  minimize({saved:true,result:'applied'});
  click('[data-tab="details"]');assert.match(find('.settings-body').textContent,/实验/);
  receive({version:1,type:'navigate',page:'minimize'});
  assert.ok(find('#minimize-durationMs'));
  click('[data-action="back"]');
  assert.ok(find('[data-open="minimize"]'));
  click('[data-action="taskbar-toggle"]');
  taskbar({revision:5,enabled:false,pendingRecovery:true,error:'recovery'});
  assert.match(find('#notice').textContent,/恢复/);
  assert.doesNotMatch(find('#notice').textContent,/已应用|保存成功/);
  console.log('系统设置 DOM 回归通过：卡片启停、准确命令、异步确认、真实失败、草稿与焦点保留、重复输入阻断和重置返回。');
} finally {
  window.dispatchEvent(new window.Event('beforeunload'));
  assert.equal(callbacks.size,0);
  dom.window.close();
}
