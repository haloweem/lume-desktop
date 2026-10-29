import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
const evidence=path.resolve(ui,'../../build-artifacts/native-rebuild/music-material-verification');
const require=createRequire(import.meta.url);
const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const enabled=process.env.LUME_UI_BROWSER_TESTS==='1';
const html=`<!doctype html><html><head><meta charset="utf-8">
<link rel="stylesheet" href="/music-visualizers/gallery.css">
<style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:#09090b}
#background,#stage,#effects{position:absolute;inset:0;width:100%;height:100%}
#stage iframe{width:100%;height:100%;border:0;display:block}
#bar{position:absolute;bottom:32px;left:50%;transform:translateX(-50%);width:clamp(980px,68vw,1240px);
max-width:calc(100% - 32px);height:clamp(104px,6.2vw,112px);border-radius:999px;background:transparent}
#bar button{position:absolute;z-index:1;left:48%;top:40%;border:0;background:transparent;color:white;font:24px Arial}
</style></head><body><canvas id="background"></canvas><div id="effects"></div><div id="stage" data-music-stage></div><footer id="bar"><button aria-label="Play">&#9654;</button></footer></body></html>`;

async function harness(run){
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    if(pathname==='/fixture-strict'){
      const index=await readFile(path.join(ui,'index.html'),'utf8');
      const policy=index.match(/http-equiv="Content-Security-Policy"\s+content="([^"]+)"/)[1];
      return res.writeHead(200,{'Content-Type':'text/html','Content-Security-Policy':policy}).end(html);
    }
    if(pathname==='/fixture')return res.writeHead(200,{'Content-Type':'text/html'}).end(html);
    if(pathname==='/child')return res.writeHead(200,{'Content-Type':'text/html'}).end(
      '<!doctype html><html><body style="margin:0"><div data-lumenic-liquid-glass-scene style="position:absolute;inset:0"><div id="words" style="position:absolute;left:18px;top:34px;color:rgb(255,0,0);font:700 40px Arial">REAL IFRAME LYRICS</div><canvas data-lumenic-liquid-glass-scene-lyrics id="ink" width="100" height="100" style="position:absolute;left:20px;top:95px;width:100px;height:100px"></canvas></div></body></html>');
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(ui+path.sep)||/\/(?:source|src|licenses)\//.test(pathname))return res.writeHead(404).end();
    try{
      res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp','.png':'image/png'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    }catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try{
    browser=await chromium.launch({headless:true,channel:'msedge'});
    const base=`http://127.0.0.1:${server.address().port}`;
    await mkdir(evidence,{recursive:true});
    await run({browser,base,PNG});
  }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
}
async function setup(page,base){
  await page.route('**/*',route=>{
    const url=route.request().url();
    return url.startsWith(base)||url.startsWith('data:')||url.startsWith('blob:')?route.continue():route.abort();
  });
  await page.goto(`${base}/fixture`);
  await page.evaluate(async()=>{
    window.materialModule=await import('/music-material.js');
    const source=document.getElementById('background');
    source.width=innerWidth;source.height=innerHeight;
    const ctx=source.getContext('2d');
    for(let y=0;y<source.height;y+=24)for(let x=0;x<source.width;x+=24){
      ctx.fillStyle=['#d5204a','#17ae91','#dedfe5','#202327'][(x/24+y/24)%4];
      ctx.fillRect(x,y,24,24);
    }
    source.dataset.glassFrameVersion='1';
    window.descriptor={canvas:source,media:null,mediaSource:null};
    window.materialErrors=[];
    window.mount=()=>materialModule.mountMusicMaterial(document.getElementById('bar'),{
      getScene:()=>descriptor,root:document.body,visible:true,onError:error=>materialErrors.push(error.message),
    });
    window.material=mount();
  });
  await page.waitForFunction(()=>material.snapshot().state==='active');
}
const settle=page=>page.evaluate(async()=>{for(let i=0;i<5;i++)await new Promise(requestAnimationFrame);});
test('Lumiere original runtime draws lyrics, seeks, hides, resumes and releases',
  {skip:!enabled,timeout:120000},()=>harness(async({browser,base})=>{
    const page=await browser.newPage({viewport:{width:1280,height:800},deviceScaleFactor:1.5}),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.goto(`${base}/fixture-strict`);
    const parity=await page.evaluate(async()=>{
      const {lumiereTheme}=await import('/music-visualizers/lumiere.js');
      const {DEFAULT_THEME,prepareLumiereLines}=await import('/music-visualizers/lumiere/runtime.js');
      const theme=lumiereTheme({lyricsFontColor:'#ff0000',lyricsKaraokeFillColor:'#00ff00'});
      const words=[{text:'original',startTime:2,endTime:2.8}];
      const prepared=prepareLumiereLines([{startTime:0,endTime:4,fullText:'Hello world',words:[]},
        {startTime:2,endTime:3,fullText:'original',words}]);
      return {theme,expected:DEFAULT_THEME,words:prepared[0].words,
        preserved:prepared[1].words===words,hints:!!prepared[0].renderHints};
    });
    assert.deepEqual(parity.theme,parity.expected,'original default colors; generic karaoke colors never override Lumiere');
    assert.deepEqual(parity.words.map(word=>word.text),['Hello','world'],'upstream word segmentation');
    assert.ok(Math.abs(parity.words.at(-1).endTime-3.6)<1e-9,'upstream leaves the final 10% for the line tail');
    assert.equal(parity.preserved,true);assert.equal(parity.hints,true);
    const details=await page.evaluate(async()=>{
      const {createLumiereSongGate}=await import('/music-visualizers/lumiere-song.js');
      const {mountLumiereSubtitles}=await import('/music-visualizers/lumiere-subtitles.js');
      const {DEFAULT_THEME,prepareLumiereLines}=await import('/music-visualizers/lumiere/runtime.js');
      let callback,cleared=0;
      const testWindow={performance:{now:()=>0},setTimeout:fn=>{callback=fn;return 1;},clearTimeout:()=>{cleared++;}};
      const first=[{startTime:0,endTime:5,fullText:'First',words:[]}];
      const second=[{startTime:0,endTime:5,fullText:'Second',translation:'第二句',words:[]}];
      const gate=createLumiereSongGate(testWindow,()=>{});
      gate.resolve('one',first,0);
      const held=gate.resolve('two',[],0).seed;
      const arrived=gate.resolve('two',second,1).seed;
      gate.resolve('three',[],0);callback();
      const instrumental=gate.resolve('three',[],3).isInstrumental;
      gate.dispose();
      const holder=document.createElement('div');document.body.append(holder);
      const subtitles=mountLumiereSubtitles(holder);
      subtitles.update(prepareLumiereLines(second),1,DEFAULT_THEME);
      const translated=holder.querySelector('[data-lumiere-subtitle="translation"]')?.textContent;
      subtitles.update(prepareLumiereLines(second),10,DEFAULT_THEME);
      const creditsHidden=holder.querySelector('[data-lumiere-subtitles]').hidden;
      subtitles.dispose();holder.remove();
      return {held,arrived,instrumental,cleared,translated,creditsHidden};
    });
    assert.equal(details.held,'one');assert.equal(details.arrived,'two');
    assert.equal(details.instrumental,true);assert.ok(details.cleared>0);
    assert.equal(details.translated,'第二句');assert.equal(details.creditsHidden,true,'final translation leaves room for credits');
    await page.evaluate(async()=>{
      const {mountMusicVisualizer}=await import('/music-visualizer.js');
      const {DEFAULT_MUSIC_PREFERENCES}=await import('/music-preference-values.js');
      window.fixture={current:{id:'lumiere-fixture',title:'绘光测试',artist:'Lume'},
        currentTime:14,duration:90,playing:false,visible:true,
        preferences:{...DEFAULT_MUSIC_PREFERENCES,mode:'lumiere'},
        lines:Array.from({length:12},(_,i)=>({startTime:i*6,endTime:i*6+5,
          fullText:['风穿过了山谷','窗外亮起微光','A melody through the night'][i%3],words:[]}))};
      window.view=mountMusicVisualizer(document.getElementById('stage'),{onError:e=>{throw e;}});
      await view.update(fixture,'lumiere');
    });
    await page.waitForFunction(()=>document.querySelector('[data-lumiere-state="ready"] canvas, [data-lumiere-state="error"]'));
    assert.deepEqual(errors,[],'Lumiere must initialize under the production CSP');
    // Lume shows no upcoming-line strip; translations remain (checked below).
    assert.equal(await page.locator('[data-lumiere-subtitle="upcoming"]').count(),0);
    for(const viewport of [{width:1840,height:1035},{width:1000,height:650},{width:1840,height:1035},{width:1280,height:800}]){
      await page.setViewportSize(viewport);await page.waitForTimeout(400);
      const size=await page.evaluate(()=>{
        const host=document.getElementById('stage').getBoundingClientRect();
        const canvas=document.querySelector('[data-lumiere-state="ready"] canvas');
        const rect=canvas.getBoundingClientRect();
        return {host:[host.width,host.height],css:[rect.width,rect.height],pixels:[canvas.width,canvas.height]};
      });
      assert.ok(size.css.every((x,i)=>Math.abs(x-size.host[i])<1),`Lumiere fills the stage after resize: ${JSON.stringify(size)}`);
      assert.deepEqual(size.pixels,size.host.map(x=>Math.round(x*1.5)),'drawing buffer follows DPR');
    }
    await page.screenshot({path:path.join(evidence,'lumiere.png')});
    const before=await page.locator('#stage').screenshot();
    await page.evaluate(()=>view.update({...fixture,currentTime:32},'lumiere'));
    const after=await page.locator('#stage').screenshot();
    assert.notDeepEqual(before,after,'seeking changes the original scene');
    await page.evaluate(()=>view.update({...fixture,preferences:{...fixture.preferences,fontFamily:'Arial',lyricsFontColor:'#ffcc44'}},'lumiere'));
    await page.evaluate(()=>view.update({...fixture,visible:false},'lumiere'));
    assert.equal(await page.locator('#stage canvas').count(),0);
    await page.evaluate(()=>view.update({...fixture,lines:[]},'lumiere'));
    await page.waitForFunction(()=>document.querySelector('[data-lumiere-state="ready"] canvas'));
    await page.evaluate(async()=>{await view.update({...fixture,playing:true},'lumiere');view.dispose();});
    assert.equal(await page.locator('#stage canvas').count(),0);
    assert.deepEqual(errors,[]);await page.close();
  }));
