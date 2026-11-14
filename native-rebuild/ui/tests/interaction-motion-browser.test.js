import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';

function installHost({language}){
  const listeners=new Set();let pending=null,revision=1;
  const island={version:1,type:'dynamicIsland-state',enabled:true,running:true,locked:true,expanded:false,
    focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:false,hover:false,autoHide:true,
    hoverDelayMs:300,animationSpeed:'normal',bounce:'normal',size:'standard',material:'none',shape:'capsule',
    leftWidget:0,rightWidget:0,screen:1,screens:[1,2],loadStatus:'loaded',error:'',result:'snapshot',saved:false};
  const settings=()=>({version:1,type:'app-settings-state',revision:revision++,language,balancedMode:true,
    autostart:false,busy:false,error:'',update:{status:'idle',version:'',progress:0}});
  const receive=message=>listeners.forEach(listener=>listener({data:message}));
  window.motionHost={commands:[],reply(failed=false){
    if(!pending)throw new Error('Missing pending command');
    if(!failed){island[pending.key]=pending.value;if(pending.key==='enabled')island.running=pending.value;}
    pending=null;receive({...island,result:'applied',saved:!failed,error:failed?'save':''});
  }};
  window.chrome={webview:{addEventListener(type,listener){if(type==='message')listeners.add(listener);},
    removeEventListener(type,listener){listeners.delete(listener);},postMessage(command){
      window.motionHost.commands.push(command);
      if(command==='v1|ready')queueMicrotask(()=>{
        receive(settings());receive(island);
        receive({version:1,type:'wallpaper-state',revision:1,status:'idle',error:'',loadStatus:'fresh',playerAvailable:true,
          catalogStatus:'ready',catalogError:'',likedIds:[],catalog:[],library:[],displays:[],
          download:{id:'',status:'idle',received:0,total:0,error:''},pauseFullscreen:false});
      });
      else if(typeof command==='string'&&command.startsWith('v1|dynamicIsland|')){
        const [, , key,raw]=command.split('|');
        if(key==='preview'){
          if(raw!=='stop')queueMicrotask(()=>receive({version:1,type:'dynamicIsland-preview',mode:raw,shape:island.shape,
            size:island.size,material:island.material,width:0,height:0,image:'',error:'render'}));
        }else {if(pending)throw new Error('Duplicate pending request');pending={key,value:raw==='true'?true:raw==='false'?false:raw};}
      }else if(command?.scope==='app-settings'&&command.action==='snapshot')queueMicrotask(()=>receive(settings()));
    }}};
}

async function sampleSlide(page,group,action,screenshot){
  const result=await page.evaluate(async({group,action})=>{
    const previous=document.querySelector(group),old=previous.querySelector('[data-lume-motion-indicator]').getBoundingClientRect();
    document.querySelector(action).click();await new Promise(requestAnimationFrame);
    const current=document.querySelector(group),indicator=current.querySelector('[data-lume-motion-indicator]');
    const animation=indicator.getAnimations()[0];if(!animation)throw new Error('Selection did not animate');
    animation.pause();const duration=animation.effect.getTiming().duration;
    const box=()=>{const r=indicator.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};};
    animation.currentTime=0;const start=box();animation.currentTime=duration/2;const middle=box();
    animation.currentTime=duration;const end=box();animation.currentTime=duration/2;
    return {old:{x:old.x,y:old.y},start,middle,end,replaced:previous!==current,duration};
  },{group,action});
  if(screenshot)await page.locator(group).screenshot({path:screenshot});
  await page.locator(group).evaluate(element=>element.querySelector('[data-lume-motion-indicator]').getAnimations().forEach(animation=>animation.finish()));
  return result;
}
const between=(middle,a,b)=>middle>Math.min(a,b)+.2&&middle<Math.max(a,b)-.2;

