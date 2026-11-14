import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';
import {validHostMessage} from '../bridge.js';

const island={version:1,type:'dynamicIsland-state',enabled:true,running:true,locked:true,
  expanded:false,focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:false,
  hover:false,autoHide:true,size:'standard',material:'none',shape:'capsule',leftWidget:0,rightWidget:0,
  screen:1,screens:[1],hoverDelayMs:300,animationSpeed:'normal',bounce:'normal',loadStatus:'loaded',error:'',result:'snapshot',saved:false};
const settings={version:1,type:'app-settings-state',revision:1,language:'zh-CN',balancedMode:true,
  autostart:false,busy:false,error:'',update:{status:'idle',version:'',progress:0}};

function installFixture({island,settings,language}) {
  const listeners=new Set(),commands=[];
  let current=structuredClone(island),pending=null,revision=1;
  const receive=message=>listeners.forEach(listener=>listener({data:message}));
  const snapshot=()=>receive({...current,result:'snapshot',saved:false,error:''});
  window.islandScreensFixture={
    commands,
    set(screen,screens) {current={...current,screen,screens};snapshot();},
    reply(failed=false) {
      if(pending===null)throw new Error('No pending display policy command');
      if(!failed)current[pending.key]=pending.value;
      pending=null;
      receive({...current,result:'applied',saved:!failed,error:failed?'save':''});
    },
    navigate() {receive({version:1,type:'navigate',page:'dynamicIsland'});},

  };
  window.chrome={webview:{
    addEventListener(type,listener) {if(type==='message')listeners.add(listener);},
    removeEventListener(type,listener) {if(type==='message')listeners.delete(listener);},
    postMessage(command) {
      commands.push(command);
      if(command==='v1|ready') {
        queueMicrotask(()=>{
          receive({...settings,language,revision:++revision});snapshot();
          window.islandScreensFixture.navigate();
        });
      } else if(typeof command==='string'&&/^v1\|dynamicIsland\|screen\|\d+$/.test(command)) {
        if(pending!==null)throw new Error('Duplicate display policy request while saving');
        pending={key:'screen',value:Number(command.split('|')[3])};
      } else if(typeof command==='string'&&/^v1\|dynamicIsland\|hover\|(true|false)$/.test(command)) {
        if(pending!==null)throw new Error('Duplicate expansion mode request while saving');
        pending={key:'hover',value:command.split('|')[3]==='true'};
      } else if(typeof command==='string'&&/^v1\|dynamicIsland\|(hoverDelayMs|animationSpeed|bounce)\|[^|]+$/.test(command)) {
        if(pending!==null)throw new Error('Duplicate animation request while saving');
        const [, , key, value]=command.split('|');
        pending={key,value:key==='hoverDelayMs'?Number(value):value};
      } else if(command?.scope==='app-settings'&&command.action==='snapshot') {
        queueMicrotask(()=>receive({...settings,language,revision:++revision}));
      }
    },
  }};
}