const foregroundPixels=page=>page.evaluate(async()=>{
  const {createMusicMaterialScene}=await import('/music-material/scene.js');
  const capture=createMusicMaterialScene(document);
  const result=capture.capture(document.getElementById('bar'),document.body,null);
  const bytes=result.canvas.getContext('2d').getImageData(0,0,result.canvas.width,result.canvas.height).data;
  let count=0;for(let i=3;i<bytes.length;i+=4)if(bytes[i]>10)count++;
  capture.dispose();return count;
});
function changed(PNG,a,b){
  const x=PNG.sync.read(a),y=PNG.sync.read(b);let pixels=0;
  assert.equal(x.data.length,y.data.length);
  for(let i=0;i<x.data.length;i+=4)
    if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>45)pixels++;
  return pixels;
}

test('whole-bar historical glass has real optical pixels and bounded lifecycle',
  {skip:!enabled,timeout:120000},()=>harness(async({browser,base,PNG})=>{
    const report=[];
    for(const [width,height] of [[1440,900],[390,844]]){
      const page=await browser.newPage({viewport:{width,height}}),errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      await setup(page,base);
      const original=await page.locator('#bar').screenshot();
      const stats=await page.evaluate(()=>{
        const c=material.canvas,gl=c.getContext('webgl2');
        const data=new Uint8Array(c.width*c.height*4);
        gl.readPixels(0,0,c.width,c.height,gl.RGBA,gl.UNSIGNED_BYTE,data);
        let opaque=0,transparent=0;
        for(let i=3;i<data.length;i+=4){if(data[i]>200)opaque++;if(data[i]===0)transparent++;}
        return {opaque,transparent,width:c.width,height:c.height};
      });
      assert.ok(stats.opaque>1000,'nonblank optical canvas');
      assert.ok(stats.transparent>100,'capsule corners are transparent');
      await page.evaluate(()=>material.canvas.style.visibility='hidden');
      const bare=await page.locator('#bar').screenshot();
      const difference=changed(PNG,original,bare);
      assert.ok(difference>800,'refraction is visibly different from the unmodified backdrop');
      await page.evaluate(()=>material.canvas.style.visibility='');
      await page.screenshot({path:path.join(evidence,`glass-${width}.png`)});
      const retained=await page.evaluate(async()=>{
        const canvas=material.canvas,gl=canvas.getContext('webgl2');
        for(let i=0;i<4;i++){
          const button=document.querySelector('#bar button');
          button.replaceWith(button.cloneNode(true));material.update();
          await new Promise(requestAnimationFrame);
        }
        return material.canvas===canvas&&canvas.getContext('webgl2')===gl;
      });
      assert.equal(retained,true,'footer content replacement retains material context');
      await page.evaluate(()=>material.update({visible:false}));
      await settle(page);
      const hidden=await page.evaluate(()=>material.snapshot());
      assert.equal(hidden.state,'hidden');assert.equal(hidden.pendingFrame,false);
      await page.waitForTimeout(100);
      assert.equal(await page.evaluate(()=>material.snapshot().frames),hidden.frames);
      await page.evaluate(()=>material.update({visible:true}));
      await page.waitForFunction(()=>material.snapshot().state==='active');
      await page.evaluate(()=>{
        Object.defineProperty(document,'hidden',{configurable:true,value:true});
        document.dispatchEvent(new Event('visibilitychange'));
      });
      assert.equal(await page.evaluate(()=>material.snapshot().pendingFrame),false);
      assert.equal(await page.evaluate(()=>material.snapshot().state),'hidden');
      await page.evaluate(()=>{
        delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));
      });
      await page.waitForFunction(()=>material.snapshot().state==='active');
      await page.evaluate(()=>{
        window.loss=material.canvas.getContext('webgl2').getExtension('WEBGL_lose_context');
        if(!loss)throw new Error('context loss extension unavailable');
        loss.loseContext();
      });
      await page.waitForFunction(()=>material.snapshot().state==='context-lost');
      assert.equal(await page.evaluate(()=>material.snapshot().pendingFrame),false);
      await page.waitForTimeout(100);
      await page.evaluate(()=>loss.restoreContext());
      await page.waitForFunction(()=>material.snapshot().state==='active');
      for(let i=0;i<4;i++){
        await page.evaluate(()=>{window.old=material;material=mount();});
        await page.waitForFunction(()=>material.snapshot().state==='active');
        assert.equal(await page.locator('[data-music-material]').count(),1);
        assert.equal(await page.evaluate(()=>old.snapshot().state),'disposed');
        assert.equal(await page.evaluate(()=>old.snapshot().pendingFrame),false);
      }
      await page.evaluate(()=>{material.dispose();material.dispose();descriptor.canvas.dispatchEvent(new Event('lume-glass-media-frame'));dispatchEvent(new Event('resize'));});
      await settle(page);
      assert.equal(await page.locator('[data-music-material]').count(),0);
      assert.equal(await page.evaluate(()=>material.snapshot().pendingFrame),false);
      assert.deepEqual(await page.evaluate(()=>materialErrors),[]);
      assert.deepEqual(errors,[]);
      report.push({viewport:{width,height},difference,...stats});
      await page.close();
    }
    await writeFile(path.join(evidence,'pixels.json'),JSON.stringify(report,null,2)+'\n');
  }));

