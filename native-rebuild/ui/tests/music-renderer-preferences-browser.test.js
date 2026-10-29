import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('bounded preferences paint all available renderers in place and reset in Edge',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:240000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url))),require=createRequire(import.meta.url);
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const pixelsChanged=(a,b)=>{
    const x=PNG.sync.read(a),y=PNG.sync.read(b);let count=0;
    for(let i=0;i<x.data.length;i+=4)if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>45)count++;
    return count;
  };
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    if(pathname==='/__preferences')return res.writeHead(200,{'Content-Type':'text/html'}).end(
      '<!doctype html><html style="color-scheme:dark"><head><meta charset="utf-8"><link rel="stylesheet" href="/music-visualizers/gallery.css"></head><body style="margin:0;background:#18251d"><div id="host" style="position:fixed;inset:0"></div><style>iframe{width:100%;height:100%;display:block;border:0}</style></body></html>');
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    if(/\/(?:src|source|licenses)\//.test(pathname))return res.writeHead(404).end();
    try{res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');res.end(await readFile(file));}
    catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try{
    browser=await chromium.launch({headless:true,channel:'msedge'});
    for(const width of [1280,390])for(const mode of ['partita','fume','sonnet','gallery','3d']){
      const page=await browser.newPage({viewport:{width,height:width===390?844:800}}),errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
      await page.addInitScript(()=>{
        window.gpuCanvases=new Set();
        const original=HTMLCanvasElement.prototype.getContext;
        HTMLCanvasElement.prototype.getContext=function(type,...args){
          const result=original.call(this,type,...args);
          if(result&&/webgl/.test(type))window.gpuCanvases.add(this);
          return result;
        };
      });
      await page.goto(`http://127.0.0.1:${server.address().port}/__preferences`);
      await page.evaluate(async mode=>{
        window.defaults={version:2,mode,fontFamily:'',animationIntensity:'normal',lyricsFontColor:'#ffffff',lyricsKaraokeFillColor:'#ffffff',
          sonnetColors:{lyricsFontColor:'#ffffff',karaokeFillColor:'#38bdf8',glowColor:'#60a5fa'}};
        window.fixture={lines:Array.from({length:12},(_,i)=>({startTime:i*6,endTime:i*6+6,
          fullText:'A melody through the night',translation:'',words:[]})),
          current:{id:'preference-fixture',title:'Theme test',artist:'Lume',album:'Reference',artwork:'assets/music/forest.webp'},
          currentTime:25,activeLineIndex:4,duration:72,playing:false,visible:true};
        const host=document.getElementById('host');
        if(mode==='3d')window.controller=(await import('/music-visualizers/three-d.js')).mountThreeD(host,fixture);
        else if(mode==='gallery')window.controller=(await import('/music-visualizers/gallery.js')).mountGallery(host,fixture);
        else window.controller=(await import('/music-visualizers/reference-host.js')).mountReference(host,fixture,mode);
        window.firstChild=host.firstElementChild;
      },mode);
      const reference=!['3d','gallery'].includes(mode);
      const surface=reference?page.frames().find(frame=>frame!==page.mainFrame()):page.mainFrame();
      if(reference)await surface.waitForSelector('#reference-stage[data-reference-state="ready"]');
      if(mode==='sonnet')await surface.waitForSelector('canvas');
      const settle=()=>page.evaluate(async()=>{for(let i=0;i<80;i++)await new Promise(requestAnimationFrame);});
      await settle();
      await surface.evaluate(()=>{
        window.initialDocument=document;
        window.initialCanvases=[...document.querySelectorAll('canvas')].filter(canvas=>gpuCanvases.has(canvas));
        window.initialGpuCount=window.gpuCanvases.size;
      });
      const original=await page.screenshot();
      const change=async preferences=>{
        await page.evaluate(preferences=>controller.update({...fixture,preferences}),preferences);
        await settle();
        assert.equal(await page.evaluate(()=>firstChild===document.getElementById('host').firstElementChild),true,`${mode}: host retained`);
        assert.equal(await surface.evaluate(()=>initialDocument===document&&initialGpuCount===gpuCanvases.size&&
          initialCanvases.every(canvas=>canvas.isConnected)),true,`${mode}: document and GPU canvases/contexts retained`);
      };
      const defaults=await page.evaluate(()=>window.defaults);
      const preferences={...defaults,fontFamily:'Courier New',animationIntensity:'calm',lyricsFontColor:'#ff2244',lyricsKaraokeFillColor:'#22ff88',
        sonnetColors:{lyricsFontColor:'#ffaabb',karaokeFillColor:'#22ff88',glowColor:'#ccffee'}};
      await change(preferences);
      const changed=await page.screenshot();
      assert.ok(pixelsChanged(original,changed)>100,`${mode}/${width}: preference paint effect`);
      if(mode==='gallery')assert.match(await page.locator('.MusicGalleryFillText').first().evaluate(node=>node.style.backgroundImage),/34, 255, 136/);
      const rootRenders=reference?await surface.locator('#reference-stage').getAttribute('data-reference-renders'):null;
      await page.evaluate(async preferences=>{
        for(let i=0;i<8;i++){controller.update({...fixture,preferences:structuredClone(preferences)});await new Promise(requestAnimationFrame);}
        controller.update(fixture);
      },preferences);
      if(reference)assert.equal(await surface.locator('#reference-stage').getAttribute('data-reference-renders'),rootRenders,'equal/absent preferences do not root-render');
      await change(defaults);
      const reset=await page.screenshot();
      assert.ok(pixelsChanged(changed,reset)>100,`${mode}/${width}: reset paint effect`);
      // Exercise asynchronous icon replacement and disposal while a new theme is loading.
      if(mode==='sonnet'){
        await page.evaluate(preferences=>{
          controller.update({...fixture,preferences});
          controller.update({...fixture,preferences:{...preferences,lyricsFontColor:'#3344ff'}});
        },preferences);
        await settle();
      }
      await page.evaluate(()=>controller.dispose());
      assert.equal(await page.locator('#host > *').count(),0);
      assert.deepEqual(errors,[],`${mode}/${width}`);
      console.log(`${mode}/${width}: changed ${pixelsChanged(original,changed)} pixels; reset ${pixelsChanged(changed,reset)} pixels; retained runtime`);
      await page.close();
    }
  }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
