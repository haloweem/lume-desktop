import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';

test('真实 Edge 验证播放器右侧面板的 tab、关闭焦点、iframe 连续性与音乐壁纸往返',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:180000},async t=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const evidence=path.resolve(ui,`../../build-artifacts/native-rebuild/music-player-panel-${new Date().toISOString().replace(/[:.]/g,'-')}`);
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const server=createServer(async(req,res)=>{
    const file=path.resolve(ui,'.'+new URL(req.url,'http://localhost').pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
  let browser;
  try {
    browser=await chromium.launch({headless:true,channel:'msedge'});
    const page=await browser.newPage({viewport:{width:1440,height:900}});
    const errors=[],csp=[];
    page.setDefaultTimeout(15000);
    page.on('pageerror',error=>errors.push(error.message));
    page.on('console',message=>{
      if(message.type()==='error'&&/Content Security Policy|Refused to|lume reference/i.test(message.text()))
        csp.push(message.text());
    });
    await page.addInitScript(()=>{
      const listeners=new Set();
      window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
      window.chrome={webview:{
        addEventListener:(_,listener)=>listeners.add(listener),
        removeEventListener:(_,listener)=>listeners.delete(listener),
        postMessage:message=>harness.commands.push(message),
        postMessageWithAdditionalObjects:message=>harness.commands.push(message),
      }};
    });
    await page.addInitScript(installMusicPreferencesHarness);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
    await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
    await page.evaluate(async()=>{
      const {initialMusicState}=await import('./music.js');
      const {query,nav,lyrics,...native}=initialMusicState();
      const track={id:'panel-fixture',title:'Panel fixture',artist:'Lume',album:'Reference',
        source:'C:/Music/panel-fixture.mp3',artwork:'',duration:180};
      window.panelSnapshot={...native,version:1,type:'music-state',session:22,revision:1,
        pageOpen:true,pageVisible:true,status:'paused',playing:false,current:track,queue:[track],
        timeline:{position:24,duration:180},
        capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true}};
      window.panelLyrics={version:1,type:'music-lyrics',session:22,trackId:track.id,
        lines:[{startTime:0,endTime:180,fullText:'Panel continuity line',translation:'',words:[]}]};
      harness.receive({version:1,type:'navigate',page:'music'});
      harness.receive(panelSnapshot);
      harness.receive(panelLyrics);
    });

    await page.locator('[data-music-action="open-player"]').click();
    await page.locator('[data-music-action="lyrics"]').click();
    await page.locator('[data-music-player-panel]').waitFor();
    const panelGeometry=await page.locator('[data-music-player-panel]').evaluate(panel=>{
      const rect=panel.getBoundingClientRect();
      return {width:rect.width,right:innerWidth-rect.right,bottom:innerHeight-rect.bottom,
        backdrop:getComputedStyle(panel).backdropFilter};
    });
    assert.deepEqual(panelGeometry,{width:320,right:32,bottom:40,backdrop:'blur(24px)'});

    const tabIds=['cover','controls','globalSettings','account'];
    for(const id of tabIds) {
      const tab=page.locator(`[data-music-panel-tab="${id}"]`);
      await tab.click();
      assert.equal(await tab.getAttribute('aria-pressed'),'true',`${id} selected`);
      assert.equal(await page.locator(`[data-music-panel-content="${id}"]`).isVisible(),true,`${id} content visible`);
      for(const other of tabIds.filter(value=>value!==id))
        assert.equal(await page.locator(`[data-music-panel-content="${other}"]`).isVisible(),false,`${other} hidden`);
    }

    const tab=page.locator('[data-music-panel-tab="account"]');
    await tab.focus();
    await page.keyboard.press('ArrowLeft');
    assert.equal(await page.locator('[data-music-panel-tab="globalSettings"]').getAttribute('aria-pressed'),'true');
    await page.keyboard.press('End');
    assert.equal(await page.locator('[data-music-panel-tab="account"]').getAttribute('aria-pressed'),'true');

    await page.locator('[data-music-panel-tab="account"]').press('Escape');
    await page.waitForSelector('[data-music-player-panel]',{state:'detached'});
    assert.equal(await page.locator('[data-music-action="lyrics"]').evaluate(element=>element===document.activeElement),true);
    await page.locator('[data-music-action="lyrics"]').click();
    assert.equal(await page.locator('[data-music-panel-tab="account"]').getAttribute('aria-pressed'),'true');

    const frameBefore=page.frames().find(frame=>frame.url().endsWith('#classic'));
    assert.ok(frameBefore,'classic renderer iframe exists');
    await frameBefore.evaluate(()=>window.panelFrameToken='retained');
    const referenceLoadsBefore=await page.locator('.MusicReferenceStage').count();
    await page.locator('[data-music-panel-tab="controls"]').click();
    await page.locator('[data-music-panel-tab="globalSettings"]').click();
    await page.evaluate(()=>harness.receive({...window.panelSnapshot,revision:2,timeline:{position:25,duration:180}}));
    assert.equal(await frameBefore.evaluate(()=>window.panelFrameToken),'retained');
    assert.equal(await page.locator('.MusicReferenceStage').count(),referenceLoadsBefore);
    assert.equal(await frameBefore.evaluate(()=>window.parent===window.top),true);

    await page.locator('.MusicPanelDismiss').click();
    await page.waitForSelector('[data-music-player-panel]',{state:'detached'});
    await page.locator('[data-music-action="lyrics"]').click();
    await page.locator('[data-music-panel-tab="globalSettings"]').waitFor();
    assert.equal(await frameBefore.evaluate(()=>window.panelFrameToken),'retained','panel close/reopen keeps the iframe');

    await page.mouse.click(160,150);
    await page.waitForSelector('[data-music-player-panel]',{state:'detached'});
    assert.equal(await page.locator('[data-music-action="lyrics"]').evaluate(element=>element===document.activeElement),true);

    await page.locator('[data-music-action="lyrics"]').click();
    assert.equal(await page.locator('[data-music-panel-tab="globalSettings"]').getAttribute('aria-pressed'),'true');
    await page.locator('[data-music-panel-tab="account"]').click();
    await page.locator('[data-titlebar-music]').click();
    await page.waitForSelector('[data-music-root]',{state:'detached'});
    assert.equal(await page.locator('[data-titlebar-music]').getAttribute('aria-label'),'音乐');

    await page.locator('[data-titlebar-music]').click();
    await page.locator('[data-music-player-panel]').waitFor();
    await page.evaluate(()=>harness.receive(panelLyrics));
    await page.waitForSelector('.MusicReferenceStage');
    assert.equal(await page.locator('[data-music-panel-tab="account"]').getAttribute('aria-pressed'),'true');
    assert.equal(await page.locator('[data-music-panel-content="account"]').isVisible(),true);
    assert.equal(await page.locator('.MusicReferenceStage').count(),1,'reopening music owns one fresh renderer iframe');
    assert.equal(await page.locator('[data-music-player-panel]').evaluate(panel=>panel.dataset.panelTab),'account');

    await mkdir(evidence,{recursive:true});
    await page.screenshot({path:path.join(evidence,'panel-account-restored.png'),fullPage:true});
    assert.deepEqual(errors,[]);
    assert.deepEqual(csp,[]);
    await writeFile(path.join(evidence,'report.json'),JSON.stringify({
      verifiedAt:new Date().toISOString(),
      browser:{channel:'msedge',version:browser.version(),headless:true,viewport:page.viewportSize()},
      panelGeometry, tabs:tabIds, keyboardNavigation:true, outsideDismissal:true,
      focusRestoredTo:'lyrics',iframeRetainedAcrossPanelUpdates:true,
      tabRestoredAfterWallpaperRoundTrip:true, pageErrors:errors,cspErrors:csp,
    },null,2)+'\n');
    await page.close();
  } finally {
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
  }
});
