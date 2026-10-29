import test from 'node:test';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('production browser toggles music and wallpaper surfaces through repeated and fullscreen interactions',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:180000},async t=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const evidence=path.resolve(ui,`../../build-artifacts/native-rebuild/music-navigation-${new Date().toISOString().replace(/[:.]/g,'-')}`);
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
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    browser=await chromium.launch({headless:true,channel:'msedge'});
    const page=await browser.newPage({viewport:{width:1440,height:900}}),errors=[],csp=[];
    page.setDefaultTimeout(15000);
    page.on('pageerror',error=>errors.push(error.message));
    page.on('console',message=>{if(message.type()==='error'&&/Content Security Policy|Refused to|lume reference/i.test(message.text()))csp.push(message.text());});
    await page.addInitScript(()=>{
      const listeners=new Set();
      window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
      window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),removeEventListener:(_,listener)=>listeners.delete(listener),
        postMessage:message=>harness.commands.push(message),postMessageWithAdditionalObjects:message=>harness.commands.push(message)}};
    });
    await page.addInitScript(installMusicPreferencesHarness);
    await page.goto(`http://127.0.0.1:${server.address().port}/index.html`);
    await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
    await page.evaluate(()=>harness.receive({version:1,type:'wallpaper-state',revision:1,status:'idle',error:'',
      loadStatus:'loaded',playerAvailable:true,catalogStatus:'ready',catalogError:'',
      catalog:[{id:'fixture-wallpaper',title:'Forest preview',kind:'video',
        thumbnail:'https://static.lumedesktop.com/forest.jpg',url:'https://static.lumedesktop.com/forest.mp4',
        width:1920,height:1080,bytes:1024,libraryId:''}],library:[],likedIds:[],
      displays:[{id:'1',name:'Main display',width:1920,height:1080,primary:true,wallpaperId:'',state:'stopped',
        autoPaused:false,fit:'cover',volume:0,error:''}],
      download:{id:'',status:'idle',received:0,total:0,error:''},pauseFullscreen:false}));
    await page.locator('[data-view="wallpaperOfficial"]').click();
    await page.locator('[data-wallpaper-id="fixture-wallpaper"]').click();
    await page.locator('[data-wallpaper-detail]').waitFor();
    const wallpaperCommands=()=>page.evaluate(()=>harness.commands.filter(command=>command&&command.scope==='wallpaper'));
    await page.waitForFunction(()=>harness.commands.some(command=>command?.scope==='wallpaper'&&command.action==='preview'));
    const previewCount=async()=>(await wallpaperCommands()).filter(command=>command.action==='preview').length;
    const endPreviewCount=async()=>(await wallpaperCommands()).filter(command=>command.action==='endPreview').length;
    const messages=()=>page.evaluate(()=>harness.commands.filter(value=>typeof value==='string'));
    const musicCommands=()=>page.evaluate(()=>harness.commands.filter(value=>value&&typeof value==='object'&&value.scope==='music'));
    const commandCounts=async()=>{
      const commands=await musicCommands();
      return {open:commands.filter(command=>command.action==='open').length,
        close:commands.filter(command=>command.action==='close').length};
    };
    const commandChecks=[];
    const assertRoundTrip=async(scenario,before)=>{
      const after=await commandCounts(),delta={open:after.open-before.open,close:after.close-before.close};
      assert.deepEqual(delta,{open:1,close:1},`${scenario}: one open and close per visit`);
      assert.equal(await page.locator('[data-music-root]').count(),0,`${scenario}: music view is disposed`);
      assert.equal(page.frames().length,1,`${scenario}: lyric iframe is disposed`);
      commandChecks.push({scenario,...delta});
      return delta;
    };
    const firstPreviewCount=await previewCount(),firstEndPreviewCount=await endPreviewCount();
    const launcher=page.locator('[data-titlebar-music]');
    const firstVisit=await commandCounts();
    await launcher.click();
    await page.locator('[data-music-root]').waitFor();
    assert.equal(await endPreviewCount(),firstEndPreviewCount+1,'entering music releases the wallpaper preview');
    assert.equal(await previewCount(),firstPreviewCount);
    await page.evaluate(async()=>{
      const {initialMusicState}=await import('./music.js');
      const {query,nav,lyrics,...native}=initialMusicState();
      const track={id:'fixture',title:'Reference track',artist:'Lume',album:'Test',source:'C:/Music/fixture.mp3',artwork:'',duration:180};
      window.snapshot={...native,version:1,type:'music-state',session:12,revision:1,pageOpen:true,pageVisible:true,
        status:'playing',playing:true,current:track,queue:[track],timeline:{position:36,duration:180},
        capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true}};
      window.lyricsMessage={version:1,type:'music-lyrics',session:12,trackId:track.id,
        lines:[{startTime:0,endTime:180,fullText:'Original lyric response',translation:'',words:[]}]};
      harness.receive(snapshot);
      harness.receive(lyricsMessage);
    });
    await page.locator('[data-music-action="open-player"]').click();
    await page.locator('[data-music-action="lyrics"]').click();
    await page.locator('[data-music-visualizer-trigger]').click();
    await page.locator('[data-music-visualizer="gallery"]').click();
    await page.waitForFunction(()=>document.querySelector('[data-music-stage]')?.dataset.visualizerState==='ready');
    assert.match(await page.locator('[data-gallery-lyric-line]').first().textContent(),/Original lyric response/);
    await launcher.click();
    await page.locator('[data-wallpaper-detail]').waitFor();
    assert.equal(await endPreviewCount(),firstEndPreviewCount+1);
    assert.equal(await previewCount(),firstPreviewCount+1,'returning to wallpaper detail acquires a fresh preview lease');
    await assertRoundTrip('wallpaper-detail-lyrics',firstVisit);
    await page.evaluate(()=>harness.receive({...window.lyricsMessage,
      lines:[{startTime:0,endTime:180,fullText:'Stale lyric response',translation:'',words:[]}]}));
    const secondPreviewCount=await previewCount(),secondEndPreviewCount=await endPreviewCount();
    const secondVisit=await commandCounts();
    await launcher.click();
    await page.locator('[data-music-stage]').waitFor();
    assert.equal(await endPreviewCount(),secondEndPreviewCount+1,'reentering music releases the restored preview');
    assert.equal(await previewCount(),secondPreviewCount);
    await page.waitForFunction(()=>document.querySelector('[data-music-stage]')?.dataset.visualizerState==='ready');
    assert.equal(await page.locator('[data-music-stage] [data-gallery-lyric-line]').count(),0,
      'a lyric response arriving while the music page is closed is discarded');
    await page.evaluate(()=>harness.receive({...window.lyricsMessage,
      lines:[{startTime:0,endTime:180,fullText:'Fresh lyric response',translation:'',words:[]}]}));
    await page.waitForFunction(()=>document.querySelector('[data-gallery-lyric-line]'));
    assert.match(await page.locator('[data-gallery-lyric-line]').first().textContent(),/Fresh lyric response/);
    await page.locator('[data-music-panel-tab="globalSettings"]').press('Escape');
    await page.locator('.MusicPanelDismiss').waitFor({state:'detached'});
    await page.locator('[data-music-action="back-home"]').click();
    await launcher.click();
    await page.locator('[data-wallpaper-detail]').waitFor();
    assert.equal(await endPreviewCount(),secondEndPreviewCount+1);
    assert.equal(await previewCount(),secondPreviewCount+1,'a second music visit releases and restores the wallpaper preview');
    await assertRoundTrip('wallpaper-detail-late-lyrics',secondVisit);
    const wallpaperPreviewLease={roundTrips:2,previewDelta:await previewCount()-firstPreviewCount,
      endPreviewDelta:await endPreviewCount()-firstEndPreviewCount};
    await page.locator('[data-action="back"]').click();
    await page.locator('[data-view="wallpaperLocal"]').click();
    await page.locator('[data-wallpaper-library]').waitFor();
    assert.equal(await launcher.getAttribute('aria-label'),'音乐');
    const errorVisit=await commandCounts();
    await launcher.click();
    await page.locator('[data-music-root]').waitFor();
    assert.equal(await launcher.getAttribute('aria-label'),'壁纸');
    assert.match(await launcher.locator('path').getAttribute('d'),/^M64 48H448/);
    await page.evaluate(async()=>{
      const {initialMusicState}=await import('./music.js');
      const {query,nav,lyrics,...native}=initialMusicState();
      const track={id:'fixture',title:'Reference track',artist:'Lume',album:'Test',source:'C:/Music/fixture.mp3',artwork:'',duration:180};
      window.snapshot={...native,version:1,type:'music-state',session:12,revision:1,pageOpen:true,pageVisible:true,
        status:'playing',playing:true,current:track,queue:[track],timeline:{position:36,duration:180},
        capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true}};
      harness.receive(snapshot);
      window.lyricsMessage={version:1,type:'music-lyrics',session:12,trackId:track.id,
        lines:[{startTime:0,endTime:180,fullText:'A line for the stage',translation:'',words:[]}]};
      harness.receive(lyricsMessage);
    });
    await page.locator('[data-music-action="open-player"]').click();
    await page.locator('[data-music-action="lyrics"]').click();
    await page.locator('[data-music-visualizer-trigger]').click();
    await page.locator('[data-music-visualizer="classic"]').click();
    await page.locator('.MusicReferenceStage').waitFor();
    const playingSnapshot=await page.evaluate(()=>window.snapshot);
    const publishSnapshot=fields=>page.evaluate(fields=>{
      window.snapshot={...window.snapshot,...fields,revision:window.snapshot.revision+1};
      harness.receive(window.snapshot);
    },fields);
    const before=await messages();
    assert.ok(before.includes('v1|window|dropzone-local'));
    assert.ok(before.includes('v1|window|dropzone-music'));
    await publishSnapshot({status:'error',playing:false,error:'playback-failed'});
    assert.equal(await page.locator('.MusicPlayerStatus').textContent(),'音乐操作未成功');
    await launcher.click();
    await page.locator('[data-wallpaper-library]').waitFor();
    assert.equal(await launcher.getAttribute('aria-label'),'音乐');
    const after=await messages();
    assert.equal(after.at(-1),'v1|window|dropzone-local','the local wallpaper drop target regains ownership');
    await assertRoundTrip('error',errorVisit);
    await mkdir(evidence,{recursive:true});
    await page.screenshot({path:path.join(evidence,'wallpaper-return.png'),fullPage:true});

    const queueVisit=await commandCounts();
    await launcher.click();
    await page.locator('[data-music-root]').waitFor();
    await publishSnapshot({status:'playing',playing:true,current:{id:'fixture',title:'Reference track',artist:'Lume',
      album:'Test',source:'C:/Music/fixture.mp3',artwork:'',duration:180},queue:[{id:'fixture',title:'Reference track',
      artist:'Lume',album:'Test',source:'C:/Music/fixture.mp3',artwork:'',duration:180}],timeline:{position:36,duration:180},
      capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true},error:''});
    await page.evaluate(()=>harness.receive(window.lyricsMessage));
    await page.locator('[data-music-panel-tab="globalSettings"]').press('Escape');
    await page.locator('[data-music-action="queue"]').click();
    await page.locator('.MusicQueuePanel').waitFor();
    await launcher.click();
    await page.locator('[data-wallpaper-library]').waitFor();
    await assertRoundTrip('queue',queueVisit);
    const emptyVisit=await commandCounts();
    await launcher.click();
    await page.locator('.MusicQueuePanel').waitFor();
    await publishSnapshot({status:'empty',playing:false,current:null,queue:[],timeline:{position:0,duration:0},
      capabilities:{canToggle:false,canSeek:false,canNext:false,canPrevious:false}});
    await page.locator('.MusicEmptyQueue').waitFor();
    assert.equal(await page.locator('.MusicEmptyQueue').textContent(),'暂无歌曲');
    await launcher.click();
    await page.locator('[data-wallpaper-library]').waitFor();
    await assertRoundTrip('empty',emptyVisit);

    const threeDVisit=await commandCounts();
    await launcher.click();
    await page.locator('[data-music-root]').waitFor();
    await publishSnapshot({status:'playing',playing:true,current:{id:'fixture',title:'Reference track',artist:'Lume',
      album:'Test',source:'C:/Music/fixture.mp3',artwork:'',duration:180},queue:[{id:'fixture',title:'Reference track',
      artist:'Lume',album:'Test',source:'C:/Music/fixture.mp3',artwork:'',duration:180}],timeline:{position:36,duration:180},
      capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true},error:''});
    await page.evaluate(()=>harness.receive(window.lyricsMessage));
    await page.locator('aside [data-music-action="close-panel"]').click();
    await page.locator('[data-music-action="lyrics"]').click();
    await page.locator('[data-music-visualizer-trigger]').click();
    await page.locator('[data-music-visualizer="3d"]').click();
    await page.waitForFunction(()=>['ready','error','empty'].includes(document.querySelector('[data-music-stage]')?.dataset.visualizerState));
    assert.equal(await page.locator('[data-music-stage]').getAttribute('data-visualizer-state'),'ready',
      JSON.stringify(await page.locator('[data-music-stage]').evaluate(stage=>({mode:stage.dataset.visualizerMode,
        state:stage.dataset.visualizerState,text:stage.textContent,player:document.querySelector('.MusicNowPlayingMeta strong')?.textContent}))));
    assert.ok(await page.locator('[data-music-stage] canvas').count()>0,'the 3D lyric stage owns a canvas');
    await page.locator('[data-music-panel-tab="globalSettings"]').press('Escape');
    await launcher.click();
    await page.locator('[data-wallpaper-library]').waitFor();
    await assertRoundTrip('3d',threeDVisit);

    const classicVisit=await commandCounts();
    await launcher.click();
    await page.locator('[data-music-root]').waitFor();
    await page.locator('[data-music-action="lyrics"]').click();
    await page.locator('[data-music-visualizer-trigger]').click();
    await page.locator('[data-music-visualizer="classic"]').click();
    await page.waitForFunction(()=>document.querySelector('[data-music-stage]')?.dataset.visualizerMode==='classic');
    await launcher.click();
    await page.locator('[data-wallpaper-library]').waitFor();
    await assertRoundTrip('classic',classicVisit);

    for(let i=0;i<20;i++) {
      const beforeRoundTrip=await commandCounts();
      await launcher.click();
      await page.locator('[data-music-root]').waitFor();
      assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'player');
      await publishSnapshot({timeline:{position:37+i,duration:180}});
      assert.equal(await page.locator('[data-music-progress]').inputValue(),String(37+i));
      await launcher.click();
      await page.locator('[data-wallpaper-library]').waitFor();
      await assertRoundTrip(`repeated-${i+1}`,beforeRoundTrip);
    }

    await page.evaluate(()=>harness.receive({version:1,type:'window',fullscreen:true}));
    const fullscreenCases=[];
    for(const state of ['home','player','lyrics','queue','empty','error']) {
      for(const input of ['mouse','keyboard']) {
        await t.test(`fullscreen ${state} returns to wallpaper via ${input}`,async()=>{
          const beforeCase=await commandCounts();
          await launcher.click();
          await page.locator('[data-music-root]').waitFor();
          const {revision,...playingFields}=playingSnapshot;
          await publishSnapshot(playingFields);
          await page.evaluate(()=>harness.receive(window.lyricsMessage));
          if(await page.locator('[data-music-panel]').count()) {
            const currentPanel=await page.locator('[data-music-panel]').getAttribute('data-music-panel');
            if(currentPanel==='lyrics')
              await page.locator('.MusicPanelDismiss').click({position:{x:700,y:400}});
            else await page.locator('aside [data-music-action="close-panel"]').click();
            await page.locator('.MusicPanelDismiss').waitFor({state:'detached'});
          }
          const screen=state==='home'?'home':'player';
          if(await page.locator('[data-music-root]').getAttribute('data-screen')!==screen)
            await page.locator(`[data-music-action="${screen==='home'?'back-home':'open-player'}"]`).click();
          const panel=state==='lyrics'?'lyrics':['queue','empty'].includes(state)?'queue':'';
          if(panel)await page.locator(`[data-music-action="${panel}"]`).click();
          if(state==='empty')await publishSnapshot({status:'empty',playing:false,current:null,queue:[],
            timeline:{position:0,duration:0},capabilities:{canToggle:false,canSeek:false,canNext:false,canPrevious:false}});
          if(state==='error')await publishSnapshot({status:'error',playing:false,error:'playback-failed'});
          assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),screen);
          assert.equal(await page.locator('[data-music-panel]').count(),panel?1:0);
          if(panel) {
            await page.locator(`[data-music-panel="${panel}"]`).waitFor();
            assert.equal(await page.locator('.MusicPanelDismiss').isVisible(),true);
          }
          if(state==='empty') {
            assert.equal(await page.locator('.MusicEmptyQueue').textContent(),'暂无歌曲');
            assert.equal(await page.locator('[data-music-action="toggle"]').isDisabled(),true);
            assert.equal(await page.locator('[data-music-stage]').getAttribute('data-visualizer-state'),'empty');
          } else if(screen==='player') {
            await page.frameLocator('.MusicReferenceStage').locator('#reference-stage[data-reference-state="ready"]').waitFor();
            assert.equal(await page.locator('.MusicReferenceStage').count(),1);
          }
          assert.equal(await page.locator('.MusicPlayerStatus').textContent(),state==='error'?'音乐操作未成功':'');
          assert.equal(await launcher.getAttribute('aria-label'),'壁纸');
          assert.equal((await messages()).at(-1),'v1|window|dropzone-music');

          const focusTarget=panel==='lyrics'?'[data-music-panel-tab][aria-pressed="true"]':
            panel?'aside [data-music-action="close-panel"]':
            screen==='home'?'[data-music-search]':'[data-music-action="back-home"]';
          await page.locator(focusTarget).focus();
          assert.equal(await page.locator(focusTarget).evaluate(element=>element===document.activeElement),true);
          await page.mouse.move(1000,500);
          await page.waitForFunction(()=>document.querySelector('[data-lume-shared-titlebar]').dataset.visible==='false');
          assert.equal(await launcher.getAttribute('tabindex'),'-1');
          if(input==='mouse') {
            const bounds=await launcher.boundingBox();
            assert.ok(bounds);
            await page.mouse.move(bounds.x+bounds.width/2,1);
            await page.waitForFunction(()=>document.querySelector('[data-lume-shared-titlebar]').dataset.visible==='true');
            await launcher.click();
          } else {
            await page.keyboard.press('F6');
            assert.equal(await page.locator('[data-lume-shared-titlebar]').evaluate(root=>root.contains(document.activeElement)),true,
              `${state}: F6 reaches an enabled titlebar control`);
            await page.waitForFunction(()=>document.querySelector('[data-lume-shared-titlebar]').dataset.visible==='true');
            let keyboardReached=false;
            for(let i=0;i<8;i++) {
              if(await launcher.evaluate(element=>element===document.activeElement)){keyboardReached=true;break;}
              await page.keyboard.press('Tab');
            }
            assert.equal(keyboardReached,true,`${state}: ${await page.evaluate(()=>document.activeElement?.className)}`);
            await page.keyboard.press('Enter');
          }
          await page.locator('[data-wallpaper-library]').waitFor();
          assert.equal(await launcher.getAttribute('aria-label'),'音乐');
          assert.equal((await messages()).at(-1),'v1|window|dropzone-local');
          const delta=await assertRoundTrip(`fullscreen-${state}-${input}`,beforeCase);
          fullscreenCases.push({state,input,screen,panel,focusTarget,titlebarInitiallyHidden:true,
            returnedTo:'wallpaperLocal',wallpaperDropZoneRestored:true,commands:delta});
        });
      }
    }
    assert.equal(fullscreenCases.length,12);
    assert.deepEqual(errors,[]);
    assert.deepEqual(csp,[]);
    const totals=await commandCounts();
    await writeFile(path.join(evidence,'report.json'),JSON.stringify({verifiedAt:new Date().toISOString(),
      browser:{channel:'msedge',version:browser.version(),headless:true,viewport:page.viewportSize()},
      verificationScope:{productionUI:true,bridgeHarness:true,fullscreen:'injected window message',
        audioPlaybackVerified:false,nativeHostPidVerified:false,desktopWallpaperPlaybackVerified:false,
        webView2WindowVerified:false,iframeFocusedKeyboardVerified:false},
      roundTrips:20,fullscreenMouse:true,fullscreenKeyboard:true,fullscreenCases,
      wallpaperPreviewLease,
      lateLyrics:{discardedWhileClosed:true,freshResponseRenderedAfterReopen:true},
      visualizers:['gallery','classic','3d'],wallpaperDropZoneRestored:true,commandChecks,
      musicOpenCount:totals.open,musicCloseCount:totals.close,pageErrors:errors,cspErrors:csp},null,2)+'\n');
    await page.close();
  } finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
