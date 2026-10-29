// Development only. Run the native probe first; this loads the original widget
// files read-only and never starts Electron, live samplers, or a Shell icon host.
// node capture-widget-foreground-reference.mjs <original-widgets> <output> <playwright>
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';

const [sourceArg,outputArg,playwrightArg]=process.argv.slice(2);
assert(sourceArg&&outputArg&&playwrightArg,'Pass original widgets, native capture output, and Playwright paths');
const source=await fs.realpath(sourceArg),output=await fs.realpath(outputArg);
const relation=path.relative(source,output);
assert(relation&&((relation.startsWith(`..${path.sep}`))||path.isAbsolute(relation)),
  'The output directory must be outside the read-only original widgets');
const manifestBytes=await fs.readFile(path.join(output,'foreground-manifest.json'));
const manifest=JSON.parse(manifestBytes.toString('utf8'));
assert.equal(manifest.version,1);
assert([96,120,144,192].includes(manifest.dpi),'Unsupported capture DPI');
assert.equal(manifest.timezone,'Asia/Shanghai');
assert.equal(manifest.clock,'2026-09-26T04:00:00Z');
const expectedIds=['almanac-default','performance-default','performance-unavailable',
  'organizer-empty','organizer-icons','organizer-collapsed','organizer-search',
  'organizer-search-empty','organizer-list','organizer-icons-scrolled','organizer-list-scrolled'];
assert.deepEqual(manifest.cases?.map(scene=>scene.id),expectedIds,'The eleven required foreground scenes must be captured');
const files=['index.html','widget.css','widget-dom.js','widget-assets.js','renderer.js',
  'almanac.js','lunar-calendar.js','vendor/lunar.js','performance.css','performance.js',
  'organizer.js','widget-layout.js','runtime.js'];
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const provenance=JSON.parse(await fs.readFile(path.join(output,'native-capture-provenance.json'),'utf8'));
assert.equal(provenance.version,1);
assert.equal(provenance.manifestSha256,sha(manifestBytes),'Native manifest changed since capture');
assert.equal(await fs.realpath(provenance.nativeExecutable),await fs.realpath(manifest.nativeExecutable));
assert.equal(provenance.nativeExecutableSha256,sha(await fs.readFile(manifest.nativeExecutable)),
  'Native executable changed since capture; regenerate pixels with the current probe');
