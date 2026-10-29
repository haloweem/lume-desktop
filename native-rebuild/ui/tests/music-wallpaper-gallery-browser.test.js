import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir,tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('wallpaper gallery browser: native-query owner, 2/3 columns, 16:9 thumbnails, focus and late replies',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:60000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const require=createRequire(import.meta.url);
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const evidence=path.join(tmpdir(),'lume-wallpaper-gallery-'+Date.now());
  const token='a'.repeat(64);
  const html=`<!doctype html><html><meta charset="utf-8"><link rel="stylesheet" href="/music-wallpaper-gallery.css">
    <style>body{margin:0;background:#24352e;color:white;font:14px Arial}main{padding:24px}button{font:inherit}</style>
    <main id="stage"><button id="open">Background</button></main>
    <script type="module">
      import {createMusicBackground,createMusicBackgroundCatalogQuery} from '/music-background.js';
      import {createMusicWallpaperGallery} from '/music-wallpaper-gallery.js';
      const listeners=new Set();
      window.commands=[];
      const bridge={subscribe(fn){listeners.add(fn);return ()=>listeners.delete(fn)},
        musicBackground(action,fields){commands.push({action,...fields});return true}};
      const background=createMusicBackground({send:bridge.musicBackground});
      background.receive({version:1,type:'music-background-state',request:0,revision:0,status:'ready',error:'',media:null});
      window.reply=data=>{background.receive(data);for(const fn of [...listeners])fn(data)};
      window.replyCatalog=(request,patch={})=>reply({version:1,type:'music-background-catalog',request,status:'ready',
        error:'',total:6,selectedId:'fixture-2',items:Array.from({length:6},(_,i)=>({
          id:'fixture-'+i,name:i===0?'A very long wallpaper name that must remain inside its column':'Wallpaper '+i,
          type:i%2?'video':'image',thumbnailUrl:location.origin+'/${token}/media.webp'})),...patch});
      window.gallery=createMusicWallpaperGallery({document,query:createMusicBackgroundCatalogQuery(bridge),
        onSelect:id=>background.select(id)});
      document.querySelector('#open').onclick=()=>gallery.open();
      window.stage=document.querySelector('#stage');
      window.ready=true;
    </script></html>`;
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/')return res.writeHead(200,{'Content-Type':'text/html; charset=utf-8'}).end(html);
    const file=pathname===`/${token}/media.webp`?path.join(ui,'assets/music/forest.webp'):path.resolve(ui,'.'+pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    }catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    await mkdir(evidence,{recursive:true});
    browser=await chromium.launch({headless:true,channel:'msedge'});
    for(const width of [1440,390]) {
      const page=await browser.newPage({viewport:{width,height:900}}),errors=[],requests=[];
      page.setDefaultTimeout(10000);
      page.on('pageerror',error=>errors.push(error.message));
      page.on('request',request=>requests.push(request.url()));
      const origin=`http://127.0.0.1:${server.address().port}`;
      await page.goto(origin);await page.waitForFunction(()=>window.ready);
      await page.locator('#open').click();
      assert.match(await page.locator('[role=status]').innerText(),/正在读取/);
      await page.evaluate(()=>replyCatalog(commands.at(-1).request));
      await page.waitForFunction(()=>[...document.querySelectorAll('dialog img')].every(image=>image.naturalWidth>0));
      const geometry=await page.locator('dialog').evaluate(node=>{
        const grid=node.querySelector('.MusicWallpaperGalleryGrid');
        return {rect:node.getBoundingClientRect().toJSON(),scroll:node.scrollWidth,width:node.clientWidth,
          background:getComputedStyle(node).backgroundColor,blur:getComputedStyle(node).backdropFilter,
          scrollbar:getComputedStyle(node.querySelector('.MusicWallpaperGalleryContent'),'::-webkit-scrollbar').width,
          thumb:getComputedStyle(node.querySelector('.MusicWallpaperGalleryContent'),'::-webkit-scrollbar-thumb').backgroundColor,
          columns:getComputedStyle(grid).gridTemplateColumns.split(' ').length,
          previews:[...node.querySelectorAll('.MusicWallpaperGalleryPreview')].map(n=>n.getBoundingClientRect().toJSON())};
      });
      assert.equal(geometry.columns,width===390?2:3);
      assert.equal(geometry.background,'rgba(0, 0, 0, 0.2)','historical music-frosted-surface tint');
      assert.equal(geometry.blur,'blur(24px)','historical music-frosted-surface blur');
      assert.equal(geometry.scrollbar,'6px');
      assert.equal(geometry.thumb,'rgba(255, 255, 255, 0.18)');
      assert.ok(geometry.rect.x>=0 && geometry.rect.right<=width);
      assert.ok(geometry.rect.y>=0 && geometry.rect.bottom<=900 && geometry.rect.height<=720);
      assert.equal(geometry.scroll,geometry.width);
      for(const rect of geometry.previews)assert.ok(Math.abs(rect.width/rect.height-16/9)<.03);
      assert.match(await page.locator('[data-wallpaper-id=fixture-1]').innerText(),/动态/);
      assert.equal(await page.locator('[data-wallpaper-id=fixture-2]').getAttribute('aria-pressed'),'true');
      const pixels=PNG.sync.read(await page.locator('.MusicWallpaperGalleryPreview').first().screenshot());
      const colors=new Set();
      for(let i=0;i<pixels.data.length;i+=16)colors.add(pixels.data.subarray(i,i+3).toString('hex'));
      assert.ok(colors.size>100,'real thumbnail pixels, not an empty preview');
      await page.screenshot({path:path.join(evidence,`gallery-${width}.png`)});
      await page.locator('[data-wallpaper-id=fixture-5]').focus();await page.keyboard.press('Tab');
      assert.equal(await page.locator('[data-gallery-refresh]').evaluate(node=>node===document.activeElement),true);
      await page.keyboard.press('Shift+Tab');
      assert.equal(await page.locator('[data-wallpaper-id=fixture-5]').evaluate(node=>node===document.activeElement),true);
      await page.keyboard.press('Escape');assert.equal(await page.locator('dialog').count(),0);
      assert.equal(await page.locator('#open').evaluate(node=>node===document.activeElement),true);
      await page.locator('#open').click();
      const stale=await page.evaluate(()=>commands.at(-1).request);
      await page.keyboard.press('Escape');await page.locator('#open').click();
      await page.evaluate(stale=>replyCatalog(stale),stale);
      assert.equal(await page.locator('[data-wallpaper-id]').count(),0);
      await page.evaluate(()=>replyCatalog(commands.at(-1).request,{status:'error',error:'load',items:[],total:0}));
      await page.locator('[role=alert]').waitFor({state:'visible'});
      await page.locator('[data-gallery-refresh]').click();
      await page.evaluate(()=>replyCatalog(commands.at(-1).request,{items:[],total:0}));
      assert.match(await page.locator('[role=status]').innerText(),/还没有本地/);
      await page.locator('[data-gallery-refresh]').click();
      await page.evaluate(()=>replyCatalog(commands.at(-1).request));
      await page.locator('[data-wallpaper-id=fixture-1]').click();
      assert.equal(await page.evaluate(()=>commands.at(-1).action),'select');
      assert.equal(await page.locator('[data-gallery-refresh]').isDisabled(),true);
      await page.keyboard.press('Escape');
      await page.evaluate(token=>reply({version:1,type:'music-background-state',request:commands.at(-1).request,
        revision:1,status:'ready',error:'',media:{id:'music-owned',name:'Fixture',kind:'image',mimeType:'image/webp',
          url:location.origin+'/'+token+'/media.webp'}}),token);
      assert.equal(await page.locator('dialog').count(),0);
      await page.locator('#open').click();await page.evaluate(()=>gallery.setVisible(false));
      assert.equal(await page.locator('dialog').count(),0);
      assert.equal(await page.evaluate(()=>stage===document.querySelector('#stage')),true);
      assert.equal(requests.some(url=>!url.startsWith(origin+'/')),false,'no external request or catalog fetch');
      assert.deepEqual(errors,[]);
      await page.close();
    }
    console.log('Gallery browser evidence: '+evidence);
  } finally {
    await browser?.close();await new Promise(resolve=>server.close(resolve));
  }
});
