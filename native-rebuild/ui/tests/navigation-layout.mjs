// Development-only real-browser regression. No browser package is shipped.
// Usage: node tests/navigation-layout.mjs <playwright-core directory> [evidence directory]
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const {chromium}=createRequire(import.meta.url)(path.resolve(process.argv[2]));
const ui=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const evidence=process.argv[3] && path.resolve(process.argv[3]);
if(evidence) fs.mkdirSync(evidence,{recursive:true});
const types={'.js':'text/javascript','.css':'text/css','.html':'text/html','.svg':'image/svg+xml','.png':'image/png','.jpg':'image/jpeg'};
const server=http.createServer((req,res)=>{
  const name=decodeURIComponent(new URL(req.url,'http://localhost').pathname);
  const file=path.resolve(ui,'.'+(name==='/'?'/index.html':name));
  if(!file.startsWith(ui+path.sep)||!fs.existsSync(file)||!fs.statSync(file).isFile()){res.writeHead(404).end();return;}
  res.setHeader('Content-Type',types[path.extname(file)]||'application/octet-stream');
  res.end(fs.readFileSync(file));
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const browser=await chromium.launch({headless:true,channel:'msedge'});
const results=[];
const failures=[];
try {
  for(const viewport of [{width:1536,height:864},{width:1280,height:720},{width:680,height:864},{width:1920,height:1080}]) {
    const page=await browser.newPage({viewport});
    const errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.locator('[data-view="desktopBeauty"]').click();
    await page.locator('[data-open="widgets"]').click();
    const measure=()=>page.evaluate(()=>{
      const box=selector=>{const r=document.querySelector(selector).getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};};
      return {button:box('.NavigationBackButton'),arrow:box('.NavigationBackButton svg'),viewport:{width:innerWidth,height:innerHeight}};
    });
    const list=await measure();
    if(evidence && viewport.width===1536) await page.screenshot({path:path.join(evidence,'widgets.png')});
    for(const widget of ['clock','calendar']) {
      await page.locator(`[data-open="${widget}"]`).click();
      const settings=await measure();
      const colorControls=widget==='calendar'?await page.locator('[data-action="calendar-textColor"], [data-action="calendar-highlightColor"]').evaluateAll(elements=>elements.map(element=>{
        const r=element.getBoundingClientRect(),swatch=element.querySelector('.calendar-color-swatch').getBoundingClientRect();
        return {width:r.width,height:r.height,swatchWidth:swatch.width,swatchHeight:swatch.height};
      })):[];
      if(evidence && viewport.width===1536) await page.screenshot({path:path.join(evidence,`${widget}-settings.png`)});
      await page.locator('[data-tab="details"]').click();
      const details=await measure();
      await page.locator('[data-action="back"]').click();
      const returned=await measure();
      results.push({viewport,widget,list,settings,details,returned,colorControls});
      try {
        assert.deepEqual(errors,[]);
        assert.deepEqual(list.viewport,viewport,'must measure the requested viewport');
        assert.equal(list.button.width,48);assert.equal(list.button.height,48);
        if(widget==='calendar') {
          assert.equal(colorControls.length,2,'both calendar color controls must exist');
          for(const control of colorControls) {
            assert.equal(control.width,240,'calendar color control width');
            assert.equal(control.height,38,'calendar color control height');
            assert.equal(control.swatchWidth,28,'calendar color swatch width');
            assert.equal(control.swatchHeight,28,'calendar color swatch height');
          }
        }
        for(const [label,value] of Object.entries({settings,details,returned})) {
          assert.deepEqual(value.button,list.button,`${viewport.width}x${viewport.height} ${widget}: ${label} button moved`);
          assert.deepEqual(value.arrow,list.arrow,`${widget} ${label} arrow moved`);
        }
      } catch(error) { failures.push(error.message); }
    }
    await page.close();
  }
} finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
console.log(JSON.stringify(results,null,2));
if(evidence) fs.writeFileSync(path.join(evidence,'navigation-layout.json'),JSON.stringify(results,null,2));
assert.deepEqual(failures,[],'Navigation geometry must remain unchanged through settings and back');
console.log('Navigation layout passed: clock and calendar, full production page, 4 viewports, list/settings/details/back, button and arrow bounds.');
