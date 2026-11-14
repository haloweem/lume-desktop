import test from 'node:test';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness,createFixturePage} from './component-settings-browser.mjs';

test('real component controls route selections and number commits through the command controller',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:30000,
},async()=>{
  const output=fileURLToPath(new URL('../../../build-artifacts/decoupling-20261108-final/ui/browser-commands/',import.meta.url));
  const harness=await createBrowserHarness(output);
  const fixture=await createFixturePage(harness,{width:1536,height:864}),{page}=fixture;
  const last=()=>page.evaluate(()=>componentFixture.commands.at(-1));
  try {
    await page.evaluate(()=>{componentFixture.prepare('weather');componentFixture.navigate('weather');});
    await page.locator('#weather-material').click();
    await page.locator('[data-select-owner="weather-material"][data-select-value="liquid-glass"]').click();
    assert.equal(await last(),'v1|weather|material|liquid-glass');
    assert.equal(await page.locator('#weather-material').isEnabled(),false,
      'a pending native request still prevents repeated component choices');

    await page.evaluate(()=>{componentFixture.prepare('minimize');componentFixture.navigate('minimize');});
    const duration=page.locator('#minimize-durationMs');
    const before=await page.evaluate(()=>componentFixture.commands.length);
    await duration.fill('49');await duration.press('Tab');
    assert.equal(await page.evaluate(()=>componentFixture.commands.length),before,'invalid duration emits no native command');
    assert.equal(await duration.inputValue(),'450','invalid duration restores the confirmed value');
    assert.match(await page.locator('#notice').textContent(),/50 到 2000/);
    await duration.fill('50');await duration.press('Tab');
    assert.equal(await last(),'v1|minimize|durationMs|50');

    await page.evaluate(()=>{componentFixture.prepare('icons');componentFixture.navigate('icons');});
    const delay=page.locator('#icons-delay'),previous=await page.evaluate(()=>componentFixture.commands.length);
    await delay.fill('2');await delay.press('Tab');
    assert.equal(await page.evaluate(()=>componentFixture.commands.length),previous,'invalid delay emits no native command');
    assert.equal(await delay.inputValue(),'5');
    await delay.fill('3');await delay.press('Tab');
    assert.equal(await last(),'v1|icons|delay|3');
    assert.deepEqual(fixture.report.errors,[]);
    assert.deepEqual(fixture.report.consoleErrors,[]);
  } finally {await fixture.context.close();await harness.close();}
});
