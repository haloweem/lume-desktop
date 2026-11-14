import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';

test('Dock running-area switch uses the real page, acknowledgement and saved state in both languages',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000,
},async t=>{
  const output=fileURLToPath(new URL('../../../build-artifacts/dock-running-area-20261109/browser/',import.meta.url));
  const harness=await createBrowserHarness(output);
  try {
    for(const language of ['zh-CN','en'])for(const width of [1536,680])await t.test(`${language} ${width}`,async()=>{
      const page=await harness.browser.newPage({viewport:{width,height:1000}});
      const errors=[],report={language,width,cases:[],errors};
      harness.report.viewports.push(report);
      page.on('pageerror',error=>errors.push(error.message));
      try {
        await page.addInitScript(({language})=>{
          const listeners=new Set(),stored=localStorage.getItem('dock-running-area-test');
          const state=stored?JSON.parse(stored):{version:1,type:'dock-state',enabled:true,running:true,
            material:'liquid-glass',reflection:false,showRunning:true,systemTray:true,iconScale:100,
            position:50,bottomOffset:0,iconSpacing:10,pinned:11,added:0,launchpadScale:100,launchpadIconScale:100,
            materialStatus:'native-ready',loadStatus:'loaded',error:'',result:'snapshot',saved:false};
          const emit=data=>listeners.forEach(listener=>listener({data}));
          window.runningAreaFixture={commands:[],state,pending:null,reply(fail=false){
            if(!this.pending)throw new Error('No Dock request is pending');
            const [,,key,value]=this.pending.split('|');this.pending=null;
            if(!fail){
              if(key==='showRunning')state.showRunning=value==='true';
              else if(key==='reset')Object.assign(state,{showRunning:true,reflection:false,
                iconScale:100,position:50,bottomOffset:0,iconSpacing:10,launchpadScale:100,launchpadIconScale:100});
              else throw new Error(`Unexpected Dock action ${key}`);
              localStorage.setItem('dock-running-area-test',JSON.stringify(state));
            }
            emit({...state,result:'applied',saved:!fail,error:fail?'save':''});
          },snapshot(){emit({...state,result:'snapshot',saved:false,error:''});}};
          window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
            removeEventListener:(_,listener)=>listeners.delete(listener),postMessage(command){
              runningAreaFixture.commands.push(command);
              if(command==='v1|ready')queueMicrotask(()=>{
                emit({version:1,type:'app-settings-state',revision:1,language,balancedMode:true,autostart:false,
                  busy:false,error:'',update:{status:'idle',version:'',progress:0}});
                const first={...state,result:'snapshot',saved:false,error:''};
                // Exercise a pre-option host on first opening, then persisted
                // explicit values on subsequent reloads of the real page.
                if(!stored)delete first.showRunning;
                emit(first);emit({version:1,type:'navigate',page:'dock'});
              });
              if(typeof command==='string'&&command.startsWith('v1|dock|')){
                if(runningAreaFixture.pending)throw new Error('Duplicate Dock request while busy');
                runningAreaFixture.pending=command;
              }
            }}};
        },{language});
        const openSettings=async()=>{
          await page.locator('#dock-bottom-offset').waitFor();
          await page.locator('[data-settings-scope="dock"] [data-tab="settings"]').click();
          await page.locator('[data-action="dock-show-running"]').waitFor();
          await page.evaluate(()=>document.fonts.ready);
        };
        const toggle=page.locator('[data-action="dock-show-running"]');
        const capture=async name=>{
          await page.locator('.settings-side-nav__links').evaluate(async nav=>{
            await new Promise(resolve=>requestAnimationFrame(resolve));
            await Promise.all(nav.getAnimations({subtree:true}).map(animation=>animation.finished.catch(()=>{})));
          });
          await page.screenshot({path:path.join(harness.output,`dock-running-${language}-${width}-${name}.png`),fullPage:true});
        };
        await page.goto(harness.origin);await openSettings();
        assert.equal(await toggle.getAttribute('aria-label'),language==='en'?'Running apps':'运行区');
        assert.equal(await toggle.getAttribute('aria-checked'),'true','old snapshots default to visible running apps');
        assert.equal(await toggle.locator('..').locator('.setting-help').textContent(),
          language==='en'?'Show running apps in the Dock':'在 Dock 中显示正在运行的应用');
        const order=await page.locator('[data-settings-scope="dock"] .settings-body').evaluate(body=>
          [...body.children].map(row=>row.querySelector('[data-action]')?.dataset.action).filter(Boolean));
        assert.deepEqual(order,['dock-reflection','dock-show-running','dock-reset']);
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
        const geometry=await toggle.evaluate(button=>{
          const row=button.closest('.setting-row'),previous=row.previousElementSibling;
          const current=row.getBoundingClientRect(),before=previous.getBoundingClientRect(),control=button.getBoundingClientRect();
          return {follows:previous.querySelector('[data-action]')?.dataset.action==='dock-reflection',
            below:current.top>=before.bottom-1,fits:control.left>=0&&control.right<=innerWidth};
        });
        assert.deepEqual(geometry,{follows:true,below:true,fits:true});
        report.cases.push('localized order, layout and legacy default');await capture('default');

        await toggle.click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|showRunning|false');
        assert.equal(await toggle.getAttribute('aria-checked'),'true','wait for a native acknowledgement');
        assert.equal(await toggle.getAttribute('aria-disabled'),'true');
        await page.evaluate(()=>runningAreaFixture.snapshot());
        assert.equal(await toggle.getAttribute('aria-disabled'),'true','background snapshot does not finish the request');
        await toggle.evaluate(button=>button.click());
        assert.equal(await page.evaluate(()=>runningAreaFixture.commands.filter(value=>value==='v1|dock|showRunning|false').length),1);
        await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await toggle.getAttribute('aria-checked'),'false');
        assert.notEqual(await toggle.getAttribute('aria-disabled'),'true');
        assert.equal(await page.evaluate(()=>runningAreaFixture.state.systemTray),true,'running-area toggle is independent of control center');
        report.cases.push('single busy request, snapshot and saved off acknowledgement');

        await page.locator('[data-settings-scope="dock"] [data-action="back"]').click();
        await page.locator('[data-view="desktopBeauty"]').click();
        await page.locator('[data-open="dock"]').click();await openSettings();
        assert.equal(await toggle.getAttribute('aria-checked'),'false','catalog reopening preserves confirmed off');
        await page.reload();await openSettings();
        assert.equal(await toggle.getAttribute('aria-checked'),'false','new manager page restores persisted host value');
        report.cases.push('catalog reopen and page reload preserve saved off');await capture('reopened-off');

        await toggle.click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|showRunning|true');
        await page.evaluate(()=>runningAreaFixture.reply(true));
        assert.equal(await toggle.getAttribute('aria-checked'),'false','failed save retains confirmed off');
        assert.notEqual(await toggle.getAttribute('aria-disabled'),'true');
        assert.match(await page.locator('[data-settings-scope="dock"] .status').textContent(),
          language==='en'?/Could not save/:/保存失败/);
        assert.equal(await page.evaluate(()=>JSON.parse(localStorage.getItem('dock-running-area-test')).showRunning),false);
        report.cases.push('failed save preserves UI and persisted state');await capture('save-failed');

        await page.locator('[data-action="dock-reset"]').click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|reset');
        assert.equal(await toggle.getAttribute('aria-checked'),'false');
        await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await toggle.getAttribute('aria-checked'),'true','native reset restores default on');
        await page.reload();await openSettings();
        assert.equal(await toggle.getAttribute('aria-checked'),'true');
        report.cases.push('reset default survives new page');await capture('reset-on');
        assert.deepEqual(errors,[]);report.passed=true;
      } catch(error){report.error=error.message;throw error;}
      finally {await page.close();}
    });
  } finally {await harness.close();}
});
