import test from 'node:test';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness,createFixturePage,checkCardNotices,viewports} from './component-settings-browser.mjs';

test('real app routes registered component pages, native feedback and repeated DOM replacements',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000,
},async t=>{
  const output=fileURLToPath(new URL('../../../build-artifacts/decoupling-20261108-round2/ui/browser-controller/',import.meta.url));
  const harness=await createBrowserHarness(output);
  const routes=['clock','calendar','performance','spectrum','todayTodo','almanac','weather',
    'organizer','icons','taskbar','minimize','radial','dock','dynamicIsland'];
  let samples=0;
  try {
    for(const viewport of viewports) {
      const fixture=await createFixturePage(harness,viewport),{page}=fixture;
      try {
        for(const scope of routes) {
          await page.evaluate(scope=>componentFixture.navigate(scope),scope);
          const panel=page.locator(`[data-settings-scope="${scope}"]`);
          await panel.waitFor({state:'visible'});
          for(const tab of ['settings','details','settings']) {
            await panel.locator(`[data-tab="${tab}"]`).click();
            assert.equal(await panel.count(),1,`${scope}: only its registered page is mounted`);
            assert.equal(await panel.locator(`[data-tab="${tab}"]`).getAttribute('aria-selected'),'true');
            assert.ok((await panel.locator('.settings-body').textContent()).trim(),`${scope}: ${tab} content remains`);
            samples++;
          }
        }
        // The same controller sees both an inline status surface and a card
        // after navigation replaces the DOM; it must not cache the old surface.
        await page.evaluate(()=>componentFixture.navigate('taskbar'));
        await page.evaluate(()=>componentFixture.error('taskbar'));
        const inline=page.locator('[data-settings-scope="taskbar"] .status');
        assert.equal(await inline.evaluate(element=>element.hidden),false);
        assert.ok((await inline.textContent()).trim());
        assert.equal(await page.locator('#notice').evaluate(element=>element.hidden),true,
          'an inline taskbar error does not also show a global notice');
        await page.locator('[data-settings-scope="taskbar"] [data-action="back"]').click();
        await page.evaluate(()=>componentFixture.error('taskbar'));
        assert.equal(await page.locator('#notice').evaluate(element=>element.hidden),false,
          'the same host error reaches the global surface after leaving settings');
        await page.clock.fastForward(10000);
        assert.equal(await page.locator('#notice').evaluate(element=>element.hidden),false,
          'error feedback is not auto dismissed');
        await checkCardNotices(fixture,{scope:'weather'});
        assert.deepEqual(fixture.report.errors,[]);
        assert.deepEqual(fixture.report.consoleErrors,[]);
      } finally {await fixture.context.close();}
    }
  } finally {await harness.close();}
  assert.equal(samples,routes.length*viewports.length*3);
  t.diagnostic(`${samples} settings/details/back-to-settings samples across ${viewports.length} real viewports; ${output}`);
});
