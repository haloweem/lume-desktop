import test from 'node:test';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('production entry runs all available stages under CSP without panel reloads or native coupling',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:180000},async()=>{
  const source=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const ui=path.resolve(process.env.LUME_UI_ROOT||source);
  const evidence=path.resolve(source,`../../build-artifacts/native-rebuild/music-entry-${process.env.LUME_UI_ROOT?'staged-':''}${new Date().toISOString().replace(/[:.]/g,'-')}`);
  await mkdir(evidence,{recursive:true});
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const require=createRequire(import.meta.url);
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const difference=(a,b)=>{
    const left=PNG.sync.read(a),right=PNG.sync.read(b);let count=0;
    for(let i=0;i<left.data.length;i+=4)
      if(Math.abs(left.data[i]-right.data[i])+Math.abs(left.data[i+1]-right.data[i+1])+Math.abs(left.data[i+2]-right.data[i+2])>45)count++;
    return count;
  };
  const server=createServer(async(req,res)=>{
    const file=path.resolve(ui,'.'+new URL(req.url,'http://localhost').pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  const report=[];
  try {
    browser=await chromium.launch({headless:true,channel:'msedge'});
    for(const [width,height] of [[1536,864],[390,844]]) {
      const page=await browser.newPage({viewport:{width,height}}),errors=[],csp=[],requests=[];
      page.setDefaultTimeout(15000);
      page.on('pageerror',error=>errors.push(error.message));
      page.on('console',message=>{if(message.type()==='error'&&/Content Security Policy|Refused to|lume reference/i.test(message.text()))csp.push(message.text());});
      page.on('request',request=>requests.push(request.url()));
      await page.addInitScript(()=>{
        const listeners=new Set();
        window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
        window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
          removeEventListener:(_,listener)=>listeners.delete(listener),postMessage:message=>harness.commands.push(message)}};
      });
      await page.addInitScript(installMusicPreferencesHarness);
      await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
      await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
      await page.evaluate(async()=>{
        const {initialMusicState}=await import('./music.js');
        const {query,nav,lyrics,...native}=initialMusicState();
        const track={id:'fixture',title:'\u5c71\u95f4\u6765\u4fe1',artist:'Lume',album:'Reference',source:'C:/Music/fixture.mp3',artwork:'',duration:240};
        window.snapshot={...native,version:1,type:'music-state',session:9,revision:1,pageOpen:true,pageVisible:true,
          status:'paused',current:track,queue:[track],timeline:{position:25,duration:240},
          capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true}};
        window.lyricsMessage={version:1,type:'music-lyrics',session:9,trackId:track.id,
          lines:Array.from({length:40},(_,i)=>({startTime:i*6,endTime:i*6+6,fullText:['\u98ce\u7a7f\u8fc7\u4e86\u5c71\u8c37','\u7a97\u5916\u4eae\u8d77\u5fae\u5149','A melody through the night'][i%3],translation:'',words:[]}))};
        harness.receive({version:1,type:'navigate',page:'music'});
        harness.receive(snapshot);harness.receive(lyricsMessage);
      });
      assert.equal(requests.some(url=>url.includes('/music-visualizers/')),false);
      await page.locator('[data-music-action="open-player"]').click();
      await page.evaluate(()=>harness.receive(lyricsMessage));
      for(const mode of ['partita','fume','sonnet','gallery','3d','lumiere']) {
        if(mode!=='partita') {
          await page.locator('[data-music-action="lyrics"]').click();
          await page.locator('[data-music-visualizer-trigger]').click();
          await page.locator(`[data-music-visualizer="${mode}"]`).click();
          await page.locator('[data-music-panel-tab="globalSettings"]').press('Escape');
        }
        await page.waitForFunction(mode=>document.querySelector('[data-music-stage]')?.dataset.visualizerMode===mode,mode);
        if(!['3d','gallery','lumiere'].includes(mode)) {
          const iframe=page.frameLocator('.MusicReferenceStage');
          await iframe.locator('#reference-stage[data-reference-state="ready"]').waitFor();
          const frame=page.frames().find(item=>item.url().endsWith('#'+mode));
          await frame.evaluate(()=>{window.continuityToken='retained';});
          const loadsBefore=requests.filter(url=>url.includes('/reference/index.html')).length;
          await page.locator('[data-music-action="queue"]').click();
          await page.locator('aside [data-music-action="close-panel"]').click();
          await page.evaluate(()=>harness.receive({...snapshot,revision:2,repeat:'one'}));
          assert.equal(await frame.evaluate(()=>window.continuityToken),'retained',`${mode} iframe survives panels and snapshot metadata`);
          assert.equal(requests.filter(url=>url.includes('/reference/index.html')).length,loadsBefore);
          await page.evaluate(()=>harness.receive({version:1,type:'visibility',visible:false}));
          await iframe.locator('#reference-stage[data-reference-state="hidden"]').waitFor({state:'attached'});
          assert.equal(await iframe.locator('#reference-stage > *').count(),0);
          await page.evaluate(()=>harness.receive({version:1,type:'visibility',visible:true}));
          await iframe.locator('#reference-stage[data-reference-state="ready"]').waitFor();
        } else {
          await page.waitForFunction(()=>document.querySelector('[data-music-stage]')?.dataset.visualizerState==='ready');
          if(mode==='lumiere')await page.locator('[data-lumiere-state="ready"] canvas').waitFor();
          assert.equal(page.frames().length,1,'leaving reference mode releases its browsing context');
        }
        await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
        await page.evaluate(async()=>{for(let i=0;i<45;i++)await new Promise(requestAnimationFrame);});
        const painted=await page.screenshot({path:path.join(evidence,`${mode}-${width}.png`)});
        await page.locator('[data-music-stage]').evaluate(stage=>stage.style.visibility='hidden');
        const background=await page.screenshot();
        await page.locator('[data-music-stage]').evaluate(stage=>stage.style.visibility='');
        const changedPixels=difference(painted,background);
        assert.ok(changedPixels>150,`${mode}/${width} paints real lyrics`);
        if(!['gallery','3d','lumiere'].includes(mode))
          assert.ok(changedPixels<width*height*.85,`${mode}/${width} preserves the music backdrop through the iframe (${changedPixels})`);
        const geometry=await page.evaluate(()=>({overflow:document.documentElement.scrollWidth>innerWidth,
          stage:document.querySelector('[data-music-stage]').getBoundingClientRect().toJSON(),
          player:document.querySelector('.MusicPlayerBar').getBoundingClientRect().toJSON()}));
        assert.equal(geometry.overflow,false);
        report.push({mode,width,height,changedPixels,...geometry});
      }
      const actions=await page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music').map(command=>command.action));
      assert.equal(actions.some(action=>['play','pause','toggle','next','previous','setRepeat'].includes(action)),false,'renderer changes never send playback commands');
      await page.locator('[data-music-action="back-home"]').click();
      assert.equal(await page.locator('[data-music-stage]').count(),0);
      assert.equal(page.frames().length,1);
      await page.locator('[data-music-action="open-player"]').click();
      await page.locator('[data-music-action="lyrics"]').click();
      await page.locator('[data-music-visualizer-trigger]').click();
      await page.locator('[data-music-visualizer="partita"]').click();
      await page.locator('[data-music-panel-tab="globalSettings"]').press('Escape');
      const keysFrame=page.frameLocator('.MusicReferenceStage');
      await keysFrame.locator('#reference-stage[data-reference-state="ready"]').waitFor();
      await keysFrame.locator('body').click({position:{x:20,y:120}});
      await page.keyboard.press('Space');
      await page.waitForFunction(()=>harness.commands.some(command=>command?.scope==='music'&&command.action==='toggle'));
      await page.keyboard.press('ArrowRight');
      await page.waitForFunction(()=>harness.commands.some(command=>command?.scope==='music'&&command.action==='seek'&&command.position===30));
      assert.equal(await page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music'&&command.action==='toggle').length),1);
      await page.keyboard.press('Control+k');
      await page.locator('[data-music-search]').waitFor();
      assert.equal(await page.locator('[data-music-search]').evaluate(element=>element===document.activeElement),true);
      assert.equal(page.frames().length,1,'keyboard navigation disposes the iframe too');
      assert.deepEqual(errors,[]);assert.deepEqual(csp,[]);
      await page.close();
    }
    await writeFile(path.join(evidence,'report.json'),JSON.stringify(report,null,2));
  } finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
