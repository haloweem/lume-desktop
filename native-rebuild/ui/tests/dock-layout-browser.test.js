import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {createBrowserHarness} from './component-settings-browser.mjs';

test('Dock layout controls work through the real page and native protocol',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:60000,
},async()=>{
  const harness=await createBrowserHarness();
  try {
    for(const width of [1536,680]) {
      const page=await harness.browser.newPage({viewport:{width,height:1000}});
      try {
        await page.addInitScript(()=>{
          const listeners=new Set();
          const state={version:1,type:'dock-state',enabled:true,running:true,material:'liquid-glass',
            reflection:false,iconScale:100,position:50,bottomOffset:0,iconSpacing:10,pinned:11,added:0,
            launchpadScale:100,launchpadIconScale:100,
            materialStatus:'native-ready',loadStatus:'loaded',error:'',result:'snapshot',saved:false};
          const emit=data=>listeners.forEach(listener=>listener({data}));
          window.dockFixture={commands:[],state,delay:0,fail:false};
          window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
            removeEventListener:(_,listener)=>listeners.delete(listener),postMessage(command){
              window.dockFixture.commands.push(command);
              if(command==='v1|ready')queueMicrotask(()=>{
                emit({...state});emit({version:1,type:'navigate',page:'dock'});
              });
              if(typeof command==='string'&&command.startsWith('v1|dock|'))setTimeout(()=>{
                const [, ,key,value]=command.split('|');
                if(window.dockFixture.fail){emit({...state,error:'save',result:'applied',saved:false});return;}
                if(key==='reset')Object.assign(state,{iconScale:100,position:50,bottomOffset:0,iconSpacing:10,launchpadScale:100,launchpadIconScale:100});
                else if(['iconScale','position','bottomOffset','iconSpacing','launchpadScale','launchpadIconScale'].includes(key))state[key]=Number(value);
                emit({...state,result:'applied',saved:true});
              },window.dockFixture.delay);
            }}};
        });
        await page.goto(harness.origin);
        await page.locator('#dock-bottom-offset').waitFor();
        for(const [id,value,field] of [['dock-icon-scale','105','iconScale'],['dock-bottom-offset','80','bottomOffset'],['dock-icon-spacing','24','iconSpacing'],['dock-launchpad-scale','83','launchpadScale'],['dock-launchpad-icon-scale','50','launchpadIconScale']]) {
          await page.locator(`#${id}`).evaluate((input,value)=>{
            input.value=value;
            input.dispatchEvent(new Event('input',{bubbles:true}));
            input.dispatchEvent(new Event('change',{bubbles:true}));
          },value);
          await page.waitForFunction(([field,value])=>window.dockFixture.state[field]===Number(value),[field,value]);
          assert.ok(await page.evaluate(command=>window.dockFixture.commands.includes(command),`v1|dock|${field}|${value}`));
          assert.equal(await page.locator(`#${id}-number`).inputValue(),value,'slider syncs numeric input');
        }
        for(const [id,value,key] of [['dock-icon-scale','81','iconScale'],['dock-bottom-offset','73','bottomOffset'],['dock-icon-spacing','17','iconSpacing'],['dock-launchpad-scale','97','launchpadScale'],['dock-launchpad-icon-scale','120','launchpadIconScale']]){
          const field=page.locator(`#${id}-number`);await field.fill(value);
          assert.equal(await page.locator(`#${id}`).inputValue(),value,'typing previews slider');
          await field.press('Enter');
          await page.waitForFunction(([key,value])=>window.dockFixture.state[key]===Number(value),[key,value]);
          assert.equal(await field.inputValue(),value);
        }
        const numeric=page.locator('#dock-launchpad-icon-scale-number');
        for(const invalid of ['','49','121','75.5']){
          await numeric.fill(invalid);await numeric.press('Enter');
          assert.equal(await numeric.inputValue(),'120','invalid input restores confirmed value');
          assert.equal(await page.evaluate(()=>window.dockFixture.state.launchpadIconScale),120);
        }
        await numeric.fill('50');await numeric.press('Tab');
        await page.waitForFunction(()=>window.dockFixture.state.launchpadIconScale===50);
        await numeric.fill('88');await numeric.press('Escape');
        assert.equal(await numeric.inputValue(),'50','Escape cancels draft');
        assert.equal(await page.evaluate(()=>window.dockFixture.state.launchpadIconScale),50);
        await page.evaluate(()=>{window.dockFixture.delay=150;window.dockFixture.fail=true;});
        await numeric.fill('81');await numeric.press('Enter');
        assert.equal(await numeric.inputValue(),'81','submitted draft remains until native acknowledgement');
        await page.waitForFunction(()=>document.querySelector('#dock-launchpad-icon-scale-number').value==='50');
        assert.equal(await page.locator('#dock-launchpad-icon-scale').inputValue(),'50','failed save restores slider too');
        assert.equal(await page.evaluate(()=>window.dockFixture.state.launchpadIconScale),50);
        await page.evaluate(()=>{window.dockFixture.delay=0;window.dockFixture.fail=false;});
        const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth);
        assert.equal(overflow,false,'layout fits the viewport');
        await page.screenshot({path:path.join(harness.output,`dock-layout-${width}.png`),fullPage:true});
        await page.locator('[data-action="dock-reset"]').click();
        await page.waitForFunction(()=>window.dockFixture.state.position===50&&window.dockFixture.state.iconSpacing===10);
        assert.equal(await page.locator('#dock-position').count(),0);
      } finally {await page.close();}
    }
  } finally {await harness.close();}
});
