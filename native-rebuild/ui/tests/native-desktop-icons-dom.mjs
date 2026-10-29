// 使用 LUME_JSDOM_PATH 加载既有依赖；可选参数为原生会话日志。
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createRequire} from 'node:module';
import {installStartupBrowserApis} from './startup-dom.mjs';
const {JSDOM}=createRequire(import.meta.url)(process.env.LUME_JSDOM_PATH || process.argv[2]);
const message=(fields={})=>({
  version:1,type:'icons-state',enabled:true,running:true,enableAutoHide:false,
  autoHideDelay:3,showIconsOnAnyInput:true,manuallyHidden:false,desktops:1,
  autoHidden:0,pendingRestores:0,loadStatus:'loaded',error:'',result:'snapshot',saved:false,...fields,
});
const nativeReply=process.argv[3]
  ? JSON.parse(readFileSync(process.argv[3],'utf8').match(/saved_visibility_reply=(\{[^\r\n]+\})/)[1])
  : message({enableAutoHide:true,result:'applied',saved:true,error:'visibility'});
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
const find=selector=>{
  const element=document.querySelector(selector);
  assert.ok(element,`Expected icon control ${selector}`);return element;
};
const click=selector=>find(selector).click();
const assertControlsEnabled=()=>{
  assert.equal(document.querySelector('[data-settings-scope="icons"] [data-action="icons-toggle"]'),null);
  for(const selector of ['icons-autoHide','icons-restoreOnInput','icons-reset'])
    assert.equal(find(`[data-action="${selector}"]`).disabled,false,selector);
  assert.equal(find('#icons-delay').disabled,false);
};
try {
  await import('../app.js?desktop-icons-feedback');
  await finishStartup(commands);
  receive(message());
  receive({version:1,type:'navigate',page:'icons'});
  click('[data-action="icons-autoHide"]');
  assert.equal(commands.at(-1),'v1|icons|autoHide|true');
  assert.equal(find('[data-action="icons-autoHide"]').disabled,true);
  receive(nativeReply);
  assert.equal(find('[data-action="icons-autoHide"]').getAttribute('aria-checked'),'true',
    'the native saved preference reaches the live page even when applying visibility failed');
  assert.equal(find('#icons-delay').value,'3');
  assertControlsEnabled();
  assert.match(find('.status').textContent,/桌面.*响应/);
  assert.equal(find('.status').getAttribute('role'),'alert');
  assert.doesNotMatch(find('.status').textContent,/正在保存|已保存|已应用/);

  click('[data-action="back"]');
  click('.Card [data-action="icons-toggle"]');
  assert.equal(commands.at(-1),'v1|icons|enabled|false');
  receive(message({enableAutoHide:true}));
  assert.equal(find('[data-action="icons-toggle"]').disabled,true,
    'a healthy earlier snapshot still cannot acknowledge the pending disable');
  receive(message({enableAutoHide:true,running:false,desktops:0,pendingRestores:1,error:'rollback'}));
  assert.equal(find('[data-action="icons-toggle"]').getAttribute('aria-checked'),'true');
  assert.equal(find('[data-action="icons-toggle"]').disabled,false,'关闭失败后卡片可以重试');
  click('[data-open="icons"]');
  assertControlsEnabled();
  assert.match(find('.status').textContent,/再次关闭/);
  click('[data-action="back"]');
  click('.Card [data-action="icons-toggle"]');
  assert.equal(commands.at(-1),'v1|icons|enabled|false','error snapshot permits a real disable retry');
  receive(message({enabled:false,running:false,desktops:0,enableAutoHide:true,result:'applied',saved:true}));
  assert.equal(find('[data-action="icons-toggle"]').getAttribute('aria-checked'),'false');
  assert.equal(find('[data-action="icons-toggle"]').disabled,false);
  click('[data-open="icons"]');
  assertControlsEnabled();

  click('[data-action="back"]');
  click('[data-action="icons-toggle"]');
  assert.equal(commands.at(-1),'v1|icons|enabled|true');
  receive(nativeReply);
  assert.equal(find('[data-action="icons-toggle"]').disabled,false);
  assert.equal(find('[data-action="icons-toggle"]').getAttribute('aria-checked'),'true');
  assert.match(find('#notice').textContent,/桌面.*响应/);
  assert.doesNotMatch(find('#notice').textContent,/已保存|已应用/);
  click('[data-action="icons-toggle"]');
  assert.equal(commands.at(-1),'v1|icons|enabled|false','catalog error feedback also leaves disable usable');
  console.log('桌面图标 DOM 回归通过：保存与错误回执、详情控件恢复、卡片关闭失败重试及提示。');
} finally {
  window.dispatchEvent(new window.Event('beforeunload'));
  assert.equal(callbacks.size,0);
  dom.window.close();
}
