import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {createBrowserHarness} from './component-settings-browser.mjs';
import {validHostMessage} from '../bridge.js';

const island={version:1,type:'dynamicIsland-state',enabled:true,running:true,locked:true,
  expanded:false,focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:false,
  hover:false,autoHide:true,size:'standard',material:'none',shape:'capsule',leftWidget:0,rightWidget:0,
  screen:1,screens:[1],loadStatus:'loaded',error:'',result:'snapshot',saved:false};
const settings={version:1,type:'app-settings-state',revision:1,language:'zh-CN',balancedMode:true,
  autostart:false,busy:false,error:'',update:{status:'idle',version:'',progress:0}};

function installFixture({island,settings,language}) {
  const listeners=new Set(),commands=[];
  let current=structuredClone(island),pending=null,revision=1;
  const receive=message=>listeners.forEach(listener=>listener({data:message}));
  const snapshot=()=>receive({...current,result:'snapshot',saved:false,error:''});
  window.islandScreensFixture={
    commands,
    set(screen,screens) {current={...current,screen,screens};snapshot();},
    reply(failed=false) {
      if(pending===null)throw new Error('No pending display policy command');
      if(!failed)current[pending.key]=pending.value;
      pending=null;
      receive({...current,result:'applied',saved:!failed,error:failed?'save':''});
    },
    navigate() {receive({version:1,type:'navigate',page:'dynamicIsland'});},
  };
  window.chrome={webview:{
    addEventListener(type,listener) {if(type==='message')listeners.add(listener);},
    removeEventListener(type,listener) {if(type==='message')listeners.delete(listener);},
    postMessage(command) {
      commands.push(command);
      if(command==='v1|ready') {
        queueMicrotask(()=>{
          receive({...settings,language,revision:++revision});snapshot();
          window.islandScreensFixture.navigate();
        });
      } else if(typeof command==='string'&&/^v1\|dynamicIsland\|screen\|\d+$/.test(command)) {
        if(pending!==null)throw new Error('Duplicate display policy request while saving');
        pending={key:'screen',value:Number(command.split('|')[3])};
      } else if(typeof command==='string'&&/^v1\|dynamicIsland\|hover\|(true|false)$/.test(command)) {
        if(pending!==null)throw new Error('Duplicate expansion mode request while saving');
        pending={key:'hover',value:command.split('|')[3]==='true'};
      } else if(typeof command==='string'&&/^v1\|dynamicIsland\|preview\|(compact|expanded)$/.test(command)) {
        queueMicrotask(()=>receive({version:1,type:'dynamicIsland-preview',mode:command.split('|')[3],
          shape:current.shape,size:current.size,material:current.material,width:0,height:0,image:'',error:'render'}));
      } else if(command?.scope==='app-settings'&&command.action==='snapshot') {
        queueMicrotask(()=>receive({...settings,language,revision:++revision}));
      }
    },
  }};
}

