import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import {createBrowserHarness} from './component-settings-browser.mjs';
import {setTimeout as delay} from 'node:timers/promises';

test('native island frames follow shape, material, size and expansion controls',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:60000,
},async()=>{
  const harness=await createBrowserHarness();
  try {
    for(const width of [1536,680]) {
      const page=await harness.browser.newPage({viewport:{width,height:864},reducedMotion:'reduce'});
      let delayNext=false;
      await page.route('**/__preview/native?*',async route=>{
        const query=new URL(route.request().url()).searchParams;
        const file=path.resolve('build-artifacts/native-preview',
          `island-native-${query.get('shape')}-${query.get('size')}-${query.get('material')}-${query.get('mode')}.png`);
        if(delayNext){delayNext=false;await delay(500);}
        await route.fulfill({contentType:'image/png',body:await readFile(file)});
      });
      await page.goto(harness.origin+'/dynamic-island-preview.html');
      await page.waitForFunction(()=>{const image=document.querySelector('[data-island-native-preview]');return image&&!image.hidden&&image.complete&&image.naturalWidth===960;});
      const previous=await page.locator('[data-island-native-preview]').getAttribute('src');
      delayNext=true;
      await page.locator('[data-island-preference="size"][data-value="small"]').click();
      assert.equal(await page.locator('[data-island-native-preview]').getAttribute('src'),previous);
      assert.equal(await page.locator('[data-island-preview-wait]').isVisible(),false,'retain the previous frame while settings render');
      await page.waitForFunction(previous=>document.querySelector('[data-island-native-preview]').src!==previous,previous);
      await page.locator('button[data-island-shape="standard"]').click();
      for(const mode of ['compact','expanded']) {
        await page.locator(`[data-island-preview-mode="${mode}"]`).click();
        for(const size of ['small','standard','large']) {
          await page.locator(`[data-island-preference="size"][data-value="${size}"]`).click();
          for(const material of ['none','liquid-glass','frosted-glass']) {
            await page.locator(`[data-island-preference="material"][data-value="${material}"]`).click();
            await page.waitForFunction(({mode,size,material})=>{
              const image=document.querySelector('[data-island-native-preview]');
              return !image.hidden&&image.complete&&image.naturalWidth===960&&image.naturalHeight===300&&
                document.querySelector(`[data-island-preview-mode="${mode}"]`).getAttribute('aria-pressed')==='true'&&
                document.querySelector(`[data-island-preference="size"][data-value="${size}"]`).getAttribute('aria-pressed')==='true'&&
                document.querySelector(`[data-island-preference="material"][data-value="${material}"]`).getAttribute('aria-pressed')==='true';
            },{mode,size,material});
            assert.equal(await page.locator('[data-island-preview-wait]').isVisible(),false);
          }
        }
      }
      const computed=await page.evaluate(()=>({
        title:getComputedStyle(document.querySelector('.island-appearance__head h2')).color,
        option:getComputedStyle(document.querySelector('.island-appearance__segments button:not([aria-pressed="true"])')).color,
        hint:getComputedStyle(document.querySelector('.island-home__components-heading p')).color,
        labels:[...document.querySelectorAll('[data-island-preview-mode]')].map(e=>e.textContent.trim()),
        overflow:document.documentElement.scrollWidth>innerWidth,
      }));
      assert.deepEqual(computed.labels,['收缩','展开']);assert.equal(computed.overflow,false);
      for(const value of [computed.title,computed.option,computed.hint]) {
        const components=value.match(/\d+/g).map(Number);
        assert.ok(Math.min(...components)>=170,`${value} should remain legible on dark backgrounds`);
      }
      await page.screenshot({path:path.resolve(`build-artifacts/island-native-web-${width}.png`)});
      await page.close();
    }
  } finally {await harness.close();}
});
