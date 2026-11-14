import test from 'node:test';
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile,mkdir} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('cached wallpaper catalog upgrades an active preview and loops without reloading media',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1'||!process.env.LUME_UI_TEST_VIDEO,timeout:60000,
},async()=>{
  const root=path.resolve(fileURLToPath(new URL('../',import.meta.url)));
  const video=await readFile(process.env.LUME_UI_TEST_VIDEO);
  const cover=await readFile(path.join(root,'assets/black-texture.webp'));
  const server=createServer(async(req,res)=>{
    try{
      const url=new URL(req.url,'http://localhost');
      const target=path.resolve(root,'.'+decodeURIComponent(url.pathname==='/'?'/index.html':url.pathname));
      if(!target.startsWith(root+path.sep))return res.writeHead(403).end();
      const type={'.html':'text/html','.js':'text/javascript','.css':'text/css'}[path.extname(target)];
      const data=await readFile(target);
      res.writeHead(200,{'Content-Type':type||'application/octet-stream'}).end(data);
    }catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||
    path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
  let browser;
  try{
    browser=await chromium.launch({headless:true,executablePath:process.env.LUME_BROWSER_EXECUTABLE});
    const page=await browser.newPage({viewport:{width:1100,height:720}});
    const errors=[],requests=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.route('https://static.lumedesktop.com/**',route=>{
      const url=route.request().url();requests.push(url);
      return route.fulfill({contentType:url.endsWith('.mp4')?'video/mp4':'image/webp',
        body:url.endsWith('.mp4')?video:cover});
    });
    await page.addInitScript(()=>{
      const listeners=new Set();
      const fresh=location.search.includes('fresh=1');
      const item={id:'preview-flow',title:'Preview flow',kind:'video',
        thumbnail:'https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/flow.jpg',
        url:'https://static.lumedesktop.com/lume/wallpapers/official/videos/original.mp4',
        width:3840,height:2160,bytes:72521358,libraryId:''};
      window.previewCommands=[];
      window.previewState={version:1,type:'wallpaper-state',revision:0,status:'idle',error:'',loadStatus:'fresh',
        playerAvailable:true,catalogStatus:fresh?'idle':'ready',catalogError:fresh?'':'WALLPAPER_CATALOG_CACHED',catalog:fresh?[]:[item],
        library:[],displays:[],download:{id:'',status:'idle',received:0,total:0,error:''},pauseFullscreen:false,likedIds:[]};
      window.emitPreview=fields=>{
        window.previewState={...window.previewState,...fields,revision:window.previewState.revision+1};
        for(const fn of listeners)fn({data:window.previewState});
      };
      window.emitPreviewVisibility=visible=>{
        for(const fn of listeners)fn({data:{version:1,type:'visibility',visible}});
      };
      window.finishPreviewRefresh=()=>window.emitPreview({catalogStatus:'ready',catalogError:'',
        catalog:[{...item,url:'https://static.lumedesktop.com/lume/wallpapers/official/previews/short.mp4'}]});
      window.chrome={webview:{addEventListener:(type,fn)=>listeners.add(fn),
        removeEventListener:(type,fn)=>listeners.delete(fn),postMessage(command){
          window.previewCommands.push(command);
          if(command==='v1|ready'||command?.scope==='wallpaper'&&['snapshot','preview','endPreview'].includes(command.action))
            queueMicrotask(()=>window.emitPreview({}));
          if(command?.scope==='wallpaper'&&command.action==='refreshVideos')
            queueMicrotask(()=>window.emitPreview({catalogStatus:'loading',catalogError:''}));
          if(command?.scope==='wallpaper'&&command.action==='refresh')
            queueMicrotask(()=>window.emitPreview({status:'busy',catalogStatus:'loading',catalogError:'',
              catalog:[{...item,url:'https://static.lumedesktop.com/lume/wallpapers/official/previews/short.mp4'}]}));
        }}};
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.waitForFunction(()=>window.previewCommands.some(c=>c.action==='refreshVideos'));
    const commands=()=>page.evaluate(()=>window.previewCommands.filter(c=>c?.scope==='wallpaper').map(c=>c.action));
    assert.equal((await commands()).filter(c=>c==='refreshVideos').length,1);
    assert.ok(!(await commands()).includes('refresh'),'cached startup avoids the large image catalog');
    await page.locator('.HomeHero [data-wallpaper-id="preview-flow"]').click();
    await page.waitForFunction(()=>document.querySelector('.wallpaper-preview-video')?.dataset.ready==='true');
    assert.match(await page.locator('video').getAttribute('src'),/\/videos\/original\.mp4$/);
    await page.evaluate(()=>window.finishPreviewRefresh());
    await page.waitForFunction(()=>{const v=document.querySelector('video');
      return v?.dataset.ready==='true'&&v.currentSrc.endsWith('/previews/short.mp4')&&!v.paused;});
    await page.evaluate(()=>{
      const v=document.querySelector('video');window.loopVideo=v;window.previewLoops=0;
      let previous=v.currentTime;
      v.addEventListener('timeupdate',()=>{if(previous>v.currentTime+0.25)window.previewLoops++;previous=v.currentTime;});
    });
    const before=requests.filter(url=>url.endsWith('/previews/short.mp4')).length;
    await page.waitForFunction(()=>window.previewLoops>=2,null,{timeout:26000});
    assert.deepEqual(await page.locator('video').evaluate(v=>({same:v===window.loopVideo,loop:v.loop,
      playing:!v.paused&&!v.ended,rate:v.playbackRate})),{same:true,loop:true,playing:true,rate:1});
    assert.equal(requests.filter(url=>url.endsWith('/previews/short.mp4')).length,before,
      'the buffered clip repeats without another media request');
    if(process.env.LUME_UI_SCREENSHOT_DIR){
      await mkdir(process.env.LUME_UI_SCREENSHOT_DIR,{recursive:true});
      await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,'preview-loop.png')});
    }
    await page.evaluate(()=>window.emitPreview({catalogError:'WALLPAPER_CATALOG_OFFLINE:video:WALLPAPER_HTTP_503'}));
    assert.equal(await page.locator('video').evaluate(v=>v===window.loopVideo&&!v.paused),true);
    await page.locator('[data-wallpaper-action="preview"]').click();
    await page.waitForFunction(()=>!document.querySelector('video')?.hasAttribute('src'));
    await page.evaluate(()=>{window.emitPreviewVisibility(false);window.emitPreviewVisibility(true);});
    assert.equal(await page.locator('video').getAttribute('src'),null,'manual pause survives hide and show');
    await page.locator('[data-wallpaper-action="preview"]').click();
    await page.waitForFunction(()=>document.querySelector('video')?.dataset.ready==='true');
    await page.evaluate(()=>window.emitPreviewVisibility(false));
    await page.waitForFunction(()=>!document.querySelector('video')?.hasAttribute('src'));
    assert.equal((await commands()).filter(c=>c==='refreshVideos').length,1,'snapshots do not repeat the refresh');
    await page.goto(`http://127.0.0.1:${server.address().port}/?fresh=1`);
    await page.locator('.HomeHero [data-wallpaper-id="preview-flow"]').click();
    await page.waitForFunction(()=>{const v=document.querySelector('video');return v?.currentTime>0&&!v.paused;});
    assert.equal(await page.evaluate(()=>window.previewState.catalogStatus),'loading',
      'new installs can preview before the image collection finishes loading');
    assert.equal((await commands()).filter(c=>c==='refresh').length,1);
    assert.ok(!(await commands()).includes('refreshVideos'));
    await page.evaluate(()=>{window.freshVideo=document.querySelector('video');
      window.emitPreview({status:'idle',catalogStatus:'ready'});});
    assert.equal(await page.locator('video').evaluate(v=>v===window.freshVideo&&!v.paused),true);
    assert.deepEqual(errors,[]);
  }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
