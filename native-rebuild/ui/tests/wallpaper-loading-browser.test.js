import test from 'node:test';
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile,mkdir} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('wallpaper loading recovery with production CSP and real image decoding',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:45000,
},async t=>{
  const root=fileURLToPath(new URL('../',import.meta.url));
  const html=await readFile(path.join(root,'index.html'),'utf8');
  const csp=html.match(/<meta\s+http-equiv="Content-Security-Policy"[\s\S]*?>/)[0];
  const server=createServer(async(req,res)=>{
    try{
      const url=new URL(req.url,'http://localhost');
      if(url.pathname==='/')return res.end(`<!doctype html><html><head>${csp}<link rel="stylesheet" href="/app.css"></head><body><div id="fixture" style="position:relative;width:900px;height:520px"></div></body></html>`);
      const file=path.resolve(root,'.'+decodeURIComponent(url.pathname));
      if(!file.startsWith(root))return res.writeHead(403).end();
      res.setHeader('Content-Type',file.endsWith('.html')?'text/html':file.endsWith('.js')?'text/javascript':file.endsWith('.css')?'text/css':'application/octet-stream');
      res.end(await readFile(file));
    }catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
  const browser=await chromium.launch({headless:true,executablePath:process.env.LUME_BROWSER_EXECUTABLE});
  try{
    const page=await browser.newPage({viewport:{width:1000,height:650}});
    const cover=await readFile(path.join(root,'assets/black-texture.webp'));
    const requests=[];let firstFailure=true;
    await page.route('https://static.lumedesktop.com/**',async route=>{
      const url=new URL(route.request().url());requests.push(url.href);
      if(url.pathname.endsWith('/retry.jpg')&&firstFailure){firstFailure=false;return route.fulfill({status:503,body:''});}
      if(url.pathname.includes('hero-thumbnails/slow')){
        await new Promise(resolve=>setTimeout(resolve,1200));
        return route.fulfill({contentType:'image/webp',body:cover}).catch(()=>{});
      }
      if(url.pathname.endsWith('/missing.jpg')&&!url.pathname.includes('hero-thumbnails/'))return route.fulfill({status:404,body:''});
      if(url.pathname.endsWith('/all-broken.jpg'))return route.fulfill({status:404,body:''});
      return route.fulfill({contentType:'image/webp',body:cover});
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    async function show(name,detail=false){
      await page.evaluate(async({name,detail})=>{
        const {mountWallpaperImages}=await import('/wallpaper-images.js');
        const {wallpaperDetail,wallpaperCard}=await import('/pages.js');
        const {initialState}=await import('/state.js');
        window.imageCleanup?.();const root=document.querySelector('#fixture');
        const thumbnail=`https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/${name}.jpg`;
        const item={id:'test',title:name,kind:'video',thumbnail,url:'https://static.lumedesktop.com/video.mp4',bytes:1024,width:1920,height:1080};
        const state=initialState();state.wallpaper.catalog=[item];state.wallpaperSelection={source:'official',id:'test'};
        root.innerHTML=detail?wallpaperDetail(state):wallpaperCard(item,'official',true);
        window.imageCleanup=mountWallpaperImages(root,{timeoutMs:180});
      },{name,detail});
    }
    await t.test('thumbnail remains visible until the slow large cover finishes without being downgraded',async()=>{
      await show('slow',true);
      await page.waitForFunction(()=>document.querySelector('.wallpaper-detail-underlay')?.naturalWidth>0);
      assert.equal(await page.locator('.wallpaper-detail-poster').evaluate(i=>i.naturalWidth),0);
      assert.equal(await page.locator('.wallpaper-preview-video').evaluate(i=>getComputedStyle(i).opacity),'0');
      await page.waitForTimeout(400);
      assert.ok(await page.locator('.wallpaper-detail-poster').evaluate(i=>i.src.includes('/hero-thumbnails/slow.jpg')));
      await page.waitForFunction(()=>document.querySelector('.wallpaper-detail-poster')?.naturalWidth>0);
    });
    await t.test('transient image failure retries successfully',async()=>{
      await show('retry');await page.waitForFunction(()=>document.querySelector('.wallpaper-card img')?.naturalWidth>0);
      assert.equal(await page.locator('.wallpaper-card img').getAttribute('hidden'),null);
    });
    await t.test('missing thumbnail uses alternate CDN cover',async()=>{
      await show('missing');await page.waitForFunction(()=>document.querySelector('.wallpaper-card img')?.naturalWidth>0);
      assert.ok(await page.locator('.wallpaper-card img').evaluate(i=>i.src.includes('/hero-thumbnails/')));
    });
    await t.test('permanent failures stop and revisiting starts fresh',async()=>{
      await show('all-broken');await page.waitForFunction(()=>document.querySelector('.wallpaper-card img')?.hidden);
      const before=requests.filter(s=>s.includes('all-broken')).length;
      assert.ok(before<=3);await show('all-broken');
      await page.waitForFunction(()=>document.querySelector('.wallpaper-card img')?.hidden);
      assert.ok(requests.filter(s=>s.includes('all-broken')).length>before);
    });
    await t.test('removed surfaces do not retry later or request COS',async()=>{
      await show('slow');const before=requests.length;
      await page.evaluate(()=>{window.imageCleanup();document.querySelector('#fixture').innerHTML='';});
      await page.waitForTimeout(400);assert.equal(requests.length,before);
      assert.ok(requests.every(s=>new URL(s).hostname==='static.lumedesktop.com'));
    });
    await show('slow',true);
    await page.waitForFunction(()=>document.querySelector('.wallpaper-detail-underlay')?.naturalWidth>0);
    const evidence=path.resolve(root,'../../build-artifacts/wallpaper-loading');await mkdir(evidence,{recursive:true});
    await page.screenshot({path:path.join(evidence,'progressive-detail.png')});
    if(process.env.LUME_UI_TEST_VIDEO)await t.test('full app buffers beyond 12 seconds, plays without downloading and switches to local after download',async()=>{
      const app=await browser.newPage({viewport:{width:1100,height:720}});
      const video=await readFile(process.env.LUME_UI_TEST_VIDEO);
      const errors=[];app.on('pageerror',error=>errors.push(error.message));
      let remoteVideoRequests=0;
      await app.route(/^https:\/\/(static\.lumedesktop\.com|lume-wallpaper\.local)\//,async route=>{
        const url=new URL(route.request().url()),isVideo=url.pathname.endsWith('.mp4');
        if(isVideo&&url.hostname==='static.lumedesktop.com'){
          remoteVideoRequests++;
          await new Promise(resolve=>setTimeout(resolve,13000));
        }
        return route.fulfill({contentType:isVideo?'video/mp4':'image/webp',body:isVideo?video:cover});
      });
      await app.addInitScript(()=>{
        const listeners=new Set();
        const item={id:'flow',title:'Preview flow',kind:'video',thumbnail:'https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/flow.jpg',
          url:'https://static.lumedesktop.com/preview.mp4',width:160,height:90,bytes:1024,libraryId:''};
        window.flow={version:1,type:'wallpaper-state',revision:0,status:'idle',error:'',loadStatus:'fresh',
          playerAvailable:true,catalogStatus:'ready',catalogError:'',catalog:[item],library:[],displays:[],
          download:{id:'',status:'idle',received:0,total:0,error:''},pauseFullscreen:false,likedIds:[]};
        window.emitFlow=fields=>{window.flow={...window.flow,...fields,revision:window.flow.revision+1};
          for(const fn of listeners)fn({data:window.flow});};
        window.chrome={webview:{addEventListener:(type,fn)=>listeners.add(fn),removeEventListener:(type,fn)=>listeners.delete(fn),
          postMessage(command){
            if(command==='v1|ready'||command?.scope==='wallpaper'&&['snapshot','preview','endPreview'].includes(command.action))
              queueMicrotask(()=>window.emitFlow({}));
          }}};
      });
      await app.goto(`http://127.0.0.1:${server.address().port}/index.html`);
      await app.locator('.HomeHero [data-wallpaper-id="flow"]').click();
      await app.waitForFunction(()=>document.querySelector('.wallpaper-detail-underlay')?.naturalWidth>0);
      assert.equal(await app.locator('.wallpaper-preview-video').evaluate(v=>getComputedStyle(v).opacity),'0');
      await app.waitForFunction(()=>document.querySelector('.wallpaper-preview-video')?.dataset.ready==='true');
      const before=await app.locator('.wallpaper-preview-video').evaluate(v=>{window.flowVideo=v;return {src:v.src,width:v.videoWidth};});
      assert.equal(before.width,160);assert.match(before.src,/static\.lumedesktop/);
      await app.waitForFunction(()=>document.querySelector('.wallpaper-preview-video')?.currentTime>0.1);
      assert.equal(await app.evaluate(()=>window.flow.library.length),0);
      assert.equal(remoteVideoRequests,1);
      assert.equal(await app.getByText('预览播放失败，请检查网络或媒体文件',{exact:true}).count(),0);
      await app.evaluate(()=>window.emitFlow({download:{id:'flow',status:'running',received:256,total:1024,error:''}}));
      assert.equal(await app.locator('.wallpaper-preview-video').evaluate(v=>v===window.flowVideo),true);
      await app.evaluate(()=>{
        const item=window.flow.catalog[0],url='https://lume-wallpaper.local/downloaded.mp4';
        const {libraryId,...cached}=item;
        window.emitFlow({catalog:[{...item,libraryId:'flow',url}],library:[{...cached,url,missing:false,official:true}],
          download:{id:'flow',status:'complete',received:1024,total:1024,error:''}});
      });
      await app.waitForFunction(()=>{const video=document.querySelector('.wallpaper-preview-video');
        return video?.dataset.ready==='true'&&video.src.includes('lume-wallpaper.local');});
      assert.deepEqual(errors,[]);
      await app.screenshot({path:path.join(evidence,'app-preview-downloaded.png')});
      await app.close();
    });
  }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
});
