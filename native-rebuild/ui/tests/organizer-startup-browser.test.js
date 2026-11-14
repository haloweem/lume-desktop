import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness,createFixturePage} from './component-settings-browser.mjs';
import {validHostMessage} from '../bridge.js';

const output=process.env.LUME_ORGANIZER_BROWSER_OUTPUT||fileURLToPath(
  new URL('../../../build-artifacts/organizer-capacity-20261109/browser/',import.meta.url));

test('真实 Edge：收纳开关即时反馈、等待状态、失败回滚与关闭',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:60000,
},async()=>{
  const harness=await createBrowserHarness(path.join(output,'startup'));
  try {
    for(const viewport of [{width:1280,height:720},{width:680,height:864}]) {
      const fixture=await createFixturePage(harness,viewport);
      const {page}=fixture;
      try {
        await page.evaluate(()=>{componentFixture.prepare('organizer');componentFixture.navigate('organizer');});
        await page.locator('[data-action="back"]').click();
        const toggle=page.locator('[data-action="organizer-toggle"]');
        const label=page.locator('[data-organizer-pending]');
        for(const {enabled,fail} of [{enabled:true,fail:true},{enabled:true,fail:false},{enabled:false,fail:false}]) {
          await toggle.click();
          assert.equal(await toggle.getAttribute('aria-checked'),String(enabled));
          assert.equal(await toggle.getAttribute('aria-busy'),'true');
          assert.equal(await toggle.isDisabled(),true);
          assert.equal(await label.isVisible(),true);
          assert.match(await label.textContent(),enabled?/正在开启/:/正在关闭/);
          const bounds=await label.boundingBox(),switchBounds=await toggle.boundingBox();
          assert.ok(bounds.x>=0 && bounds.x+bounds.width<switchBounds.x,'等待文字与开关不能重叠');
          await page.clock.runFor(1500);
          assert.equal(await toggle.getAttribute('aria-checked'),String(enabled),'慢回执期间保持用户请求');
          if(enabled&&!fail)await page.screenshot({path:path.join(harness.output,`${viewport.width}-organizer-starting.png`),animations:'disabled'});
          await page.evaluate(fail=>componentFixture.reply('organizer',fail),fail);
          assert.equal(await toggle.getAttribute('aria-checked'),String(fail?!enabled:enabled));
          assert.equal(await toggle.isEnabled(),true);
          assert.equal(await toggle.getAttribute('aria-busy'),'false');
          assert.equal(await label.isVisible(),false);
        }
        for(const key of ['errors','consoleErrors','remoteRequests','failedRequests','httpErrors'])
          assert.deepEqual(fixture.report[key],[],`浏览器 ${key}`);
        for(const file of ['/app.js','/app.css','/organizer.js','/state.js'])
          assert.ok(fixture.report.localRequests.includes(file),`使用真实 ${file}`);
      } finally {await fixture.context.close();}
    }
  } finally {await harness.close();}
});

