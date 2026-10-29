import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {createBrowserHarness,createFixturePage} from './component-settings-browser.mjs';

test('真实 Edge：收纳开关即时反馈、等待状态、失败回滚与关闭',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:60000,
},async()=>{
  const harness=await createBrowserHarness();
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