test('shader readiness gates real parent active styles and failures retain fallback chrome',
  {skip:!enabled,timeout:90000},()=>harness(async({browser,base})=>{
    const report=[];
    for(const failure of ['unavailable','compile','link','draw','none']){
      const page=await browser.newPage({viewport:{width:1100,height:760}}),errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      await page.route('**/*',route=>route.request().url().startsWith(base)?route.continue():route.abort());
      await page.goto(`${base}/music.html`);
      const initial=await page.evaluate(async failure=>{
        const {mountMusicMaterial}=await import('/music-material.js');
        const root=document.getElementById('music-preview');
        root.innerHTML='<section class="MusicRoot"><footer class="MusicPlayerBar"><div class="MusicProgressLine"></div><div class="MusicPlayerLayout"><button>Play</button></div></footer></section>';
        const surface=root.querySelector('.MusicPlayerBar');
        const owns=gl=>gl.canvas.hasAttribute('data-music-material');
        const proto=WebGL2RenderingContext.prototype;
        if(failure==='unavailable'){
          const getContext=HTMLCanvasElement.prototype.getContext;
          HTMLCanvasElement.prototype.getContext=function(type,...args){
            return type==='webgl2'&&this.hasAttribute('data-music-material')?null:getContext.call(this,type,...args);
          };
        }else if(failure==='compile'){
          const shaderSource=proto.shaderSource;
          proto.shaderSource=function(shader,source){return shaderSource.call(this,shader,owns(this)?'invalid shader':source);};
        }else if(failure==='link'){
          const getParameter=proto.getProgramParameter;
          proto.getProgramParameter=function(program,key){
            return owns(this)&&key===this.LINK_STATUS?false:getParameter.call(this,program,key);
          };
        }else if(failure==='draw'){
          const draw=proto.drawArrays;
          proto.drawArrays=function(mode,first,count){return draw.call(this,mode,first,owns(this)?-1:count);};
        }
        window.materialErrors=[];
        window.material=mountMusicMaterial(surface,{root:root.firstElementChild,
          onError:error=>materialErrors.push(error.message)});
        window.readChrome=()=>{
          const css=getComputedStyle(surface);
          return {state:material.snapshot().state,active:surface.matches(':has(> canvas[data-music-material-state="active"])'),
            blur:css.backdropFilter,background:css.backgroundColor,shadow:css.boxShadow};
        };
        return readChrome();
      },failure);
      assert.equal(initial.state,'initializing');
      assert.equal(initial.active,false);assert.equal(initial.blur,'blur(64px)');
      await page.waitForFunction(()=>['active','fallback'].includes(material.snapshot().state));
      const ready=await page.evaluate(()=>({chrome:readChrome(),snapshot:material.snapshot(),errors:materialErrors}));
      if(failure==='none'){
        assert.equal(ready.chrome.active,true);assert.equal(ready.chrome.blur,'none');
        assert.equal(ready.chrome.background,'rgba(0, 0, 0, 0)');assert.equal(ready.chrome.shadow,'none');
        assert.ok(ready.snapshot.frames>0);assert.deepEqual(ready.errors,[]);
        await page.evaluate(()=>{
          window.loss=material.canvas.getContext('webgl2').getExtension('WEBGL_lose_context');loss.loseContext();
        });
        await page.waitForFunction(()=>material.snapshot().state==='context-lost');
        const lost=await page.evaluate(()=>readChrome());
        assert.equal(lost.active,false);assert.equal(lost.blur,'blur(64px)');
        assert.equal(lost.background,initial.background);assert.equal(lost.shadow,initial.shadow);
        await page.waitForTimeout(100);
        await page.evaluate(()=>loss.restoreContext());
        await page.waitForFunction(()=>material.snapshot().state==='active');
        assert.equal(await page.evaluate(()=>readChrome().blur),'none');
      }else{
        assert.equal(ready.chrome.state,'fallback',failure);
        assert.equal(ready.chrome.active,false,failure);assert.equal(ready.chrome.blur,'blur(64px)',failure);
        assert.equal(ready.chrome.background,initial.background);assert.equal(ready.chrome.shadow,initial.shadow);
        assert.equal(ready.snapshot.frames,0);assert.equal(ready.snapshot.pendingFrame,false);
        assert.equal(ready.errors.length,1);
        assert.deepEqual(await page.evaluate(()=>[material.canvas.width,material.canvas.height]),[1,1]);
      }
      await page.evaluate(()=>material.dispose());
      assert.deepEqual(errors,[]);
      report.push({failure,initial,...ready});
      await page.close();
    }
    await writeFile(path.join(evidence,'readiness.json'),JSON.stringify(report,null,2)+'\n');
  }));