test('real entry: navigation, confirmed segments and anchored menus',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000,
},async t=>{
  const harness=await createBrowserHarness(process.env.LUME_MOTION_BROWSER_OUTPUT||fileURLToPath(new URL('../../../build-artifacts/interaction-motion-browser-20261104',import.meta.url)));
  try{
    for(const [width,language] of [[1536,'zh-CN'],[680,'en']])await t.test(`${width}px ${language}`,async()=>{
      const page=await harness.browser.newPage({viewport:{width,height:864},reducedMotion:'no-preference'});
      const errors=[];page.on('pageerror',error=>errors.push(error.message));
      try{
        await page.addInitScript(installHost,{language});await page.goto(harness.origin);
        await page.locator('.HomeModuleTabs[data-lume-motion-group]').waitFor();
        const main=await sampleSlide(page,'.HomeModuleTabs','[data-view="desktopBeauty"]',path.join(harness.output,`${width}-${language}-main-mid.png`));
        assert.ok(main.replaced&&between(main.middle.x,main.start.x,main.end.x),'top-level highlight survives actual page reconstruction');
        const report={width,language,main};harness.report.viewports.push(report);
        await page.locator('[data-view="wallpaperOfficial"]').click();
        await page.locator('[data-wallpaper-select-trigger="wallpaper-kind"]').click();
        assert.ok(await page.locator('.wallpaper-select-menu').evaluate(element=>element.getAnimations().length>0),'wallpaper filters use the anchored entrance');
        await page.locator('.wallpaper-select-menu').evaluate(element=>element.getAnimations().forEach(animation=>animation.finish()));
        await page.screenshot({path:path.join(harness.output,`${width}-${language}-wallpaper-dropdown.png`)});
        await page.locator('[data-view="desktopBeauty"]').click();
        await page.locator('[data-open="dynamicIsland"]').click();
        await page.locator('.settings-side-nav__links[data-lume-motion-group]').waitFor();
        const sidebar=await sampleSlide(page,'.settings-side-nav__links','[data-tab="settings"]',path.join(harness.output,`${width}-${language}-sidebar-mid.png`));
        assert.ok(sidebar.replaced&&between(sidebar.middle.y,sidebar.start.y,sidebar.end.y),'sidebar highlight slides vertically across the real page switch');
        await page.locator('[data-tab="home"]').click();
        await page.locator('[aria-labelledby="island-material-label"][data-lume-motion-group]').waitFor();
        await page.evaluate(()=>document.querySelector('[data-island-preference="material"][data-value="frosted-glass"]').click());
        assert.equal(await page.locator('[data-island-preference="material"][data-value="none"]').getAttribute('aria-pressed'),'true');
        assert.equal(await page.locator('[aria-labelledby="island-material-label"] [data-lume-motion-indicator]').evaluate(element=>element.getAnimations().length),0,
          'pending selections do not animate before native confirmation');
        const material=await page.evaluate(async()=>{
          window.motionHost.reply();await new Promise(requestAnimationFrame);
          const indicator=document.querySelector('[aria-labelledby="island-material-label"] [data-lume-motion-indicator]');
          const animation=indicator.getAnimations()[0];if(!animation)throw new Error('Confirmed segment did not animate');
          animation.pause();const duration=animation.effect.getTiming().duration;
          animation.currentTime=0;const start=indicator.getBoundingClientRect().x;
          animation.currentTime=duration/2;const middle=indicator.getBoundingClientRect().x;
          animation.currentTime=duration;const end=indicator.getBoundingClientRect().x;animation.finish();return {start,middle,end};
        });
        assert.ok(between(material.middle,material.start,material.end));
        await page.evaluate(()=>document.querySelector('[data-island-preference="material"][data-value="none"]').click());
        await page.evaluate(()=>window.motionHost.reply(true));
        assert.equal(await page.locator('[data-island-preference="material"][data-value="frosted-glass"]').getAttribute('aria-pressed'),'true');
        assert.equal(await page.locator('[data-island-preview-mode]').count(),0);
        await page.locator('[data-tab="settings"]').click();
        const trigger=page.locator('#dynamicIsland-layer');await trigger.click();
        const menu=page.locator('#dynamicIsland-layer-options');
        const opening=await menu.evaluate(element=>{
          const animation=element.getAnimations()[0];if(!animation)throw new Error('Menu opened abruptly');
          animation.pause();animation.currentTime=85;const style=getComputedStyle(element);
          const result={origin:style.transformOrigin,opacity:Number(style.opacity),translate:style.translate,scale:style.scale};animation.finish();return result;
        });
        assert.ok(opening.opacity>0&&opening.opacity<1&&Number.parseFloat(opening.origin.split(' ')[1])===0,'dropdown grows from the trigger edge');
        await page.keyboard.press('Escape');
        const closing=await menu.evaluate(element=>({hidden:element.hidden,inert:element.inert,ariaHidden:element.getAttribute('aria-hidden'),
          closing:element.hasAttribute('data-lume-motion-closing')}));
        assert.deepEqual(closing,{hidden:true,inert:true,ariaHidden:'true',closing:true},'closing pixels cannot receive input or remain in the accessibility tree');
        await menu.evaluate(async element=>{element.getAnimations().forEach(animation=>animation.finish());await Promise.resolve();});
        assert.equal(await menu.isVisible(),false);assert.equal(await trigger.evaluate(element=>element===document.activeElement),true);
        await trigger.click();await page.keyboard.press('ArrowDown');await page.keyboard.press('Escape');
        await page.waitForTimeout(150);
        assert.equal(await menu.isVisible(),false,'rapid reopen and keyboard close leave no outgoing menu behind');
        const bounds=await page.locator('.settings-side-nav__links').evaluate(group=>{
          const selected=group.querySelector('[aria-selected="true"]'),indicator=group.querySelector('[data-lume-motion-indicator]');
          indicator.getAnimations().forEach(animation=>animation.finish());
          const a=selected.getBoundingClientRect(),b=indicator.getBoundingClientRect();return {x:Math.abs(a.x-b.x),y:Math.abs(a.y-b.y),width:Math.abs(a.width-b.width)};
        });
        assert.ok(bounds.x<1&&bounds.y<1&&bounds.width<1);
        await page.screenshot({path:path.join(harness.output,`${width}-${language}-settings.png`)});
        report.sidebar=sidebar;report.material=material;report.opening=opening;report.passed=true;
        await page.locator('[data-titlebar-settings]').click();
        await page.locator('.application-settings__nav[data-lume-motion-group]').waitFor();
        const application=await sampleSlide(page,'.application-settings__nav','[data-settings-section="about"]');
        assert.ok(between(application.middle.y,application.start.y,application.end.y),'application settings categories use the vertical slide');
        report.application=application;
        assert.deepEqual(errors,[]);
      }finally{await page.close();}
    });
  }finally{await harness.close();}
});