test('real settings entry: display policy and expansion mode follow native acknowledgements and layout',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000,
},async t=>{
  assert.ok(validHostMessage(island));assert.ok(validHostMessage(settings));
  const harness=await createBrowserHarness(process.env.LUME_ISLAND_BROWSER_OUTPUT||path.resolve('build-artifacts/island-display-policy-browser-20261104'));
  try {
    for(const width of [1536,680])for(const language of ['zh-CN','en']) {
      await t.test(`${width}px ${language}`,async()=>{
        const page=await harness.browser.newPage({viewport:{width,height:864},reducedMotion:'reduce'});
        const errors=[],remoteRequests=[];
        page.on('pageerror',error=>errors.push(error.message));
        await page.route('**/*',route=>{
          if(new URL(route.request().url()).origin===harness.origin)return route.continue();
          remoteRequests.push(route.request().url());return route.abort('blockedbyclient');
        });
        const report={width,height:864,language,counts:[],layouts:[],passed:false};
        harness.report.viewports.push(report);
        try {
          await page.addInitScript(installFixture,{island,settings,language});
          await page.goto(harness.origin);
          const options=page.locator('[data-island-screen-options]');
          await options.waitFor();
          await page.evaluate(()=>document.fonts.ready);
          assert.equal(await page.locator('.island-display h2').textContent(),language==='en'?'Display':'显示');
          assert.equal(await page.locator('#island-screen-label').textContent(),language==='en'?'Display policy':'显示策略');
          const select=async screen=>{
            const button=options.locator(`[data-island-screen="${screen}"]`);
            if(await button.getAttribute('aria-pressed')==='true')return;
            await button.click();
            await page.evaluate(()=>window.islandScreensFixture.reply());
            assert.equal(await button.getAttribute('aria-pressed'),'true');
          };
          for(const count of [1,2,4]) {
            await page.evaluate(count=>window.islandScreensFixture.set(1,Array.from({length:count},(_,i)=>i+1)),count);
            assert.equal(await options.locator('button').count(),count+1);
            assert.deepEqual(await options.locator('button').allTextContents(),[
              ...Array.from({length:count},(_,i)=>`${language==='en'?'Screen':'屏幕'} ${i+1}`),
              language==='en'?'All screens':'全部屏幕',
            ]);
            await select(0);await select(count);
            const layout=await page.evaluate(()=>{
              const body=document.querySelector('.island-display .island-appearance__body').getBoundingClientRect();
              const buttons=[...document.querySelectorAll('[data-island-screen]')].map(button=>{
                const rect=button.getBoundingClientRect();
                return {left:rect.left,right:rect.right,top:rect.top,bottom:rect.bottom,
                  fits:button.scrollWidth<=button.clientWidth+1};
              });
              const appearance=document.querySelector('.island-appearance__body').getBoundingClientRect();
              const display=document.querySelector('.island-display').getBoundingClientRect();
              return {documentWidth:document.documentElement.scrollWidth,viewport:innerWidth,
                afterAppearance:display.top>=appearance.bottom,body:{left:body.left,right:body.right,top:body.top,bottom:body.bottom},buttons};
            });
            assert.ok(layout.documentWidth<=layout.viewport,'settings must not overflow the viewport');
            assert.ok(layout.afterAppearance,'Display must follow the existing Appearance section');
            for(const button of layout.buttons) {
              assert.ok(button.fits,'every screen label remains readable');
              assert.ok(button.left>=layout.body.left&&button.right<=layout.body.right&&
                button.top>=layout.body.top&&button.bottom<=layout.body.bottom,'screen options stay inside the settings card');
            }
            report.counts.push(count);report.layouts.push(layout);
            await page.locator('.island-appearance').screenshot({path:path.join(harness.output,`${width}-${language}-${count}-screens.png`)});
          }
          await page.evaluate(()=>window.islandScreensFixture.set(2,[1,2]));
          await options.locator('[data-island-screen="0"]').click();
          assert.equal(await options.locator('[aria-pressed="true"]').getAttribute('data-island-screen'),'2','selection waits for host acknowledgement');
          assert.ok(await options.locator('[data-island-screen="0"]').evaluate(button=>button.disabled||button.getAttribute('aria-disabled')==='true'));
          await page.evaluate(()=>window.islandScreensFixture.reply(true));
          assert.equal(await options.locator('[aria-pressed="true"]').getAttribute('data-island-screen'),'2','failed saves retain the confirmed selection');
          await select(0);await select(2);
          await page.evaluate(()=>window.islandScreensFixture.set(2,[1]));
          assert.equal(await options.locator('button').count(),2);
          assert.equal(await page.locator('[data-island-screen-unavailable]').isVisible(),true);
          await page.evaluate(()=>window.islandScreensFixture.set(2,[1,2,3,4]));
          assert.equal(await options.locator('button').count(),5);
          assert.equal(await page.locator('[data-island-screen-unavailable]').isVisible(),false);
          assert.equal(await options.locator('[data-island-screen="2"]').getAttribute('aria-pressed'),'true');
          await page.evaluate(()=>window.islandScreensFixture.navigate());
          assert.equal(await page.locator('[data-island-screen="2"]').getAttribute('aria-pressed'),'true','returning to the page preserves the host selection');
          const commands=await page.evaluate(()=>window.islandScreensFixture.commands.filter(command=>typeof command==='string'&&command.startsWith('v1|dynamicIsland|screen|')));
          assert.ok(commands.includes('v1|dynamicIsland|screen|0')&&commands.includes('v1|dynamicIsland|screen|4'));
          const modes=page.locator('.island-expansion');
          assert.equal(await modes.locator('h2').textContent(),language==='en'?'Expansion mode':'展开模式');
          assert.deepEqual(await modes.locator('button').allTextContents(),language==='en'?['Click to expand','Hover to expand']:['点击展开','悬停展开']);
          const click=modes.locator('[data-island-expansion="click"]'),hover=modes.locator('[data-island-expansion="hover"]');
          assert.equal(await click.getAttribute('aria-pressed'),'true');
          await hover.click();
          assert.equal(await click.getAttribute('aria-pressed'),'true','expansion mode waits for host confirmation');
          assert.ok(await hover.evaluate(button=>button.disabled||button.getAttribute('aria-disabled')==='true'));
          await page.evaluate(()=>window.islandScreensFixture.reply(true));
          assert.equal(await click.getAttribute('aria-pressed'),'true','failed expansion-mode saves retain click mode');
          await hover.click();await page.evaluate(()=>window.islandScreensFixture.reply());
          assert.equal(await hover.getAttribute('aria-pressed'),'true');
          await page.evaluate(()=>window.islandScreensFixture.navigate());
          assert.equal(await hover.getAttribute('aria-pressed'),'true','returning to the page preserves the expansion mode');
          const expansion=await page.evaluate(()=>{
            const card=document.querySelector('.island-expansion').getBoundingClientRect();
            const display=document.querySelector('.island-display').getBoundingClientRect();
            const appearance=document.querySelector('.island-appearance').getBoundingClientRect();
            return {afterDisplay:card.top>=display.bottom,overflow:document.documentElement.scrollWidth>innerWidth,
              labelsFit:[...document.querySelectorAll('[data-island-expansion]')].every(button=>button.scrollWidth<=button.clientWidth+1),
              clip:{x:appearance.left,y:appearance.top,width:appearance.width,height:card.bottom-appearance.top}};
          });
          assert.ok(expansion.afterDisplay&&expansion.labelsFit&&!expansion.overflow,'expansion mode follows Display and its labels fit at both widths');
          await page.screenshot({path:path.join(harness.output,`${width}-${language}-expansion-mode.png`),clip:expansion.clip});
          await click.click();await page.evaluate(()=>window.islandScreensFixture.reply());
          assert.equal(await click.getAttribute('aria-pressed'),'true');
          const modeCommands=await page.evaluate(()=>window.islandScreensFixture.commands.filter(command=>typeof command==='string'&&command.startsWith('v1|dynamicIsland|hover|')));
          assert.deepEqual(modeCommands,['v1|dynamicIsland|hover|true','v1|dynamicIsland|hover|true','v1|dynamicIsland|hover|false']);
          report.expansion={...expansion,commands:modeCommands,passed:true};
          assert.deepEqual(errors,[]);assert.deepEqual(remoteRequests,[]);
          report.commands=commands;report.passed=true;
        } finally {await page.close();}
      });
    }
  } finally {await harness.close();}
  t.diagnostic(`Screenshots and layout evidence: ${harness.output}`);
});