function capacityFixture({language}) {
  const listeners=new Set(),commands=[],received=[];
  let pending=null;
  const state={version:1,type:'organizer-state',enabled:true,running:true,locked:false,
    material:'liquid-glass',materialStatus:'inactive',loadStatus:'loaded',error:'',width:260,height:44,
    result:'snapshot',saved:false,title:'Desktop collection',count:73,mode:'collapsed',layout:'icons',iconScale:70,
    collections:[{id:'default',title:'Desktop collection',enabled:true,running:true,count:73}]};
  const emit=data=>{received.push(data);listeners.forEach(listener=>listener({data}));};
  const snapshot=()=>emit({...state,collections:state.collections.map(collection=>({...collection})),result:'snapshot',saved:false,error:''});
  window.organizerCapacityFixture={commands,received,state,
    count(value){state.count=value;state.collections[0].count=value;snapshot();},
    navigate(){emit({version:1,type:'navigate',page:'organizer'});},
    reply(fail=false){
      if(pending===null)throw new Error('No organizer request is pending');
      if(!fail){
        state[pending.key]=pending.value;
        if(pending.key==='enabled'){
          state.running=pending.value;state.collections[0].enabled=state.collections[0].running=pending.value;
        }
      }
      pending=null;
      emit({...state,collections:state.collections.map(collection=>({...collection})),result:'applied',saved:!fail,error:fail?'save':''});
    },
  };
  window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),removeEventListener:(_,listener)=>listeners.delete(listener),
    postMessage(command){
      commands.push(command);
      if(command==='v1|ready')queueMicrotask(()=>{
        emit({version:1,type:'app-settings-state',revision:1,language,balancedMode:true,autostart:false,busy:false,error:'',update:{status:'idle',version:'',progress:0}});
        snapshot();organizerCapacityFixture.navigate();
      });
      else if(typeof command==='string'&&/^v1\|organizer\|(enabled|material|iconScale)\|/.test(command)){
        if(pending!==null)throw new Error('Duplicate organizer request while busy');
        const [,scope,key,value]=command.split('|');
        if(scope!=='organizer')throw new Error('Unexpected component command');
        pending={key,value:key==='enabled'?value==='true':key==='iconScale'?Number(value):value};
      }
    },
  }};
}

