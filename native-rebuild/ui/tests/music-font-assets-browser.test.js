import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('real imported font decodes per document, paints all available modes and releases on hide/dispose',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:240000},async()=>{
  const ui=fileURLToPath(new URL('..',import.meta.url));
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||
    path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
  const bytes=await readFile(path.join(ui,'assets/fonts/Quicksand.otf'));
  const id='0123456789abcdef0123456789abcdef';
  const asset={id,family:`LumeImportedFont_${id}`,label:'Quicksand',extension:'otf',size:bytes.length};
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/') {
      res.setHeader('Content-Type','text/html; charset=utf-8');
      return res.end('<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/music.css"><style>body{margin:0;background:#18251d}#host{position:fixed;inset:0}iframe{width:100%;height:100%;border:0}</style><main id="host"></main>');
    }
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(path.resolve(ui)+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css','.html':'text/html','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    browser=await chromium.launch({channel:'msedge',headless:true});
    for(const width of [1280,390])for(const mode of ['partita','fume','sonnet','gallery','3d']) {
      const page=await browser.newPage({viewport:{width,height:844}});
      const errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      await page.route('https://lume-fonts.local/**',route=>route.fulfill({
        status:200,headers:{'access-control-allow-origin':'*','content-type':'font/otf','content-length':String(bytes.length)},body:bytes,
      }));
      await page.addInitScript(()=>{
        window.fontPaints=[];window.gpuCanvases=new Set();
        const font=Object.getOwnPropertyDescriptor(CanvasRenderingContext2D.prototype,'font');
        Object.defineProperty(CanvasRenderingContext2D.prototype,'font',{...font,set(value){
          if(value.includes('LumeImportedFont_'))
            fontPaints.push({value,ready:[...document.fonts].some(face=>value.includes(face.family)&&face.status==='loaded')});
          font.set.call(this,value);
        }});
        const getContext=HTMLCanvasElement.prototype.getContext;
        HTMLCanvasElement.prototype.getContext=function(type,...args){
          const context=getContext.call(this,type,...args);
          if(context&&/webgl/.test(type))gpuCanvases.add(this);
          return context;
        };
      });
      await page.goto(`http://127.0.0.1:${server.address().port}/`);
      await page.evaluate(async({mode,asset})=>{
        const {createMusicFontAssets}=await import('/music-font-assets.js');
        const {mountMusicVisualizer}=await import('/music-visualizer.js');
        const {DEFAULT_MUSIC_PREFERENCES}=await import('/music-preference-values.js');
        window.manager=createMusicFontAssets();manager.setAssets([asset]);
        window.asset=asset;
        window.state={lines:Array.from({length:12},(_,i)=>({startTime:i*6,endTime:i*6+6,
          fullText:'A melody through the night',translation:'',words:[]})),
          current:{id:'font-fixture',title:'Font test',artist:'Lume',album:'',artwork:''},
          currentTime:25,activeLineIndex:4,duration:72,playing:false,visible:true,
          preferences:{...DEFAULT_MUSIC_PREFERENCES,mode,fontFamily:'Arial'}};
        window.controller=mountMusicVisualizer(document.getElementById('host'),{
          ensureFont:manager.ensureFont,onError:error=>{window.fontError=error.message;},
        });
        await controller.update(state,mode);
      },{mode,asset});
      const reference=!['gallery','3d'].includes(mode);
      if(reference)await page.frameLocator('iframe').locator('#reference-stage[data-reference-state="ready"]').waitFor();
      const surface=reference?page.frames().find(frame=>frame!==page.mainFrame()):page.mainFrame();
      if(mode==='sonnet'||mode==='3d')await surface.waitForSelector('canvas');
      const settle=()=>page.evaluate(async()=>{for(let i=0;i<50;i++)await new Promise(requestAnimationFrame);});
      await settle();
      await page.evaluate(()=>window.initialChild=document.getElementById('host').firstElementChild);
      await surface.evaluate(()=>{window.initialGpuCount=gpuCanvases.size;window.initialGpu=[...gpuCanvases].filter(node=>node.isConnected);});
      await page.evaluate(()=>{state={...state,preferences:{...state.preferences,fontFamily:asset.family}};return controller.update(state,undefined,{syncClock:false});});
      await surface.waitForFunction(family=>[...document.fonts].some(face=>face.family===family&&face.status==='loaded'),asset.family);
      await settle();
      assert.equal(await page.evaluate(()=>window.fontError),undefined,`${mode}: no decode error`);
      assert.equal(await page.evaluate(()=>initialChild===document.getElementById('host').firstElementChild),true,`${mode}: stage retained`);
      const gpu=await surface.evaluate(()=>({before:initialGpuCount,after:gpuCanvases.size,
        connected:initialGpu.map(node=>node.isConnected)}));
      assert.ok(gpu.before===gpu.after&&gpu.connected.every(Boolean),`${mode}: GPU retained ${JSON.stringify(gpu)}`);
      const painted=await surface.evaluate(family=>({
        valid:fontPaints.every(item=>item.ready),
        uses:fontPaints.some(item=>item.value.includes(family))||
          [...document.querySelectorAll('[style]')].some(node=>node.style.fontFamily.includes(family)),
      }),asset.family);
      assert.deepEqual(painted,{valid:true,uses:true},`${mode}/${width}: imported family paints only after decode`);
      assert.ok((await page.screenshot()).length>1000);
      await page.evaluate(()=>{state={...state,visible:false};return controller.update(state,undefined,{syncClock:false});});
      await page.waitForFunction(()=>![...document.fonts].some(face=>face.family===asset.family));
      if(reference)await surface.waitForFunction(family=>![...document.fonts].some(face=>face.family===family),asset.family);
      await page.evaluate(()=>{state={...state,visible:true};return controller.update(state,undefined,{syncClock:false});});
      await surface.waitForFunction(family=>[...document.fonts].some(face=>face.family===family),asset.family);
      await page.evaluate(()=>{controller.dispose();manager.dispose();});
      assert.equal(await page.locator('#host > *').count(),0);
      assert.equal(await page.evaluate(()=>[...document.fonts].some(face=>face.family===asset.family)),false);
      assert.deepEqual(errors,[],`${mode}/${width}`);
      console.log(`${mode}/${width}: imported face decoded before paint; stage retained; hidden faces released`);
      await page.close();
    }

    const page=await browser.newPage();
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.evaluate(async()=>{
      const {createMusicFontPicker}=await import('/music-font-picker.js');
      const root=document.getElementById('host');
      root.className='MusicRoot';root.dataset.musicRoot='';
      root.innerHTML='<button data-trigger>更换</button>';
      window.uploads=[];window.commits=[];
      window.picker=createMusicFontPicker(root,{
        queryFonts:async()=>[],
        onUpload:({signal})=>new Promise((resolve,reject)=>uploads.push({signal,resolve,reject})),
      });
      root.addEventListener('click',event=>{
        const button=event.target.closest('button');
        if(button?.matches('[data-trigger]'))void picker.open(button);
        if(button?.matches('[data-music-font-close]'))picker.close();
        if(button?.matches('[data-music-font-family]')) {
          commits.push({fontFamily:button.dataset.musicFontFamily});
          picker.close();
        }
      });
    });
    const open=()=>page.locator('[data-trigger]').click();
    const upload=page.locator('[data-music-font-upload]');
    const close=()=>page.locator('[data-music-font-close]').click();
    await open();await upload.click();
    assert.equal(await upload.isDisabled(),true);
    await page.evaluate(()=>uploads[0].resolve(null));
    assert.equal(await upload.isEnabled(),true);
    assert.equal(await page.getByRole('dialog').count(),1);
    await upload.click();await page.evaluate(()=>uploads[1].reject(new Error('decode')));
    assert.equal(await page.locator('[data-music-font-upload-error]').innerText(),'上传字体失败，请检查字体文件。');
    await upload.click();await close();
    assert.equal(await page.evaluate(()=>uploads[2].signal.aborted),true);
    await page.evaluate(family=>uploads[2].resolve(family),asset.family);
    assert.deepEqual(await page.evaluate(()=>commits),[]);
    await open();await upload.click();
    await page.evaluate(family=>uploads[3].resolve(family),asset.family);
    assert.deepEqual(await page.evaluate(()=>commits),[{fontFamily:asset.family}]);
    assert.equal(await page.getByRole('dialog').count(),0);
    assert.equal(await page.locator('[data-trigger]').evaluate(node=>node===document.activeElement),true);
    await page.evaluate(async asset=>{
      const {createMusicFontAssets}=await import('/music-font-assets.js');
      const {createMusicFontPicker}=await import('/music-font-picker.js');
      window.labels=createMusicFontAssets();
      window.labelAsset={...asset,label:'原始字体 <img onerror=alert(1)>'};
      labels.setAssets([labelAsset]);
      window.labelQueries=0;
      picker=createMusicFontPicker(document.getElementById('host'),{
        queryFonts:async()=>{labelQueries++;return [...labels.listAssets(),{family:'Arial'},{family:'Zulu'},...labels.listAssets()];},
        getFontLabel:labels.labelFor,
      });
      document.getElementById('host').addEventListener('input',event=>{
        if(event.target.matches('[data-music-font-search]'))picker.search(event.target.value);
      });
      await picker.open(document.querySelector('[data-trigger]'),asset.family);
    },asset);
    const rows=page.locator('[data-music-font-list] [data-music-font-family]');
    const search=page.getByRole('searchbox',{name:'搜索字体'});
    assert.equal(await page.locator('[data-music-font-current]').innerText(),'当前：原始字体 <img onerror=alert(1)>');
    assert.deepEqual(await rows.allTextContents(),['原始字体 <img onerror=alert(1)>','Arial','Zulu']);
    assert.deepEqual(await page.locator('[data-music-font-list] h3').allTextContents(),['已导入字体','系统字体']);
    assert.equal(await page.getByRole('dialog').locator('img').count(),0,'asset labels are text only');
    assert.equal(await rows.first().getAttribute('data-music-font-family'),asset.family);
    await search.fill('原始');
    assert.equal(await rows.count(),1,'search uses the original display label');
    await rows.first().click();
    assert.deepEqual(await page.evaluate(()=>commits.at(-1)),{fontFamily:asset.family},'selection commits alias, never label');
    await page.evaluate(()=>labels.setAssets([{...labelAsset,label:'Renamed font'}]));
    await open();
    assert.equal(await rows.first().innerText(),'Renamed font');
    assert.equal(await page.evaluate(()=>labelQueries),2,'merged imports refresh on reopening');
    await close();
    await page.evaluate(()=>labels.setAssets([]));
    await open();
    assert.deepEqual(await rows.allTextContents(),['Arial','Zulu'],'removed imports do not remain cached');
    assert.equal(await page.locator('[data-music-font-list] h3').count(),0);
    await close();
    await page.evaluate(async family=>{
      const {createMusicFontPicker}=await import('/music-font-picker.js');
      picker=createMusicFontPicker(document.getElementById('host'),{
        queryFonts:async()=>[{family}],getFontLabel:()=>'\u0000invalid',
      });
      await picker.open(document.querySelector('[data-trigger]'),family);
    },asset.family);
    assert.equal(await rows.first().innerText(),asset.family,'invalid hook labels fall back to family');
    assert.equal(await page.locator('[data-music-font-current]').innerText(),`当前：${asset.family}`);
    await close();
    await page.evaluate(()=>labels.dispose());
    await page.close();
  } finally {
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
  }
});
