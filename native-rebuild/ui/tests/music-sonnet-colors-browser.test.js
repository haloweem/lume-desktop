import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';

test('Sonnet colors preview, commit, reset and survive wallpaper navigation without recreating Pixi',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:180000},async()=>{
  const ui=fileURLToPath(new URL('..',import.meta.url));
  const evidence=path.resolve(ui,`../../build-artifacts/native-rebuild/music-sonnet-colors-${new Date().toISOString().replace(/[:.]/g,'-')}`);
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const require=createRequire(import.meta.url),{chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const difference=(a,b)=>{
    const x=PNG.sync.read(a),y=PNG.sync.read(b);
    assert.equal(x.width,y.width);assert.equal(x.height,y.height);
    let pixels=0;
    for(let i=0;i<x.data.length;i+=4)
      if(Math.abs(x.data[i]-y.data[i])+Math.abs(x.data[i+1]-y.data[i+1])+Math.abs(x.data[i+2]-y.data[i+2])>60)pixels++;
    return pixels;
  };
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(path.resolve(ui)+path.sep))return res.writeHead(403).end();
    try {
      const bytes=await readFile(file);
      res.writeHead(200,{'Content-Type':({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream'}).end(bytes);
    }catch{res.writeHead(404).end();}
  });
  await mkdir(evidence,{recursive:true});
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  const report=[];
  try {
    browser=await chromium.launch({channel:'msedge',headless:true});
    for(const [width,height] of [[1440,900],[390,844]]) {
      const page=await browser.newPage({viewport:{width,height}}),errors=[];
      page.setDefaultTimeout(15000);
      page.on('pageerror',error=>errors.push(error.message));
      page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
      await page.addInitScript(()=>{
        const listeners=new Set();
        window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
        window.chrome={webview:{addEventListener:(_,fn)=>listeners.add(fn),removeEventListener:(_,fn)=>listeners.delete(fn),
          postMessage:message=>harness.commands.push(message)}};
        window.gpuCanvases=new Set();
        const getContext=HTMLCanvasElement.prototype.getContext;
        HTMLCanvasElement.prototype.getContext=function(type,...args){
          const result=getContext.call(this,type,...args);
          if(result&&/webgl/.test(type))gpuCanvases.add(this);
          return result;
        };
      });
      await page.addInitScript(installMusicPreferencesHarness);
      await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
      await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
      await page.evaluate(async()=>{
        const {initialMusicState}=await import('./music.js');
        const {query,nav,lyrics,...native}=initialMusicState();
        const track={id:'sonnet-colors',title:'Color fixture',artist:'Lume',album:'',source:'C:/fixture.wav',artwork:'',duration:72};
        harness.receive({...native,version:1,type:'music-state',session:9,revision:1,pageOpen:true,pageVisible:true,
          status:'paused',playing:false,current:track,queue:[track],timeline:{position:25,duration:72},
          capabilities:{canToggle:true,canPrevious:true,canNext:true,canSeek:true}});
        harness.receive({version:1,type:'navigate',page:'music'});
        window.lyricsFixture={version:1,type:'music-lyrics',session:9,trackId:track.id,
          lines:Array.from({length:12},(_,i)=>({startTime:i*6,endTime:i*6+6,
            fullText:'A melody through the night',translation:'',words:[]}))};
        harness.receive(lyricsFixture);
      });
      const settings=()=>page.locator('[data-music-action="lyrics"]').click();
      const tab=id=>page.locator(`[data-music-panel-tab="${id}"]`).click();
      const close=()=>page.locator('[data-music-panel-tab][aria-pressed="true"]').press('Escape');
      const mode=async id=>{
        await tab('globalSettings');
        await page.locator('[data-music-visualizer-trigger]').click();
        await page.locator(`[data-music-visualizer="${id}"]`).click();
      };
      const settle=()=>page.evaluate(async()=>{for(let i=0;i<80;i++)await new Promise(requestAnimationFrame);});
      const saves=()=>page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music-preferences'&&command.action==='apply'));
      await page.locator('[data-music-open-player]').click();
      await settings();await mode('sonnet');await tab('controls');
      assert.equal(await page.locator('[data-music-color-group="global"]').isVisible(),false);
      for(const [key,value] of Object.entries({lyricsFontColor:'#ffffff',karaokeFillColor:'#38bdf8',glowColor:'#60a5fa'})) {
        const field=page.locator(`[data-music-sonnet-color="${key}"]`);
        assert.equal(await field.isVisible(),true);
        assert.equal(await field.inputValue(),value);
      }
      const frame=page.frames().find(item=>item.url().endsWith('#sonnet'));
      assert.ok(frame);
      await frame.waitForSelector('#reference-stage[data-reference-state="ready"] canvas');
      await settle();
      await frame.evaluate(()=>{
        window.initialCanvas=document.querySelector('canvas');
        window.initialGpuCount=gpuCanvases.size;
        window.initialDocument=document;
      });
      await page.evaluate(()=>window.initialStage=document.querySelector('.MusicReferenceStage'));
      await page.screenshot({path:path.join(evidence,`controls-${width}.png`)});
      await close();await settle();
      const baseline=await frame.locator('canvas').first().screenshot({path:path.join(evidence,`default-${width}.png`)});
      await settings();await tab('controls');
      const fill=page.locator('[data-music-sonnet-color="karaokeFillColor"]');
      const beforeSaves=(await saves()).length;
      const beforeMusic=await page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music'));
      await fill.evaluate(input=>{
        window.initialColorInput=input;
        input.value='#ff2200';input.dispatchEvent(new Event('input',{bubbles:true}));
      });
      await settle();
      assert.equal((await saves()).length,beforeSaves,'input is preview-only');
      assert.equal(await page.evaluate(()=>initialColorInput===document.querySelector('[data-music-sonnet-color="karaokeFillColor"]')),true);
      await fill.dispatchEvent('change');
      await page.waitForFunction(()=>harness.commands.some(command=>command?.preferences?.sonnetColors?.karaokeFillColor==='#ff2200'));
      const saved=(await saves()).at(-1).preferences;
      assert.equal(saved.lyricsFontColor,'#ffffff');assert.equal(saved.lyricsKaraokeFillColor,'#ffffff');
      await page.locator('[data-music-sonnet-reset="glowColor"]').click();
      assert.equal(await page.locator('[data-music-sonnet-color="glowColor"]').inputValue(),'#ffffff');
      assert.equal(await page.locator('[data-music-sonnet-reset="glowColor"]').isDisabled(),true);
      await close();await settle();
      const changed=await frame.locator('canvas').first().screenshot({path:path.join(evidence,`fill-${width}.png`)});
      const changedPixels=difference(baseline,changed);
      assert.ok(changedPixels>30,`Sonnet fill changes lyric canvas pixels: ${changedPixels}`);
      assert.equal(await page.evaluate(()=>initialStage===document.querySelector('.MusicReferenceStage')),true);
      assert.equal(await frame.evaluate(()=>initialCanvas===document.querySelector('canvas')&&initialCanvas.isConnected&&
        initialDocument===document&&initialGpuCount===gpuCanvases.size),true,'theme reuses Pixi canvas and context');
      await settings();await tab('controls');
      await page.locator('[data-music-sonnet-reset="karaokeFillColor"]').click();
      assert.equal(await fill.inputValue(),'#ffffff','row reset matches historical white, not the blue initial default');
      await mode('classic');await tab('controls');
      assert.equal(await page.locator('[data-music-preference="lyricsKaraokeFillColor"]').inputValue(),'#ffffff');
      assert.equal(await page.locator('[data-music-color-group="sonnet"]').isVisible(),false);
      await mode('sonnet');await tab('controls');
      assert.equal(await fill.inputValue(),'#ffffff');
      assert.deepEqual(await page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music')),beforeMusic,
        'color and mode settings issue no music commands');
      await page.locator('[data-titlebar-music]').click();
      await page.waitForSelector('[data-music-root]',{state:'detached'});
      await page.locator('[data-titlebar-music]').click();
      await page.evaluate(()=>harness.receive(lyricsFixture));
      await fill.waitFor({state:'visible'});
      assert.equal(await fill.inputValue(),'#ffffff');
      assert.equal(await page.locator('[data-music-sonnet-color="glowColor"]').inputValue(),'#ffffff');
      await page.frameLocator('.MusicReferenceStage').locator('#reference-stage[data-reference-state="ready"] canvas').waitFor();
      assert.equal(await page.locator('.MusicReferenceStage').count(),1);
      assert.equal(await page.locator('[data-music-color-group="sonnet"]').evaluate(root=>root.scrollWidth>root.clientWidth),false);
      await page.screenshot({path:path.join(evidence,`restored-${width}.png`)});
      const audioCommands=await page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music'&&
        !['open','close','snapshot','loadHome','loadLyrics','visibility'].includes(command.action)));
      assert.deepEqual(audioCommands,[],'preferences never issue playback commands');
      assert.deepEqual(errors,[]);
      report.push({width,height,changedPixels,previewWrites:0,globalColorsUnchanged:true,rowReset:'#ffffff',
        canvasRetained:true,gpuContextRetained:true,wallpaperRoundTrip:true,errors});
      await page.close();
    }
    await writeFile(path.join(evidence,'report.json'),JSON.stringify({browser:browser.version(),scenarios:report},null,2)+'\n');
    console.log(`Sonnet color evidence: ${evidence}`);
  }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
