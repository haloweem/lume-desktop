import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('seven historical renderers paint real Edge frames and honor iframe deltas',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:240000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const evidence=path.resolve(ui,`../../build-artifacts/native-rebuild/music-reference-${new Date().toISOString().replace(/[:.]/g,'-')}`);
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
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    if(pathname==='/__reference-test')return res.writeHead(200,{'Content-Type':'text/html'}).end(
      '<!doctype html><html style="color-scheme:dark"><head><meta charset="utf-8"><title>Reference QA</title></head><body style="margin:0;background:#18251d"><div id="host" style="position:fixed;inset:0"></div></body></html>');
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try{res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');res.end(await readFile(file));}
    catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;const report=[];
  try{
    browser=await chromium.launch({headless:true,channel:'msedge'});
    for(const width of [1280,390])for(const mode of ['classic','cadenza','partita','fume','tilt','monet','sonnet']){
      const height=width===390?844:800,page=await browser.newPage({viewport:{width,height}}),errors=[],requests=[];
      page.on('pageerror',error=>errors.push(error.message));
      page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
      page.on('request',request=>requests.push(request.url()));
      await page.goto(`http://127.0.0.1:${server.address().port}/__reference-test`);
      await page.evaluate(mode=>{
        window.messages=[];
        window.addEventListener('message',event=>messages.push(event.data));
        window.fixture={lines:Array.from({length:12},(_,i)=>({startTime:i*6,endTime:i*6+6,
          fullText:['\u98ce\u7a7f\u8fc7\u4e86\u5c71\u8c37','\u7a97\u5916\u4eae\u8d77\u5fae\u5149','A melody through the night'][i%3],words:[],translation:''})),
          current:{id:'reference-fixture',title:'\u5c71\u95f4\u6765\u4fe1',artist:'Lume',album:'Reference',artwork:'assets/music/forest.webp'},
          currentTime:25,activeLineIndex:4,duration:72,playing:true,visible:true};
        const frame=document.createElement('iframe');frame.id='stage';frame.style.cssText='width:100%;height:100%;border:0;display:block';
        frame.setAttribute('sandbox','allow-scripts allow-same-origin');
        frame.src=`/music-visualizers/reference/index.html#${mode}`;
        document.getElementById('host').append(frame);
        window.send=state=>frame.contentWindow.postMessage({type:'lume-lyric-stage',mode,state},location.origin);
      },mode);
      await page.waitForFunction(()=>messages.some(message=>message.type==='lume-lyric-stage-ready'||message.type==='lume-lyric-stage-error'))
        .catch(error=>{throw new Error(`${mode}/${width} did not load: ${errors.join('; ')}`,{cause:error});});
      await page.evaluate(()=>send(fixture));
      const frame=page.frames().find(item=>item.url().includes('/reference/index.html'));
      await frame.waitForSelector('#reference-stage[data-reference-state="ready"]');
      await page.evaluate(async()=>{
        for(let i=0;i<90;i++){
          send({currentTime:25+i/60,lines:undefined,current:undefined,activeLineIndex:4});
          await new Promise(requestAnimationFrame);
        }
      });
      if(mode==='sonnet')await frame.waitForSelector('canvas');
      const before=await page.screenshot({path:path.join(evidence,`${mode}-${width}.png`)});
      const geometry=await frame.evaluate(()=>({rootRenders:Number(document.getElementById('reference-stage').dataset.referenceRenders),
        overflow:document.documentElement.scrollWidth>innerWidth,body:getComputedStyle(document.body).backgroundColor,
        text:document.getElementById('reference-stage').textContent,
        canvases:[...document.querySelectorAll('canvas')].map(canvas=>({width:canvas.width,height:canvas.height,visible:getComputedStyle(canvas).visibility!=='hidden'}))}));
      await frame.locator('#reference-stage').evaluate(element=>element.style.visibility='hidden');
      const empty=await page.screenshot();
      const transparentPixel=PNG.sync.read(empty).data.subarray(0,4);
      assert.deepEqual([...transparentPixel],[24,37,29,255],`${mode}/${width} must composite the actual parent background`);
      await frame.locator('#reference-stage').evaluate(element=>element.style.visibility='');
      const painted=difference(before,empty);
      assert.ok(painted>150,`${mode}/${width} must paint visible content (${painted})`);
      assert.equal(geometry.overflow,false,`${mode}/${width} overflows`);
      assert.equal(geometry.body,'rgba(0, 0, 0, 0)');
      assert.equal(geometry.rootRenders,1,'ordinary clock deltas never root-render');
      await page.evaluate(async()=>{
        send({activeLineIndex:5,currentTime:31});
        for(let i=0;i<60;i++){send({currentTime:31+i/60});await new Promise(requestAnimationFrame);}
      });
      const after=await page.screenshot({path:path.join(evidence,`${mode}-seek-${width}.png`)});
      const changed=difference(before,after);
      assert.ok(changed>100,`${mode}/${width} responds to lyric seek`);
      await page.evaluate(()=>send({visible:false}));
      await frame.waitForSelector('#reference-stage[data-reference-state="hidden"]');
      assert.equal(await frame.locator('#reference-stage > *').count(),0);
      await page.evaluate(()=>send({visible:true,playing:false,currentTime:25,activeLineIndex:4,current:{...fixture.current,artwork:''}}));
      await frame.waitForSelector('#reference-stage[data-reference-state="ready"]');
      await page.evaluate(async()=>{for(let i=0;i<140;i++)await new Promise(requestAnimationFrame);});
      const pausedBefore=await page.screenshot({path:path.join(evidence,`${mode}-paused-${width}.png`)});
      const pausedRenders=await frame.locator('#reference-stage').getAttribute('data-reference-renders');
      await page.evaluate(async()=>{send({currentTime:28.8});for(let i=0;i<30;i++)await new Promise(requestAnimationFrame);});
      const pausedAfter=await page.screenshot({path:path.join(evidence,`${mode}-paused-seek-${width}.png`)});
      const pausedSeekChangedPixels=difference(pausedBefore,pausedAfter);
      assert.ok(pausedSeekChangedPixels>30,`${mode}/${width} paused same-line seek (${pausedSeekChangedPixels})`);
      assert.equal(await frame.locator('#reference-stage').getAttribute('data-reference-renders'),pausedRenders,'paused clock does not root-render');
      await page.evaluate(()=>document.getElementById('stage').remove());
      assert.equal(page.frames().length,1);
      assert.equal(requests.some(url=>/^https?:/.test(url)&&!url.startsWith(`http://127.0.0.1:${server.address().port}/`)),false,'no remote fetch');
      assert.equal(requests.some(url=>/mode-(classic|cadenza|partita|fume|tilt|monet|sonnet)\.js/.test(url)&&!url.endsWith(`mode-${mode}.js`)),false,'only selected renderer requested');
      const failures=await page.evaluate(()=>messages.filter(message=>message.type==='lume-lyric-stage-error'));
      assert.deepEqual(failures,[],`${mode}/${width}: host error`);
      assert.deepEqual(errors,[],`${mode}/${width}: ${errors.join('; ')} requests=${requests.join(',')}`);
      report.push({mode,width,height,painted,seekChangedPixels:changed,pausedSeekChangedPixels,geometry,errors});
      console.log(`${mode}/${width}: ${painted} painted pixels, paused seek ${pausedSeekChangedPixels}`);
      await page.close();
    }
    await writeFile(path.join(evidence,'edge-report.json'),JSON.stringify(report,null,2)+'\n');
  }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