test('same-origin iframe text/canvas capture maps offsets, border, scaling and clipping',
  {skip:!enabled,timeout:60000},()=>harness(async({browser,base,PNG})=>{
    const page=await browser.newPage({viewport:{width:1100,height:760}});
    await setup(page,base);
    await page.evaluate(()=>{
      const bar=document.getElementById('bar');
      Object.assign(bar.style,{top:'200px',bottom:'auto',height:'150px'});
      const frame=document.createElement('iframe');frame.id='child';frame.src='/child';
      frame.setAttribute('sandbox','allow-scripts allow-same-origin');
      Object.assign(frame.style,{position:'absolute',left:'90px',top:'115px',width:'620px',height:'220px',
        border:'8px solid transparent',transform:'scale(1.1)',transformOrigin:'top left'});
      document.getElementById('stage').append(frame);
    });
    await page.waitForFunction(()=>document.getElementById('child')?.contentDocument?.getElementById('ink'));
    await page.evaluate(()=>{
      const doc=document.getElementById('child').contentDocument;
      doc.getElementById('ink').getContext('2d').fillRect(0,0,100,100);
      window.childAccess=!!doc.querySelector('[data-lumenic-liquid-glass-scene]');
      material.update();
    });
    await page.waitForFunction(()=>material.snapshot().scene.iframes===1&&material.snapshot().scene.text>0&&material.snapshot().scene.canvases>0);
    assert.equal(await page.evaluate(()=>childAccess),true,'sandbox-compatible same-origin access');
    await page.evaluate(()=>document.getElementById('child').contentDocument.getElementById('ink').style.display='none');
    assert.ok(await foregroundPixels(page)>100,'text-only native DOMRect clipping produces real pixels');
    await page.evaluate(()=>document.getElementById('child').contentDocument.getElementById('ink').style.display='');
    const before=await page.locator('#bar').screenshot();
    await page.evaluate(()=>{
      const doc=document.getElementById('child').contentDocument;
      doc.getElementById('words').style.color='#00ff00';
      const ctx=doc.getElementById('ink').getContext('2d');ctx.fillStyle='#aa00ff';ctx.fillRect(0,0,100,100);
    });
    await settle(page);
    const after=await page.locator('#bar').screenshot();
    assert.ok(changed(PNG,before,after)>100,'iframe scene changes reach material texture');
    const geometry=await page.evaluate(async()=>{
      const {createMusicMaterialScene}=await import('/music-material/scene.js');
      const capture=createMusicMaterialScene(document),frame=document.getElementById('child');
      const doc=frame.contentDocument,ink=doc.getElementById('ink'),ctx=ink.getContext('2d');
      ctx.fillStyle='#ff0000';ctx.fillRect(0,0,100,100);
      doc.getElementById('words').style.display='none';
      const r=frame.getBoundingClientRect(),sx=r.width/frame.offsetWidth,sy=r.height/frame.offsetHeight;
      const x=r.left+(frame.clientLeft+40)*sx,y=r.top+(frame.clientTop+120)*sy;
      const sample=(frame,x,y)=>{
        const px=Math.floor((x-frame.bounds.left)*frame.canvas.width/(frame.bounds.right-frame.bounds.left));
        const py=Math.floor((y-frame.bounds.top)*frame.canvas.height/(frame.bounds.bottom-frame.bounds.top));
        return [...frame.canvas.getContext('2d').getImageData(px,py,1,1).data];
      };
      const first=capture.capture(document.getElementById('bar'),document.body,null);
      const full=sample(first,x,y),outside=sample(first,r.left+1,y);
      frame.style.opacity='.5';
      const faded=sample(capture.capture(document.getElementById('bar'),document.body,null),x,y);
      capture.dispose();frame.style.opacity='';
      return {full,outside,faded};
    });
    assert.deepEqual(geometry.full,[255,0,0,255],'iframe content mapped with border and positive CSS scale');
    assert.equal(geometry.outside[3],0,'outside iframe content remains clipped');
    assert.ok(geometry.faded[3]>=126&&geometry.faded[3]<=129,'iframe opacity applied exactly once');
    await page.evaluate(()=>document.getElementById('child').style.display='none');
    await settle(page);
    assert.equal(await page.evaluate(()=>material.snapshot().scene.iframes),0,'hidden frame excluded');
    await page.evaluate(()=>material.dispose());
    await page.close();
  }));