async function nativePixels(name){
  const bytes=await fs.readFile(path.join(output,name));
  assert.equal(sha(bytes),provenance.files[name],`Native pixels changed since capture: ${name}`);
  return bytes;
}
async function hashes(){
  return Object.fromEntries(await Promise.all(files.map(async name=>[name,sha(await fs.readFile(path.join(source,name)))])));
}
const sourceHashes=await hashes();
const layoutSource=await fs.readFile(path.join(source,'widget-layout.js'),'utf8');
const runtimeSource=await fs.readFile(path.join(source,'runtime.js'),'utf8');
const originalDefaults={};
for(const kind of ['almanac','performance','organizer']){
  const match=layoutSource.match(new RegExp(`${kind}:\\s*\\{\\s*width:\\s*(\\d+),\\s*height:\\s*(\\d+)\\s*\\}`));
  assert(match,`Cannot verify original default dimensions for ${kind}`);
  originalDefaults[kind]={width:Number(match[1]),height:Number(match[2])};
}
assert(/ORGANIZER_CAPTION_HEIGHT\s*=\s*36\s*;/.test(runtimeSource));
assert(/COLLAPSED_ORGANIZER_SIZE\s*=\s*Object\.freeze\(\{\s*width:\s*180,\s*height:\s*180\s*\+\s*ORGANIZER_CAPTION_HEIGHT/.test(runtimeSource),
  'Original collapsed size changed; re-check the native fixture');
const ids=new Set();
for(const scene of manifest.cases){
  assert(/^[a-z0-9-]+$/.test(scene.id)&&!ids.has(scene.id),'Unsafe or duplicate scene id');
  ids.add(scene.id);
  assert(['almanac','performance','organizer'].includes(scene.kind));
  assert.equal(scene.dpi,manifest.dpi);
  assert.equal(scene.pixelWidth,scene.width*manifest.dpi/96);
  assert.equal(scene.pixelHeight,scene.height*manifest.dpi/96);
  assert.equal(scene.pixels,`${scene.id}.bgra`);
  const size=scene.collection?.mode==='collapsed'?{width:180,height:216}:originalDefaults[scene.kind];
  assert.deepEqual({width:scene.width,height:scene.height},size,'Native fixture must use the original real default size');
}
function decodeBgra(pixels,width,height){
  assert.equal(pixels.length,width*height*4,'Native byte count does not match the physical dimensions');
  const rgba=Buffer.alloc(pixels.length);
  for(let i=0;i<pixels.length;i+=4){
    const a=pixels[i+3];
    rgba[i]=a?Math.min(255,Math.round(pixels[i+2]*255/a)):0;
    rgba[i+1]=a?Math.min(255,Math.round(pixels[i+1]*255/a)):0;
    rgba[i+2]=a?Math.min(255,Math.round(pixels[i]*255/a)):0;
    rgba[i+3]=a;
  }
  return rgba;
}
const selectors={
  almanac:['#widget','#almanac','.almanac-heading','.almanac-seal','#almanac-lunar',
    '.almanac-cycle','#almanac-year','#almanac-day-cycle','.almanac-activities',
    '.almanac-activity','.almanac-badge','#almanac-yi','#almanac-ji'],
  performance:['#widget','#performance','.performance-heading','#performance-status',
    '.performance-grid','.performance-metric','.performance-label',
    '#performance-cpu','#performance-gpu','#performance-memory',
    '.performance-meter','.performance-meter span','.performance-detail',
    '.performance-network','.performance-rate','#performance-download','#performance-upload'],
  organizer:['#widget','#organizer','#organizer-header','#organizer-heading',
    '.organizer-heading-icon','#organizer-title','#organizer-search-toggle',
    '#organizer-search','#organizer-search-input','#organizer-search-clear','#organizer-scroll',
    '#organizer-grid','.organizer-cell','.organizer-icon-image','.organizer-icon-name',
    '#organizer-empty','#organizer-empty svg','#organizer-empty strong','#organizer-empty span','#organizer-search-empty'],
};
const {chromium}=createRequire(import.meta.url)(path.resolve(playwrightArg));
let browser;
const results=[],errors=[];
try{
  browser=await chromium.launch({headless:true,channel:'msedge'});
  const context=await browser.newContext({
    bypassCSP:true,offline:true,timezoneId:manifest.timezone,locale:'zh-CN',
    deviceScaleFactor:manifest.dpi/96,reducedMotion:'reduce',
  });
  const canvasPage=await context.newPage();
  async function pngFromRgba(rgba,width,height){
    return canvasPage.evaluate(({pixels,width,height})=>{
      const canvas=document.createElement('canvas');canvas.width=width;canvas.height=height;
      const bytes=Uint8ClampedArray.from(atob(pixels),character=>character.charCodeAt(0));
      canvas.getContext('2d').putImageData(new ImageData(bytes,width,height),0,0);
      return canvas.toDataURL('image/png');
    },{pixels:rgba.toString('base64'),width,height});
  }
  assert.deepEqual(manifest.icon,{pixels:'fixture-icon.bgra',width:48,height:48});
  const iconBytes=await nativePixels(manifest.icon.pixels);
  const iconUrl=await pngFromRgba(decodeBgra(iconBytes,48,48),48,48);
  for(const scene of manifest.cases){
    const page=await context.newPage();page.setDefaultTimeout(5000);
    page.on('pageerror',error=>errors.push({scene:scene.id,error:error.message}));
    await page.setViewportSize({width:scene.width,height:scene.height});
    await page.clock.setFixedTime(new Date(manifest.clock));
    const collection=scene.collection?structuredClone(scene.collection):null;
    if(collection)for(const item of collection.items)item.icon=iconUrl;
    await page.addInitScript(({collection})=>{
      const widgetListeners=[],organizerListeners=[];
      window.__foregroundCapture={widgetListeners,organizerListeners};
      window.desktopWidget={
        onUpdate(fn){widgetListeners.push(fn);return()=>{};},
        setGlassPanels(){},setGlassDiscs(){},
      };
      // Only the original renderer's read and update contracts are substituted.
      // Editing, opening files and persistence are never permitted by this probe.
      const forbidden=()=>{throw new Error('Foreground comparison cannot mutate or open user data');};
      window.organizer={
        read:async()=>structuredClone(collection),
        onUpdate(fn){organizerListeners.push(fn);return()=>{};},
        addFiles:forbidden,pick:forbidden,open:forbidden,remove:forbidden,reorder:forbidden,rename:forbidden,
      };
    },{collection});
    const url=pathToFileURL(path.join(source,'index.html'));
    url.searchParams.set('kind',scene.kind);url.searchParams.set('locked','true');
    await page.goto(url.href);
    // Root glass and almanac panel glass are supplied by a native compositor in
    // production. Request that exact original mode; only foreground is compared.
    await page.addStyleTag({content:'body{background:#202020}.widget-tool{opacity:0!important}'});
    await page.evaluate(({scene,collection})=>{
      for(const fn of window.__foregroundCapture.widgetListeners)
        fn({kind:scene.kind,nativeGlass:true,status:scene.status,snapshot:scene.snapshot});
      if(collection)for(const fn of window.__foregroundCapture.organizerListeners)
        fn(collection,{nativeGlass:true});
    },{scene,collection});
    if(scene.search?.open){
      await page.locator('#organizer-search-toggle').click();
      await page.locator('#organizer-search-input').fill(scene.search.query);
    }
    if(scene.kind==='organizer'&&scene.scrollTop){
      await page.locator('#organizer-scroll').evaluate((element,top)=>{element.scrollTop=top;},scene.scrollTop);
    }
    await page.evaluate(()=>{document.activeElement?.blur?.();});
    await page.mouse.move(scene.width-1,scene.height-1);
    await page.evaluate(()=>document.fonts.ready);
    await page.waitForFunction(()=>[...document.querySelectorAll('img')].every(image=>image.complete&&image.naturalWidth>0));
    await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    const observed=await page.evaluate(({scene,list})=>{
      function rect(node){
        const r=node.getBoundingClientRect(),s=getComputedStyle(node);
        return {x:r.x,y:r.y,width:r.width,height:r.height,fontFamily:s.fontFamily,fontSize:s.fontSize,
          fontWeight:s.fontWeight,lineHeight:s.lineHeight,padding:s.padding,gap:s.gap,
          display:s.display,visibility:s.visibility,scrollWidth:node.scrollWidth,clientWidth:node.clientWidth,
          scrollHeight:node.scrollHeight,clientHeight:node.clientHeight,text:node.textContent};
      }
      const cells=[...document.querySelectorAll('.organizer-cell')]
        .filter(node=>getComputedStyle(node).display!=='none')
        .map(node=>({id:node.dataset.id,bounds:rect(node),name:node.querySelector('.organizer-icon-name')?.textContent}));
      return {
        width:innerWidth,height:innerHeight,dpr:devicePixelRatio,
        nativeGlassReady:document.querySelector('#widget').classList.contains('native-glass-ready'),
        localDate:[new Date().getFullYear(),new Date().getMonth()+1,new Date().getDate()],
        labels:Object.fromEntries(Object.keys(scene.expected||{}).map(selector=>[selector,document.querySelector(selector)?.textContent])),
        title:document.querySelector('#organizer-title')?.value,
        searchOpen:document.querySelector('#organizer')?.classList.contains('search-open')||false,
        query:document.querySelector('#organizer-search-input')?.value||'',
        scrollTop:document.querySelector('#organizer-scroll')?.scrollTop||0,
        cells,selectors:Object.fromEntries(list.map(selector=>[selector,[...document.querySelectorAll(selector)].map(rect)])),
      };
    },{scene,list:selectors[scene.kind]});
    assert.equal(observed.width,scene.width);assert.equal(observed.height,scene.height);
    assert.equal(observed.dpr,manifest.dpi/96);
    assert.equal(observed.nativeGlassReady,true,'Original compositor-owned backgrounds must be excluded');
    if(scene.kind==='almanac')assert.deepEqual(observed.localDate,[scene.date.year,scene.date.month,scene.date.day]);
    if(scene.expected)assert.deepEqual(observed.labels,scene.expected,'Original and native text/data disagree; do not compare different snapshots');
    if(collection){
      assert.equal(observed.title,collection.title);
      assert.equal(observed.searchOpen,scene.search.open);
      assert.equal(observed.query,scene.search.query);
      assert.equal(observed.scrollTop,scene.scrollTop||0,'Original and native scroll offsets disagree');
      assert.deepEqual(observed.cells.map(cell=>cell.id),scene.expectedVisibleIds,'Original and native filtering/order disagree');
      for(const cell of observed.cells)assert.equal(cell.name,collection.items.find(item=>item.id===cell.id).name);
    }
    const reference=await page.screenshot({path:path.join(output,`original-${scene.id}.png`),animations:'disabled'});
    const nativeBytes=await nativePixels(scene.pixels);
    const nativeData=await pngFromRgba(decodeBgra(nativeBytes,scene.pixelWidth,scene.pixelHeight),scene.pixelWidth,scene.pixelHeight);
    await canvasPage.setViewportSize({width:scene.width,height:scene.height});
    await canvasPage.setContent(`<style>body{margin:0;background:#202020}img{display:block;width:${scene.width}px;height:${scene.height}px}</style><img src="${nativeData}">`);
    await canvasPage.waitForFunction(()=>document.querySelector('img').complete);
    const native=await canvasPage.screenshot({path:path.join(output,`native-${scene.id}.png`)});
    const originalData=`data:image/png;base64,${reference.toString('base64')}`;
    const nativeScreenshotData=`data:image/png;base64,${native.toString('base64')}`;
    const diff=await canvasPage.evaluate(async({original,native,width,height})=>{
      async function pixels(url){
        const image=new Image();image.src=url;await image.decode();
        if(image.naturalWidth!==width||image.naturalHeight!==height)throw new Error('Screenshot DPI mismatch');
        const canvas=document.createElement('canvas');canvas.width=width;canvas.height=height;
        const context=canvas.getContext('2d');context.drawImage(image,0,0);
        return context.getImageData(0,0,width,height).data;
      }
      const a=await pixels(original),b=await pixels(native),heat=new Uint8ClampedArray(a.length);
      let total=0,changed=0,maximum=0;
      for(let i=0;i<a.length;i+=4){
        const differences=[Math.abs(a[i]-b[i]),Math.abs(a[i+1]-b[i+1]),Math.abs(a[i+2]-b[i+2])];
        const delta=Math.max(...differences);total+=differences[0]+differences[1]+differences[2];
        if(delta>24)++changed;maximum=Math.max(maximum,delta);
        heat[i]=Math.min(255,delta*3);heat[i+3]=255;
      }
      const canvas=document.createElement('canvas');canvas.width=width;canvas.height=height;
      canvas.getContext('2d').putImageData(new ImageData(heat,width,height),0,0);
      return {meanAbsoluteRgb:total/(width*height*3),changedPixelsOver24:changed,
        changedRatioOver24:changed/(width*height),maximumChannelDifference:maximum,
        png:canvas.toDataURL('image/png')};
    },{original:originalData,native:nativeScreenshotData,width:scene.pixelWidth,height:scene.pixelHeight});
    await fs.writeFile(path.join(output,`difference-${scene.id}.png`),Buffer.from(diff.png.split(',')[1],'base64'));
    delete diff.png;
    await canvasPage.setViewportSize({width:scene.width*2,height:scene.height+32});
    await canvasPage.setContent(`<style>body{margin:0;background:#202020;color:white;font:12px "Segoe UI";display:grid;grid-template-columns:1fr 1fr}p{margin:8px;height:16px}img{display:block;width:${scene.width}px;height:${scene.height}px}</style><p>原版源码 · ${scene.width}×${scene.height} DIP · ${scene.dpi} DPI</p><p>原生前景 · 同一数据</p><img src="${originalData}"><img src="${nativeScreenshotData}">`);
    await canvasPage.waitForFunction(()=>[...document.images].every(image=>image.complete));
    await canvasPage.screenshot({path:path.join(output,`comparison-${scene.id}.png`)});
    const geometryDeltas=[];
    function difference(name,native,original){
      if(!original)return;
      const delta=Object.fromEntries(['x','y','width','height'].map(key=>[key,native[key]-original[key]]));
      geometryDeltas.push({name,native,original,delta,maxAbsoluteDelta:Math.max(...Object.values(delta).map(Math.abs))});
    }
    if(scene.nativePanels)scene.nativePanels.forEach((panel,index)=>
      difference(`almanac-panel-${index}`,panel,observed.selectors['.almanac-activity'][index]));
    if(scene.nativeLayout)for(const cell of scene.nativeLayout.cells)
      difference(`organizer-cell-${cell.id}`,cell.bounds,observed.cells.find(item=>item.id===cell.id)?.bounds);
    results.push({id:scene.id,kind:scene.kind,width:scene.width,height:scene.height,dpi:scene.dpi,
      observed,nativeLayout:scene.nativeLayout,nativePanels:scene.nativePanels,geometryDeltas,pixelDifference:diff,
      captures:{original:`original-${scene.id}.png`,native:`native-${scene.id}.png`,
        comparison:`comparison-${scene.id}.png`,difference:`difference-${scene.id}.png`},
      nativePixelsSha256:sha(nativeBytes),originalPngSha256:sha(reference),nativePngSha256:sha(native)});
    await page.close();
  }
  assert.deepEqual(errors,[],'Original renderer errors invalidate the comparison');
  assert.deepEqual(await hashes(),sourceHashes,'Read-only original widget files changed');
  const report={
    version:1,source,sourceHashes,manifestSha256:sha(manifestBytes),
    nativeExecutable:manifest.nativeExecutable,nativeExecutableSha256:provenance.nativeExecutableSha256,
    nativeCapturedAtUtc:provenance.capturedAtUtc,referenceCapturedAtUtc:new Date().toISOString(),
    browser:browser.version(),node:process.version,dpi:manifest.dpi,originalDefaults,
    scope:'Same fixed inputs, original default DIP sizes, equal device scale, neutral #202020 background. Original native-glass-ready mode excludes compositor-owned surfaces. This is foreground evidence only.',
    exclusions:['Native glass/refraction and desktop composition','Live performance sampling','Shell icon extraction',
      'Focused native text editor and IME','Interactive motion and additional window sizes'],
    note:'Pixel differences are diagnostics, not pass/fail thresholds; font rasterization and original CSS shadows can differ.',
    results,
  };
  await fs.writeFile(path.join(output,'foreground-reference-layout.json'),JSON.stringify(report,null,2));
  console.log(`Captured ${results.length} original/native foreground pairs at ${manifest.dpi} DPI; glass remains unverified.`);
}finally{
  try{await browser?.close();}
  finally{assert.deepEqual(await hashes(),sourceHashes,'Original sources must remain unchanged even on failure');}
}
