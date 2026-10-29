import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';

test('desktop entry uses the bundled static forest independently of home posters and Escape preserves playback',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:60000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const require=createRequire(import.meta.url);
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=require(path.join(runtime,'playwright')),{PNG}=require(path.join(runtime,'pngjs'));
  const png=new PNG({width:64,height:64});
  for(let i=0;i<png.data.length;i+=4){png.data[i]=18;png.data[i+1]=180;png.data[i+2]=192;png.data[i+3]=255;}
  const bytes=PNG.sync.write(png),evidence=path.resolve(ui,'../../build-artifacts/native-rebuild/music-home-background');
  const server=createServer(async(req,res)=>{
    if(req.url==='/favicon.ico')return res.writeHead(204).end();
    const file=path.resolve(ui,'.'+new URL(req.url,'http://localhost').pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try{res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');res.end(await readFile(file));}
    catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    await mkdir(evidence,{recursive:true});
    browser=await chromium.launch({headless:true,channel:'msedge'});
    const page=await browser.newPage({viewport:{width:1440,height:900}}),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.route('https://**/*',route=>route.fulfill({status:200,
      headers:{'content-type':'image/png','access-control-allow-origin':'*'},body:bytes}));
    await page.addInitScript(()=>{
      const listeners=new Set();
      window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
      window.chrome={webview:{addEventListener:(_,fn)=>listeners.add(fn),removeEventListener:(_,fn)=>listeners.delete(fn),
        postMessage:command=>{
          harness.commands.push(command);
          if(command?.scope==='music-background'&&command.action==='snapshot')
            queueMicrotask(()=>harness.receive({version:1,type:'music-background-state',revision:0,request:0,status:'ready',error:'',media:null}));
        }}};
    });
    await page.addInitScript(installMusicPreferencesHarness);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
    await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
    await page.evaluate(async()=>{
      const catalog=['floating-in-space-by-visualdon','forest-2'].map(id=>({id,title:id,kind:'video',
        thumbnail:`https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/${id}.webp`,
        url:`https://static.lumedesktop.com/lume/wallpapers/official/${id}.mp4`,
        width:1920,height:1080,bytes:1024,libraryId:''}));
      harness.receive({version:1,type:'wallpaper-state',revision:1,status:'idle',error:'',loadStatus:'loaded',
        playerAvailable:true,catalogStatus:'ready',catalogError:'',catalog,library:[],likedIds:[],displays:[],
        download:{id:'',status:'idle',received:0,total:0,error:''},pauseFullscreen:false});
      const {initialMusicState}=await import('./music.js'),{query,nav,lyrics,...native}=initialMusicState();
      const track={id:'fixture',title:'Desktop fixture',artist:'Lume',album:'',artwork:'',source:'C:/fixture.wav',duration:60};
      harness.receive({...native,version:1,type:'music-state',session:1,revision:1,pageOpen:true,pageVisible:true,
        status:'paused',current:track,queue:[track],timeline:{position:3,duration:60},
        capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true}});
    });
    const firstHero=await page.locator('.HomeHero img').getAttribute('src');
    assert.match(firstHero,/forest-2\.webp$/);
    await page.locator('[data-titlebar-music]').click();
    await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundId==='default-forest'&&
      document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
    const pixel=await page.locator('.MusicBackground').evaluate(node=>
      [...node.getContext('2d').getImageData(node.width/2,node.height/2,1,1).data]);
    assert.equal(pixel[3],255,'bundled forest paints an opaque background');
    assert.notDeepEqual(pixel,[18,180,192,255],'home poster must not replace the bundled default');
    await page.locator('[data-music-open-player]').click();
    await page.evaluate(()=>harness.receive({version:1,type:'window',fullscreen:true}));
    await page.locator('[data-music-root]').evaluate(node=>{node.tabIndex=-1;node.focus();});
    await page.keyboard.press('Escape');
    assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'player');
    assert.equal(await page.evaluate(()=>harness.commands.filter(value=>value==='v1|window|maximize').length),1);
    await page.evaluate(()=>harness.receive({version:1,type:'window',fullscreen:false}));
    await page.keyboard.press('Escape');
    assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'home');
    assert.deepEqual(await page.evaluate(()=>harness.commands.filter(command=>
      command?.scope==='wallpaper'&&['apply','applyAll','stop','stopAll','import'].includes(command.action))),[]);
    assert.equal(await page.locator('.MusicBackground').count(),1);
    assert.deepEqual(errors,[]);
    await page.screenshot({path:path.join(evidence,'desktop-default-home.png')});
  } finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