test('production nine renderer boundaries capture real scenes; volatile 3D requires explicit redraw',
  {skip:!enabled,timeout:240000},()=>harness(async({browser,base})=>{
    const report=[];
    for(const mode of ['partita','fume','sonnet','gallery','3d']){
      const page=await browser.newPage({viewport:{width:1100,height:760}}),errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      await setup(page,base);
      await page.evaluate(async mode=>{
        Object.assign(document.getElementById('bar').style,{top:'300px',bottom:'auto',height:'160px'});
        window.fixture={lines:Array.from({length:12},(_,i)=>({startTime:i*6,endTime:i*6+6,
          fullText:'A melody through the night',translation:'',words:[]})),
          current:{id:'material-fixture',title:'Material',artist:'Lume',album:'',artwork:'assets/music/forest.webp'},
          currentTime:25,activeLineIndex:4,duration:72,playing:false,visible:true};
        const host=document.getElementById('stage');
        if(mode==='3d')window.visualizer=(await import('/music-visualizers/three-d.js')).mountThreeD(host,fixture);
        else if(mode==='gallery')window.visualizer=(await import('/music-visualizers/gallery.js')).mountGallery(host,fixture);
        else window.visualizer=(await import('/music-visualizers/reference-host.js')).mountReference(host,fixture,mode);
        material.update();
      },mode);
      if(!['gallery','3d'].includes(mode))
        await page.waitForFunction(()=>document.querySelector('iframe')?.contentDocument?.querySelector('#reference-stage[data-reference-state="ready"]'));
      await page.waitForTimeout(1800);
      await settle(page);
      if(mode!=='3d')await page.waitForFunction(()=>{
        const scene=material.snapshot().scene;
        return scene.canvases+scene.text>0||material.snapshot().state==='fallback';
      },null,{timeout:10000}).catch(async error=>{
        const state=await page.evaluate(()=>({material:material.snapshot(),errors:materialErrors,
          frame:document.querySelector('iframe')?.getBoundingClientRect().toJSON(),
          html:document.querySelector('iframe')?.contentDocument?.body.innerHTML.slice(0,5000)}));
        throw new Error(`${mode}: ${JSON.stringify(state)}`,{cause:error});
      });
      const snapshot=await page.evaluate(()=>material.snapshot());
      assert.equal(snapshot.state,'active',`${mode}: ${JSON.stringify(await page.evaluate(()=>materialErrors))}`);
      let foreground=0,providedScene=null;
      if(mode==='3d'){
        // Parent may integrate its producer registration concurrently. Either
        // complete capture or the explicit missing-provider diagnostic is valid.
        assert.ok(snapshot.scene.canvases>0||snapshot.scene.issues.includes('3d-snapshot-provider-required'));
        // Exercise the narrow integration hook on the actual historical runtime.
        await page.evaluate(async()=>{
          visualizer.dispose();
          const {Mineradio3DStageRuntime}=await import('/music-visualizers/vendor/mineradio-stage.js');
          const {resolvePreferenceTheme}=await import('/music-visualizers/reference/preference-theme.js');
          window.stageRuntime=new Mineradio3DStageRuntime(document.getElementById('stage'),{
            lines:fixture.lines,activeLineIndex:4,currentTime:25,paused:true,visible:true,
            theme:resolvePreferenceTheme('3d'),showTranslation:true,visualizerEffectMode:'none',
          });
          window.unregister=materialModule.registerMusicMaterialSceneSource(stageRuntime.getCanvas(),{
            refresh:()=>stageRuntime.refreshSceneFrame(),
          });
          material.update();
        });
        await page.waitForFunction(()=>material.snapshot().scene.canvases>0&&!material.snapshot().scene.issues.length);
        foreground=await foregroundPixels(page);
        providedScene=await page.evaluate(()=>material.snapshot().scene);
        await page.evaluate(()=>{unregister();stageRuntime.destroy();});
      }else{
        assert.ok(snapshot.scene.canvases+snapshot.scene.text>0,`${mode}: actual foreground leaves captured`);
        assert.deepEqual(snapshot.scene.issues,[],mode);
        foreground=await foregroundPixels(page);
      }
      assert.ok(foreground>30,`${mode}: real nontransparent foreground pixels, not just discovered nodes`);
      if(mode!=='3d')await page.evaluate(()=>visualizer.dispose());
      report.push({mode,...snapshot.scene,foregroundPixels:foreground,providedScene});
      await page.evaluate(()=>material.dispose());
      assert.deepEqual(errors,[],mode);
      await page.close();
    }
    await writeFile(path.join(evidence,'renderers.json'),JSON.stringify(report,null,2)+'\n');
  }));