test('shared motion adapters: wrapped items, rapid reversal, upward menus, reduced motion and disposal',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:45000,
},async()=>{
  const harness=await createBrowserHarness(fileURLToPath(new URL('../../../build-artifacts/interaction-motion-adapters-20261104',import.meta.url)));
  const page=await harness.browser.newPage({viewport:{width:900,height:700},reducedMotion:'no-preference'});
  try{
    await page.route(harness.origin+'/motion-fixture',route=>route.fulfill({contentType:'text/html',body:`<!doctype html><meta charset="utf-8">
      <link rel="stylesheet" href="/app.css"><link rel="stylesheet" href="/dynamic-island.css"><link rel="stylesheet" href="/application-settings.css"><link rel="stylesheet" href="/music.css"><link rel="stylesheet" href="/interaction-motion.css">
      <main id="fixture" style="position:relative;width:100%;height:100%;padding:30px">
      <div class="island-appearance__segments" aria-labelledby="fixture-label" style="width:170px;flex-wrap:wrap"><button data-value="one" aria-pressed="true">One</button><button data-value="two" aria-pressed="false">Two</button><button data-value="three" aria-pressed="false" style="flex-basis:100%">Three</button></div>
      <div class="MusicAudioQualityPicker" style="position:absolute;bottom:35px;left:60px;width:120px"><button aria-expanded="false">Quality</button><div class="MusicAudioQualityMenu" role="menu" hidden><button>Standard</button><button>Lossless</button></div></div>
      <div class="MusicVisualizerPicker" style="position:absolute;top:330px;left:400px;width:130px"><button aria-expanded="false">Effect</button><div class="MusicVisualizerMenu" role="listbox" hidden><button>One</button><button>Two</button></div></div>
      <button class="switch" role="switch" aria-checked="false"><span></span></button>
      <nav class="application-settings__nav" style="width:200px;padding:0;margin:20px"><button data-settings-section="general" class="active">General</button><button data-settings-section="about">About</button></nav>
      <div class="MusicSidebar" style="position:relative;width:190px;height:100px"><nav class="MusicNav"><button class="MusicNavItem is-active" data-music-nav="one">One</button><button class="MusicNavItem" data-music-nav="two">Two</button></nav></div>
      <div class="MusicRailTabs" style="width:220px"><button aria-selected="true">Queue</button><button aria-selected="false">Playlists</button></div>
      <div class="MusicPlaylistTabs" style="width:220px"><button data-music-playlist-group="owned" aria-pressed="true">Created</button><span>|</span><button data-music-playlist-group="saved" aria-pressed="false">Saved</button></div></main>
      <script type="module">import {mountInteractionMotion} from '/interaction-motion.js';window.motion=mountInteractionMotion(document.querySelector('#fixture'));</script>`}));
    await page.goto(harness.origin+'/motion-fixture');
    await page.waitForFunction(()=>!!window.motion);
    const wrapped=await page.evaluate(async()=>{
      const group=document.querySelector('.island-appearance__segments'),buttons=group.querySelectorAll('button');
      buttons[0].setAttribute('aria-pressed','false');buttons[2].setAttribute('aria-pressed','true');await new Promise(requestAnimationFrame);
      const indicator=group.querySelector('[data-lume-motion-indicator]'),animation=indicator.getAnimations()[0];
      animation.pause();animation.currentTime=80;const before=indicator.getBoundingClientRect();
      buttons[2].setAttribute('aria-pressed','false');buttons[1].setAttribute('aria-pressed','true');await new Promise(requestAnimationFrame);
      const reverse=indicator.getAnimations()[0];reverse.pause();reverse.currentTime=0;const after=indicator.getBoundingClientRect();
      reverse.finish();return {before:{x:before.x,y:before.y},after:{x:after.x,y:after.y}};
    });
    assert.ok(Math.abs(wrapped.before.x-wrapped.after.x)<1&&Math.abs(wrapped.before.y-wrapped.after.y)<1,'rapid selection reversal begins from the currently drawn position');
    for(const [selector,attribute,on,off] of [['.application-settings__nav','class','active',''],['.MusicSidebar','class','MusicNavItem is-active','MusicNavItem'],
      ['.MusicRailTabs','aria-selected','true','false'],['.MusicPlaylistTabs','aria-pressed','true','false']]){
      const result=await page.evaluate(async({selector,attribute,on,off})=>{
        const group=document.querySelector(selector),buttons=[...group.querySelectorAll('button')];
        buttons[0].setAttribute(attribute,off);buttons[1].setAttribute(attribute,on);await new Promise(requestAnimationFrame);
        const indicator=group.querySelector('[data-lume-motion-indicator]'),animation=indicator.getAnimations()[0];
        if(!animation)throw new Error(`No animation for ${selector}`);animation.finish();
        return {color:indicator.style.background,height:indicator.style.height};
      },{selector,attribute,on,off});
      assert.ok(result.color&&result.color!=='transparent','adapters preserve the existing highlight color');
      if(selector==='.MusicPlaylistTabs')assert.equal(result.height,'1px','underlined tabs use a sliding line');
    }
    await page.locator('.island-appearance__segments').evaluate(async group=>{
      group.innerHTML='<button aria-pressed="false">New one</button><button aria-pressed="true">New two</button>';
      await new Promise(requestAnimationFrame);
    });
    assert.equal(await page.locator('.island-appearance__segments [data-lume-motion-indicator]').count(),1,'changing screen/button inventories restores exactly one indicator');
    const switchMotion=await page.locator('.switch').evaluate(async control=>{
      const thumb=control.querySelector('span'),before=Number.parseFloat(getComputedStyle(thumb).left);
      control.classList.add('checked');control.setAttribute('aria-checked','true');await new Promise(requestAnimationFrame);
      const animation=thumb.getAnimations()[0];if(!animation)throw new Error('Switch thumb did not transition');
      animation.pause();animation.currentTime=90;const middle=Number.parseFloat(getComputedStyle(thumb).left);
      animation.finish();return {before,middle,end:Number.parseFloat(getComputedStyle(thumb).left)};
    });
    assert.ok(between(switchMotion.middle,switchMotion.before,switchMotion.end),'switch thumb moves continuously');
    for(const selector of ['.MusicAudioQualityMenu','.MusicVisualizerMenu']){
      const result=await page.locator(selector).evaluate(async element=>{
        const previous=getComputedStyle(element).transform;
        element.hidden=false;await new Promise(requestAnimationFrame);
        const animation=element.getAnimations()[0];animation.pause();animation.currentTime=85;
        const style=getComputedStyle(element),result={origin:style.transformOrigin,transform:style.transform,previous,height:element.offsetHeight};
        animation.finish();return result;
      });
      if(selector==='.MusicAudioQualityMenu')assert.ok(Math.abs(Number.parseFloat(result.origin.split(' ')[1])-result.height)<1,'upward popovers originate at the bottom edge');
      else assert.ok(Math.abs(Number(result.transform.slice(7,-1).split(',')[5])+result.height/2)<1,'a centered menu keeps its existing centering transform');
    }
    await page.emulateMedia({reducedMotion:'reduce'});
    await page.evaluate(async()=>{
      const buttons=document.querySelectorAll('.island-appearance__segments button');
      buttons[1].setAttribute('aria-pressed','false');buttons[0].setAttribute('aria-pressed','true');
      document.querySelectorAll('[role="menu"],[role="listbox"]').forEach(menu=>menu.hidden=true);await new Promise(requestAnimationFrame);
    });
    assert.ok(await page.locator('[data-lume-motion-indicator]').evaluateAll(elements=>elements.every(element=>element.getAnimations().length===0)));
    assert.equal(await page.locator('[data-lume-motion-closing]').count(),0);
    assert.equal(await page.locator('.switch span').evaluate(element=>getComputedStyle(element).transitionDuration),'0s');
    await page.evaluate(()=>window.motion.dispose());
    assert.equal(await page.locator('[data-lume-motion-indicator],[data-lume-motion-group],[data-lume-motion-menu]').count(),0,'disposal releases decorations and animations');
  }finally{await page.close();await harness.close();}
});
