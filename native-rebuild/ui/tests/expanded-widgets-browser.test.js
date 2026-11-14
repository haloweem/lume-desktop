import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';
import {encodeExpandedLayout} from '../expanded-widgets.js';

function fixture({language}){
  const listeners=new Set(),commands=[];let pending=null;
  let current={version:1,type:'dynamicIsland-state',enabled:true,running:true,locked:true,
    expanded:false,focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:false,
    hover:false,autoHide:true,size:'standard',material:'none',shape:'capsule',leftWidget:0,rightWidget:0,
    screen:1,screens:[1,2],hoverDelayMs:300,animationSpeed:'normal',bounce:'normal',expandedLayout:1,clockStyle:'simple',calendarStyle:'default',
    loadStatus:'loaded',error:'',result:'snapshot',saved:false};
  const emit=value=>listeners.forEach(listener=>listener({data:value}));
  window.expandedFixture={commands,
    reply(failed=false){if(pending===null)throw Error('no pending setting');
      if(!failed)current={...current,[pending.key]:pending.value};pending=null;
      emit({...current,result:'applied',saved:!failed,error:failed?'save':''});},
    current:()=>current.expandedLayout,style:()=>current.clockStyle,calendarStyle:()=>current.calendarStyle,
    snapshot(){emit({...current,result:'snapshot',saved:false});},
    navigate(){emit({version:1,type:'navigate',page:'dynamicIsland'});},
  };
  window.chrome={webview:{
    addEventListener(name,listener){if(name==='message')listeners.add(listener);},
    removeEventListener(name,listener){if(name==='message')listeners.delete(listener);},
    postMessage(command){commands.push(command);
      if(command==='v1|ready')queueMicrotask(()=>{
        emit({version:1,type:'app-settings-state',revision:1,language,balancedMode:true,autostart:false,
          busy:false,error:'',update:{status:'idle',version:'',progress:0}});
        emit(current);window.expandedFixture.navigate();
      });
      else if(typeof command==='string'&&/^v1\|dynamicIsland\|(expandedLayout|clockStyle|calendarStyle|material)\|[^|]+$/.test(command)){
        if(pending!==null)throw Error('duplicate setting save');
        const [,,key,value]=command.split('|');pending={key,value:key==='expandedLayout'?Number(value):value};
      }
    },
  }};
}

