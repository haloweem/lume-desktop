import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';

test('production music background confirms native selection, retains failed media and restores across navigation',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const require=createRequire(import.meta.url),{chromium}=require(path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const picture=new PNG({width:64,height:64});
  for(let i=0;i<picture.data.length;i+=4){picture.data[i]=18;picture.data[i+1]=180;picture.data[i+2]=192;picture.data[i+3]=255;}
  const bytes=PNG.sync.write(picture);
  const evidence=path.resolve(ui,`../../build-artifacts/native-rebuild/music-background-${Date.now()}`);
  await mkdir(evidence,{recursive:true});
  const server=createServer(async(req,res)=>{
    if(req.url==='/favicon.ico')return res.writeHead(204).end();
    const file=path.resolve(ui,'.'+new URL(req.url,'http://localhost').pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try{res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');res.end(await readFile(file));}
    catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;const results=[],focusFailures=[];
  try {
    browser=await chromium.launch({headless:true,channel:'msedge'});
    for(const [width,height] of [[1440,900],[390,844]]) {
      const page=await browser.newPage({viewport:{width,height}}),errors=[];
      page.setDefaultTimeout(10000);
      page.on('pageerror',error=>errors.push(error.message));
      await page.route('https://lume-music.local/**',route=>{
        if(route.request().url().includes('broken'))return route.abort('failed');
        return route.fulfill({
          status:200,
          headers:{'content-type':'image/png','access-control-allow-origin':'*'},body:bytes
        });
      });
      await page.addInitScript(()=>{
        const listeners=new Set();
        window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
        window.backgroundState={version:1,type:'music-background-state',revision:0,request:0,status:'ready',error:'',media:null};
        window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),removeEventListener:(_,listener)=>listeners.delete(listener),
          postMessage:command=>{harness.commands.push(command);
            if(command?.scope==='music-background'&&command.action==='snapshot')queueMicrotask(()=>harness.receive({...backgroundState,request:0}));}}};
      });
      await page.addInitScript(installMusicPreferencesHarness);
      await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
      await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
      await page.evaluate(async()=>{
        const {initialMusicState}=await import('./music.js'),{query,nav,lyrics,...native}=initialMusicState();
        const track={id:'bg-fixture',title:'Background fixture',artist:'Lume',album:'',artwork:'',source:'C:/fixture.wav',duration:60};
        window.musicSnapshot={...native,version:1,type:'music-state',session:1,revision:1,pageOpen:true,pageVisible:true,
          status:'paused',current:track,queue:[track],timeline:{position:3,duration:60},
          capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true}};
        harness.receive(musicSnapshot);harness.receive({version:1,type:'navigate',page:'music'});
      });
      await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
      await page.evaluate(()=>window.backgroundCanvas=document.querySelector('.MusicBackground'));
      await page.locator('[data-music-open-player]').click();
      await page.locator('[data-music-action="lyrics"]').click();
      const trigger=page.locator('[data-music-background-trigger]');
      const choose=page.locator('[data-music-background-action="choose"]');
      await trigger.click();
      await choose.click();assert.equal(await choose.isDisabled(),true);
      const command=await page.evaluate(()=>harness.commands.find(command=>command?.scope==='music-background'&&command.action==='choose'));
      assert.ok(command);assert.deepEqual(Object.keys(command).sort(),['action','request','revision','scope','version']);
      await page.evaluate(request=>{
        backgroundState={...backgroundState,revision:1,request,media:{id:'selected-1',name:'selected.png',kind:'image',mimeType:'image/png',url:'https://lume-music.local/selected-1.png'}};
        harness.receive(backgroundState);
      },command.request);
      await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundId==='selected-1'&&document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
      await page.waitForFunction(()=>{
        const canvas=document.querySelector('.MusicBackground'),context=canvas?.getContext('2d');
        if(!canvas||!context)return false;
        const pixel=context.getImageData(canvas.width/2,canvas.height/2,1,1).data;
        return pixel[0]===18&&pixel[1]===180&&pixel[2]===192&&pixel[3]===255;
      });
      const pixel=await page.locator('.MusicBackground').evaluate(canvas=>Array.from(canvas.getContext('2d').getImageData(canvas.width/2,canvas.height/2,1,1).data));
      assert.deepEqual(pixel,[18,180,192,255]);
      assert.equal(await page.evaluate(()=>backgroundCanvas===document.querySelector('.MusicBackground')),true,'music home/player share one background owner');
      await page.screenshot({path:path.join(evidence,`selected-${width}.png`)});
      await trigger.click();await choose.click();
      await page.evaluate(()=>{
        const command=harness.commands.filter(command=>command?.scope==='music-background'&&command.action==='choose').at(-1);
        backgroundState={...backgroundState,request:command.request,revision:2,media:{...backgroundState.media,id:'broken',url:'https://lume-music.local/broken.png'}};
        harness.receive(backgroundState);
      });
      await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='error');
      assert.equal(await page.locator('.MusicBackground').getAttribute('data-music-background-id'),'selected-1');
      await trigger.click();await page.locator('[data-music-background-action="clear"]').click();
      await page.evaluate(()=>{
        const command=harness.commands.find(command=>command?.scope==='music-background'&&command.action==='clear');
        backgroundState={...backgroundState,request:command.request,revision:3,media:null};harness.receive(backgroundState);
      });
      await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundId==='default-forest'&&document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
      // Exercise the actual production settings menu, not a standalone gallery harness.
      const menu=page.locator('[data-music-background-menu]');
      const local=page.locator('[data-music-background-action="gallery"]');
      await trigger.focus();await page.keyboard.press('ArrowDown');
      assert.equal(await trigger.getAttribute('aria-expanded'),'true');
      assert.equal(await choose.evaluate(node=>node===document.activeElement),true);
      await page.keyboard.press('ArrowDown');
      assert.equal(await local.evaluate(node=>node===document.activeElement),true);
      await page.keyboard.press('Home');
      assert.equal(await choose.evaluate(node=>node===document.activeElement),true);
      await page.keyboard.press('End');
      assert.equal(await local.evaluate(node=>node===document.activeElement),true);
      await page.keyboard.press('Escape');
      assert.equal(await menu.isHidden(),true);
      assert.equal(await trigger.evaluate(node=>node===document.activeElement),true);
      await trigger.click();
      // A genuine outside pointer dismisses the menu without choosing a source.
      await page.locator('label[for=music-font-quick]').click();
      assert.equal(await menu.isHidden(),true);
      const beforeGallery=await page.evaluate(()=>{
        window.galleryStage=document.querySelector('[data-music-stage]');
        window.galleryBackground=document.querySelector('.MusicBackground');
        return harness.commands.filter(command=>command?.scope==='music').length;
      });
      await trigger.click();await local.click();
      await page.locator('dialog.MusicWallpaperGallery').waitFor();
      const firstCatalog=await page.evaluate(()=>harness.commands.filter(command=>
        command?.scope==='music-background'&&command.action==='catalog').at(-1));
      assert.deepEqual(Object.keys(firstCatalog).sort(),['action','limit','offset','request','scope','version']);
      assert.equal(firstCatalog.offset,0);assert.equal(firstCatalog.limit,120);
      await page.evaluate(request=>harness.receive({version:1,type:'music-background-catalog',request,
        status:'busy',error:'',total:0,items:[],selectedId:''}),firstCatalog.request);
      assert.match(await page.locator('dialog [role=status]').innerText(),/正在读取/);
      await page.evaluate(request=>harness.receive({version:1,type:'music-background-catalog',request,
        status:'ready',error:'',total:2,selectedId:'local-first',items:[
          {id:'local-first',name:'Downloaded fixture',type:'image',thumbnailUrl:''},
          {id:'local-second',name:'Second fixture',type:'video',thumbnailUrl:''}]}),firstCatalog.request);
      assert.equal(await page.locator('[data-wallpaper-id=local-first]').getAttribute('aria-pressed'),'true');
      await page.locator('[data-wallpaper-id=local-second]').click();
      const select=await page.evaluate(()=>harness.commands.filter(command=>
        command?.scope==='music-background'&&command.action==='select').at(-1));
      assert.deepEqual(select,{version:1,scope:'music-background',action:'select',
        request:select.request,revision:3,id:'local-second'});
      assert.equal(await page.locator('dialog.MusicWallpaperGallery').count(),1,'await native confirmation before closing');
      await page.evaluate(request=>{
        backgroundState={...backgroundState,request,revision:4,media:{id:'gallery-selected',name:'Gallery fixture',
          kind:'image',mimeType:'image/png',url:'https://lume-music.local/gallery-selected.png'}};
        harness.receive(backgroundState);
      },select.request);
      await page.waitForFunction(()=>!document.querySelector('dialog.MusicWallpaperGallery'));
      await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundId==='gallery-selected'&&
        document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
      assert.equal(await page.evaluate(()=>galleryStage===document.querySelector('[data-music-stage]')&&
        galleryBackground===document.querySelector('.MusicBackground')),true,'gallery selection preserves stage and compositor');
      assert.equal(await page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music').length),beforeGallery,
        'catalog/select cannot issue playback or lyrics commands');
      if(!await trigger.evaluate(node=>node===document.activeElement))
        focusFailures.push({width,active:await page.evaluate(()=>document.activeElement?.tagName)});
      // Navigation must dispose an outstanding query and ignore its eventual reply.
      await trigger.click();await local.click();
      const abandoned=await page.evaluate(()=>harness.commands.filter(command=>command?.action==='catalog').at(-1).request);
      await page.evaluate(()=>harness.receive({version:1,type:'navigate',page:'clock'}));
      await page.waitForFunction(()=>!document.querySelector('[data-music-root]'));
      assert.equal(await page.locator('dialog.MusicWallpaperGallery').count(),0);
      await page.evaluate(request=>harness.receive({version:1,type:'music-background-catalog',request,status:'ready',
        error:'',total:1,selectedId:'',items:[{id:'late-fixture',name:'Late fixture',type:'image',thumbnailUrl:''}]}),abandoned);
      assert.equal(await page.locator('dialog.MusicWallpaperGallery').count(),0);
      await page.locator('[data-titlebar-music]').click();
      await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
      await page.locator('[data-titlebar-music]').click();
      assert.equal(await page.locator('.MusicBackground').count(),0);
      await page.locator('[data-titlebar-music]').click();
      await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
      assert.equal(await page.locator('.MusicBackground').count(),1);
      const wallpaperMutations=await page.evaluate(()=>harness.commands.filter(command=>command?.scope==='wallpaper'&&['apply','stop','import'].includes(command.action)));
      assert.deepEqual(wallpaperMutations,[]);assert.deepEqual(errors,[]);
      results.push({width,height,pixel,failedMediaRetained:true,defaultRestored:true,nativePathsInCommands:false,
        wallpaperMutations:0,gallerySelection:true,stagePreserved:true,menuKeyboard:true,navigationClosesGallery:true});
      await page.close();
    }
    await writeFile(path.join(evidence,'production-report.json'),JSON.stringify(results,null,2)+'\n');
    console.log('Production background evidence: '+evidence);
    assert.deepEqual(focusFailures,[],'modal must return focus to the visible source trigger');
  } finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
