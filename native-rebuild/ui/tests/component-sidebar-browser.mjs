// Real index/CSP/app.js navigation, including the music component's separate router.
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {mkdir,writeFile} from 'node:fs/promises';
import {createBrowserHarness,createFixturePage,checkSettingsPages,components,viewports} from './component-settings-browser.mjs';

const output=path.resolve(fileURLToPath(new URL('../../../build-artifacts/component-sidebar-browser',import.meta.url)));
await mkdir(output,{recursive:true});
const harness=await createBrowserHarness(output);
const summaries=[];
const routes=['clock','calendar','performance','spectrum','almanac','todayTodo','weather','organizer',
  'icons','taskbar','minimize','radial','dock','music-desktop'];
try {
  for(const viewport of viewports) {
    const fixture=await createFixturePage(harness,viewport);
    const {page}=fixture;
    try {
      if(viewport.width===1536){
        const quiet=await page.evaluate(async()=>{
          const {initialState}=await import('/state.js');
          const {setLanguage}=await import('/localization.js');
          const entries=[['clock','clockPage','syncClockControls','clock'],['calendar','calendarPage','syncCalendarControls','calendar'],
            ['performance','performancePage','syncPerformanceControls','performance'],['spectrum','spectrumPage','syncSpectrumControls','spectrum'],
            ['today-todo','todayTodoPage','syncTodayTodoControls','todayTodo'],['almanac','almanacPage','syncAlmanacControls','almanac'],
            ['weather','weatherPage','syncWeatherControls','weather'],['organizer','organizerPage','syncOrganizerControls','organizer'],
            ['desktop-icons','desktopIconsPage','syncDesktopIconsControls','icons'],['taskbar','taskbarPage','syncTaskbarControls','taskbar'],
            ['minimize','minimizePage','syncMinimizeControls','minimize'],['radial','radialPage','syncRadialControls','radial'],
            ['dock','dockPage','syncDockControls','dock'],['dynamic-island','islandPage','syncIslandControls','dynamicIsland']];
          const checked=[];
          for(const language of ['zh-CN','en']){
            setLanguage(language);
            for(const [file,render,sync,scope] of entries){
              const module=await import(`/${file}.js`),state=initialState(),root=document.createElement('main');
              const key=scope==='clock'?'status':`${scope}Status`,errorKey=scope==='clock'?'error':`${scope}Error`;
              state[key]='saving';state.organizerPending=null;
              root.innerHTML=module[render](state);
              const status=root.querySelector('.status');
              if(!status.hidden||status.textContent)throw Error(`${scope}: initial saving notice`);
              module[sync](root,state);
              if(!status.hidden||status.textContent)throw Error(`${scope}: synchronized saving notice`);
              state[key]='error';state[errorKey]='runtime';module[sync](root,state);
              if(status.hidden||!status.textContent)throw Error(`${scope}: missing actionable error`);
              state[key]='saving';state[errorKey]='';module[sync](root,state);
              if(!status.hidden||status.textContent)throw Error(`${scope}: retry notice`);
              checked.push(`${language}:${scope}`);
            }
          }
          setLanguage('zh-CN');
          const {musicDesktopPage}=await import('/music.js');const root=document.createElement('main');
          root.innerHTML=musicDesktopPage({pending:true});
          if(!root.querySelector('.status').hidden||root.querySelector('.status').textContent)throw Error('music saving notice');
          root.innerHTML=musicDesktopPage({pending:false,error:'save'});
          if(root.querySelector('.status').hidden)throw Error('music missing error');
          return checked.length+1;
        });
        assert.equal(quiet,29,'all component settings suppress save/apply progress and retain errors');
      }
      // Existing end-to-end parameter coverage for all visible catalog entries.
      // The minimize animation is deliberately hidden from the product catalog.
      for(const component of components.filter(value=>value.scope!=='minimize'))
        await checkSettingsPages(fixture,component);
      for(const scope of routes) {
        await page.evaluate(scope=>{
          if(scope==='music-desktop')componentFixture.navigate('music','desktop');
          else componentFixture.navigate(scope);
        },scope);
        const root=page.locator(`[data-settings-scope="${scope}"]`);
        await root.waitFor({state:'visible'});
        assert.equal(await root.count(),1,`${scope}: real route uses the shared frame`);
        let originalControls;
        for(const tab of (scope==='dock'?['dock','details','dock']:['settings','details','settings'])) {
          await root.locator(`[data-tab="${tab}"]`).click();
          const geometry=await root.evaluate((element,tab)=>{
            const body=element.querySelector('.settings-body'),nav=element.querySelector('.settings-side-nav');
            const b=body.getBoundingClientRect(),n=nav.getBoundingClientRect();
            const selected=[...element.querySelectorAll('[role="tab"][aria-selected="true"]')].map(node=>node.dataset.tab);
            const details=body.querySelector('.settings-details');
            const heading=body.querySelector('.settings-component-title');
            const controls=[...body.querySelectorAll('input,select,button[id],button[data-action]')].map(node=>
              `${node.id}|${node.dataset.action||node.dataset.musicAction||''}|${node.type}|${node.getAttribute('role')||''}`);
            return {scope:element.dataset.settingsScope,tab,selected,
              tabs:[...nav.querySelectorAll('[role="tab"]')].map(node=>node.dataset.tab),
              navWidth:n.width,navRight:n.right,bodyLeft:b.left,
              legacyTabs:element.querySelectorAll('.settings-tabs,.settings-head').length,
              title:heading?.textContent,expectedTitle:element.getAttribute('aria-label'),
              titleSize:heading?parseFloat(getComputedStyle(heading).fontSize):0,
              titleWeight:heading?Number(getComputedStyle(heading).fontWeight):0,
              titleOffset:heading?heading.getBoundingClientRect().left-b.left-parseFloat(getComputedStyle(body).paddingLeft):null,
              text:body.textContent.trim(),controls,background:getComputedStyle(element).backgroundColor,
              detailsOffset:details?details.getBoundingClientRect().left-b.left-parseFloat(getComputedStyle(body).paddingLeft):null,
              horizontalOverflow:body.scrollWidth>body.clientWidth+1||document.documentElement.scrollWidth>innerWidth+1};
          },tab);
          assert.deepEqual(geometry.tabs,scope==='dock'?['dock','launchpad','control-center','settings','details']:
            scope==='todayTodo'?['todo','settings','details']:['settings','details'],
            `${scope}: only the component's supported tabs are present`);
          assert.deepEqual(geometry.selected,[tab],`${scope}: actual app tab state follows sidebar click`);
          assert.equal(geometry.legacyTabs,0,`${scope}: old top tabs/header removed`);
          assert.equal(geometry.title,geometry.expectedTitle,`${scope}: settings and details visibly identify their component`);
          assert.equal(geometry.titleSize,26);assert.ok(geometry.titleWeight>=700);
          assert.ok(Math.abs(geometry.titleOffset)<1);
          if(scope==='taskbar')assert.equal(geometry.title,'任务栏美化');
          if(scope==='icons')assert.equal(geometry.title,'图标隐藏');
          if(scope==='weather'&&tab==='details')assert.doesNotMatch(geometry.text,/数据来源与许可|CC BY 4.0/);
          assert.equal(geometry.navWidth,viewport.width<=480?60:viewport.width<=760?64:72);
          assert.equal(geometry.navRight,geometry.bodyLeft);
          assert.equal(geometry.background,'rgb(18, 18, 18)');
          assert.equal(geometry.horizontalOverflow,false,`${scope}: narrow viewport has no horizontal overflow`);
          assert.ok(geometry.text,`${scope}: settings/details content remains`);
          if(tab==='details')assert.ok(Math.abs(geometry.detailsOffset)<1,`${scope}: details align at body inset`);
          else if(originalControls)assert.deepEqual(geometry.controls,originalControls,`${scope}: returning preserves parameter controls`);
          else originalControls=geometry.controls;
          summaries.push({viewport,...geometry});
          if(['taskbar','icons','weather','dock','music-desktop'].includes(scope)&&tab!=='settings')
            await page.screenshot({path:path.join(output,`${viewport.width}-${scope}-${tab}.png`)});
        }
        if(['taskbar','icons','weather'].includes(scope))await page.screenshot({path:path.join(output,`${viewport.width}-${scope}-settings.png`)});
        await root.locator('[data-action="back"]').click();
        assert.equal(await page.locator(`[data-settings-scope="${scope}"]`).count(),0,`${scope}: sidebar back exits component route`);
      }
      await page.evaluate(()=>componentFixture.navigate('dynamicIsland'));
      const island=page.locator('[data-settings-scope="dynamicIsland"]');
      await island.locator('[data-tab="home"]').click();
      assert.equal(await island.locator('[data-island-membership],[data-island-layout],.island-home__components-heading').count(),0,
        'empty membership area is removed from the real home page');
      assert.equal(await island.locator('[data-island-native-preview]').count(),0,'overall preview is removed');
      assert.equal(await island.locator('[data-island-styles]').count(),1,'component styles stay on the home page');
      assert.equal(await island.locator('[data-island-preference="material"]').count(),3,'appearance material controls stay');
      await page.screenshot({path:path.join(output,`${viewport.width}-island-home.png`)});
      for(const tab of ['settings','details']){
        await island.locator(`[data-tab="${tab}"]`).click();
        assert.equal(await island.locator('.settings-component-title').textContent(),'灵动岛');
      }
      for(const tab of ['home','todo','settings','details']) {
        await island.locator(`[data-tab="${tab}"]`).click();
        const selected=island.locator(`[data-tab="${tab}"][aria-selected="true"]`);
        await selected.locator('img').evaluate(image=>image.decode());
        assert.equal(await selected.locator('img').evaluate(image=>getComputedStyle(image).filter),'none',
          `${tab}: selected asset keeps its internal colors and symbols`);
        if(viewport.width===1536)await selected.screenshot({path:path.join(output,`selected-${tab}.png`)});
      }
      assert.deepEqual(fixture.report.errors,[]);
      assert.deepEqual(fixture.report.consoleErrors,[]);
      assert.deepEqual(fixture.report.remoteRequests,[]);
    } finally {await fixture.context.close();}
  }
  await writeFile(path.join(output,'sidebar-layout.json'),JSON.stringify(summaries,null,2));
  console.log(`PASS ${routes.length} component routes, ${viewports.length} viewports, ${summaries.length} real settings/details/back samples; island empty area removed.`);
} finally {await harness.close();}