test('expanded widgets and static styles preserve ordering, acknowledgements and navigation',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:120000,
},async t=>{
  const output=process.env.LUME_ISLAND_BROWSER_OUTPUT||fileURLToPath(new URL('../../../build-artifacts/island-clock-styles-20261109/browser',import.meta.url));
  const harness=await createBrowserHarness(output);
  try{
    for(const width of [1536,680])for(const language of ['zh-CN','en'])await t.test(`${width}px ${language}`,async()=>{
      const page=await harness.browser.newPage({viewport:{width,height:864},reducedMotion:'no-preference'});
      page.setDefaultTimeout(8000);
      const errors=[],failedRequests=[];page.on('pageerror',error=>errors.push(error.message));
      page.on('response',response=>{if(response.status()>=400)failedRequests.push(response.url());});
      const report={width,language,passed:false};harness.report.viewports.push(report);
      try{
        await page.addInitScript(fixture,{language});await page.goto(harness.origin);
        const picker=page.locator('[data-island-expanded-picker]');await picker.waitFor({state:'visible'});
        await page.evaluate(()=>document.fonts.ready);
        await page.waitForFunction(()=>[...document.querySelectorAll('.island-expanded-icon')].every(image=>image.complete&&image.naturalWidth===256));
        assert.equal(await page.locator('[data-island-native-preview],[data-island-preview-mode],[data-island-compact-widget]').count(),0);
        assert.deepEqual(await page.locator('.settings-side-nav [data-tab]').evaluateAll(nodes=>nodes.map(node=>node.dataset.tab)),['home','todo','notifications','settings','details']);
        assert.deepEqual(await page.locator('[data-island-style-widget]').evaluateAll(nodes=>nodes.map(node=>node.dataset.islandStyleWidget)),['music','calendar-weather','todos','clock','system']);
        const categoriesBefore=await page.evaluate(()=>window.expandedFixture.commands.length);
        const waitImages=()=>page.waitForFunction(()=>[...document.querySelectorAll('[data-island-style-card] img')].every(image=>image.complete&&image.naturalWidth>0));
        for(const widget of ['music','calendar-weather','todos','system','clock']){
          await page.locator(`[data-island-style-widget="${widget}"]`).click();
          assert.equal(await page.locator(`[data-island-style-widget="${widget}"]`).getAttribute('aria-pressed'),'true');
          assert.equal(await page.locator('[data-island-style-card]').count(),widget==='clock'?6:widget==='calendar-weather'?2:1);await waitImages();
          assert.equal(await page.locator('[data-island-style-card] img').first().evaluate(image=>image.getAttribute('src').endsWith('-en.png')),language==='en','thumbnail language follows the page');
        }
        assert.equal(await page.evaluate(()=>window.expandedFixture.commands.length),categoriesBefore,'category selection sends no command');
        await page.locator('[data-island-style-widget="calendar-weather"]').click();
        const oldCalendar=page.locator('[data-island-calendar-style="default"]'),almanac=page.locator('[data-island-calendar-style="chinese-almanac"]');
        await almanac.click();assert.equal(await almanac.getAttribute('aria-pressed'),'false');
        assert.equal(await page.locator('[data-island-calendar-style][aria-disabled="true"]').count(),2);
        await page.evaluate(()=>window.expandedFixture.reply(true));assert.equal(await oldCalendar.getAttribute('aria-pressed'),'true');
        await almanac.focus();await almanac.press('Enter');await page.evaluate(()=>window.expandedFixture.reply());
        assert.equal(await almanac.getAttribute('aria-pressed'),'true');
        assert.equal(await page.evaluate(()=>window.expandedFixture.calendarStyle()),'chinese-almanac');
        assert.equal(await page.evaluate(()=>window.expandedFixture.current()),1,'calendar style preserves the enabled widget layout');
        await page.locator('[data-tab="settings"]').click();await page.locator('[data-tab="home"]').click();
        assert.equal(await almanac.getAttribute('aria-pressed'),'true','calendar style survives page reentry');
        await waitImages();await page.screenshot({path:path.join(output,`calendar-almanac-${width}-${language}.png`)});
        await page.locator('[data-island-style-widget="clock"]').click();
        const simple=page.locator('[data-island-clock-style="simple"]'),progress=page.locator('[data-island-clock-style="progress"]');
        await progress.click();assert.equal(await progress.getAttribute('aria-pressed'),'false','style waits for native confirmation');
        assert.equal(await page.locator('[data-island-clock-style][aria-disabled="true"]').count(),6);
        const commandsBefore=await page.evaluate(()=>window.expandedFixture.commands.length);
        await progress.click({force:true});
        assert.equal(await page.evaluate(()=>window.expandedFixture.commands.length),commandsBefore,'busy style cannot submit twice');
        await page.evaluate(()=>window.expandedFixture.snapshot());assert.equal(await progress.getAttribute('aria-disabled'),'true');
        await page.evaluate(()=>window.expandedFixture.reply(true));assert.equal(await simple.getAttribute('aria-pressed'),'true','save failure retains confirmed style');
        await progress.click();await page.evaluate(()=>window.expandedFixture.reply());assert.equal(await progress.getAttribute('aria-pressed'),'true');
        assert.equal(await page.evaluate(()=>window.expandedFixture.current()),1,'choosing a style does not enable or reorder widgets');
        for(const style of ['digital','date','flip','pixel','simple','progress']){
          const choice=page.locator(`[data-island-clock-style="${style}"]`),previous=await page.evaluate(()=>window.expandedFixture.style());
          await choice.click();assert.equal(await choice.getAttribute('aria-pressed'),'false','each new style waits for acknowledgement');
          await page.evaluate(()=>window.expandedFixture.reply(true));
          assert.equal(await page.locator(`[data-island-clock-style="${previous}"]`).getAttribute('aria-pressed'),'true','failed style changes retain the last confirmed style');
          await choice.click();await page.evaluate(()=>window.expandedFixture.reply());
          assert.equal(await choice.getAttribute('aria-pressed'),'true');
          await page.locator('[data-tab="settings"]').click();await page.locator('[data-tab="home"]').click();
          assert.equal(await choice.getAttribute('aria-pressed'),'true','each style survives page reentry');
        }
        const cardGeometry=await page.locator('[data-island-clock-style]').evaluateAll(cards=>cards.map(card=>{
          const {x,y,width,height}=card.getBoundingClientRect();return {x,y,width,height};
        }));
        assert.equal(cardGeometry.length,6);
        assert.ok(cardGeometry.every(card=>card.width<=240.5),'style cards retain compact dimensions');
        if(width>=1536)assert.ok(cardGeometry.slice(0,4).every(card=>Math.abs(card.y-cardGeometry[0].y)<1)&&cardGeometry[4].y>cardGeometry[0].y,'wide windows fill four compact columns before wrapping the new styles');
        else assert.ok(cardGeometry[2].y>cardGeometry[0].y,'narrow windows wrap style cards without enlarging them');
        const titleStyle=await page.locator('[data-island-style-title]').evaluate(node=>{const style=getComputedStyle(node);return {size:parseFloat(style.fontSize),weight:Number(style.fontWeight),color:style.color};});
        assert.deepEqual(titleStyle,{size:14,weight:700,color:'rgb(224, 231, 240)'});
        const sectionTitles=await page.locator('[data-island-component-title],.island-styles > h2').evaluateAll(nodes=>nodes.map(node=>{
          const style=getComputedStyle(node);return {size:parseFloat(style.fontSize),weight:Number(style.fontWeight),color:style.color};
        }));
        assert.deepEqual(sectionTitles,Array(2).fill({size:18,weight:700,color:'rgb(244, 246, 250)'}),
          'main section headings take the previous category emphasis in wide and narrow layouts');
        assert.deepEqual(await page.locator('.island-styles').evaluate(node=>{
          const style=getComputedStyle(node);return {margin:style.marginTop,padding:style.paddingTop,transform:style.transform};
        }),{margin:'24px',padding:'24px',transform:'none'},'cards move upward by reducing positive section spacing');
        const choose=id=>picker.locator(`[data-island-expanded-toggle="${id}"]`);
        const music=choose(0),calendar=choose(1),clock=choose(5),todos=choose(4),performance=choose(3);
        assert.deepEqual(await picker.locator('[data-island-expanded-chip]').evaluateAll(nodes=>nodes.map(node=>Number(node.dataset.islandExpandedChip))),[0,1,4,5,3]);
        assert.equal(await picker.locator('[role="switch"]').count(),5);
        const toggle=async(widget,failed=false)=>{await widget.locator('.island-widget-enable').click();await page.evaluate(failed=>window.expandedFixture.reply(failed),failed);};
        await toggle(calendar,true);assert.equal(await calendar.getAttribute('aria-checked'),'false');await toggle(calendar);
        const moveEarlier=picker.locator('[data-island-expanded-move="1"][data-direction="-1"]');
        await moveEarlier.focus();await moveEarlier.press('Enter');await page.evaluate(()=>window.expandedFixture.reply());
        assert.equal(await page.evaluate(()=>window.expandedFixture.current()),encodeExpandedLayout([1,0]));
        await clock.locator('.island-widget-enable').click();assert.equal(await picker.locator('[role="switch"][aria-disabled="true"]').count(),5);
        const pendingLayouts=await page.evaluate(()=>window.expandedFixture.commands.length);await clock.click({force:true});
        assert.equal(await page.evaluate(()=>window.expandedFixture.commands.length),pendingLayouts);
        await page.evaluate(()=>window.expandedFixture.reply());
        await clock.focus();await clock.press('Alt+ArrowLeft');await page.evaluate(()=>window.expandedFixture.reply(true));
        assert.equal(await page.evaluate(()=>window.expandedFixture.current()),encodeExpandedLayout([1,0,5]),'failed reorder rolls back');
        await page.waitForTimeout(220);
        const clockBox=await picker.locator('[data-island-expanded-chip="5"]').boundingBox(),musicBox=await picker.locator('[data-island-expanded-chip="0"]').boundingBox();
        await page.mouse.move(clockBox.x+clockBox.width/2,clockBox.y+clockBox.height/2);await page.mouse.down();
        await page.mouse.move(musicBox.x+musicBox.width/2,musicBox.y+musicBox.height/2,{steps:8});await page.mouse.up();
        await page.evaluate(()=>window.expandedFixture.reply());
        assert.equal(await page.evaluate(()=>window.expandedFixture.current()),encodeExpandedLayout([1,5,0]),'pointer reordering works');
        await toggle(todos);await toggle(performance,true);assert.equal(await performance.getAttribute('aria-checked'),'false');await toggle(performance);
        assert.equal(await picker.locator('[role="switch"][aria-checked="true"]').count(),5);
        assert.equal(await page.evaluate(()=>window.expandedFixture.current()),encodeExpandedLayout([1,5,4,0,3]));
        await performance.focus();await performance.press('Alt+ArrowLeft');await page.evaluate(()=>window.expandedFixture.reply(true));
        assert.equal(await page.evaluate(()=>window.expandedFixture.current()),encodeExpandedLayout([1,5,4,0,3]));
        await performance.press('Alt+ArrowLeft');await page.evaluate(()=>window.expandedFixture.reply());
        assert.equal(await page.evaluate(()=>window.expandedFixture.current()),encodeExpandedLayout([1,5,4,3,0]));
        await performance.press('Alt+ArrowRight');await page.evaluate(()=>window.expandedFixture.reply());
        assert.equal(await page.evaluate(()=>window.expandedFixture.style()),'progress');
        await page.locator('[data-island-preference="material"][data-value="frosted-glass"]').click();await page.evaluate(()=>window.expandedFixture.reply());
        await page.locator('[data-tab="settings"]').click();assert.equal(await page.locator('#dynamicIsland-layer').count(),1);
        await page.locator('[data-tab="details"]').click();assert.equal(await page.locator('.settings-details').count(),1);
        await page.locator('[data-tab="notifications"]').click();assert.equal(await page.locator('[data-island-notifications]').count(),1);
        await page.waitForFunction(()=>[...document.querySelectorAll('.in-icon img')].every(image=>image.complete&&image.naturalWidth===256));
        await page.screenshot({path:path.join(output,`${width}-${language}-reminder-icons.png`)});
        await page.locator('[data-tab="home"]').click();assert.equal(await progress.getAttribute('aria-pressed'),'true');
        assert.equal(await picker.locator('[role="switch"][aria-checked="true"]').count(),5);
        assert.deepEqual(await picker.locator('[data-island-expanded-chip]').evaluateAll(nodes=>nodes.map(node=>Number(node.dataset.islandExpandedChip))),[1,5,4,0,3]);
        await waitImages();await page.evaluate(()=>document.activeElement.blur());await page.waitForTimeout(230);
        await page.screenshot({path:path.join(output,`${width}-${language}-clock-styles.png`),animations:'disabled'});
        await page.locator('[data-island-styles]').screenshot({path:path.join(output,`${width}-${language}-clock-cards.png`),animations:'disabled'});
        await page.locator('[data-island-style-widget="system"]').click();await waitImages();
        await page.screenshot({path:path.join(output,`${width}-${language}-performance-style.png`),animations:'disabled'});
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
        await page.locator('[data-tab="details"]').click();await page.waitForTimeout(300);
        assert.deepEqual(await page.evaluate(()=>window.expandedFixture.commands.filter(command=>typeof command==='string'&&command.startsWith('v1|dynamicIsland|preview|'))),[]);
        assert.deepEqual(errors,[]);assert.deepEqual(failedRequests,[]);report.passed=true;
      }finally{if(!report.passed)await page.screenshot({path:path.join(output,`${width}-${language}-failed.png`)}).catch(()=>{});await page.close();}
    });
  }finally{await harness.close();}
});
