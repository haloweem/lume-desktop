import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';

const {chromium}=createRequire(import.meta.url)(path.resolve(process.argv[2]));
const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
const output=fs.mkdtempSync(path.join(os.tmpdir(),'lume-1.0.3-radial-ui-'));
const server=http.createServer((request,response)=>{
  const route=new URL(request.url,'http://localhost').pathname;
  const file=path.resolve(ui,'.'+(route==='/'?'/index.html':route));
  if(!file.startsWith(ui+path.sep)||!fs.existsSync(file)||!fs.statSync(file).isFile()) {
    response.writeHead(404).end();return;
  }
  response.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css',
    '.html':'text/html;charset=utf-8'})[path.extname(file)]||'application/octet-stream');
  response.end(fs.readFileSync(file));
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try {
  browser=await chromium.launch({channel:'msedge',headless:true});
  const page=await browser.newPage({viewport:{width:1440,height:900}});
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  await page.addInitScript(()=>{
    const listeners=new Set(),commands=[];
    let radial={version:1,type:'radial-state',enabled:false,running:false,visible:false,
      material:'liquid-glass',shortcutCode:9,materialStatus:'inactive',loadStatus:'fresh',
      error:'',result:'snapshot',saved:false};
    const receive=data=>listeners.forEach(listener=>listener({data}));
    window.radialFixture={commands,receive,offlineCapture:false,
      navigate(section) {receive({version:1,type:'navigate',page:'radial',...(section?{section}:{})});},
      snapshot(fields={}) {radial={...radial,...fields};receive(radial);}};
    window.chrome={webview:{
      addEventListener(type,listener) {if(type==='message')listeners.add(listener);},
      removeEventListener(type,listener) {if(type==='message')listeners.delete(listener);},
      postMessage(command) {
        commands.push(command);
        if(command.startsWith('v1|radial|capture|')) {
          if(window.radialFixture.offlineCapture)throw new Error('capture transport unavailable');
          queueMicrotask(()=>receive({...radial,result:'snapshot',saved:false}));
        }
        if(command==='v1|ready')queueMicrotask(()=>receive(radial));
      },
    }};
  });
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  const radialCommands=()=>page.evaluate(()=>window.radialFixture.commands.filter(command=>
    typeof command==='string'&&command.startsWith('v1|radial|')&&!command.startsWith('v1|radial|capture|')));
  const captureCommands=()=>page.evaluate(()=>window.radialFixture.commands.filter(command=>
    typeof command==='string'&&command.startsWith('v1|radial|capture|')));
  const allRadialCommands=()=>page.evaluate(()=>window.radialFixture.commands.filter(command=>
    typeof command==='string'&&command.startsWith('v1|radial|')));
  const snapshot=fields=>page.evaluate(fields=>window.radialFixture.snapshot(fields),fields);
  const applied=fields=>snapshot({result:'applied',saved:true,loadStatus:'loaded',error:'',...fields});
  await page.locator('[data-view="desktopBeauty"]').click();
  const cards=await page.locator('.Card [data-open]').evaluateAll(items=>items.map(item=>item.dataset.open));
  assert.ok(cards.includes('radial'),'real catalog exposes radial settings');
  assert.equal(cards[cards.indexOf('radial')+1],'widgets');
  const toggle=page.locator('[data-action="radial-toggle"]');
  await toggle.click();assert.equal((await radialCommands()).at(-1),'v1|radial|enabled|true');
  assert.equal(await toggle.isDisabled(),true);assert.equal(await toggle.getAttribute('aria-checked'),'false');
  await snapshot({result:'snapshot',saved:false,materialStatus:'native-starting'});
  assert.equal(await toggle.isDisabled(),true,'snapshot cannot settle pending');
  await applied({enabled:true,running:true,materialStatus:'native-ready'});
  assert.equal(await toggle.getAttribute('aria-checked'),'true');assert.equal(await toggle.isDisabled(),false);
  await page.locator('[data-open="radial"]').click();
  assert.equal(await page.locator('[data-radial-slot]').count(),10);
  assert.equal(await page.locator('[data-radial-available="true"]').count(),1);
  assert.equal(await page.locator('[data-radial-placeholder]').count(),8);
  const recorder=page.locator('[data-radial-shortcut]');
  assert.equal(await recorder.getAttribute('aria-pressed'),'false','ordinary open never starts capture');
  assert.deepEqual(await captureCommands(),[],'ordinary catalog/page open never pauses the native shortcut hook');
  await recorder.focus();let count=(await radialCommands()).length;
  await page.keyboard.press('Tab');assert.equal((await radialCommands()).length,count,'normal Tab stays navigation');
  await page.locator('#radial-material').click();
  await page.locator('[data-select-owner="radial-material"][data-select-value="frosted-glass"]').click();
  assert.equal((await radialCommands()).at(-1),'v1|radial|material|frosted-glass');
  assert.equal(await page.locator('[data-action="radial-reset"]').isDisabled(),true);
  await snapshot({result:'applied',saved:false,error:'save'});
  assert.equal(await page.locator('#radial-material span').first().textContent(),'液态玻璃');
  assert.match(await page.locator('[data-settings-scope="radial"] .status').textContent(),/保存失败/);
  await applied({material:'frosted-glass'});
  await recorder.click();assert.equal(await recorder.getAttribute('aria-pressed'),'true');
  assert.deepEqual(await captureCommands(),['v1|radial|capture|true'],'successful start announces capture');
  assert.equal(await recorder.isDisabled(),false,'capture snapshot does not mark settings as saving');
  assert.equal(await page.locator('[data-action="radial-reset"]').isDisabled(),false);
  assert.equal(await page.locator('[data-settings-scope="radial"] .status').textContent(),'');
  count=(await radialCommands()).length;
  await page.keyboard.down('Control');await page.keyboard.up('Control');
  assert.equal((await radialCommands()).length,count,'modifiers alone never persist');
  await page.keyboard.down('Control');await page.keyboard.down('Alt');await page.keyboard.down('b');
  assert.equal((await radialCommands()).length,count,'keydown alone never persists');
  await page.keyboard.up('b');await page.keyboard.up('Alt');await page.keyboard.up('Control');
  assert.equal((await radialCommands()).at(-1),'v1|radial|shortcut|834');
  assert.deepEqual((await allRadialCommands()).slice(-3),['v1|radial|capture|true',
    'v1|radial|capture|false','v1|radial|shortcut|834'],'capture ends before persisted shortcut');
  assert.equal(await recorder.getAttribute('aria-pressed'),'false');
  await snapshot({result:'snapshot',saved:false});assert.equal(await recorder.getAttribute('aria-disabled'),'true');
  await applied({shortcutCode:834});assert.match(await recorder.textContent(),/Ctrl \+ Alt \+ B/);
  for(const fields of [{keyCode:65},{keyCode:0}]) {
    await recorder.click();count=(await radialCommands()).length;
    await recorder.evaluate((element,fields)=>element.dispatchEvent(new KeyboardEvent('keydown',
      {bubbles:true,cancelable:true,code:'KeyQ',key:'a',ctrlKey:true,...fields})),fields);
    assert.equal((await radialCommands()).length,count,'AZERTY keydown alone never persists');
    assert.equal(await recorder.textContent(),'Ctrl + A','AZERTY preview follows logical Windows VK');
    await recorder.evaluate((element,fields)=>element.dispatchEvent(new KeyboardEvent('keyup',
      {bubbles:true,cancelable:true,code:'KeyQ',key:'a',ctrlKey:true,...fields})),fields);
    assert.equal((await radialCommands()).at(-1),'v1|radial|shortcut|577','AZERTY Ctrl+A persists 577, not physical Ctrl+Q');
    assert.deepEqual((await allRadialCommands()).slice(-3),['v1|radial|capture|true',
      'v1|radial|capture|false','v1|radial|shortcut|577'],'non-US capture ordering');
    await applied({shortcutCode:577});assert.equal(await recorder.textContent(),'Ctrl + A');
  }
  await recorder.click();count=(await radialCommands()).length;
  await page.keyboard.down('c');await page.locator('[data-tab="details"]').focus();await page.keyboard.up('c');
  assert.equal((await radialCommands()).length,count,'blur cancels candidate');
  assert.equal((await captureCommands()).at(-1),'v1|radial|capture|false','element blur ends capture');
  assert.equal(await recorder.getAttribute('aria-pressed'),'false');
  await recorder.click();await page.keyboard.press('Escape');
  assert.equal((await radialCommands()).length,count,'Escape cancels capture');
  assert.equal((await captureCommands()).at(-1),'v1|radial|capture|false','Escape ends capture');
  assert.equal(await recorder.getAttribute('aria-pressed'),'false');
  for(const fields of [
    {key:'Tab',code:'Tab',altKey:true},{key:'Delete',code:'Delete',altKey:true,ctrlKey:true},
    {key:'l',code:'KeyL',metaKey:true},{key:'Tab',code:'Tab',metaKey:true},
    {key:'Process',code:'KeyA'},{key:'a',code:'KeyA',isComposing:true},
  ]) {
    await recorder.click();
    await recorder.evaluate((element,fields)=>{
      for(const type of ['keydown','keyup'])element.dispatchEvent(new KeyboardEvent(type,{bubbles:true,cancelable:true,...fields}));
    },fields);
    assert.equal((await radialCommands()).length,count,`reserved or composing ${fields.code}`);
    await page.keyboard.press('Escape');
  }
  await recorder.click();await page.keyboard.press('Tab');
  assert.deepEqual((await allRadialCommands()).slice(-3),['v1|radial|capture|true',
    'v1|radial|capture|false','v1|radial|shortcut|9'],'Tab handshake completes before save');
  assert.equal((await radialCommands()).at(-1),'v1|radial|shortcut|9');await applied({shortcutCode:9});
  const ordinaryCount=(await captureCommands()).length;
  await page.evaluate(()=>window.radialFixture.navigate());
  assert.equal(await recorder.getAttribute('aria-pressed'),'false');
  await page.evaluate(()=>window.radialFixture.receive({version:1,type:'navigate',page:'radial',section:'settings'}));
  assert.equal(await recorder.getAttribute('aria-pressed'),'false','unknown section ignored');
  assert.equal((await captureCommands()).length,ordinaryCount,'ordinary/invalid native navigation does not capture');
  await page.evaluate(()=>window.radialFixture.navigate('shortcut'));
  assert.equal(await recorder.evaluate(element=>element===document.activeElement),true);
  assert.equal(await recorder.getAttribute('aria-pressed'),'true','native custom-shortcut entry starts capture');
  assert.equal((await captureCommands()).at(-1),'v1|radial|capture|true','native menu entry starts transient hook pause');
  await page.evaluate(()=>window.radialFixture.navigate());
  assert.equal((await captureCommands()).at(-1),'v1|radial|capture|false','render/navigation ends capture without blur');
  assert.equal(await recorder.getAttribute('aria-pressed'),'false','ordinary native open cancels recording');
  await page.evaluate(()=>window.radialFixture.navigate('shortcut'));
  await page.keyboard.press('Escape');
  await recorder.click();count=(await radialCommands()).length;
  await page.keyboard.down('d');
  await recorder.evaluate(element=>element.dispatchEvent(new CompositionEvent('compositionstart',{bubbles:true})));
  await page.keyboard.up('d');
  assert.equal((await radialCommands()).length,count,'IME composition cancels an incomplete shortcut');
  await page.keyboard.press('Escape');
  await recorder.click();await page.keyboard.down('f');
  await page.evaluate(()=>window.dispatchEvent(new Event('blur')));
  await page.keyboard.up('f');
  assert.equal((await radialCommands()).length,count,'window blur cancels without needing element blur');
  assert.equal((await captureCommands()).at(-1),'v1|radial|capture|false','window blur ends capture');
  await recorder.click();
  await recorder.evaluate(element=>{
    element.dispatchEvent(new KeyboardEvent('keydown',{bubbles:true,cancelable:true,key:'g',code:'KeyG'}));
    element.dispatchEvent(new KeyboardEvent('keyup',{bubbles:true,cancelable:true,key:'Process',code:'KeyG'}));
  });
  assert.equal((await radialCommands()).length,count,'IME process key on release cannot persist a candidate');
  await page.keyboard.press('Escape');
  await recorder.click();await page.keyboard.down('e');
  await page.evaluate(()=>window.radialFixture.receive({version:1,type:'visibility',visible:false}));
  await page.keyboard.up('e');
  assert.equal((await radialCommands()).length,count,'hidden manager cancels an incomplete shortcut');
  assert.equal((await captureCommands()).at(-1),'v1|radial|capture|false','manager hide ends capture');
  await page.evaluate(()=>window.radialFixture.receive({version:1,type:'visibility',visible:true}));
  await page.locator('[data-action="radial-reset"]').click();
  assert.equal((await radialCommands()).at(-1),'v1|radial|reset');
  await applied({material:'liquid-glass',shortcutCode:9});
  count=(await radialCommands()).length;
  await page.locator('[data-radial-slot="0"]').click({button:'right'});
  assert.equal((await radialCommands()).length,count,'context menu is parent-owned');
  for(const language of ['zh-CN','en']) {
    if(language==='en')await recorder.click();
    await page.evaluate(language=>window.radialFixture.receive({version:1,type:'app-settings-state',
      revision:language==='en'?2:1,language,balancedMode:true,autostart:false,busy:false,error:'',
      update:{status:'idle',version:'',progress:0}}),language);
    if(language==='en') {
      assert.equal(await recorder.getAttribute('aria-pressed'),'false','language render cancels recording');
      assert.equal((await captureCommands()).at(-1),'v1|radial|capture|false','language change ends capture');
    }
    for(const [width,height] of [[1440,900],[390,844]]) {
      await page.setViewportSize({width,height});
      assert.equal(await page.locator('[data-radial-slot]').count(),10);
      const overflow=await page.locator('[data-settings-scope="radial"]').evaluate(root=>{
        const container=root.getBoundingClientRect();
        return [...root.querySelectorAll('[data-radial-slot],.setting-select,[data-radial-shortcut],.reset')]
          .filter(element=>{const rect=element.getBoundingClientRect();return rect.left<container.left-1||
            rect.right>container.right+1||element.scrollWidth>element.clientWidth+1;})
          .map(element=>element.className);
      });
      assert.deepEqual(overflow,[],`${language} ${width}px no horizontal overflow`);
      await page.screenshot({path:path.join(output,`${language}-${width}.png`),fullPage:true});
    }
    if(language==='en') {
      assert.match(await page.locator('[data-settings-scope="radial"] h3').first().textContent(),/Radial menu/);
      assert.equal(await recorder.getAttribute('title'),'Record shortcut');
    }
  }
  const noticeBefore=await page.locator('#notice').textContent();
  await page.evaluate(()=>{window.radialFixture.offlineCapture=true;});
  await recorder.click();assert.equal(await recorder.getAttribute('aria-pressed'),'true');
  assert.equal(await recorder.isDisabled(),false,'failed capture send does not disable preferences');
  await page.keyboard.press('Escape');
  assert.equal(await page.locator('#notice').textContent(),noticeBefore,'failed capture/cancel sends do not create unavailable toasts');
  assert.equal(await page.locator('[data-settings-scope="radial"] .status').textContent(),'');
  await page.evaluate(()=>{window.radialFixture.offlineCapture=false;});
  await recorder.click();count=(await radialCommands()).length;
  await page.evaluate(()=>{
    window.radialFixture.offlineCapture=true;window.dispatchEvent(new Event('beforeunload'));
  });
  assert.equal((await captureCommands()).at(-1),'v1|radial|capture|false','disposal attempts capture end even if transport fails');
  assert.equal((await radialCommands()).length,count,'disposal does not save preferences');
  assert.equal(await page.locator('#notice').textContent(),noticeBefore,'disposal stays silent');
  const captures=await captureCommands();
  for(let index=0;index<captures.length;index++)
    assert.equal(captures[index],`v1|radial|capture|${index%2===0}`,'capture commands alternate begin/end');
  assert.equal(captures.length%2,0,'no capture remains outstanding');
  assert.deepEqual(errors,[],'real application has no uncaught browser exceptions');
  const early=await browser.newPage();
  await early.addInitScript(()=>{
    const listeners=new Set(),commands=[];
    const receive=data=>listeners.forEach(listener=>listener({data}));
    window.earlyRadial={receive,commands};
    window.chrome={webview:{addEventListener:(type,listener)=>listeners.add(listener),
      removeEventListener:(type,listener)=>listeners.delete(listener),postMessage(command){
        commands.push(command);
        if(command==='v1|ready')queueMicrotask(()=>receive({version:1,type:'navigate',page:'radial',section:'shortcut'}));
      }}};
  });
  await early.goto(`http://127.0.0.1:${server.address().port}/`);
  const earlyRecorder=early.locator('[data-radial-shortcut]');
  await earlyRecorder.waitFor();assert.equal(await earlyRecorder.getAttribute('aria-pressed'),'false');
  const earlyCaptures=()=>early.evaluate(()=>window.earlyRadial.commands.filter(command=>command.startsWith('v1|radial|capture|')));
  assert.deepEqual(await earlyCaptures(),[],'loading/deferred start never announces capture');
  await early.evaluate(()=>window.earlyRadial.receive({version:1,type:'radial-state',enabled:false,
    running:false,visible:false,material:'liquid-glass',shortcutCode:9,materialStatus:'inactive',
    loadStatus:'fresh',error:'',result:'snapshot',saved:false}));
  assert.equal(await earlyRecorder.getAttribute('aria-pressed'),'true','explicit shortcut navigation waits for state');
  assert.equal(await earlyRecorder.evaluate(element=>element===document.activeElement),true);
  assert.deepEqual(await earlyCaptures(),['v1|radial|capture|true'],'delayed snapshot starts capture only once ready');
  await early.keyboard.press('Escape');
  assert.deepEqual(await earlyCaptures(),['v1|radial|capture|true','v1|radial|capture|false']);
  await early.evaluate(()=>window.earlyRadial.receive({version:1,type:'navigate',page:'radial'}));
  assert.equal(await earlyRecorder.getAttribute('aria-pressed'),'false','ordinary navigation clears a deferred capture');
  await early.reload();await earlyRecorder.waitFor();
  await early.evaluate(()=>{
    window.earlyRadial.receive({version:1,type:'navigate',page:'radial'});
    window.earlyRadial.receive({version:1,type:'radial-state',enabled:false,running:false,visible:false,
      material:'liquid-glass',shortcutCode:9,materialStatus:'inactive',loadStatus:'fresh',error:'',
      result:'snapshot',saved:false});
  });
  assert.equal(await earlyRecorder.getAttribute('aria-pressed'),'false','ordinary open cancels capture before delayed state arrives');
  assert.deepEqual(await earlyCaptures(),[],'ordinary navigation cancels deferred start without emitting capture');
  await early.close();
  console.log('Radial browser: non-US VK, transient capture ordering, snapshot/failed-send/disposal isolation, cancellation, deferred native navigation, preferences and four responsive/language screenshots passed.');
  console.log(`Screenshots: ${output}`);
} finally {
  await browser?.close();await new Promise(resolve=>server.close(resolve));
}
