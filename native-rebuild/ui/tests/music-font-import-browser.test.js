import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {DEFAULT_MUSIC_PREFERENCES} from '../music-preference-values.js';

test('production app imports native font assets, hands off leases and restores confirmed metadata',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:120000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||
    path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
  const bytes=await readFile(path.join(ui,'assets/fonts/Quicksand.otf'));
  const id='a'.repeat(32),badId='b'.repeat(32);
  const asset={id,family:`LumeImportedFont_${id}`,label:'Original Quicksand',extension:'otf',size:bytes.length};
  const badAsset={id:badId,family:`LumeImportedFont_${badId}`,label:'Invalid font',extension:'woff2',size:4};
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.html':'text/html; charset=utf-8','.js':'text/javascript',
        '.css':'text/css','.webp':'image/webp','.otf':'font/otf'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser,releaseChild;
  try {
    browser=await chromium.launch({channel:'msedge',headless:true});
    const page=await browser.newPage({viewport:{width:1280,height:900}});
    page.setDefaultTimeout(10000);
    const errors=[],csp=[],commands=[],commits=[],cancelled=[];
    let assets=[],preferences={...structuredClone(DEFAULT_MUSIC_PREFERENCES),mode:'sonnet'},revision=0;
    let chooseMode='success',pendingChoose=null,systemUnavailable=false,fontRequests=0;
    const reply=(request=0,error='',candidate=null)=>({
      version:1,type:'music-font-assets-state',request,error,assets:structuredClone(assets),candidate,
    });
    page.on('pageerror',error=>errors.push(error.message));
    page.on('console',message=>{
      if(message.type()==='error'&&/Content Security Policy|Refused to/i.test(message.text()))csp.push(message.text());
    });
    // The simulated native owner survives page reload; preferences contain only the alias.
    await page.exposeFunction('nativePost',(command,loadedFamilies)=>{
      commands.push(command);
      if(command?.scope==='music-preferences') {
        if(command.action==='apply') {
          assert.equal(command.revision,revision);
          commits.push({command,loadedFamilies});
          preferences=structuredClone(command.preferences);revision++;
        }
        return {version:1,type:'music-preferences-state',request:command.request||0,
          revision,status:'ready',error:'',preferences:structuredClone(preferences)};
      }
      if(command?.scope==='music-fonts')return {version:1,type:'music-fonts-state',
        request:command.request,families:systemUnavailable?[]:['Arial','Georgia'],
        error:systemUnavailable?'unavailable':''};
      if(command?.scope!=='music-font-assets')return null;
      if(command.action==='snapshot')return reply();
      if(command.action==='choose') {
        if(chooseMode==='cancel')return reply(command.request,'cancelled');
        if(chooseMode==='hold')return new Promise(resolve=>pendingChoose={request:command.request,resolve});
        const candidate=chooseMode==='bad'?badAsset:asset;
        if(!assets.some(item=>item.id===candidate.id))assets.push(candidate);
        return reply(command.request,'',candidate);
      }
      if(command.action==='cancel') {
        cancelled.push(command.request);
        if(pendingChoose?.request===command.request) {
          pendingChoose.resolve(reply(command.request,'cancelled'));pendingChoose=null;
        }
        return null;
      }
      if(command.action==='drop') {
        assets=assets.filter(item=>item.id!==command.id||item.family===preferences.fontFamily);
        return reply(command.request);
      }
      throw new Error(`Unexpected font action: ${command.action}`);
    });
    await page.addInitScript(()=>{
      window.fontProbe={removed:[],published:[]};
      const remove=document.fonts.delete.bind(document.fonts);
      document.fonts.delete=face=>{
        if(face.family.startsWith('LumeImportedFont_'))fontProbe.removed.push(face.family);
        return remove(face);
      };
      if(window!==window.top) {
        window.addEventListener('message',event=>{
          const family=event.data?.state?.preferences?.fontFamily;
          if(event.source===parent && event.origin===location.origin &&
            event.data?.type==='lume-lyric-stage' && family?.startsWith('LumeImportedFont_'))
            fontProbe.published.push({family,ready:[...document.fonts].some(face=>face.family===family&&face.status==='loaded')});
        });
        return;
      }
      const listeners=new Set();
      window.harness={receive:message=>{for(const listener of listeners)listener({data:message});}};
      window.chrome={webview:{
        addEventListener:(_,listener)=>listeners.add(listener),
        removeEventListener:(_,listener)=>listeners.delete(listener),
        postMessage:command=>{
          void window.nativePost(command,[...document.fonts].filter(face=>face.status==='loaded').map(face=>face.family))
            .then(message=>{if(message)harness.receive(message);});
        },
      }};
    });
    const childPending=new Promise(resolve=>releaseChild=resolve);
    await page.route('https://lume-fonts.local/**',async route=>{
      const filename=new URL(route.request().url()).pathname;
      assert.ok([`/${id}.otf`,`/${badId}.woff2`].includes(filename),'only metadata-derived native font URLs');
      const body=filename===`/${id}.otf`?bytes:Buffer.alloc(4);
      if(filename===`/${id}.otf`&&++fontRequests===2)await childPending;
      await route.fulfill({status:200,headers:{'access-control-allow-origin':'*',
        'content-type':'application/octet-stream','content-length':String(body.length)},body});
    });
    const origin=`http://127.0.0.1:${server.address().port}`;
    const openPlayer=async()=>{
      await page.waitForFunction(()=>!!document.querySelector('[data-titlebar-music]'));
      await page.evaluate(async()=>{
        const {initialMusicState}=await import('./music.js');
        const {query,nav,lyrics,...native}=initialMusicState();
        const track={id:'font-import',title:'Font import fixture',artist:'Lume',album:'Test',
          source:'C:/fixture/font-import.mp3',artwork:'',duration:60};
        harness.receive({version:1,type:'navigate',page:'music'});
        harness.receive({...native,version:1,type:'music-state',session:19,revision:1,pageOpen:true,
          pageVisible:true,status:'paused',playing:false,current:track,queue:[track],
          timeline:{position:12,duration:60},capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true}});
        harness.receive({version:1,type:'music-lyrics',session:19,trackId:track.id,
          lines:Array.from({length:10},(_,i)=>({startTime:i*6,endTime:i*6+6,
            fullText:'A melody through the night',translation:'',words:[]}))});
      });
      await page.locator('[data-music-action="open-player"]').click();
      await page.frameLocator('.MusicReferenceStage').locator('#reference-stage[data-reference-state="ready"]').waitFor();
      await page.locator('[data-music-action="lyrics"]').click();
      await page.locator('[data-music-panel-tab="globalSettings"]').click();
      await page.waitForFunction(()=>!document.querySelector('[data-music-font-trigger]').disabled);
    };
    const trigger=page.locator('.MusicFontChange');
    const upload=page.locator('[data-music-font-upload]');
    const close=page.locator('[data-music-font-close]');
    const select=page.locator('#music-font');
    const rows=page.locator('[data-music-font-list] [data-music-font-family]');
    const faceReady=family=>[...document.fonts].some(face=>face.family===family&&face.status==='loaded');
    await page.goto(`${origin}/index.html`);
    await openPlayer();
    assert.equal(commands.some(command=>['music-fonts','music-font-assets'].includes(command?.scope)),false,
      'no font enumeration before opening the picker');
    await page.evaluate(()=>{
      fontProbe.stage=document.querySelector('[data-music-stage]');
      fontProbe.frame=document.querySelector('.MusicReferenceStage');
    });
    const child=page.frames().find(frame=>frame.url().endsWith('#sonnet'));
    await child.evaluate(()=>fontProbe.stage=document.getElementById('reference-stage'));
    await trigger.click();
    await rows.first().waitFor();
    assert.deepEqual(await rows.allTextContents(),['Arial','Georgia']);
    await upload.click();
    await page.waitForFunction(family=>document.querySelector('#music-font')?.value===family&&
      document.querySelector('[data-music-settings]')?.getAttribute('aria-busy')==='false',asset.family);
    assert.equal(preferences.fontFamily,asset.family);
    assert.equal(commits.length,1);
    assert.ok(commits[0].loadedFamilies.includes(asset.family),'real FontFace is decoded before native preference commit');
    assert.deepEqual(Object.keys(preferences).sort(),Object.keys(DEFAULT_MUSIC_PREFERENCES).sort(),'no assets or blobs in preference DTO');
    assert.equal(preferences.version,DEFAULT_MUSIC_PREFERENCES.version);
    assert.equal(await page.getByRole('dialog').count(),0);
    assert.equal(await select.locator('option:checked').textContent(),asset.label);
    assert.equal(await page.evaluate(faceReady,asset.family),true);
    assert.deepEqual(await page.evaluate(()=>fontProbe.removed),[],'picker close never removes the preloaded parent face');
    assert.equal(await child.evaluate(faceReady,asset.family),false,'iframe decoding remains pending');
    assert.deepEqual(await child.evaluate(()=>fontProbe.published),[],'unready alias never reaches Sonnet');
    await page.evaluate(family=>fontProbe.face=[...document.fonts].find(face=>face.family===family),asset.family);
    releaseChild();
    await child.waitForFunction(faceReady,asset.family);
    await child.waitForFunction(()=>fontProbe.published.length>0);
    assert.ok((await child.evaluate(()=>fontProbe.published)).every(item=>item.ready));
    assert.equal(fontRequests,2,'one parent preload plus one new-document decode, no handoff refetch');
    await page.locator('[data-music-panel-tab="cover"]').click();
    await page.locator('[data-music-panel-tab="globalSettings"]').click();
    assert.equal(await select.locator('option:checked').textContent(),asset.label,'tab updates preserve original asset label');
    await trigger.click();
    await page.getByRole('button',{name:asset.label,exact:true}).waitFor();
    await close.click();
    assert.deepEqual(await page.evaluate(()=>({
      stage:fontProbe.stage===document.querySelector('[data-music-stage]'),
      frame:fontProbe.frame===document.querySelector('.MusicReferenceStage'),
      face:document.fonts.has(fontProbe.face),removed:fontProbe.removed,
    })),{stage:true,frame:true,face:true,removed:[]});
    assert.equal(await child.evaluate(()=>fontProbe.stage===document.getElementById('reference-stage')),true);
    assert.ok((await page.screenshot()).length>1000);

    chooseMode='cancel';
    await trigger.click();await upload.click();
    await page.waitForFunction(()=>!document.querySelector('[data-music-font-upload]').disabled);
    assert.equal(await page.locator('[data-music-font-upload-error]').isVisible(),false);
    assert.equal(await page.getByRole('dialog').count(),1,'native chooser cancellation keeps the picker open');
    assert.equal(commits.length,1);
    chooseMode='hold';
    await upload.click();
    await page.waitForFunction(()=>document.querySelector('[data-music-font-upload]').disabled);
    await close.click();
    await page.waitForFunction(()=>document.querySelector('[data-music-font-trigger]').getAttribute('aria-expanded')==='false');
    // Drain the exposed native callback before checking the correlated cancellation.
    await page.evaluate(()=>window.nativePost({test:'barrier'},[]));
    assert.equal(cancelled.length,1);
    assert.equal(pendingChoose,null);
    await trigger.click();
    assert.equal(await page.locator('[data-music-font-upload-error]').isVisible(),false);

    chooseMode='bad';
    await upload.click();
    await page.locator('[data-music-font-upload-error]:not([hidden])').waitFor();
    await page.evaluate(()=>window.nativePost({test:'barrier'},[]));
    assert.ok(commands.some(command=>command?.scope==='music-font-assets'&&command.action==='drop'&&command.id===badId));
    assert.deepEqual(assets,[asset],'decode failure drops only the uncommitted native candidate');
    assert.equal(preferences.fontFamily,asset.family);
    assert.equal(commits.length,1);
    assert.equal(await page.evaluate(faceReady,asset.family),true);
    await close.click();

    await page.evaluate(()=>harness.receive({version:1,type:'visibility',visible:false}));
    await page.waitForFunction(family=>![...document.fonts].some(face=>face.family===family),asset.family);
    await child.waitForFunction(family=>![...document.fonts].some(face=>face.family===family),asset.family);
    await page.evaluate(()=>harness.receive({version:1,type:'visibility',visible:true}));
    await page.waitForFunction(faceReady,asset.family);
    await child.waitForFunction(faceReady,asset.family);
    assert.equal(fontRequests,4,'show refetches both documents instead of retaining decoded raw bytes');

    // A fresh production app must restore metadata before loading the persisted alias.
    systemUnavailable=true;
    const snapshotsBefore=commands.filter(command=>command?.scope==='music-font-assets'&&command.action==='snapshot').length;
    const previousChooseRequests=commands.filter(command=>command?.scope==='music-font-assets'&&command.action==='choose')
      .map(command=>command.request);
    await page.reload();
    await openPlayer();
    const restored=page.frames().find(frame=>frame.url().endsWith('#sonnet'));
    await page.waitForFunction(faceReady,asset.family);
    await restored.waitForFunction(faceReady,asset.family);
    assert.equal(commands.filter(command=>command?.scope==='music-font-assets'&&command.action==='snapshot').length,
      snapshotsBefore+1,'concurrent restore consumers share one native metadata snapshot');
    assert.equal(await select.inputValue(),asset.family);
    assert.equal(await select.locator('option:checked').textContent(),asset.label);
    await trigger.click();
    await page.getByRole('button',{name:asset.label,exact:true}).waitFor();
    assert.deepEqual(await rows.allTextContents(),[asset.label],'system enumeration failure leaves imports selectable');
    assert.notEqual(await page.locator('[data-music-font-status]').getAttribute('role'),'alert');
    await close.click();
    assert.equal(commits.length,1,'restore and opening/closing never resave preferences');
    chooseMode='success';
    await trigger.click();await upload.click();
    await page.getByRole('dialog').waitFor({state:'detached'});
    const chooseRequests=commands.filter(command=>command?.scope==='music-font-assets'&&command.action==='choose')
      .map(command=>command.request);
    assert.equal(chooseRequests.length,previousChooseRequests.length+1,'reload can upload through the surviving native owner');
    const reloadedRequest=chooseRequests.at(-1);
    assert.ok(Number.isSafeInteger(reloadedRequest)&&reloadedRequest>0);
    assert.equal(previousChooseRequests.includes(reloadedRequest),false,'new page must not reuse a previous choose request');
    assert.equal(preferences.fontFamily,asset.family);
    assert.equal(await select.locator('option:checked').textContent(),asset.label);
    assert.equal(await page.evaluate(faceReady,asset.family),true);
    assert.equal(await restored.evaluate(faceReady,asset.family),true);
    assert.equal(commits.length,1,'choosing the already confirmed asset needs no redundant preference save');
    assert.equal(commands.some(command=>command?.scope==='music'&&
      ['play','pause','toggle','next','previous','seek','setRepeat'].includes(command.action)),false);
    await page.locator('[data-titlebar-music]').click();
    await page.waitForFunction(()=>!document.querySelector('[data-music-root]'));
    assert.equal(await page.evaluate(faceReady,asset.family),false,'navigation releases the parent lease');
    assert.equal(page.frames().length,1,'navigation disposes the iframe');
    assert.deepEqual(errors,[]);
    assert.deepEqual(csp,[]);
  } finally {
    releaseChild?.();
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
  }
});
