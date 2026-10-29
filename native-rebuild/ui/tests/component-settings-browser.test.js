import test from 'node:test';
import assert from 'node:assert/strict';
import {
  components,viewports,createBrowserHarness,createFixturePage,recordCase,
  checkSettingsPages,checkCardNotices,checkBrowserEnvironment,
} from './component-settings-browser.mjs';

test('真实 Edge：九组件五视口设置、卡片启停、提示时序与横向布局',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:240000,
},async t=>{
  const harness=await createBrowserHarness();
  try {
    for(const viewport of viewports) {
      await t.test(`${viewport.width}x${viewport.height}`,async suite=>{
        const fixture=await createFixturePage(harness,viewport);
        try {
          for(const component of components) {
            await suite.test(`${component.scope}：设置和详情无主开关、保留参数且无横向溢出`,()=>
              recordCase(fixture,`${component.scope}-settings`,()=>
                checkSettingsPages(fixture,component)));
            await suite.test(`${component.scope}：卡片启停、淡出、错误替换、失败重试和减少动态效果`,()=>
              recordCase(fixture,`${component.scope}-notice`,()=>
                checkCardNotices(fixture,component)));
          }
          await suite.test('真实入口及样式表完整、没有页面异常或外部请求',()=>
            checkBrowserEnvironment(fixture));
        } finally {
          await fixture.context.close();
        }
      });
    }
    assert.equal(harness.report.viewports.length,5,'五种视口都必须执行');
  } finally {
    await harness.close();
  }
  t.diagnostic(`截图和测量记录：${harness.output}`);
});