test('真实 Edge：中英文折叠收纳的大数量回执解除等待，材质及图标设置可保存回滚',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000,
},async t=>{
  const harness=await createBrowserHarness(path.join(output,'settings'));
  try {
    for(const language of ['zh-CN','en'])for(const width of [1536,680])await t.test(`${language} ${width}`,async()=>{
      const page=await harness.browser.newPage({viewport:{width,height:864}});
      const report={language,width,geometry:{mode:'collapsed',width:260,height:44},counts:[],controls:[],errors:[],failedRequests:[]};harness.report.viewports.push(report);
      page.on('pageerror',error=>report.errors.push(error.message));
      page.on('response',response=>{if(response.status()>=400)report.failedRequests.push(response.url());});
      try {
        await page.addInitScript(capacityFixture,{language});await page.goto(harness.origin);
        let iconScale=70,material='liquid-glass';
        const checkSettings=async count=>{
          const panel=page.locator('[data-settings-scope="organizer"]');await panel.waitFor();
          assert.doesNotMatch(await panel.textContent(),/已收纳项目|Collected items|\d+ 个项目|\d+ items?/);
          assert.equal(await panel.locator('.setting-row').count(),2);
          assert.equal(await panel.locator('#organizer-material').count(),1);
          assert.equal(await panel.locator('#organizer-icon-scale').inputValue(),String(iconScale));
          assert.equal(await panel.locator('[data-action="organizer-reset"]').isEnabled(),true);
          assert.equal(await panel.locator('.settings-component-title').textContent(),language==='en'?'Icon organizer':'图标收纳');
          assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
          await page.evaluate(()=>document.fonts.ready);
          await page.screenshot({path:path.join(harness.output,`${language}-${width}-count-${count}.png`),fullPage:true,animations:'disabled'});
        };
        await checkSettings(73);
        let enabled=true;
        for(const count of [121,1001,10000]){
          await page.evaluate(count=>organizerCapacityFixture.count(count),count);
          await page.locator('[data-settings-scope="organizer"] [data-action="back"]').click();
          const toggle=page.locator('[data-action="organizer-toggle"]');await toggle.waitFor();
          assert.equal(await toggle.getAttribute('aria-checked'),String(enabled));
          await toggle.click();
          assert.equal(await toggle.getAttribute('aria-checked'),String(!enabled));
          assert.equal(await toggle.getAttribute('aria-busy'),'true');
          assert.equal(await toggle.isDisabled(),true);
          const fail=count===10000;
          await page.evaluate(fail=>organizerCapacityFixture.reply(fail),fail);
          if(!fail)enabled=!enabled;
          assert.equal(await toggle.getAttribute('aria-checked'),String(enabled));
          assert.equal(await toggle.getAttribute('aria-busy'),'false','large native counts must not leave the card waiting');
          assert.equal(await toggle.isEnabled(),true);
          assert.equal(await page.locator('[data-organizer-pending]').isVisible(),false);
          if(fail){
            await toggle.click();await page.evaluate(()=>organizerCapacityFixture.reply());enabled=!enabled;
            assert.equal(await toggle.getAttribute('aria-busy'),'false','retry with the same large count also completes');
            assert.equal(await toggle.getAttribute('aria-checked'),String(enabled));
          }
          await page.evaluate(()=>organizerCapacityFixture.navigate());await checkSettings(count);
          const materialControl=page.locator('#organizer-material'),slider=page.locator('#organizer-icon-scale');
          const nextMaterial=material==='liquid-glass'?'frosted-glass':'liquid-glass';
          await materialControl.click();
          await page.locator(`[data-select-owner="organizer-material"][data-select-value="${nextMaterial}"]`).click();
          assert.equal(await page.evaluate(()=>organizerCapacityFixture.commands.at(-1)),`v1|organizer|material|${nextMaterial}`);
          assert.equal(await materialControl.isDisabled(),true);
          assert.equal(await slider.getAttribute('aria-disabled'),'true');
          await page.evaluate(count=>organizerCapacityFixture.count(count),count);
          assert.equal(await materialControl.isDisabled(),true,'a compact snapshot cannot settle an outstanding settings write');
          await page.evaluate(()=>organizerCapacityFixture.reply());material=nextMaterial;
          assert.equal(await materialControl.isEnabled(),true);
          assert.equal(await slider.getAttribute('aria-disabled'),'false');
          const nextScale=iconScale+7,number=page.locator('#organizer-icon-scale-number');
          await number.fill(String(nextScale));await number.press('Enter');
          assert.equal(await page.evaluate(()=>organizerCapacityFixture.commands.at(-1)),`v1|organizer|iconScale|${nextScale}`);
          assert.equal(await slider.getAttribute('aria-disabled'),'true');
          assert.equal(await page.locator('[data-action="organizer-reset"]').isDisabled(),true);
          await page.evaluate(fail=>organizerCapacityFixture.reply(fail),fail);
          if(!fail)iconScale=nextScale;
          assert.equal(await slider.inputValue(),String(iconScale),'compact reply keeps the confirmed or rolled-back icon size');
          assert.equal(await slider.getAttribute('aria-disabled'),'false');
          assert.equal(await number.inputValue(),String(iconScale));
          assert.equal(await page.locator('[data-action="organizer-reset"]').isEnabled(),true);
          if(fail){
            assert.equal(await page.locator('[data-settings-scope="organizer"] .status[role="alert"]').isVisible(),true);
            await number.fill(String(nextScale));await number.press('Enter');
            await page.evaluate(()=>organizerCapacityFixture.reply());iconScale=nextScale;
            assert.equal(await slider.inputValue(),String(iconScale));
            assert.equal(await slider.getAttribute('aria-disabled'),'false','retry with a compact reply unlocks settings');
          }
          await checkSettings(count);
          report.controls.push({count,material,iconScale,saved:true,failureRollback:fail});
          report.counts.push({count,enabled,saved:true,failureRollback:fail});
        }
        const messages=await page.evaluate(()=>organizerCapacityFixture.received);
        for(const message of messages)assert.equal(validHostMessage(message),true,'all synthetic host messages use the production validator');
        const countMessages=messages.filter(message=>message.type==='organizer-state');
        assert.ok(countMessages.length>0);
        assert.ok(countMessages.every(message=>message.mode==='collapsed'&&message.width===260&&message.height===44),
          'every large-count snapshot and settings reply retains the exact native strip geometry');
        for(const count of [121,1001,10000])assert.ok(countMessages.some(message=>message.count===count&&message.collections[0].count===count&&message.result==='applied'));
        assert.deepEqual(report.errors,[]);assert.deepEqual(report.failedRequests,[]);report.passed=true;
      }catch(error){report.error=error.message;throw error;}
      finally{await page.close();}
    });
  }finally{await harness.close();}
});
