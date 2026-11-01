import test from 'node:test';
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';

function installFixture(language) {
 const listeners=new Set(),commands=[],cards=['music','calendar','weather','performance','todo','reminder','focus','launchpad','actions'];
 const mini=['none','date','weekday','lunar','clock','dayProgress','weekProgress','monthProgress','yearProgress',
  'battery','cpu','memory','disk','weather','temperature','humidity','wind','network','music','focus','planet',
  'flame','heart','gift','butterfly','moon','snow','orbit','coffee','cat','tree'];
 const customization={version:1,customIcons:false,overview:false,left:{widget:'date',style:'circle'},
  right:{widget:'battery',style:'circle'},enabled:[true,true,true,true,true,true,false,false,false],order:[...cards]};
 let state={version:1,type:'dynamicIsland-state',enabled:true,running:true,locked:false,expanded:false,
  focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:true,hover:false,autoHide:true,
  size:'standard',material:'none',loadStatus:'loaded',error:'',result:'snapshot',saved:false,customization};
 const receive=m=>listeners.forEach(listener=>listener({data:m}));
 const emit=fields=>{state={...state,...fields};receive(state);};
 const settings={version:1,type:'app-settings-state',revision:1,language,balancedMode:true,autostart:false,
  busy:false,error:'',update:{status:'idle',version:'',progress:0}};
 window.islandHarness={commands,pending:[],get state(){return state;},receive,
  settle(fail=false) {
   const pending=this.pending.splice(0);const next=structuredClone(state.customization);
   if(!fail)for(const command of pending) {
    const [, , action,raw]=command.split('|');
    if(['customIcons','overview'].includes(action))next[action]=raw==='true';
    else if(['leftWidget','rightWidget'].includes(action)){next[action.startsWith('left')?'left':'right'].widget=mini[Number(raw)];next.customIcons=true;}
    else if(['leftStyle','rightStyle'].includes(action))next[action.startsWith('left')?'left':'right'].style=raw;
    else if(action==='card') {
     const code=Number(raw),index=Math.floor(code/4),op=code%4,id=cards[index];
     if(op<2)next.enabled[index]=op===0;
     else {const from=next.order.indexOf(id),to=Math.max(0,Math.min(cards.length-1,from+(op===2?-1:1)));
      next.order.splice(from,1);next.order.splice(to,0,id);}
    }
     else if(action==='cardMove') {
      const code=Number(raw),index=Math.floor(code/cards.length),to=code%cards.length,id=cards[index];
      const from=next.order.indexOf(id);if(from>=0){next.order.splice(from,1);next.order.splice(to,0,id);}
     }
   }
   emit({customization:next,result:'applied',saved:!fail,error:fail?'save':''});
  },snapshot(){emit({result:'snapshot',saved:false,error:''});},
  navigate(){receive({version:1,type:'navigate',page:'dynamicIsland'});},
 };
 window.chrome={webview:{addEventListener(type,listener){if(type==='message')listeners.add(listener);},
  removeEventListener(type,listener){listeners.delete(listener);},postMessage(command){
   commands.push(command);
   if(command==='v1|ready')queueMicrotask(()=>{receive(settings);receive(state);});
   if(typeof command==='string'&&command.startsWith('v1|dynamicIsland|'))window.islandHarness.pending.push(command);
  }}};
}
test('actual app settings support independent choices, save failure recovery and responsive galleries',{
 skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:120000,
},async()=>{
 const ui=fileURLToPath(new URL('../',import.meta.url));
 const output=process.env.LUME_UI_SCREENSHOT_DIR||fileURLToPath(new URL('../../../build-artifacts/island-customization-20261101/',import.meta.url));
 const playwright=process.env.LUME_PLAYWRIGHT_DIR||path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
 const {chromium}=await import(pathToFileURL(path.join(playwright,'index.mjs')).href);
 const server=createServer(async(req,res)=>{
  try {const route=new URL(req.url,'http://localhost').pathname;
   const file=path.resolve(ui,'.'+(route==='/'?'/index.html':route));
   if(!file.startsWith(ui))return res.writeHead(403).end();
   const bytes=await readFile(file);res.writeHead(200,{'Content-Type':({'.js':'text/javascript','.html':'text/html;charset=utf-8','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream'}).end(bytes);
  }catch {res.writeHead(404).end();}
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 let browser;const evidence=[],errors=[];
 try {
  await mkdir(output,{recursive:true});
  browser=await chromium.launch({headless:true,...(process.env.LUME_BROWSER_EXECUTABLE?{executablePath:process.env.LUME_BROWSER_EXECUTABLE}:{channel:'msedge'})});
  for(const viewport of [{width:1280,height:900},{width:680,height:864}])for(const language of ['zh-CN','en']) {
   const page=await browser.newPage({viewport});page.on('pageerror',e=>errors.push(e.message));
   await page.addInitScript(installFixture,language);
   await page.goto(`http://127.0.0.1:${server.address().port}/`);
   await page.waitForFunction(()=>islandHarness.commands.includes('v1|ready'));
   await page.evaluate(()=>islandHarness.navigate());
   await page.locator('[data-island-slot="left"]').waitFor();
   assert.equal(await page.locator('.island-mini-choice').count(),60);
   assert.equal(await page.locator('[data-island-card]').count(),9);
   assert.equal(await page.locator('[data-action="dynamicIsland-toggle"],[data-action="dynamicIsland-hover"],[data-action="dynamicIsland-expand"],[data-action="dynamicIsland-collapse"],.island-preview').count(),0);
   const left=page.locator('[data-island-slot="left"]'),right=page.locator('[data-island-slot="right"]');
   const clickAndSettle=async locator=>{await locator.click();await page.evaluate(()=>islandHarness.settle());};
   await left.locator('[data-action="dynamicIsland-leftWidget"][data-value="5"]').click();
   assert.equal(await left.locator('[data-value="1"][data-action="dynamicIsland-leftWidget"]').getAttribute('aria-pressed'),'true','selection waits for native acknowledgement');
   assert.equal(await right.locator('[data-value="9"][data-action="dynamicIsland-rightWidget"]').getAttribute('aria-disabled'),'true');
   await page.evaluate(()=>islandHarness.snapshot());
   assert.equal(await right.locator('[data-value="9"][data-action="dynamicIsland-rightWidget"]').getAttribute('aria-disabled'),'true','unrelated snapshot must not release pending request');
   await page.evaluate(()=>islandHarness.settle(true));
   assert.equal(await left.locator('[data-value="1"][data-action="dynamicIsland-leftWidget"]').getAttribute('aria-pressed'),'true','failed save preserves selection');
   assert.equal(await right.locator('[data-value="9"][data-action="dynamicIsland-rightWidget"]').getAttribute('aria-disabled'),'false');
   assert.equal(await page.locator('[data-settings-scope="dynamicIsland"] .status').getAttribute('role'),'alert');
   await clickAndSettle(left.locator('[data-action="dynamicIsland-leftWidget"][data-value="5"]'));
   await clickAndSettle(right.locator('[data-action="dynamicIsland-rightWidget"][data-value="14"]'));
   assert.equal(await left.locator('[data-value="5"][data-action="dynamicIsland-leftWidget"]').getAttribute('aria-pressed'),'true');
   assert.equal(await right.locator('[data-value="14"][data-action="dynamicIsland-rightWidget"]').getAttribute('aria-pressed'),'true');
   await clickAndSettle(left.locator('[data-action="dynamicIsland-leftStyle"][data-value="capsule"]'));
   assert.equal(await left.locator('.island-mini-preview.capsule').count(),30);
   assert.equal(await right.locator('.island-mini-preview.circle').count(),30);
   await clickAndSettle(left.locator('.island-clear'));
   assert.equal(await left.locator('[data-action="dynamicIsland-leftWidget"][data-value="5"]').getAttribute('aria-pressed'),'false');
   assert.equal(await right.locator('[data-value="14"][data-action="dynamicIsland-rightWidget"]').getAttribute('aria-pressed'),'true');
   await clickAndSettle(left.locator('[data-action="dynamicIsland-leftWidget"][data-value="20"]'));
   await page.screenshot({path:path.join(output,`island-settings-${viewport.width}-${language}-styles.png`)});
   const focus=page.locator('[data-island-card="focus"]');
   await clickAndSettle(focus.locator('[role="switch"]'));
   assert.equal(await focus.locator('[role="switch"]').getAttribute('aria-checked'),'true');
   await clickAndSettle(focus.locator('[data-value="26"]'));
   let order=await page.locator('[data-island-card]').evaluateAll(nodes=>nodes.map(n=>n.dataset.islandCard));
   assert.equal(order.indexOf('focus'),5,'move earlier applies native ordering');
   await clickAndSettle(focus.locator('[data-value="27"]'));
   order=await page.locator('[data-island-card]').evaluateAll(nodes=>nodes.map(n=>n.dataset.islandCard));
   assert.equal(order.indexOf('focus'),6);
   // A distant drop is encoded into one atomic native command.
   await focus.dragTo(page.locator('[data-island-card="music"]'));
   assert.equal(await page.evaluate(()=>islandHarness.pending.length),1,'drag sends one atomic native command');
   assert.equal(await page.evaluate(()=>islandHarness.pending[0]),'v1|dynamicIsland|cardMove|54');
   for(let step=0;step<islandExpandedWidgetCount();step++) {
    const done=await page.evaluate(()=>islandHarness.state.customization.order.indexOf('focus')===0);
    if(done)break;
    assert.ok(await page.evaluate(()=>islandHarness.pending.length),`drag step ${step} waits for native acknowledgement`);
    await page.evaluate(()=>islandHarness.settle());
   }
   order=await page.locator('[data-island-card]').evaluateAll(nodes=>nodes.map(n=>n.dataset.islandCard));
   assert.equal(order[0],'focus','distant drag must reach the drop target');
   const moveCommands=()=>page.evaluate(()=>islandHarness.commands.filter(command=>command.startsWith('v1|dynamicIsland|cardMove|')).length);
   const beforeFailedMove=await moveCommands();
   await focus.dragTo(page.locator('[data-island-card="actions"]'));
   await page.evaluate(()=>islandHarness.settle(true));
   assert.equal(await moveCommands(),beforeFailedMove+1,'failed drag step must not queue more native writes');
   assert.equal(await page.evaluate(()=>islandHarness.pending.length),0);
   order=await page.locator('[data-island-card]').evaluateAll(nodes=>nodes.map(n=>n.dataset.islandCard));
   assert.equal(order[0],'focus','failed save keeps the last confirmed order');
   const layout=await page.evaluate(()=>{
    const selectors=['.settings','.settings-card','.island-slots','.island-expanded-grid',...Array.from({length:2},(_,i)=>`[data-island-slot="${i?'right':'left'}"]`)];
    return selectors.map(selector=>{const e=document.querySelector(selector);return {selector,width:e.clientWidth,scrollWidth:e.scrollWidth,overflow:e.scrollWidth>e.clientWidth};});
   });
   assert.deepEqual(layout.filter(item=>item.overflow),[],`${viewport.width}/${language} horizontal overflow`);
   await page.screenshot({path:path.join(output,`island-settings-${viewport.width}-${language}-cards.png`)});
   evidence.push({viewport,language,layout,cardOrder:order});await page.close();
  }
  assert.deepEqual(errors,[]);await writeFile(path.join(output,'ui-browser-results.json'),JSON.stringify({entry:'index.html + app.js + native bridge fixture',evidence,errors},null,2));
 }finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
function islandExpandedWidgetCount(){return 9;}