test('real settings entry: display, expansion and animation settings follow native acknowledgements and layout',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000,
},async t=>{
  assert.ok(validHostMessage(island));assert.ok(validHostMessage(settings));
  const harness=await createBrowserHarness(process.env.LUME_ISLAND_BROWSER_OUTPUT||fileURLToPath(new URL('../../../build-artifacts/island-styles-20261109/screens-browser',import.meta.url)));
  try {
    for(const width of [1536])for(const language of ['zh-CN','en']) {
      await t.test(`${width}px ${language}`,async()=>{
        const page=await harness.browser.newPage({viewport:{width,height:864},reducedMotion:'reduce'});
        const errors=[],remoteRequests=[];
        page.on('pageerror',error=>errors.push(error.message));
        await page.route('**/*',route=>{
          if(new URL(route.request().url()).origin===harness.origin)return route.continue();
          remoteRequests.push(route.request().url());return route.abort('blockedbyclient');
        });
        const report={width,height:864,language,counts:[],layouts:[],passed:false};
        harness.report.viewports.push(report);
        try {
          await page.addInitScript(installFixture,{island,settings,language});
          await page.goto(harness.origin);
          const options=page.locator('[data-island-screen-options]');
          await options.waitFor();
          await page.evaluate(()=>document.fonts.ready);
          for(const tab of ['todo','notifications'])assert.equal(await page.locator(`[data-tab="${tab}"]`).count(),1,'implemented sidebar pages are visible');
          for(const tab of ['music','quick-launch','clock'])assert.equal(await page.locator(`[data-tab="${tab}"]`).count(),0,'unfinished sidebar pages remain hidden');
          await page.locator('[data-tab="home"]').click();
          if(width===1536) {
            await page.setViewportSize({width,height:480});
            await page.evaluate(()=>new Promise(requestAnimationFrame));
          }
          const leftScroll=page.locator('.island-appearance__scroll');
          const scrollLayout=await page.evaluate(()=>{
            const left=document.querySelector('.island-appearance__scroll');
            const right=document.querySelector('.island-home__main');
            const rect=element=>{const {top,bottom}=element.getBoundingClientRect();return {top,bottom};};
            return {left:{height:left.clientHeight,content:left.scrollHeight},right:{height:right.clientHeight,content:right.scrollHeight},
              title:rect(document.querySelector('.settings-component-title')),
              styles:rect(document.querySelector('[data-island-styles]'))};
          });
          assert.ok(scrollLayout.left.content>scrollLayout.left.height,'settings have their own overflowing scroll area');
          assert.ok(scrollLayout.right.content>scrollLayout.right.height,'the styles gallery scrolls independently');
          await page.screenshot({path:path.join(harness.output,`${width}-${language}-independent-panels.png`)});
          const leftBox=await leftScroll.boundingBox();
          await page.mouse.move(leftBox.x+leftBox.width/2,leftBox.y+Math.min(30,leftBox.height/2));
          await page.mouse.wheel(0,480);
          await page.waitForFunction(()=>document.querySelector('.island-appearance__scroll').scrollTop>0);
          const afterLeft=await page.evaluate(()=>({left:document.querySelector('.island-appearance__scroll').scrollTop,
            right:document.querySelector('.island-home__main').scrollTop,
            title:document.querySelector('.settings-component-title').getBoundingClientRect().top,
            styles:document.querySelector('[data-island-styles]').getBoundingClientRect().top}));
          assert.ok(afterLeft.left>0);assert.equal(afterLeft.right,0,'left wheel input does not move the component list');
          assert.ok(Math.abs(afterLeft.title-scrollLayout.title.top)<1,'the island title stays fixed while settings scroll');
          assert.ok(Math.abs(afterLeft.styles-scrollLayout.styles.top)<1,'the styles gallery stays fixed while settings scroll');
          const galleryBox=await page.locator('.island-home__main').boundingBox();
          await page.mouse.move(galleryBox.x+galleryBox.width*.8,galleryBox.y+galleryBox.height/2);
          await page.mouse.wheel(0,480);
          await page.waitForFunction(()=>document.querySelector('.island-home__main').scrollTop>0);
          const afterRight=await page.evaluate(()=>({left:document.querySelector('.island-appearance__scroll').scrollTop,
            right:document.querySelector('.island-home__main').scrollTop,
            title:document.querySelector('.settings-component-title').getBoundingClientRect().top,
            styles:document.querySelector('[data-island-styles]').getBoundingClientRect().top}));
          assert.equal(afterRight.left,afterLeft.left,'right wheel input leaves the settings position unchanged');
          assert.ok(afterRight.right>0,'the restored component gallery scrolls');
          assert.ok(Math.abs(afterRight.title-scrollLayout.title.top)<1,'the island title stays fixed while components scroll');
          assert.ok(afterRight.styles<scrollLayout.styles.top,'the styles gallery moves with its scroll area');
          report.independentScroll={left:scrollLayout.left,right:scrollLayout.right,passed:true};
          await page.evaluate(()=>{
            document.querySelector('.island-appearance__scroll').scrollTop=0;
            document.querySelector('.island-home__main').scrollTop=0;
          });
          if(width===1536) {
            await page.setViewportSize({width,height:864});
            await page.evaluate(()=>new Promise(requestAnimationFrame));
          }
          assert.equal(await page.locator('.island-display h2').textContent(),language==='en'?'Display':'显示');
          assert.equal(await page.locator('#island-screen-label').textContent(),language==='en'?'Display policy':'显示策略');
          const select=async screen=>{
            const button=options.locator(`[data-island-screen="${screen}"]`);
            if(await button.getAttribute('aria-pressed')==='true')return;
            await button.click();
            await page.evaluate(()=>window.islandScreensFixture.reply());
            assert.equal(await button.getAttribute('aria-pressed'),'true');
          };
          for(const count of [1,2,4]) {
            await page.evaluate(count=>window.islandScreensFixture.set(1,Array.from({length:count},(_,i)=>i+1)),count);
            assert.equal(await options.locator('button').count(),count+1);
            assert.deepEqual(await options.locator('button').allTextContents(),[
              ...Array.from({length:count},(_,i)=>`${language==='en'?'Screen':'屏幕'} ${i+1}`),
              language==='en'?'All screens':'全部屏幕',
            ]);
            await select(0);await select(count);
            const layout=await page.evaluate(()=>{
              const body=document.querySelector('.island-display .island-appearance__body').getBoundingClientRect();
              const buttons=[...document.querySelectorAll('[data-island-screen]')].map(button=>{
                const rect=button.getBoundingClientRect();
                return {left:rect.left,right:rect.right,top:rect.top,bottom:rect.bottom,
                  fits:button.scrollWidth<=button.clientWidth+1};
              });
              const appearance=document.querySelector('.island-appearance__body').getBoundingClientRect();
              const display=document.querySelector('.island-display').getBoundingClientRect();
              return {documentWidth:document.documentElement.scrollWidth,viewport:innerWidth,
                afterAppearance:display.top>=appearance.bottom,body:{left:body.left,right:body.right,top:body.top,bottom:body.bottom},buttons};
            });
            assert.ok(layout.documentWidth<=layout.viewport,'settings must not overflow the viewport');
            assert.ok(layout.afterAppearance,'Display must follow the existing Appearance section');
            for(const button of layout.buttons) {
              assert.ok(button.fits,'every screen label remains readable');
              assert.ok(button.left>=layout.body.left&&button.right<=layout.body.right&&
                button.top>=layout.body.top&&button.bottom<=layout.body.bottom,'screen options stay inside the settings card');
            }
            report.counts.push(count);report.layouts.push(layout);
            await page.locator('.island-appearance').screenshot({path:path.join(harness.output,`${width}-${language}-${count}-screens.png`)});
          }
          await page.evaluate(()=>window.islandScreensFixture.set(2,[1,2]));
          await options.locator('[data-island-screen="0"]').click();
          assert.equal(await options.locator('[aria-pressed="true"]').getAttribute('data-island-screen'),'2','selection waits for host acknowledgement');
          assert.ok(await options.locator('[data-island-screen="0"]').evaluate(button=>button.disabled||button.getAttribute('aria-disabled')==='true'));
          await page.evaluate(()=>window.islandScreensFixture.reply(true));
          assert.equal(await options.locator('[aria-pressed="true"]').getAttribute('data-island-screen'),'2','failed saves retain the confirmed selection');
          await select(0);await select(2);
          await page.evaluate(()=>window.islandScreensFixture.set(2,[1]));
          assert.equal(await options.locator('button').count(),2);
          assert.equal(await page.locator('[data-island-screen-unavailable]').isVisible(),true);
          await page.evaluate(()=>window.islandScreensFixture.set(2,[1,2,3,4]));
          assert.equal(await options.locator('button').count(),5);
          assert.equal(await page.locator('[data-island-screen-unavailable]').isVisible(),false);
          assert.equal(await options.locator('[data-island-screen="2"]').getAttribute('aria-pressed'),'true');
          await page.evaluate(()=>window.islandScreensFixture.navigate());
          assert.equal(await page.locator('[data-island-screen="2"]').getAttribute('aria-pressed'),'true','returning to the page preserves the host selection');
          const commands=await page.evaluate(()=>window.islandScreensFixture.commands.filter(command=>typeof command==='string'&&command.startsWith('v1|dynamicIsland|screen|')));
          assert.ok(commands.includes('v1|dynamicIsland|screen|0')&&commands.includes('v1|dynamicIsland|screen|4'));
          const modes=page.locator('.island-expansion');
          assert.equal(await modes.locator('h2').textContent(),language==='en'?'Expansion mode':'展开模式');
          assert.deepEqual(await modes.locator('button').allTextContents(),language==='en'?['Click to expand','Hover to expand']:['点击展开','悬停展开']);
          const click=modes.locator('[data-island-expansion="click"]'),hover=modes.locator('[data-island-expansion="hover"]');
          assert.equal(await click.getAttribute('aria-pressed'),'true');
          await hover.click();
          assert.equal(await click.getAttribute('aria-pressed'),'true','expansion mode waits for host confirmation');
          assert.ok(await hover.evaluate(button=>button.disabled||button.getAttribute('aria-disabled')==='true'));
          await page.evaluate(()=>window.islandScreensFixture.reply(true));
          assert.equal(await click.getAttribute('aria-pressed'),'true','failed expansion-mode saves retain click mode');
          await hover.click();await page.evaluate(()=>window.islandScreensFixture.reply());
          assert.equal(await hover.getAttribute('aria-pressed'),'true');
          await page.evaluate(()=>window.islandScreensFixture.navigate());
          assert.equal(await hover.getAttribute('aria-pressed'),'true','returning to the page preserves the expansion mode');
          const expansion=await page.evaluate(()=>{
            const card=document.querySelector('.island-expansion').getBoundingClientRect();
            const display=document.querySelector('.island-display').getBoundingClientRect();
            const appearance=document.querySelector('.island-appearance').getBoundingClientRect();
            return {afterDisplay:card.top>=display.bottom,overflow:document.documentElement.scrollWidth>innerWidth,
              labelsFit:[...document.querySelectorAll('[data-island-expansion]')].every(button=>button.scrollWidth<=button.clientWidth+1),
              clip:{x:appearance.left,y:appearance.top,width:appearance.width,height:card.bottom-appearance.top}};
          });
          assert.ok(expansion.afterDisplay&&expansion.labelsFit&&!expansion.overflow,'expansion mode follows Display and its labels fit at both widths');
          await page.screenshot({path:path.join(harness.output,`${width}-${language}-expansion-mode.png`),clip:expansion.clip});
          await click.click();await page.evaluate(()=>window.islandScreensFixture.reply());
          assert.equal(await click.getAttribute('aria-pressed'),'true');
          const modeCommands=await page.evaluate(()=>window.islandScreensFixture.commands.filter(command=>typeof command==='string'&&command.startsWith('v1|dynamicIsland|hover|')));
          assert.deepEqual(modeCommands,['v1|dynamicIsland|hover|true','v1|dynamicIsland|hover|true','v1|dynamicIsland|hover|false']);
          report.expansion={...expansion,commands:modeCommands,passed:true};
          const animation=page.locator('.island-animation');
          assert.equal(await animation.locator('h2').textContent(),language==='en'?'Animation settings':'动画设置');
          const slider=animation.locator('[data-island-hover-delay]'),output=animation.locator('output');
          const inputDelay=value=>slider.evaluate((input,value)=>{input.value=String(value);input.dispatchEvent(new Event('input',{bubbles:true}));},value);
          await page.evaluate(()=>{
            window.islandStableHome=document.querySelector('[data-island-home]');
            window.islandStableAnimation=document.querySelector('.island-animation');
            window.islandStableBody=document.querySelector('.settings-body');
            window.islandSavingOpacities=()=>[...document.querySelectorAll('.island-appearance__segments button,[data-island-hover-delay]')]
              .map(control=>Number(getComputedStyle(control).opacity));
          });
          assert.equal(await slider.inputValue(),'300');assert.equal(await output.textContent(),'0.3s');
          assert.equal(await slider.getAttribute('step'),'50');
          await inputDelay(1000);
          assert.equal(await output.textContent(),'1s','input immediately previews seconds');
          await slider.dispatchEvent('change');
          assert.ok((await page.evaluate(()=>window.islandSavingOpacities())).every(opacity=>opacity===1),'saving must not dim the other controls and produce a visible flash');
          const savingFrames=await page.evaluate(async()=>{
            const frames=[];
            for(let i=0;i<4;++i){await new Promise(requestAnimationFrame);frames.push(window.islandSavingOpacities());}
            return frames;
          });
          assert.ok(savingFrames.flat().every(opacity=>opacity===1),'controls retain their brightness across pending display frames');
          assert.equal(await page.locator('[data-island-native-preview]').count(),0,'saving does not recreate the removed preview');
          assert.ok(await page.evaluate(()=>window.islandStableHome===document.querySelector('[data-island-home]')&&
            window.islandStableAnimation===document.querySelector('.island-animation')&&window.islandStableBody===document.querySelector('.settings-body')),
          'saving must preserve the existing page and scroll container');
          assert.equal(await slider.inputValue(),'1000','submitted slider remains stable while awaiting acknowledgement');
          assert.equal(await slider.getAttribute('aria-disabled'),'true');
          await inputDelay(100);await slider.dispatchEvent('change');
          assert.equal(await slider.inputValue(),'1000','busy slider rejects additional changes');
          await page.evaluate(()=>window.islandScreensFixture.reply(true));
          assert.equal(await slider.inputValue(),'300');assert.equal(await output.textContent(),'0.3s','failed save restores confirmed delay');
          await inputDelay(100);await slider.dispatchEvent('change');await page.evaluate(()=>window.islandScreensFixture.reply());
          assert.equal(await slider.inputValue(),'100');assert.equal(await output.textContent(),'0.1s');
          for(const [key,values] of [['animationSpeed',['slow','normal','fast']],['bounce',['subtle','normal','bouncy']]]){
            for(const value of values){
              const button=animation.locator(`[data-island-preference="${key}"][data-value="${value}"]`);
              if(await button.getAttribute('aria-pressed')==='true')continue;
              await button.click();
              assert.equal(await button.getAttribute('aria-pressed'),'false','animation buttons wait for native acknowledgement');
              await page.evaluate(()=>window.islandScreensFixture.reply());
              assert.equal(await button.getAttribute('aria-pressed'),'true');
            }
            const failed=animation.locator(`[data-island-preference="${key}"][data-value="normal"]`);
            await failed.click();await page.evaluate(()=>window.islandScreensFixture.reply(true));
            assert.equal(await animation.locator(`[data-island-preference="${key}"][data-value="${values.at(-1)}"]`).getAttribute('aria-pressed'),'true','failed animation button saves preserve the last preference');
          }
          await page.evaluate(()=>window.islandScreensFixture.navigate());
          assert.equal(await slider.inputValue(),'100','returning to the page restores the confirmed delay');
          assert.equal(await animation.locator('[data-island-preference="animationSpeed"][data-value="fast"]').getAttribute('aria-pressed'),'true');
          assert.equal(await animation.locator('[data-island-preference="bounce"][data-value="bouncy"]').getAttribute('aria-pressed'),'true');
          await slider.scrollIntoViewIfNeeded();await slider.focus();await slider.press('End');
          await page.evaluate(()=>window.islandScreensFixture.reply());
          assert.equal(await slider.inputValue(),'1000');assert.equal(await slider.getAttribute('aria-valuetext'),'1s','range keyboard control reaches the upper bound');
          await slider.press('ArrowLeft');await page.evaluate(()=>window.islandScreensFixture.reply());
          assert.equal(await slider.inputValue(),'950');assert.equal(await output.textContent(),'0.95s','each keyboard step is exactly 0.05 seconds');
          const track=await slider.boundingBox();
          await page.mouse.click(track.x+track.width*.65,track.y+track.height/2);
          await page.evaluate(()=>window.islandScreensFixture.reply());
          assert.equal(Number(await slider.inputValue())%50,0,'pointer changes stay on the 50 ms grid');
          assert.ok((await page.evaluate(()=>window.islandSavingOpacities())).every(opacity=>opacity===1));
          const animationLayout=await animation.evaluate(card=>{
            const bounds=card.getBoundingClientRect(),mode=card.previousElementSibling.getBoundingClientRect();
            return {afterMode:bounds.top>=mode.bottom,overflow:document.documentElement.scrollWidth>innerWidth,
              labelsFit:[...card.querySelectorAll('button')].every(button=>button.scrollWidth<=button.clientWidth+1),
              rangeFits:card.querySelector('input').getBoundingClientRect().right<=bounds.right};
          });
          assert.ok(animationLayout.afterMode&&!animationLayout.overflow&&animationLayout.labelsFit&&animationLayout.rangeFits);
          await animation.screenshot({path:path.join(harness.output,`${width}-${language}-animation-settings.png`)});
          report.animation={...animationLayout,passed:true};
          assert.deepEqual(errors,[]);assert.deepEqual(remoteRequests,[]);
          report.commands=commands;report.passed=true;
        } finally {await page.close();}
      });
    }
  } finally {await harness.close();}
  t.diagnostic(`Screenshots and layout evidence: ${harness.output}`);
});
