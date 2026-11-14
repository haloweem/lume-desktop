import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';

test('Dock layout controls work through the real page and native protocol',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000,
},async()=>{
  const output=fileURLToPath(new URL('../../../build-artifacts/dock-settings-icons-20261108/browser/',import.meta.url));
  const harness=await createBrowserHarness(output);
  try {
    for(const language of ['zh-CN','en'])for(const width of [1536,680]) {
      const page=await harness.browser.newPage({viewport:{width,height:1000}});
      try {
        await page.addInitScript(({language})=>{
          const listeners=new Set();
          const state={version:1,type:'dock-state',enabled:true,running:true,material:'liquid-glass',
            reflection:false,systemTray:false,iconScale:100,position:50,bottomOffset:0,iconSpacing:10,pinned:11,added:0,
            launchpadScale:100,launchpadIconScale:100,
            materialStatus:'native-ready',loadStatus:'loaded',error:'',result:'snapshot',saved:false};
          const emit=data=>listeners.forEach(listener=>listener({data}));
          window.dockFixture={commands:[],state,delay:0,fail:false};
          window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
            removeEventListener:(_,listener)=>listeners.delete(listener),postMessage(command){
              window.dockFixture.commands.push(command);
              if(command==='v1|ready')queueMicrotask(()=>{
                emit({version:1,type:'app-settings-state',revision:1,language,balancedMode:true,autostart:false,
                  busy:false,error:'',update:{status:'idle',version:'',progress:0}});
                emit({...state});emit({version:1,type:'navigate',page:'dock'});
              });
              if(typeof command==='string'&&command.startsWith('v1|dock|'))setTimeout(()=>{
                const [, ,key,value]=command.split('|');
                if(window.dockFixture.fail){emit({...state,error:'save',result:'applied',saved:false});return;}
                if(key==='reset')Object.assign(state,{iconScale:100,position:50,bottomOffset:0,iconSpacing:10,launchpadScale:100,launchpadIconScale:100});
                else if(['iconScale','position','bottomOffset','iconSpacing','launchpadScale','launchpadIconScale'].includes(key))state[key]=Number(value);
                else if(['reflection','systemTray'].includes(key))state[key]=value==='true';
                emit({...state,result:'applied',saved:true});
              },window.dockFixture.delay);
            }}};
        },{language});
        await page.goto(harness.origin);
        await page.locator('#dock-bottom-offset').waitFor();
        const panel=page.locator('[data-settings-scope="dock"]');
        const selectTab=async tab=>{
          if(await panel.locator(`[data-tab="${tab}"]`).getAttribute('aria-selected')!=='true')
            await panel.locator(`[data-tab="${tab}"]`).click();
          assert.equal(await panel.locator(`[data-tab="${tab}"]`).getAttribute('aria-selected'),'true');
        };
        const tabFor=id=>id.startsWith('dock-launchpad-')?'launchpad':'dock';
        assert.deepEqual(await panel.locator('[role="tab"]').evaluateAll(buttons=>buttons.map(button=>button.dataset.tab)),
          ['dock','launchpad','control-center','settings','details']);
        // Visiting categories must only change the displayed controls; opening
        // the generated icons must not change preferences or write native state.
        const beforeNavigation=await page.evaluate(()=>({state:structuredClone(dockFixture.state),
          writes:dockFixture.commands.filter(command=>typeof command==='string'&&command.startsWith('v1|dock|'))}));
        for(const [tab,icon,label] of [['dock','dock','Dock'],['launchpad','launchpad',language==='en'?'Launchpad':'启动台'],
          ['control-center','control-center',language==='en'?'Control center':'控制中心']]){
          await selectTab(tab);
          assert.equal(await panel.locator('[role="tab"][aria-selected="true"]').count(),1);
          if(tab!=='dock')assert.equal(await panel.locator(`[data-tab="${tab}"]`).getAttribute('aria-label'),label);
          const image=panel.locator(`[data-tab="${tab}"] img`);
          await image.evaluate(image=>image.decode());
          const loaded=await image.evaluate(image=>({width:image.naturalWidth,src:image.getAttribute('src'),srcset:image.getAttribute('srcset')}));
          assert.ok(loaded.width>0,'the generated selected icon loads in the actual page');
          assert.match(loaded.src,new RegExp(`${icon}-selected-24\\.png$`));
          assert.match(loaded.srcset,new RegExp(`${icon}-selected-48\\.png 2x, .*${icon}-selected-72\\.png 3x`));
          assert.deepEqual(await page.evaluate(async icon=>{
            const loaded=[];
            for(const state of ['default','selected'])for(const size of [24,48,72]){
              const image=new Image();image.src=`/assets/icons/lume-rounded-v1/${icon}-${state}-${size}.png`;
              await image.decode();loaded.push(image.naturalWidth===size&&image.naturalHeight===size);
            }
            return loaded;
          },icon),Array(6).fill(true),'default and selected 1x/2x/3x resources decode');
          assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'category fits the viewport');
          await panel.locator('.settings-side-nav__links').evaluate(async nav=>{
            await new Promise(resolve=>requestAnimationFrame(resolve));
            await Promise.all(nav.getAnimations({subtree:true}).map(animation=>animation.finished.catch(()=>{})));
          });
          const highlight=await panel.locator('.settings-side-nav__links').evaluate(nav=>{
            const selected=nav.querySelector('[aria-selected="true"]').getBoundingClientRect();
            const indicator=nav.querySelector('[data-lume-motion-indicator]')?.getBoundingClientRect();
            return !indicator||Math.abs(selected.top-indicator.top)<1&&Math.abs(selected.height-indicator.height)<1;
          });
          assert.equal(highlight,true,'the existing selection animation settles onto the selected category');
          await page.screenshot({path:path.join(harness.output,`dock-${language}-${width}-${tab}.png`),fullPage:true});
        }
        await selectTab('settings');
        await panel.locator('[data-tab="settings"] img').evaluate(image=>image.decode());
        assert.match(await panel.locator('[data-tab="settings"] img').getAttribute('src'),/settings-selected-24\.png$/,
          'the existing settings entry keeps its gear icon');
        assert.equal(await page.locator('#dock-material').count(),1);
        assert.equal(await page.locator('[data-action="dock-reflection"]').count(),1);
        assert.equal(await page.locator('[data-action="dock-reset"]').count(),1);
        assert.equal(await page.locator('#dock-icon-scale').count(),0,'layout controls have their own Dock category');
        await panel.locator('.settings-side-nav__links').evaluate(async nav=>{
          await new Promise(resolve=>requestAnimationFrame(resolve));
          await Promise.all(nav.getAnimations({subtree:true}).map(animation=>animation.finished.catch(()=>{})));
        });
        await page.screenshot({path:path.join(harness.output,`dock-${language}-${width}-settings.png`),fullPage:true});
        await selectTab('details');
        await selectTab('dock');
        assert.deepEqual(await page.evaluate(()=>({state:structuredClone(dockFixture.state),
          writes:dockFixture.commands.filter(command=>typeof command==='string'&&command.startsWith('v1|dock|'))})),beforeNavigation,
          'sidebar navigation does not save or alter Dock settings');
        for(const [id,value,field] of [['dock-icon-scale','105','iconScale'],['dock-bottom-offset','80','bottomOffset'],['dock-icon-spacing','24','iconSpacing'],['dock-launchpad-scale','83','launchpadScale'],['dock-launchpad-icon-scale','50','launchpadIconScale']]) {
          await selectTab(tabFor(id));
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
          await selectTab(tabFor(id));
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
        await selectTab('control-center');
        const centerToggle=panel.locator('[data-action="dock-system-tray"]');
        assert.equal(await centerToggle.getAttribute('aria-checked'),'false');
        await centerToggle.click();
        await page.waitForFunction(()=>dockFixture.state.systemTray===true);
        assert.equal(await centerToggle.getAttribute('aria-checked'),'true');
        assert.ok(await page.evaluate(()=>dockFixture.commands.includes('v1|dock|systemTray|true')),
          'the relocated control center toggle keeps its existing native command');
        await selectTab('launchpad');
        assert.equal(await page.locator('#dock-launchpad-icon-scale-number').inputValue(),'50',
          'the native confirmed launchpad setting survives category navigation');
        await selectTab('settings');
        const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth);
        assert.equal(overflow,false,'layout fits the viewport');
        await page.screenshot({path:path.join(harness.output,`dock-layout-${language}-${width}.png`),fullPage:true});
        await page.locator('[data-action="dock-reset"]').click();
        await page.waitForFunction(()=>window.dockFixture.state.position===50&&window.dockFixture.state.iconSpacing===10);
        assert.equal(await page.locator('#dock-position').count(),0);
        await selectTab('launchpad');
        assert.equal(await page.locator('#dock-launchpad-scale-number').inputValue(),'100');
        assert.equal(await page.locator('#dock-launchpad-icon-scale-number').inputValue(),'100',
          'Dock reset retains its existing cross-category defaults');
        await panel.locator('[data-action="back"]').click();
        await page.locator('[data-view="desktopBeauty"]').click();
        await page.locator('[data-open="dock"]').click();
        await page.locator('#dock-bottom-offset').waitFor();
        assert.equal(await panel.locator('[data-tab="dock"]').getAttribute('aria-selected'),'true',
          'reopening Dock from the real catalog returns to its Dock layout category');
        assert.equal(await page.locator('#dock-material').count(),0,'reopening Dock does not select the separate settings page');
        assert.equal(await page.locator('#dock-launchpad-scale').count(),0,
          'the previous launchpad category does not leak into a new catalog visit');
      } finally {await page.close();}
    }
  } finally {await harness.close();}
});
