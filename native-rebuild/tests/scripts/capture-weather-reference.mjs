// Offline development-only comparison. Original files are read-only.
// First run lume-weather-view-test --capture <output>, then:
// node capture-weather-reference.mjs <original-widgets-dir> <output> <playwright-dir>
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const [original,destination,playwrightDirectory]=process.argv.slice(2);
if(!original||!destination||!playwrightDirectory)throw new Error('缺少原版组件目录、输出目录或 Playwright 路径');
const source=await fs.realpath(original),output=path.resolve(destination);
const {snapshot}=JSON.parse(await fs.readFile(path.join(output,'snapshot.json'),'utf8'));
const names=['index.html','widget.css','renderer.js','widget-dom.js','widget-assets.js'];
const hashes=async()=>Object.fromEntries(await Promise.all(names.map(async name=>[name,
  createHash('sha256').update(await fs.readFile(path.join(source,name))).digest('hex')])));
const sourceHashes=await hashes();
const {chromium}=createRequire(import.meta.url)(path.resolve(playwrightDirectory));
const browser=await chromium.launch({headless:true,channel:'msedge'});
try {
  const context=await browser.newContext({bypassCSP:true,offline:true,deviceScaleFactor:1});
  await context.addInitScript(()=>{
    window.weatherListeners=[];
    window.desktopWidget={onUpdate(fn){window.weatherListeners.push(fn);return ()=>{};},refresh(){}};
  });
  const page=await context.newPage(),canvasPage=await context.newPage();
  page.setDefaultTimeout(5000);
  const errors=[];page.on('pageerror',error=>errors.push(error.message));
  const results=[];
  for(const [width,height] of [[320,152],[480,220],[720,360]]){
    await page.setViewportSize({width,height});
    const url=pathToFileURL(path.join(source,'index.html'));url.search='?kind=weather';
    await page.goto(url.href);
    // Neutral backgrounds compare foreground/layout only, never prove native glass.
    await page.addStyleTag({content:'body{background:#202020}.widget-tool{opacity:0!important}'});
    await page.evaluate(snapshot=>window.weatherListeners.forEach(fn=>fn({kind:'weather',status:'ready',snapshot})),snapshot);
    await page.evaluate(()=>document.fonts.ready);
    await page.waitForFunction(()=>[...document.querySelectorAll('#weather img')].every(img=>img.complete&&img.naturalWidth>0));
    const measurement=await page.evaluate(()=>{
      const rect=element=>{
        const r=element.getBoundingClientRect(),s=getComputedStyle(element);
        return {x:r.x,y:r.y,width:r.width,height:r.height,font:s.fontFamily,fontSize:s.fontSize,fontWeight:s.fontWeight,lineHeight:s.lineHeight};
      };
      return {
        selectors:Object.fromEntries(['#weather','.weather-overview','.current-weather','#temperature','.temperature-range',
          '.weather-location','#location','#condition','#forecast','.forecast-hour','.forecast-icon','.forecast-hour strong']
          .map(selector=>[selector,rect(document.querySelector(selector))])),
        hours:[...document.querySelectorAll('.forecast-hour')].map(e=>({item:rect(e),label:rect(e.children[0]),icon:rect(e.children[1]),temp:rect(e.children[2])})),
      };
    });
    const reference=await page.screenshot({path:path.join(output,`original-${width}.png`)});
    const pixels=await fs.readFile(path.join(output,`${width}.bgra`));
    assert.equal(pixels.length,width*height*4);
    const rgba=Buffer.alloc(pixels.length);
    for(let i=0;i<pixels.length;i+=4){
      const a=pixels[i+3];rgba[i]=a?Math.min(255,Math.round(pixels[i+2]*255/a)):0;
      rgba[i+1]=a?Math.min(255,Math.round(pixels[i+1]*255/a)):0;
      rgba[i+2]=a?Math.min(255,Math.round(pixels[i]*255/a)):0;rgba[i+3]=a;
    }
    await canvasPage.setViewportSize({width,height});
    await canvasPage.setContent('<style>body{margin:0;background:#202020}canvas{display:block}</style><canvas></canvas>');
    await canvasPage.evaluate(({width,height,pixels})=>{
      const canvas=document.querySelector('canvas');canvas.width=width;canvas.height=height;
      const data=Uint8ClampedArray.from(atob(pixels),c=>c.charCodeAt(0));
      canvas.getContext('2d').putImageData(new ImageData(data,width,height),0,0);
    },{width,height,pixels:rgba.toString('base64')});
    const native=await canvasPage.screenshot({path:path.join(output,`native-${width}.png`)});
    await canvasPage.setViewportSize({width:width*2,height:height+32});
    await canvasPage.setContent(`<style>body{margin:0;background:#202020;color:white;font:12px "Segoe UI";display:grid;grid-template-columns:1fr 1fr}p{margin:8px}img{display:block}</style><p>原版源码</p><p>原生重构</p><img src="data:image/png;base64,${reference.toString('base64')}"><img src="data:image/png;base64,${native.toString('base64')}">`);
    await canvasPage.screenshot({path:path.join(output,`comparison-${width}.png`)});
    results.push({width,height,...measurement});
  }
  assert.deepEqual(errors,[]);assert.deepEqual(await hashes(),sourceHashes,'原版文件保持不变');
  await fs.writeFile(path.join(output,'reference-layout.json'),JSON.stringify({
    source,sourceHashes,scope:'同一固定天气快照、96 DPI、中性背景；仅验证前景布局，不代表原生材质或联网验收',results,
  },null,2));
  console.log(`天气前景对照已导出：${results.length} 种尺寸`);
}finally{await browser.close();}
