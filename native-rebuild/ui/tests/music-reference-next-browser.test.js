import test from 'node:test';
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile,mkdir} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('newer Folia renderers (群唱, 镜台, 时计) paint real Edge frames through the stage protocol',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:300000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const evidence=path.resolve(ui,`../../build-artifacts/native-rebuild/music-reference-next-${new Date().toISOString().replace(/[:.]/g,'-')}`);
  await mkdir(evidence,{recursive:true});
  const require=createRequire(import.meta.url);
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const difference=(a,b)=>{
    const x=PNG.sync.read(a),y=PNG.sync.read(b);let count=0;
    for(let i=0;i<x.data.length;i+=4)if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>45)count++;
    return count;
  };
  const types={'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp','.png':'image/png'};
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    if(pathname==='/__test')return res.writeHead(200,{'Content-Type':'text/html'}).end(
      '<!doctype html><html style="color-scheme:dark"><head><meta charset="utf-8"></head><body style="margin:0;background:#18251d"><div id="host" style="position:fixed;inset:0"></div></body></html>');
    const file=path.resolve(ui,'.'+decodeURIComponent(pathname));
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try{res.setHeader('Content-Type',types[path.extname(file)]||'application/octet-stream');res.end(await readFile(file));}
    catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const origin=`http://127.0.0.1:${server.address().port}`;
  let browser;
  try{
    browser=await chromium.launch({headless:true,channel:'msedge'});
    for(const width of [1280,390])for(const mode of ['cappella','diorama','pendolo','tempera']){
      const height=width===390?844:800,page=await browser.newPage({viewport:{width,height}}),errors=[],requests=[];
      page.on('pageerror',error=>errors.push(error.message));
      page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
      page.on('request',request=>requests.push(request.url()));
      await page.goto(`${origin}/__test`);
      await page.evaluate(mode=>{
        window.messages=[];
        window.addEventListener('message',event=>messages.push(event.data));
        window.fixture={lines:Array.from({length:12},(_,i)=>({startTime:i*6,endTime:i*6+6,
          fullText:['风穿过了山谷','窗外亮起微光','A melody through the night'][i%3],words:[],translation:''})),
          current:{id:'reference-fixture',title:'山间来信',artist:'Lume',album:'Reference',artwork:'assets/music/forest.webp'},
          currentTime:25,activeLineIndex:4,duration:72,playing:true,visible:true};
        const frame=document.createElement('iframe');frame.id='stage';frame.style.cssText='width:100%;height:100%;border:0;display:block';
        frame.setAttribute('sandbox','allow-scripts allow-same-origin');
        frame.src=`/music-visualizers/reference-next/index.html#${mode}`;
        document.getElementById('host').append(frame);
        window.send=state=>frame.contentWindow.postMessage({type:'lume-lyric-stage',mode,state},location.origin);
      },mode);
      await page.waitForFunction(()=>messages.some(m=>m.type==='lume-lyric-stage-ready'||m.type==='lume-lyric-stage-error'))
        .catch(error=>{throw new Error(`${mode}/${width} did not load: ${errors.join('; ')}`,{cause:error});});
      await page.evaluate(()=>send(fixture));
      const frame=page.frames().find(item=>item.url().includes('/reference-next/index.html'));
      await frame.waitForSelector('#reference-stage[data-reference-state="ready"]');
      await page.evaluate(async()=>{for(let i=0;i<150;i++){send({currentTime:25+i/60});await new Promise(requestAnimationFrame);}});
      const before=await page.screenshot({path:path.join(evidence,`${mode}-${width}.png`)});
      const overflow=await frame.evaluate(()=>document.documentElement.scrollWidth>innerWidth);
      await frame.locator('#reference-stage').evaluate(element=>element.style.visibility='hidden');
      const empty=await page.screenshot();
      await frame.locator('#reference-stage').evaluate(element=>element.style.visibility='');
      if(mode==='cappella'){
        // Every sung line, from any singer, must be legible on its bubble.
        const pairs=await frame.evaluate(()=>{
          const rgb=value=>(value.match(/[\d.]+/g)||[]).slice(0,3).map(Number);
          const lum=([r,g,b])=>[r,g,b].map(v=>v/255).map(v=>v<=0.03928?v/12.92:((v+0.055)/1.055)**2.4)
            .reduce((sum,v,i)=>sum+v*[0.2126,0.7152,0.0722][i],0);
          return [...document.querySelectorAll('#reference-stage *')].filter(element=>
            element.style.backgroundColor&&element.style.color&&element.textContent.trim())
            .map(element=>{const a=lum(rgb(getComputedStyle(element).backgroundColor)),b=lum(rgb(getComputedStyle(element).color));
              return {text:element.textContent.trim(),contrast:(Math.max(a,b)+0.05)/(Math.min(a,b)+0.05)};});
        });
        assert.ok(pairs.length>=3,`cappella bubbles found: ${pairs.length}`);
        for(const pair of pairs)assert.ok(pair.contrast>=3,`cappella bubble "${pair.text}" contrast ${pair.contrast.toFixed(2)}`);
      }
      // No upcoming-line strip: the fixture has no translations, so no subtitle text at all.
      // Pendolo draws its lyric wheel down the whole height; it has no subtitle strip.
      const strip=mode==='pendolo'?[]:await frame.evaluate(()=>[...document.querySelectorAll('#reference-stage *')]
        .filter(element=>element.children.length===0&&/A melody|风穿过|窗外亮起/.test(element.textContent||''))
        .filter(element=>{const r=element.getBoundingClientRect();return r.top>innerHeight*0.72&&r.height>0;})
        .map(element=>element.textContent.trim()));
      assert.deepEqual(strip,[],`${mode}/${width}: upcoming-line strip shown`);
      const painted=difference(before,empty);
      assert.ok(painted>150,`${mode}/${width} must paint visible content (${painted})`);
      assert.equal(overflow,false,`${mode}/${width} overflows`);
      await page.evaluate(async()=>{send({activeLineIndex:5,currentTime:31});for(let i=0;i<90;i++){send({currentTime:31+i/60});await new Promise(requestAnimationFrame);}});
      const after=await page.screenshot({path:path.join(evidence,`${mode}-seek-${width}.png`)});
      assert.ok(difference(before,after)>100,`${mode}/${width} responds to lyric seek`);
      if(mode==='tempera'&&width===1280){
        // Paused, Tempera stops its ticker; the glass must still get the stage on every frame.
        await page.evaluate(()=>send({playing:false}));
        const painted=await page.evaluate(async()=>{
          const canvas=document.querySelector('iframe').contentDocument.querySelector('canvas[data-lumenic-liquid-glass-scene-effect]');
          const sink=document.createElement('canvas');sink.width=32;sink.height=18;const ctx=sink.getContext('2d',{willReadFrequently:true});
          const out=[];
          for(let i=0;i<10;i++){
            await new Promise(r=>requestAnimationFrame(r));
            canvas.lumeMaterialSceneRefresh();ctx.clearRect(0,0,32,18);ctx.drawImage(canvas,0,0,32,18);
            const d=ctx.getImageData(0,0,32,18).data;let n=0;for(let j=3;j<d.length;j+=4)if(d[j]>10)n++;
            out.push(n);
          }
          return out;
        });
        assert.ok(painted.every(n=>n>100),`paused Tempera stays readable for the glass: ${painted}`);
        await page.evaluate(()=>send({playing:true}));
      }
      await page.evaluate(()=>send({visible:false}));
      await frame.waitForSelector('#reference-stage[data-reference-state="hidden"]');
      assert.equal(await frame.locator('#reference-stage > *').count(),0,`${mode} unmounts when hidden`);
      await page.evaluate(()=>document.getElementById('stage').remove());
      assert.equal(requests.some(url=>/^https?:/.test(url)&&!url.startsWith(origin)),false,'no remote fetch');
      assert.equal(requests.some(url=>/reference-next\/mode-(\w+)\.js/.test(url)&&!url.endsWith(`mode-${mode}.js`)),false,'only selected renderer requested');
      const failures=await page.evaluate(()=>messages.filter(m=>m.type==='lume-lyric-stage-error'));
      assert.deepEqual(failures,[],`${mode}/${width}: host error ${errors.join('; ')}`);
      assert.deepEqual(errors,[],`${mode}/${width}: ${errors.join('; ')}`);
      console.log(`${mode}/${width}: painted ${painted}`);
      await page.close();
    }
  }finally{await browser?.close();server.close();}
});
