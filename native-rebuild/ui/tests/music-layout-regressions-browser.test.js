import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('Gallery font covers title, artist and lyrics',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:60000},async()=>{
  const ui=fileURLToPath(new URL('..',import.meta.url)),require=createRequire(import.meta.url);
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    if(pathname==='/__layout')return res.writeHead(200,{'Content-Type':'text/html'}).end(
      '<!doctype html><html><body style="margin:0"><main id="host" style="position:fixed;inset:0"></main><style>iframe{width:100%;height:100%;border:0}</style></body></html>');
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(path.resolve(ui)+path.sep)||/\/(?:src|source|licenses)\//.test(pathname))return res.writeHead(404).end();
    try{
      const bytes=await readFile(file);
      res.writeHead(200,{'Content-Type':({'.js':'text/javascript','.css':'text/css','.html':'text/html','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream'}).end(bytes);
    }catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try{
    const origin=`http://127.0.0.1:${server.address().port}`;
    browser=await chromium.launch({headless:true,channel:'msedge'});
    const page=await browser.newPage({viewport:{width:1200,height:800}}),errors=[],external=[];
    page.on('pageerror',error=>errors.push(error.message));
    page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
    await page.route('**/*',route=>{
      const url=route.request().url();
      if(url.startsWith(origin+'/')||url.startsWith('data:'))return route.continue();
      external.push(url);return route.abort();
    });
    await page.goto(origin+'/__layout');
    await page.evaluate(async()=>{
      window.fixture={lines:[],current:{id:'layout-fixture',title:'Gallery title',artist:'Layout artist',
        artwork:'assets/music/forest.webp'},currentTime:0,duration:60,playing:false,visible:true};
      window.controller=(await import('/music-visualizer.js')).mountMusicVisualizer(document.getElementById('host'));
    });
    await page.evaluate(async()=>{await controller.update(fixture,'gallery');});
    await page.locator('.MusicGalleryTitle').waitFor();
    const fonts=()=>page.evaluate(()=>Object.fromEntries(
      ['.MusicGalleryTitle','.MusicGalleryArtist','.MusicGalleryBoundary'].map(selector=>
        [selector,getComputedStyle(document.querySelector(selector)).fontFamily])));
    const initial=await fonts();
    const size=()=>page.locator('.MusicGalleryTitle').evaluate(node=>getComputedStyle(node).fontSize);
    const initialSize=await size();
    assert.equal(initialSize,'69.6px','Gallery title follows the historical clamp formula at 1200px');
    await page.evaluate(async()=>{
      window.galleryRoot=document.querySelector('.MusicGallery');
      window.preferences={version:2,mode:'gallery',fontFamily:'Courier New',animationIntensity:'normal',
        lyricsFontColor:'#ffffff',lyricsKaraokeFillColor:'#ffffff',
        sonnetColors:{lyricsFontColor:'#ffffff',karaokeFillColor:'#38bdf8',glowColor:'#60a5fa'}};
      await controller.update({...fixture,preferences},'gallery');
    });
    for(const [selector,font] of Object.entries(await fonts()))assert.match(font,/^"Courier New",/,selector);
    assert.equal(await size(),initialSize,'font preference does not change the A5 font-size rules');
    await page.evaluate(async()=>{await controller.update({...fixture,preferences:{...preferences,fontFamily:''}},'gallery');});
    assert.deepEqual(await fonts(),initial);
    assert.equal(await page.evaluate(()=>galleryRoot===document.querySelector('.MusicGallery')),true);
    await page.evaluate(()=>controller.dispose());
    assert.equal(await page.locator('#host > *').count(),0);
    assert.deepEqual(errors,[]);
    assert.deepEqual(external,[]);
  }finally{
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
  }
});
