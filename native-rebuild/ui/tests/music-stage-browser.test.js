import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('real lyric stages render, move, retain their canvas and release hidden work',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1'},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const evidence=path.resolve(ui,`../../build-artifacts/native-rebuild/music-stages-${new Date().toISOString().replace(/[:.]/g,'-')}`);
  await mkdir(evidence,{recursive:true});
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const require=createRequire(import.meta.url);
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR || path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const pixelDifference=(a,b)=>{
    const left=PNG.sync.read(a),right=PNG.sync.read(b);
    assert.equal(left.width,right.width);assert.equal(left.height,right.height);
    let count=0;
    for(let i=0;i<left.data.length;i+=4)
      if(Math.abs(left.data[i]-right.data[i])+Math.abs(left.data[i+1]-right.data[i+1])+Math.abs(left.data[i+2]-right.data[i+2])>45)count++;
    return count;
  };
  const server=createServer(async(req,res)=>{
    const relative=new URL(req.url,'http://localhost').pathname;
    const file=path.resolve(ui,'.'+relative);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try{
      res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)] || 'application/octet-stream');
      res.end(await readFile(file));
    }catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;const report=[];
  try{
    browser=await chromium.launch({headless:true,channel:'msedge'});
    for(const [width,height] of [[1536,864],[390,844]]){
      const page=await browser.newPage({viewport:{width,height}}),errors=[],requests=[];
      page.on('pageerror',error=>errors.push(error.message));
      page.on('request',request=>requests.push(request.url()));
      await page.goto(`http://127.0.0.1:${server.address().port}/music.html`);
      await page.evaluate(async()=>{
        const {mountMusicView}=await import('./music-view.js'),{initialMusicState}=await import('./music.js');
        const {DEFAULT_MUSIC_PREFERENCES}=await import('./music-preferences.js');
        const realRequest=window.requestAnimationFrame.bind(window),realCancel=window.cancelAnimationFrame.bind(window);
        window.stageFrames=new Set();
        window.requestAnimationFrame=callback=>{const id=realRequest(time=>{stageFrames.delete(id);callback(time);});stageFrames.add(id);return id;};
        window.cancelAnimationFrame=id=>{stageFrames.delete(id);realCancel(id);};
        const track={id:'fixture',title:'山间来信',artist:'Lume',album:'参考排版',duration:240,source:'C:/Music/fixture.mp3',artwork:''};
        const phrases=['风穿过了山谷','窗外亮起微光','沿着回忆走向远方','此刻让旋律缓缓流淌','雨落在安静的窗边','一首写给明天的歌'];
        window.stageState={...initialMusicState(),session:9,pageOpen:true,pageVisible:true,current:track,queue:[track],
          timeline:{position:25,duration:240},status:'paused',playing:false,
          capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true},
          lyrics:Array.from({length:40},(_,i)=>({startTime:i*6,endTime:i*6+6,fullText:phrases[i%phrases.length],translation:'',words:[]}))};
        window.stageView=mountMusicView(document.querySelector('#music-preview'),{snapshot:stageState,send:()=>true,
          preferences:{preferences:DEFAULT_MUSIC_PREFERENCES,loaded:true,pending:false}});
      });
      assert.equal(requests.some(url=>/\/music-visualizers\//.test(url)),false,'home does not request a heavy renderer');
      const visibleCanvas='[data-music-stage] canvas:not([aria-hidden="true"])';
      await page.locator('[data-music-action="open-player"]').click();
      await page.locator('[data-music-action="lyrics"]').click();
      await page.locator('[data-music-visualizer-trigger]').click();
      await page.locator('[data-music-visualizer="3d"]').click();
      await page.locator('[data-music-panel-tab="globalSettings"]').press('Escape');
      await page.locator('[data-music-stage][data-visualizer-state="ready"] canvas:not([aria-hidden="true"])').waitFor();
      await page.evaluate(()=>{window.initialCanvas=document.querySelector('[data-music-stage] canvas:not([aria-hidden="true"])');});
      await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
      const before=await page.screenshot({path:path.join(evidence,`3d-${width}.png`)});
      await page.locator(visibleCanvas).evaluate(canvas=>{canvas.style.opacity='0';});
      const background=await page.screenshot();
      await page.locator(visibleCanvas).evaluate(canvas=>{canvas.style.opacity='1';});
      const pixels=pixelDifference(before,background);
      assert.ok(pixels>300,'3D canvas paints actual glyphs');
      const box=await page.locator(visibleCanvas).boundingBox();
      assert.ok(box.width>width*.95 && box.height>height*.9,'3D is full bleed');
      await page.mouse.move(width*.5,height*.4);await page.mouse.down();
      await page.mouse.move(width*.64,height*.48,{steps:8});await page.mouse.up();
      const dragged=await page.screenshot({path:path.join(evidence,`3d-dragged-${width}.png`)});
      assert.ok(pixelDifference(before,dragged)>300,'drag rotates the native reference scene');
      await page.locator('[data-music-action="queue"]').click();
      await page.locator('aside [data-music-action="close-panel"]').click();
      assert.equal(await page.evaluate(()=>document.querySelector('[data-music-stage] canvas:not([aria-hidden="true"])')===initialCanvas),true);
      await page.evaluate(()=>stageView.setVisible(false));
      await page.waitForFunction(()=>stageFrames.size===0);
      await page.evaluate(()=>stageView.setVisible(true));
      await page.locator('[data-music-action="lyrics"]').click();
      await page.locator('[data-music-visualizer-trigger]').click();
      await page.locator('[data-music-visualizer="gallery"]').click();
      await page.locator('[data-music-panel-tab="globalSettings"]').press('Escape');
      await page.waitForFunction(()=>document.querySelector('[data-music-stage]')?.dataset.visualizerState==='ready' &&
        document.querySelector('[data-music-stage]')?.dataset.visualizerMode==='gallery');
      assert.equal(await page.evaluate(()=>initialCanvas.isConnected),false,'the original 3D canvas is detached');
      assert.equal(await page.locator(visibleCanvas).count(),0,'switching releases the 3D canvas');
      await page.locator('[data-gallery-artwork] img:not([hidden])').evaluateAll(images=>
        Promise.all(images.map(image=>image.decode())));
      await page.screenshot({path:path.join(evidence,`gallery-${width}.png`)});
      const geometry=await page.evaluate(()=>{
        const stage=document.querySelector('[data-music-stage]'),footer=document.querySelector('.MusicPlayerBar').getBoundingClientRect();
        return {overflow:document.documentElement.scrollWidth>innerWidth,
          text:stage.textContent,footer:{top:footer.top,bottom:footer.bottom},
          images:[...stage.querySelectorAll('img:not([hidden])')].map(image=>({loaded:image.complete&&image.naturalWidth>0,src:image.getAttribute('src')})),
          sharedBackdropReleased:stage.querySelector('[data-gallery-backdrop]').hidden &&
            !stage.querySelector('[data-gallery-backdrop]').hasAttribute('src')};
      });
      assert.equal(geometry.overflow,false);assert.match(geometry.text,/山间来信/);
      assert.equal(geometry.sharedBackdropReleased,true,'shared background does not retain a duplicate backdrop image');
      assert.equal(geometry.images.length,2,'the sleeve and disc both retain their artwork');
      assert.ok(geometry.images.every(image=>image.loaded),`artwork assets decode: ${JSON.stringify(geometry.images)}`);
      await page.evaluate(()=>stageView.update({...stageState,playing:true,status:'playing'}));
      await page.evaluate(()=>stageView.setVisible(false));
      await page.waitForFunction(()=>stageFrames.size===0);
      await page.locator('[data-music-action="back-home"]').click();
      assert.equal(await page.locator('[data-music-stage]').count(),0);
      assert.equal(await page.evaluate(()=>stageFrames.size),0);
      assert.deepEqual(errors,[]);
      report.push({width,height,canvasPaintedPixels:pixels,geometry});
      await page.close();
    }
    await writeFile(path.join(evidence,'stages.json'),JSON.stringify(report,null,2));
  }finally{await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