test('parent-owned rain, snow and star-river canvases reach the material capture',
  {skip:!enabled,timeout:90000},()=>harness(async({browser,base})=>{
    const report=[];
    for(const mode of ['rain-window','snow','star-river']){
      const page=await browser.newPage({viewport:{width:1100,height:760}}),errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      await setup(page,base);
      await page.evaluate(async mode=>{
        Object.assign(document.getElementById('bar').style,{top:'300px',bottom:'auto'});
        const {mountMusicEffects}=await import('/music-effects.js');
        window.effects=mountMusicEffects(document.getElementById('effects'),{
          onError:error=>materialErrors.push(error.message),
        });
        await effects.update({mode,visualizer:'partita',visible:true,scene:descriptor,current:null});
        material.update();
      },mode);
      await page.waitForFunction(()=>material.snapshot().scene.canvases>0);
      await page.waitForTimeout(1400);
      const pixels=await foregroundPixels(page);
      assert.ok(pixels>10,`${mode}: nontransparent effect pixels`);
      const snapshot=await page.evaluate(()=>material.snapshot());
      assert.equal(snapshot.state,'active');
      assert.deepEqual(snapshot.scene.issues,[]);
      await page.evaluate(()=>{effects.dispose();material.dispose();});
      assert.deepEqual(await page.evaluate(()=>materialErrors),[]);
      assert.deepEqual(errors,[]);
      report.push({mode,pixels});
      await page.close();
    }
    await writeFile(path.join(evidence,'effects.json'),JSON.stringify(report,null,2)+'\n');
  }));

