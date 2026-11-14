import test from 'node:test';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';

// The previous live-preview acceptance now guards its removal from the real
// product entry. Static style thumbnails must stay independent of appearance.
test('island appearance changes never recreate the removed live preview',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:30000,
},async()=>{
  const harness=await createBrowserHarness(fileURLToPath(new URL('../../../build-artifacts/island-styles-20261109/no-preview-browser',import.meta.url)));
  try {
    const page=await harness.browser.newPage({viewport:{width:1536,height:864}});
    await page.addInitScript(()=>{
      const listeners=new Set();
      let island={version:1,type:'dynamicIsland-state',enabled:true,running:true,locked:true,expanded:false,
        focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:false,hover:false,autoHide:true,
        size:'standard',material:'none',shape:'capsule',loadStatus:'loaded',error:'',result:'snapshot',saved:false};
      const emit=message=>listeners.forEach(listener=>listener({data:message}));
      window.previewRemoval={commands:[],emit};
      window.chrome={webview:{addEventListener(type,listener){listeners.add(listener);},removeEventListener(type,listener){listeners.delete(listener);},
        postMessage(command){window.previewRemoval.commands.push(command);
          if(command==='v1|ready')queueMicrotask(()=>{emit(island);emit({version:1,type:'navigate',page:'dynamicIsland'});});
          else if(typeof command==='string'&&/^v1\|dynamicIsland\|(shape|size|material)\|/.test(command)){
            const [,,key,value]=command.split('|');island={...island,[key]:value,result:'applied',saved:true};queueMicrotask(()=>emit(island));
          }
        }}};
    });
    await page.goto(harness.origin);await page.locator('[data-island-styles]').waitFor();
    const before=await page.locator('[data-island-style-card] img').evaluateAll(images=>images.map(image=>image.getAttribute('src')));
    for(const shape of ['standard','capsule']){
      await page.locator(`button[data-island-shape="${shape}"]`).click();
      for(const size of ['small','standard','large']){
        await page.locator(`[data-island-preference="size"][data-value="${size}"]`).click();
        for(const material of ['liquid-glass','frosted-glass','none']){
          await page.locator(`[data-island-preference="material"][data-value="${material}"]`).click();
          assert.equal(await page.locator(`[data-island-preference="material"][data-value="${material}"]`).getAttribute('aria-pressed'),'true');
          assert.equal(await page.locator('[data-island-native-preview],[data-island-preview-mode],[data-island-compact-widget]').count(),0);
        }
      }
    }
    assert.deepEqual(await page.locator('[data-island-style-card] img').evaluateAll(images=>images.map(image=>image.getAttribute('src'))),before);
    await page.evaluate(()=>window.previewRemoval.emit({version:1,type:'dynamicIsland-preview',mode:'compact',shape:'capsule',material:'none',size:'standard',width:0,height:0,image:'',error:'render'}));
    assert.equal(await page.locator('[data-island-native-preview]').count(),0,'legacy unsolicited frames do not restore the preview');
    await page.locator('[data-tab="settings"]').click();await page.locator('[data-tab="home"]').click();
    assert.deepEqual(await page.evaluate(()=>window.previewRemoval.commands.filter(command=>typeof command==='string'&&command.startsWith('v1|dynamicIsland|preview|'))),[]);
    await page.close();
  } finally {await harness.close();}
});
