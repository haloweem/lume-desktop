import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('native analysis crosses the real iframe bridge without React churn or stale playback',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:120000},async()=>{
  const ui=fileURLToPath(new URL('..',import.meta.url));
  const evidence=path.resolve(ui,`../../build-artifacts/native-rebuild/music-analysis-${new Date().toISOString().replace(/[:.]/g,'-')}`);
  await mkdir(evidence,{recursive:true});
  const require=createRequire(import.meta.url);
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const cover=new PNG({width:16,height:16});
  cover.data.fill(255);
  const coverBytes=PNG.sync.write(cover);
  const coverUrls=['https://y.gtimg.cn/__lume-test-cover.png',
    'http://127.0.0.1:49123/'+'a'.repeat(64)+'/media.png'];
  const root=path.resolve(ui);
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    if(pathname==='/__analysis-test')return res.writeHead(200,{'Content-Type':'text/html'}).end(
      '<!doctype html><html style="color-scheme:dark"><head><meta charset="utf-8"><title>Analysis QA</title><style>body{margin:0;background:#18251d}#host{position:fixed;inset:0}.MusicReferenceStage{width:100%;height:100%;border:0;display:block}</style></head><body><div id="host"></div></body></html>');
    const file=path.resolve(root,'.'+pathname);
    if(!file.startsWith(root+path.sep))return res.writeHead(403).end();
    try{
      res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    }catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const origin=`http://127.0.0.1:${server.address().port}`;
  let browser;const report=[];
  try{
    browser=await chromium.launch({headless:true,channel:'msedge'});
    for(const width of [1280,390]){
      const height=width===390?844:800;
      const page=await browser.newPage({viewport:{width,height}}),errors=[],remote=[];
      page.on('pageerror',error=>errors.push(error.message));
      page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
      await page.route('**/*',route=>{
        const url=route.request().url();
        if(coverUrls.includes(url))return route.fulfill({contentType:'image/png',body:coverBytes});
        if(url.startsWith(origin+'/')||url.startsWith('data:'))return route.continue();
        remote.push(url);return route.abort();
      });
      // Test-only observation: no production diagnostics or audio values are injected.
      await page.addInitScript(()=>{
        const clear=CanvasRenderingContext2D.prototype.clearRect;
        CanvasRenderingContext2D.prototype.clearRect=function(...args){
          this.canvas.testClears=(this.canvas.testClears||0)+1;
          return clear.apply(this,args);
        };
        window.testDeltas=[];
        addEventListener('message',event=>{
          if(event.data?.type==='lume-lyric-stage')testDeltas.push(event.data.state);
        });
        window.testPixels=()=>{
          const canvas=document.querySelector('canvas');
          if(!canvas)return -1;
          const pixels=canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data;
          let count=0;
          for(let i=3;i<pixels.length;i+=4)if(pixels[i])count++;
          return count;
        };
      });
      await page.goto(origin+'/__analysis-test');
      await page.evaluate(async()=>{
        const {mountReference}=await import('/music-visualizers/reference-host.js');
        window.fixture={lines:Array.from({length:8},(_,i)=>({startTime:i*6,endTime:i*6+6,
          fullText:['A melody through the night','The morning comes in quietly'][i%2],
          words:[],translation:''})),
          current:{id:'analysis-fixture',source:'online:analysis-fixture',title:'Measured sound',artist:'Lume',album:'Reference',
            artwork:'assets/music/forest.webp'},
          currentTime:13,activeLineIndex:2,duration:48,playing:true,visible:true,
          analysis:{energy:0,bins:Array(32).fill(0)}};
        window.mounted=mountReference(document.getElementById('host'),fixture,'monet');
        window.update=delta=>{fixture={...fixture,...delta};mounted.update(fixture);};
        window.fresh=()=>update({analysis:{energy:0.8,bins:Array(32).fill(0.8)}});
      });
      await page.waitForFunction(()=>document.querySelector('iframe')?.contentDocument?.querySelector('#reference-stage[data-reference-state="ready"]'));
      const frame=page.frames().find(item=>item.url().includes('/reference/index.html'));
      await frame.waitForSelector('canvas');
      const settle=()=>page.evaluate(async()=>{for(let i=0;i<12;i++)await new Promise(requestAnimationFrame);});
      const pixels=()=>frame.evaluate(()=>testPixels());
      const clear=async()=>{await settle();assert.equal(await pixels(),0,'stopped/missing analysis must leave no audio pixels');};
      const active=async()=>{await frame.waitForFunction(()=>testPixels()>100);return pixels();};
      await clear();
      await page.screenshot({path:path.join(evidence,`zero-${width}.png`)});
      await frame.evaluate(()=>{window.testCanvas=document.querySelector('canvas');});
      const renders=await frame.locator('#reference-stage').getAttribute('data-reference-renders');
      await page.evaluate(()=>fresh());
      const painted=await active();
      await page.evaluate(async()=>{
        for(let i=0;i<90;i++){
          update({currentTime:13+i/60,analysis:{energy:0.4+i/180,bins:Array(32).fill(0.4+i/180)}});
          await new Promise(requestAnimationFrame);
        }
      });
      assert.equal(await frame.locator('#reference-stage').getAttribute('data-reference-renders'),renders);
      assert.equal(await frame.evaluate(()=>testCanvas===document.querySelector('canvas')),true,'audio updates retain the canvas');
      await frame.waitForFunction(()=>{
        let opacity=1;
        for(let node=document.querySelector('canvas');node;node=node.parentElement)opacity*=Number(getComputedStyle(node).opacity);
        return opacity>0.99;
      });
      await page.screenshot({path:path.join(evidence,`active-${width}.png`)});
      const activeScreen=PNG.sync.read(await frame.locator('canvas').screenshot());
      await page.evaluate(()=>update({analysis:null}));await clear();
      const silentScreen=PNG.sync.read(await frame.locator('canvas').screenshot());
      assert.equal(activeScreen.data.length,silentScreen.data.length);
      let screenChanged=0;
      for(let i=0;i<activeScreen.data.length;i+=4){
        if(Math.abs(activeScreen.data[i]-silentScreen.data[i])+
          Math.abs(activeScreen.data[i+1]-silentScreen.data[i+1])+
          Math.abs(activeScreen.data[i+2]-silentScreen.data[i+2])>45)screenChanged++;
      }
      assert.ok(screenChanged>100,`analysis changes visible screen pixels, not just the offscreen buffer (${screenChanged})`);
      await page.evaluate(()=>fresh());await active();
      const geometry=await frame.evaluate(()=>{
        const rect=document.querySelector('canvas').getBoundingClientRect();
        return {overflow:document.documentElement.scrollWidth>innerWidth,canvasWidth:rect.width,canvasHeight:rect.height,
          inside:rect.left>=0&&rect.top>=0&&rect.right<=innerWidth+1&&rect.bottom<=innerHeight+1};
      });
      assert.equal(geometry.overflow,false);
      assert.equal(geometry.inside,true);
      assert.ok(geometry.canvasWidth>0&&geometry.canvasHeight>0);
      const beforePackets=await frame.evaluate(()=>testDeltas.filter(delta=>delta.analysis!==undefined).length);
      await page.evaluate(async()=>{for(let i=0;i<30;i++){update({currentTime:14.5+i/60});await new Promise(requestAnimationFrame);}});
      assert.equal(await frame.evaluate(()=>testDeltas.filter(delta=>delta.analysis!==undefined).length),beforePackets,'clock ticks omit analysis payload');

      await page.evaluate(()=>update({playing:false}));
      await clear();
      await page.evaluate(()=>update({playing:true}));
      await clear();
      await page.evaluate(()=>fresh());await active();
      await page.evaluate(()=>update({visible:false}));
      await frame.waitForSelector('#reference-stage[data-reference-state="hidden"]');
      assert.equal(await frame.locator('canvas').count(),0);
      await settle();
      const stopped=await frame.evaluate(()=>testCanvas.testClears);
      await settle();
      assert.equal(await frame.evaluate(()=>testCanvas.testClears),stopped,'hidden renderer stops its canvas loop');
      await page.evaluate(()=>fresh());
      await page.evaluate(()=>update({visible:true}));
      await frame.waitForSelector('canvas');
      await clear();
      await page.evaluate(()=>fresh());await active();
      await page.evaluate(()=>update({current:{...fixture.current,id:'next',source:'online:next'}}));
      await clear();
      await page.evaluate(()=>update({currentTime:0}));await clear();
      await page.evaluate(()=>fresh());await active();

      // Exercise document visibility independently of the native visible flag.
      await page.evaluate(()=>{
        Object.defineProperty(document,'hidden',{configurable:true,value:true});
        document.dispatchEvent(new Event('visibilitychange'));
      });
      await frame.waitForSelector('#reference-stage[data-reference-state="hidden"]');
      await page.evaluate(()=>{
        delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));
      });
      await frame.waitForSelector('canvas');await clear();
      await page.evaluate(()=>fresh());await active();
      await frame.evaluate(()=>{
        Object.defineProperty(document,'hidden',{configurable:true,value:true});
        document.dispatchEvent(new Event('visibilitychange'));
      });
      assert.equal(await frame.locator('canvas').count(),0);
      await page.evaluate(()=>fresh());
      await settle();
      await frame.evaluate(()=>{
        delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));
      });
      await frame.waitForSelector('canvas');await clear();
      await page.evaluate(()=>update({analysis:{energy:0.9,bins:Array(32).fill(0.9)}}));await active();
      await page.evaluate(()=>update({analysis:null}));await clear();
      await page.screenshot({path:path.join(evidence,`cleared-${width}.png`)});
      if(width===1280)for(const artwork of coverUrls) {
        await page.evaluate(artwork=>update({current:{...fixture.current,artwork}}),artwork);
        await frame.waitForFunction(artwork=>Array.from(document.images).some(image=>
          image.src===artwork&&image.complete&&image.naturalWidth===16),artwork);
      }
      await page.evaluate(()=>{mounted.dispose();update({playing:true});});
      assert.equal(await page.locator('iframe').count(),0);
      assert.deepEqual(remote,[],'no remote requests or playback URLs');
      assert.deepEqual(errors,[]);
      report.push({width,height,painted,screenChanged,rootRendersDuringAudio:Number(renders),geometry,errors});
      console.log(`analysis/${width}: ${painted} canvas pixels, ${screenChanged} visible changed pixels, stable React root and canvas`);
      await page.close();
    }
    await writeFile(path.join(evidence,'edge-report.json'),JSON.stringify(report,null,2)+'\n');
    console.log(`analysis evidence: ${evidence}`);
  }finally{
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
  }
});