test('production mountMusicView lazily mounts glass, retains footer, captures registered 3D and releases hidden work',
  {skip:!enabled,timeout:180000},()=>harness(async({browser,base,PNG})=>{
    const report=[];
    for(const [width,height] of [[1536,864],[390,844]]){
      const page=await browser.newPage({viewport:{width,height}}),errors=[],requests=[];
      page.on('pageerror',error=>errors.push(error.message));
      page.on('request',request=>requests.push(request.url()));
      await page.route('**/*',route=>route.request().url().startsWith(base)||
        /^(data|blob):/.test(route.request().url())?route.continue():route.abort());
      await page.goto(`${base}/music.html`);
      await page.evaluate(async()=>{
        const {mountMusicView}=await import('/music-view.js');
        const {initialMusicState}=await import('/music.js');
        const {DEFAULT_MUSIC_PREFERENCES}=await import('/music-preference-values.js');
        const request=window.requestAnimationFrame.bind(window),cancel=window.cancelAnimationFrame.bind(window);
        window.pendingFrames=new Set();
        window.requestAnimationFrame=callback=>{
          const id=request(time=>{pendingFrames.delete(id);callback(time);});
          pendingFrames.add(id);return id;
        };
        window.cancelAnimationFrame=id=>{pendingFrames.delete(id);cancel(id);};
        window.preferences={preferences:{...DEFAULT_MUSIC_PREFERENCES,mode:'3d'},loaded:true,pending:false};
        const track={id:'material-production',title:'Material verification',artist:'Lume',
          album:'Local fixture',source:'fixture-only',artwork:'',duration:240};
        window.productionState={...initialMusicState(),session:7,pageOpen:true,pageVisible:true,
          current:track,queue:[track],timeline:{position:25,duration:240},status:'paused',playing:false,
          capabilities:{canToggle:true,canSeek:true,canNext:false,canPrevious:false},
          lyrics:Array.from({length:30},(_,i)=>({startTime:i*6,endTime:i*6+6,
            fullText:'A melody through the night',translation:'',words:[]}))};
        window.commands=[];
        window.productionView=mountMusicView(document.getElementById('music-preview'),{
          snapshot:productionState,preferences,send:(...args)=>{commands.push(args);return false;},
        });
      });
      assert.equal(requests.some(url=>url.endsWith('/music-material.js')),false,'home does not import glass');
      await page.locator('[data-music-action="open-player"]').click();
      await page.waitForFunction(()=>document.querySelector('[data-music-material-state="active"]')&&
        document.querySelector('[data-music-stage]')?.dataset.visualizerState==='ready'&&
        document.querySelector('[data-music-background]')?.dataset.musicBackgroundState==='ready');
      await page.evaluate(()=>{
        window.footer=document.querySelector('.MusicPlayerBar');
        window.glass=footer.querySelector('[data-music-material]');
        window.glassContext=glass.getContext('webgl2');
      });
      const chrome=await page.evaluate(()=>{
        const bar=getComputedStyle(footer),layout=getComputedStyle(footer.querySelector('.MusicPlayerLayout'));
        const progress=getComputedStyle(footer.querySelector('.MusicProgressLine')),canvas=getComputedStyle(glass);
        return {background:bar.backgroundColor,blur:bar.backdropFilter,shadow:bar.boxShadow,
          layoutZ:Number(layout.zIndex),progressZ:Number(progress.zIndex),canvasZ:Number(canvas.zIndex),
          pointerEvents:canvas.pointerEvents,overflow:bar.overflow};
      });
      assert.equal(chrome.background,'rgba(0, 0, 0, 0)');
      assert.equal(chrome.blur,'none');assert.equal(chrome.shadow,'none');
      assert.ok(chrome.layoutZ>chrome.canvasZ&&chrome.progressZ>chrome.canvasZ);
      assert.equal(chrome.pointerEvents,'none');assert.equal(chrome.overflow,'visible');
      for(const mode of ['3d','gallery','partita','lumiere','tempera']){
        await page.evaluate(mode=>{
          preferences={...preferences,preferences:{...preferences.preferences,mode}};
          productionView.updatePreferences(preferences);
        },mode);
        await page.waitForFunction(mode=>document.querySelector('[data-music-stage]')?.dataset.visualizerMode===mode&&
          document.querySelector('[data-music-stage]')?.dataset.visualizerState==='ready',mode);
        if(mode==='partita')await page.waitForFunction(()=>document.querySelector('iframe')?.contentDocument
          ?.querySelector('#reference-stage[data-reference-state="ready"]'));
        if(mode==='tempera')await page.waitForFunction(()=>document.querySelector('iframe')?.contentDocument
          ?.querySelector('canvas[data-lumenic-liquid-glass-scene-effect]'));
        if(mode==='lumiere')await page.waitForFunction(()=>document.querySelector('[data-lumiere-state="ready"] canvas'));
        await page.waitForTimeout(1000);
        const scene=await page.evaluate(async()=>{
          const {createMusicMaterialScene}=await import('/music-material/scene.js');
          const capture=createMusicMaterialScene(document);
          const result=capture.capture(footer,document.querySelector('[data-music-root]'),null);
          const {canvas,bounds,hasContent,...stats}=result;
          capture.dispose();return stats;
        });
        assert.deepEqual(scene.issues,[],`${mode}: production scene diagnostics`);
        if(mode==='3d')assert.ok(scene.canvases>0,'parent registered existing 3D runtime canvas');
        if(mode==='partita')assert.equal(scene.iframes,1,'real reference iframe traversed');
        if(mode==='tempera')assert.ok(scene.canvases>0,'Tempera WebGL canvas sampled through the iframe');
        const pixels=await page.evaluate(()=>{
          const bytes=new Uint8Array(glass.width*glass.height*4);
          glassContext.readPixels(0,0,glass.width,glass.height,glassContext.RGBA,glassContext.UNSIGNED_BYTE,bytes);
          let count=0;for(let i=3;i<bytes.length;i+=4)if(bytes[i]>200)count++;
          return count;
        });
        assert.ok(pixels>1000,`${mode}: production glass canvas nonblank`);
        const before=await page.locator('.MusicPlayerBar').screenshot();
        await page.evaluate(()=>glass.style.visibility='hidden');
        const bare=await page.locator('.MusicPlayerBar').screenshot();
        await page.evaluate(()=>glass.style.visibility='');
        const difference=changed(PNG,before,bare);
        assert.ok(difference>100,`${mode}: visible production optical material`);
        await page.screenshot({path:path.join(evidence,`production-${mode}-${width}.png`)});
        await page.locator('[data-music-action="queue"]').click();
        assert.equal(await page.evaluate(()=>document.querySelector('.MusicPlayerBar')===footer&&
          footer.querySelector('[data-music-material]')===glass&&glass.getContext('webgl2')===glassContext),true);
        await page.locator('aside [data-music-action="close-panel"]').click();
        await page.evaluate(()=>productionView.setVisible(false));
        await page.waitForFunction(()=>pendingFrames.size===0&&glass.dataset.musicMaterialState==='hidden');
        assert.deepEqual(await page.evaluate(()=>[glass.width,glass.height]),[1,1],'hidden material releases canvas buffers');
        await page.waitForTimeout(80);
        assert.equal(await page.evaluate(()=>pendingFrames.size),0,'hidden production owners leave no parent RAF');
        await page.evaluate(()=>productionView.setVisible(true));
        await page.waitForFunction(()=>glass.dataset.musicMaterialState==='active'&&glass.width>1);
        report.push({width,height,mode,pixels,difference,scene,chrome});
      }
      await page.evaluate(()=>productionView.update({...productionState,
        current:{...productionState.current,title:'Structural update'}}));
      assert.equal(await page.evaluate(()=>document.querySelector('.MusicPlayerBar')===footer&&
        footer.querySelector('[data-music-material]')===glass),true,'song structural update retains footer');
      await page.evaluate(()=>{productionView.dispose();productionView.dispose();});
      await page.waitForFunction(()=>pendingFrames.size===0);
      assert.equal(await page.locator('[data-music-material]').count(),0);
      assert.deepEqual(await page.evaluate(()=>commands),[],'fixture never requests playback or account operations');
      assert.deepEqual(errors,[]);
      await page.close();
    }
    await writeFile(path.join(evidence,'production.json'),JSON.stringify(report,null,2)+'\n');
  }));

