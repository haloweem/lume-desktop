import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';

test('Dock running-area switch uses the real page, acknowledgement and saved state in both languages',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000,
},async t=>{
  const output=fileURLToPath(new URL('../../../build-artifacts/native-rebuild/browser/dock-running-area/',import.meta.url));
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
            material:'liquid-glass',screenCount:2,screen:1,reflection:false,showRunning:true,systemTray:true,iconScale:100,
            position:50,bottomOffset:0,iconSpacing:10,pinned:11,added:0,launchpadScale:100,launchpadIconScale:100,
            materialStatus:'native-ready',loadStatus:'loaded',error:'',result:'snapshot',saved:false};
          const emit=data=>listeners.forEach(listener=>listener({data}));
          window.runningAreaFixture={commands:[],state,pending:null,reply(fail=false){
            if(!this.pending)throw new Error('No Dock request is pending');
            const [,,key,value]=this.pending.split('|');this.pending=null;
            if(!fail){
              if(['showRunning','runningPreview','animation','animateRestore','animateLaunch','animateClose','showStartMenu','launchpadEnabled','summonEnabled','reflection'].includes(key))state[key]=value==='true';
              else if(['screen','animationDuration','reflectionRatio','reflectionTransparency'].includes(key))state[key]=Number(value);
              else if(key==='material'||key==='edge')state[key]=value;
              else if(key==='summonShortcut')Object.assign(state,{summonShortcutCode:Number(value),summonHotkeyRegistered:!!Number(value)});
              else if(key==='reset')Object.assign(state,{screen:1,showRunning:true,runningPreview:false,showStartMenu:true,launchpadEnabled:true,edge:'bottom',material:'liquid-glass',reflection:false,reflectionRatio:50,reflectionTransparency:50,summonEnabled:false,summonShortcutCode:0,summonHotkeyRegistered:false,
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
                if(command.startsWith('v1|dock|summonCapture|'))return;
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
        assert.equal(await toggle.locator('..').locator('.setting-help').count(),0,
          'the compact option row has no explanatory subtitle');
        const optionBox=await toggle.evaluate(button=>{const frame=getComputedStyle(button.closest('.settings-group__body'));
          return {radius:frame.borderRadius,background:frame.backgroundColor,checked:getComputedStyle(button).backgroundColor};});
        assert.equal(optionBox.radius,'14px');
        assert.equal(optionBox.background,'rgb(25, 27, 32)');
        assert.equal(optionBox.checked,'rgb(91, 123, 234)');
        const groups=await page.locator('[data-settings-scope="dock"] .settings-group__body').evaluateAll(boxes=>boxes.map(box=>
          [...box.querySelectorAll('[data-action],[data-dock-material],[data-dock-summon-shortcut]')].map(control=>
            control.dataset.action||control.dataset.dockMaterial||'shortcut')));
        assert.deepEqual(groups,[['liquid-glass','frosted-glass','none'],['dock-animation','dock-animateRestore','dock-animateLaunch','dock-animateClose'],['dock-launchpad-enabled'],['dock-show-start-menu','dock-show-running','dock-system-tray'],
          ['dock-summon-enabled','shortcut','dock-summon-clear']]);
        const motion=page.locator('[data-action="dock-animation"]');
        const options=page.locator('[data-dock-animation-options]');
        assert.equal(await options.isVisible(),false);
        await motion.click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|animation|true');
        await page.evaluate(()=>runningAreaFixture.reply());
        assert.ok(await options.isVisible());
        const duration=options.locator('[data-setting-number]');
        assert.equal(await duration.inputValue(),'400');
        await duration.fill('680');await duration.press('Enter');
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|animationDuration|680');
        await page.evaluate(()=>runningAreaFixture.reply());
        for(const key of ['animateRestore','animateLaunch','animateClose']){
          await options.locator('[data-action="dock-'+key+'"]').click();
          assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|'+key+'|false');
          await page.evaluate(()=>runningAreaFixture.reply());
        }
        await motion.click();await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await options.isVisible(),false);
        const style=page.locator('[data-dock-running-style]');
        const icons=style.getByRole('button',{name:language==='en'?'Icon':'图标',exact:true});
        const preview=style.getByRole('button',{name:language==='en'?'Preview':'预览',exact:true});
        assert.equal(await icons.getAttribute('aria-pressed'),'true');
        assert.ok(await style.evaluate(row=>row.previousElementSibling.querySelector('[data-action="dock-show-running"]')!==null));
        await preview.click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|runningPreview|true');
        await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await preview.getAttribute('aria-pressed'),'true');
        await icons.click();await page.evaluate(()=>runningAreaFixture.reply(true));
        assert.equal(await preview.getAttribute('aria-pressed'),'true');
        const screens=page.locator('[data-dock-screens]');
        assert.equal(await screens.locator('button').count(),3);
        assert.equal(await screens.locator('..').locator('xpath=../..').locator('h2').textContent(),language==='en'?'Display':'显示');
        await screens.locator('[data-dock-screen="2"]').click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|screen|2');
        assert.equal(await screens.locator('[aria-pressed="true"]').getAttribute('data-dock-screen'),'1');
        await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await screens.locator('[aria-pressed="true"]').getAttribute('data-dock-screen'),'2');
        await screens.locator('[data-dock-screen="0"]').click();
        await page.evaluate(()=>runningAreaFixture.reply(true));
        assert.equal(await screens.locator('[aria-pressed="true"]').getAttribute('data-dock-screen'),'2');
        await page.evaluate(()=>{runningAreaFixture.state.screenCount=1;runningAreaFixture.snapshot();});
        assert.ok(await page.locator('[data-dock-screen-unavailable]').isVisible());
        await page.evaluate(()=>{runningAreaFixture.state.screenCount=2;runningAreaFixture.snapshot();});
        await screens.locator('[data-dock-screen="0"]').click();await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await screens.locator('[aria-pressed="true"]').getAttribute('data-dock-screen'),'0');
        const frosted=page.locator('[data-dock-material="frosted-glass"]');
        assert.ok(await page.locator('.settings-group__body > .setting-row:visible:not(:has([data-setting-range]))').evaluateAll(rows=>rows.every(row=>{
          const [label,control]=row.children;
          const left=label.getBoundingClientRect(),right=control.getBoundingClientRect();
          return left.right<right.left&&Math.abs(left.top+left.height/2-right.top-right.height/2)<2;
        })),'labels and controls fit on opposite sides of every row');
        assert.ok(await page.locator('[data-dock-material]').evaluateAll(buttons=>buttons.every(button=>button.scrollWidth<=button.clientWidth)));
        await frosted.click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|material|frosted-glass');
        await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await frosted.getAttribute('aria-pressed'),'true');
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
        const geometry=await toggle.evaluate(button=>{
          const row=button.closest('.setting-row'),next=row.nextElementSibling;
          const current=row.getBoundingClientRect(),after=next.getBoundingClientRect(),control=button.getBoundingClientRect();
          return {next:next.matches('[data-dock-running-style]')&&next.nextElementSibling.querySelector('[data-action]')?.dataset.action==='dock-system-tray',
            below:after.top>=current.bottom-1,fits:control.left>=0&&control.right<=innerWidth};
        });
        assert.deepEqual(geometry,{next:true,below:true,fits:true});
        report.cases.push('localized order, layout and legacy default');await capture('default');
        const launchpad=page.locator('[data-action="dock-launchpad-enabled"]');
        assert.equal(await page.locator('[data-tab="launchpad"]').count(),0);
        assert.equal(await launchpad.getAttribute('aria-checked'),'true');
        await launchpad.click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|launchpadEnabled|false');
        await page.evaluate(()=>runningAreaFixture.reply());
        assert.ok(await page.locator('#dock-launchpad-scale').isDisabled());
        assert.ok(await page.locator('#dock-launchpad-icon-scale-number').isDisabled());
        await launchpad.click();await page.evaluate(()=>runningAreaFixture.reply());
        assert.ok(await page.locator('#dock-launchpad-icon-scale-number').isEnabled());
        const startMenu=page.locator('[data-action="dock-show-start-menu"]');
        assert.equal(await startMenu.getAttribute('aria-checked'),'true');
        await startMenu.click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|showStartMenu|false');
        await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await startMenu.getAttribute('aria-checked'),'false');

        const dockTab=page.locator('[data-settings-scope="dock"] [data-tab="dock"]');
        await dockTab.click();
        const edge=page.locator('#dock-edge'),edgeDropdown=edge.locator('..').getByRole('combobox');
        assert.equal(await edge.inputValue(),'bottom');
        const edgeNames=language==='en'?['Top','Bottom','Left','Right']:['顶部','底部','左侧','右侧'];
        await edgeDropdown.click();
        assert.deepEqual(await page.getByRole('option').allTextContents(),edgeNames.map(label=>label+'✓'));
        await page.getByRole('option',{name:edgeNames[2]}).click();
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|edge|left');
        await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await edge.inputValue(),'left');
        assert.equal(await page.locator('#dock-position-label').textContent(),language==='en'?'Vertical position':'垂直位置');
        await capture('left-edge');
        const reflection=page.locator('[data-action="dock-reflection"]');
        assert.ok(await page.locator('#dock-reflection-ratio').isDisabled());
        assert.equal(await page.locator('#dock-reflection-transparency').inputValue(),'50');
        await reflection.click();await page.evaluate(()=>runningAreaFixture.reply());
        for(const [id,key,value] of [['ratio','reflectionRatio',80],['transparency','reflectionTransparency',60]]){
          const number=page.locator(`#dock-reflection-${id}-number`);
          await number.fill(String(value));await number.press('Enter');
          assert.equal(await page.evaluate(()=>runningAreaFixture.pending),`v1|dock|${key}|${value}`);
          await page.evaluate(()=>runningAreaFixture.reply());
          assert.equal(await page.locator(`#dock-reflection-${id}`).inputValue(),String(value));
        }
        await capture('reflection');await openSettings();

        const recorder=page.locator('[data-dock-summon-shortcut]'),clear=page.locator('[data-action="dock-summon-clear"]');
        assert.ok(await recorder.isHidden());
        assert.equal(await page.locator('details.settings-group > summary').count(),0);
        await page.locator('[data-action="dock-summon-enabled"]').click();await page.evaluate(()=>runningAreaFixture.reply());
        assert.ok(await recorder.isVisible());assert.match(await recorder.innerText(),/Insert/i);
        await recorder.click();await recorder.press('Control+Alt+K');
        assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|summonShortcut|843');
        await page.evaluate(()=>runningAreaFixture.reply());assert.match(await recorder.innerText(),/K/);
        await clear.click();assert.equal(await page.evaluate(()=>runningAreaFixture.pending),'v1|dock|summonShortcut|0');
        await page.evaluate(()=>runningAreaFixture.reply());assert.ok(await clear.isHidden());
        report.cases.push('summon off by default, shortcut recorded and cleared');

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
        await page.reload();
        assert.equal(await page.locator('#dock-reflection-ratio').inputValue(),'80');
        assert.equal(await page.locator('#dock-reflection-transparency').inputValue(),'60');
        await openSettings();
        assert.equal(await toggle.getAttribute('aria-checked'),'false','new manager page restores persisted host value');
        assert.ok(await style.isHidden());
        await toggle.click();await page.evaluate(()=>runningAreaFixture.reply());
        assert.equal(await preview.getAttribute('aria-pressed'),'true','restores style when re-enabled');
        await toggle.click();await page.evaluate(()=>runningAreaFixture.reply());
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
        assert.equal(await icons.getAttribute('aria-pressed'),'true');
        report.cases.push('reset default survives new page');await capture('reset-on');
        await dockTab.click();assert.ok(await page.locator('#dock-reflection-ratio').isDisabled());
        assert.equal(await page.locator('#dock-reflection-ratio').inputValue(),'50');
        assert.equal(await page.locator('#dock-reflection-transparency').inputValue(),'50');
        assert.deepEqual(errors,[]);report.passed=true;
      } catch(error){report.error=error.message;throw error;}
      finally {await page.close();}
    });
  } finally {await harness.close();}
});
