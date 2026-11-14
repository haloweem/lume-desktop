import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';

function fixture(){
  const listeners=new Set(),commands=[];
  const catalog=Array.from({length:15},(_,i)=>({id:`warm-${i}`,title:`Wallpaper ${i}`,kind:'video',
    url:`https://static.lumedesktop.com/warm-${i}.mp4`,
    thumbnail:`https://static.lumedesktop.com/thumbnails/warm-${i}.jpg`,
    heroThumbnail:`https://static.lumedesktop.com/hero-thumbnails/warm-${i}.jpg`,
    width:1920,height:1080,bytes:1000,libraryId:''}));
  if(!sessionStorage.getItem('warm-fixture-started')){
    localStorage.setItem('lume.home.hero',JSON.stringify({...catalog[3],source:catalog[3].heroThumbnail,url:catalog[3].heroThumbnail}));
    sessionStorage.setItem('warm-fixture-started','1');
  }
  let current={version:1,type:'wallpaper-state',revision:0,status:'idle',error:'',loadStatus:'loaded',
    playerAvailable:true,catalogStatus:'loading',catalogError:'',catalog:[],library:[],displays:[],likedIds:[],
    download:{id:'',status:'idle',received:0,total:0,error:''},pauseFullscreen:false};
  const emit=message=>listeners.forEach(listener=>listener({data:message}));
  window.homeFixture={commands,
    catalog(){current={...current,revision:current.revision+1,catalogStatus:'ready',catalog:structuredClone(catalog)};emit(current);},
    snapshot(){current={...structuredClone(current),revision:current.revision+1};emit(current);},
    visibility(visible,generation){emit({version:1,type:'visibility',visible,generation});},
  };
  window.chrome={webview:{
    addEventListener(name,listener){if(name==='message')listeners.add(listener);},
    removeEventListener(name,listener){if(name==='message')listeners.delete(listener);},
    postMessage(command){commands.push(command);if(command==='v1|ready')queueMicrotask(()=>{
      emit({version:1,type:'app-settings-state',revision:1,language:'zh-CN',balancedMode:true,autostart:false,
        busy:false,error:'',update:{status:'idle',version:'',progress:0}});emit(current);
    });},
  }};
}

test('home retains loaded cover nodes, restores the remembered slide and preheats only ten images',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:45000,
},async()=>{
  const output=fileURLToPath(new URL('../../../build-artifacts/page-warmup-dock-20261105/home-browser',import.meta.url));
  const harness=await createBrowserHarness(output);
  const context=await harness.browser.newContext({viewport:{width:1536,height:864},reducedMotion:'reduce'});
  const page=await context.newPage();page.setDefaultTimeout(8000);
  const errors=[],requests=[];page.on('pageerror',error=>errors.push(error.message));
  const cover=await readFile(new URL('../assets/black-texture.webp',import.meta.url));
  await page.route('https://static.lumedesktop.com/**',async route=>{
    requests.push(route.request().url());await route.fulfill({status:200,contentType:'image/webp',body:cover});
  });
  try{
    await page.addInitScript(fixture);await page.goto(harness.origin);
    await page.waitForFunction(()=>document.querySelector('.preview.current img')?.naturalWidth>0);
    assert.match(await page.locator('.preview.current img').getAttribute('src'),/warm-3.jpg$/,'first paint uses the remembered cover');
    await page.evaluate(()=>{window.firstCover=document.querySelector('.preview.current img');window.homeFixture.catalog();});
    await page.locator('.HomeHero [data-wallpaper-id="warm-3"]').waitFor();
    assert.equal(await page.evaluate(()=>window.firstCover===document.querySelector('.preview.current img')),true,
      'catalog arrival preserves the already decoded first cover');
    await page.waitForFunction(()=>performance.getEntriesByType('resource').filter(entry=>entry.name.includes('/hero-thumbnails/')).length>=10);
    assert.equal(new Set(requests).size,10,'only the ten home covers are warmed');
    assert.ok(requests.every(url=>url.endsWith('.jpg')&&url.includes('/hero-thumbnails/')),'no wallpaper video is downloaded');
    await page.evaluate(()=>{window.homeRoot=document.querySelector('.HomeRoot');window.homeImage=document.querySelector('.preview.current img');
      for(let i=0;i<4;i++)window.homeFixture.snapshot();});
    assert.deepEqual(await page.evaluate(()=>({root:window.homeRoot===document.querySelector('.HomeRoot'),
      image:window.homeImage===document.querySelector('.preview.current img')})),{root:true,image:true});
    const before=requests.length;
    await page.locator('[data-view="desktopBeauty"]').click();
    await page.locator('[data-view="home"]').click();
    assert.deepEqual(await page.evaluate(()=>({root:window.homeRoot===document.querySelector('.HomeRoot'),
      image:window.homeImage===document.querySelector('.preview.current img'),decoded:document.querySelector('.preview.current img').naturalWidth>0})),
      {root:true,image:true,decoded:true},'returning to home reattaches its existing loaded image');
    await page.evaluate(()=>window.homeFixture.visibility(false,1));
    await page.waitForTimeout(150);assert.equal(requests.length,before,'hidden manager starts no image requests');
    await page.evaluate(()=>window.homeFixture.visibility(true,2));
    await page.waitForTimeout(150);assert.equal(requests.length,before,'showing the same page keeps its warmed images');
    await page.locator('[data-dot="7"]').click();
    await page.locator('.HomeHero [data-wallpaper-id="warm-7"]').waitFor();
    await page.reload();
    await page.waitForFunction(()=>document.querySelector('.preview.current img')?.naturalWidth>0);
    assert.match(await page.locator('.preview.current img').getAttribute('src'),/warm-7.jpg$/,'new document remembers the last cover before catalog arrival');
    await page.evaluate(()=>window.homeFixture.catalog());
    await page.locator('.HomeHero [data-wallpaper-id="warm-7"]').waitFor();
    assert.deepEqual(errors,[]);
    await page.screenshot({path:path.join(output,'home-warmed.png'),animations:'disabled'});
    harness.report.viewports.push({width:1536,passed:true,warmedCovers:10,retainedImage:true,restoredSlide:7});
  }finally{await context.close();await harness.close();}
});