test('immersive reveal keeps prepared liquid glass and quality menu stays above it and closes over lyrics',
  {skip:!enabled,timeout:120000},()=>harness(async({browser,base})=>{
    for(const [width,height] of [[1536,864],[390,844]]){
      const page=await browser.newPage({viewport:{width,height}});
      await page.route('**/*',route=>route.request().url().startsWith(base)||
        /^(data|blob):/.test(route.request().url())?route.continue():route.abort());
      await page.goto(`${base}/music.html`);
      await page.evaluate(async()=>{
        const {mountMusicView}=await import('/music-view.js');
        const {initialMusicState}=await import('/music.js');
        const {DEFAULT_MUSIC_PREFERENCES}=await import('/music-preference-values.js');
        const track={id:'qq:quality-fixture',title:'Quality fixture',artist:'Lume',album:'Fixture',
          source:'fixture-only',artwork:'',duration:90};
        const state={...initialMusicState(),session:7,pageOpen:true,pageVisible:true,current:track,
          queue:[track],timeline:{position:9,duration:90},status:'paused',
          capabilities:{canToggle:true,canSeek:true,canNext:false,canPrevious:false},
          lyrics:[{startTime:0,endTime:90,fullText:'A melody through the night',translation:'',words:[]}]};
        window.musicView=mountMusicView(document.getElementById('music-preview'),{
          snapshot:state,preferences:{preferences:DEFAULT_MUSIC_PREFERENCES,loaded:true,pending:false},
          send:()=>false,
        });
      });
      await page.locator('[data-music-action="open-player"]').click();
      await page.waitForFunction(()=>document.querySelector('[data-music-material-state="active"]')&&
        document.querySelector('iframe')?.contentDocument?.querySelector('#reference-stage[data-reference-state="ready"]'));
      await page.evaluate(()=>{window.glass=document.querySelector('[data-music-material]');
        window.glassContext=glass.getContext('webgl2');});
      await page.locator('[data-music-quality-trigger]').click();
      const placement=await page.evaluate(()=>{
        const bar=document.querySelector('.MusicPlayerBar').getBoundingClientRect();
        const menu=document.querySelector('[data-music-quality-menu]').getBoundingClientRect();
        const cover=document.querySelector('[data-music-quality-dismiss]');
        return {barTop:bar.top,menuBottom:menu.bottom,
          hit:!!document.elementFromPoint(menu.left+10,menu.bottom-10)?.closest('[data-music-quality-menu]'),
          covered:document.elementFromPoint(40,200)===cover};
      });
      assert.ok(placement.menuBottom<=placement.barTop-7,JSON.stringify(placement));
      assert.equal(placement.hit,true,'quality menu is above the glass in hit testing');
      assert.equal(placement.covered,true,'empty lyric space catches clicks before the iframe');
      await page.screenshot({path:path.join(evidence,`quality-menu-${width}.png`)});
      await page.mouse.click(40,200);
      assert.equal(await page.locator('[data-music-quality-menu]').isHidden(),true);
      assert.equal(await page.locator('[data-music-quality-dismiss]').count(),0);
      const volume=page.locator('[data-music-volume-trigger]');
      await volume.hover();
      assert.equal(await page.locator('[data-music-volume-menu]').isHidden(),true);
      await volume.click();
      assert.equal(await page.locator('[data-music-volume-menu]').isVisible(),true);
      const volumePlacement=await page.evaluate(()=>{
        const menu=document.querySelector('[data-music-volume-menu]');
        const quality=document.querySelector('[data-music-quality-menu]');
        const box=menu.getBoundingClientRect(),bar=document.querySelector('.MusicPlayerBar').getBoundingClientRect();
        const style=getComputedStyle(menu),other=getComputedStyle(quality);
        return {above:box.bottom<=bar.top-7,inside:box.left>=0&&box.right<=innerWidth,
          material:style.backgroundColor===other.backgroundColor&&style.backdropFilter===other.backdropFilter};
      });
      assert.deepEqual(volumePlacement,{above:true,inside:true,material:true});
      await page.screenshot({path:path.join(evidence,`volume-menu-${width}.png`)});
      await page.mouse.click(40,200);
      assert.equal(await page.locator('[data-music-volume-menu]').isHidden(),true);
      await page.locator('[data-music-action="immersive"]').click();
      await page.mouse.move(20,20);
      await page.waitForFunction(()=>document.querySelector('.MusicRoot')?.classList.contains('is-immersive')&&
        !document.querySelector('.MusicRoot')?.classList.contains('is-immersive-revealed'));
      const hidden=await page.evaluate(()=>({same:glass===document.querySelector('[data-music-material]'),
        state:glass.dataset.musicMaterialState,width:glass.width,context:glass.getContext('webgl2')===glassContext}));
      assert.equal(hidden.same,true);assert.equal(hidden.state,'active');
      assert.ok(hidden.width>1);assert.equal(hidden.context,true);
      // Freeze the first reveal frame at opacity 0, as happens at the start
      // of the CSS transition. Check synchronously, before RAF can repair it.
      for(let attempt=0;attempt<12;attempt++){
        const first=await page.evaluate(()=>{
          const bar=document.querySelector('.MusicPlayerBar');
          const zone=document.querySelector('[data-music-immersive-zone]');
          bar.dispatchEvent(new MouseEvent('pointerout',{bubbles:true}));
          bar.style.transition='none';
          bar.style.opacity='0';
          void getComputedStyle(bar).opacity;
          zone.dispatchEvent(new MouseEvent('pointerover',{bubbles:true}));
          const result={state:glass.dataset.musicMaterialState,width:glass.width,
            blur:getComputedStyle(bar).backdropFilter};
          bar.style.opacity='';
          bar.style.transition='';
          return result;
        });
        assert.equal(first.state,'active',`first reveal frame ${attempt}`);
        assert.ok(first.width>1,'reveal must never release the prepared canvas');
        assert.equal(first.blur,'none','no frosted fallback during reveal');
      }
      await page.mouse.move(width/2,height-8);
      await page.waitForFunction(()=>document.querySelector('.MusicRoot')?.classList.contains('is-immersive-revealed'));
      const revealed=await page.evaluate(()=>{
        const bar=getComputedStyle(document.querySelector('.MusicPlayerBar'));
        return {same:glass===document.querySelector('[data-music-material]'),
          state:glass.dataset.musicMaterialState,context:glass.getContext('webgl2')===glassContext,
          background:bar.backgroundColor,blur:bar.backdropFilter};
      });
      assert.equal(revealed.same,true);assert.equal(revealed.state,'active');
      assert.equal(revealed.context,true);assert.equal(revealed.background,'rgba(0, 0, 0, 0)');
      assert.equal(revealed.blur,'none');
      await page.screenshot({path:path.join(evidence,`immersive-reveal-${width}.png`)});
      await page.evaluate(()=>musicView.dispose());
      await page.close();
    }
  }));
