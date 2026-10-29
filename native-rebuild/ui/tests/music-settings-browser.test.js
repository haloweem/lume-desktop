import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';

test('music settings preview real lyrics without rebuilding the stage and restore after navigation',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const evidence=path.resolve(ui,`../../build-artifacts/native-rebuild/music-settings-${new Date().toISOString().replace(/[:.]/g,'-')}`);
  const dependencies=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const require=createRequire(import.meta.url),{chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(dependencies,'playwright'));
  const {PNG}=require(path.join(dependencies,'pngjs'));
  await mkdir(evidence,{recursive:true});
  const server=createServer(async(req,res)=>{
    const file=path.resolve(ui,'.'+new URL(req.url,'http://localhost').pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try{res.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css','.html':'text/html','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');res.end(await readFile(file));}
    catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  if(server.address().port===6000) {
    await new Promise(resolve=>server.close(resolve));
    await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  }
  let browser;const report=[];
  try {
    browser=await chromium.launch({channel:'msedge',headless:true});
    for(const [width,height] of [[1440,900],[390,844]]) {
      const page=await browser.newPage({viewport:{width,height}}),errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      await page.addInitScript(()=>{
        const listeners=new Set();
        window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
        window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),removeEventListener:(_,listener)=>listeners.delete(listener),postMessage:message=>harness.commands.push(message)}};
      });
      await page.addInitScript(installMusicPreferencesHarness);
      await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
      await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
      await page.evaluate(async()=>{
        const {initialMusicState}=await import('./music.js');
        const {query,nav,lyrics,...native}=initialMusicState();
        const track={id:'settings-fixture',title:'Settings fixture',artist:'Lume',album:'',source:'C:/fixture.wav',artwork:'',duration:60};
        harness.receive({...native,version:1,type:'music-state',session:1,revision:1,pageOpen:true,pageVisible:true,
          status:'paused',current:track,queue:[track],timeline:{position:3,duration:60},capabilities:{canToggle:true,canPrevious:true,canNext:true,canSeek:true}});
        harness.receive({version:1,type:'navigate',page:'music'});
        window.lyricsFixture={version:1,type:'music-lyrics',session:1,trackId:track.id,
          lines:[{startTime:0,endTime:6,fullText:'A melody in the light',words:[],translation:''}]};
        harness.receive(lyricsFixture);
      });
      await page.locator('[data-music-open-player]').click();
      await page.locator('[data-music-action="lyrics"]').click();
      await page.locator('[data-music-visualizer-trigger]').click();
      await page.locator('[data-music-visualizer="classic"]').click();
      const frame=page.frameLocator('.MusicReferenceStage');
      await frame.locator('#reference-stage[data-reference-state="ready"]').waitFor();
      await page.evaluate(()=>window.originalStage=document.querySelector('.MusicReferenceStage'));
      await page.getByRole('button',{name:'常用字体',exact:true}).click();
      await page.getByRole('option',{name:'Arial',exact:true}).click();
      await page.waitForFunction(()=>harness.commands.some(command=>command?.scope==='music-preferences'&&command.preferences?.fontFamily==='Arial'));
      const stage=page.frames().find(item=>item.url().includes('reference/index.html'));
      await stage.waitForFunction(()=>[...document.querySelectorAll('[style]')].some(node=>node.style.fontFamily.includes('Arial')));
      const saves=()=>page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music-preferences'&&command.action==='apply').length);
      const beforeCount=await saves();
      await page.locator('[data-music-panel-tab="globalSettings"]').press('Escape');
      const before=PNG.sync.read(await page.screenshot({path:path.join(evidence,`before-${width}.png`)}));
      await page.locator('[data-music-action="lyrics"]').click();
      await page.locator('[data-music-panel-tab="controls"]').click();
      await page.locator('[data-music-preference="lyricsFontColor"]').waitFor({state:'visible'});
      await page.locator('[data-music-preference="lyricsFontColor"]').evaluate(input=>{
        input.value='#ff2200';input.dispatchEvent(new Event('input',{bubbles:true}));
      });
      assert.equal(await saves(),beforeCount,'color input previews before change commits');
      await page.locator('[data-music-preference="lyricsFontColor"]').dispatchEvent('change');
      await page.waitForFunction(()=>harness.commands.some(command=>command?.preferences?.lyricsFontColor==='#ff2200'));
      await stage.waitForFunction(()=>[...document.querySelectorAll('[style]')].some(node=>node.style.color==='rgb(255, 34, 0)'));
      await page.locator('[data-music-panel-tab="controls"]').press('Escape');
      const after=PNG.sync.read(await page.screenshot({path:path.join(evidence,`after-${width}.png`)}));
      let redPixels=0;
      for(let i=0;i<after.data.length;i+=4)if(before.data[i+1]>after.data[i+1]+20 && after.data[i]>after.data[i+1]+50)redPixels++;
      assert.ok(redPixels>30,`real lyric pixels change color: ${redPixels}`);
      assert.equal(await page.evaluate(()=>document.querySelector('.MusicReferenceStage')===originalStage),true);
      await page.locator('[data-titlebar-music]').click();await page.locator('[data-titlebar-music]').click();
      await page.evaluate(()=>harness.receive(lyricsFixture));
      await page.locator('[data-music-action="lyrics"]').click();
      assert.equal(await page.locator('[data-music-preference="fontFamily"]').inputValue(),'Arial');
      assert.equal(await page.locator('[data-music-preference="lyricsFontColor"]').inputValue(),'#ff2200');
      const overflow=await page.locator('[data-music-settings]').evaluate(root=>root.scrollWidth>root.clientWidth);
      assert.equal(overflow,false);assert.deepEqual(errors,[]);
      report.push({width,height,redPixels,iframeRetained:true,settingsRestored:true,errors});
      await page.close();
    }
    await writeFile(path.join(evidence,'report.json'),JSON.stringify(report,null,2)+'\n');
  } finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
