import nodeTest from 'node:test';
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

// Real Edge: :focus-visible is what tells a clicked button from a keyboard-focused one.
nodeTest('the window bar tucks away in fullscreen and music immersive mode, even after a click',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:120000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const require=createRequire(import.meta.url);
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/__test')return res.writeHead(200,{'Content-Type':'text/html'}).end(
      '<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/titlebar.css"><header id="titlebar" style="position:absolute;inset:0 0 auto;height:35px"></header>');
    const file=path.resolve(ui,'.'+pathname);
    try{res.setHeader('Content-Type',pathname.endsWith('.js')?'text/javascript':pathname.endsWith('.css')?'text/css':'application/octet-stream');res.end(await readFile(file));}
    catch{res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const browser=await chromium.launch({headless:true,channel:'msedge'});
  try{
    const page=await browser.newPage({viewport:{width:1280,height:720}});
    await page.goto(`http://127.0.0.1:${server.address().port}/__test`);
    await page.evaluate(async()=>{
      const {mountTitlebar}=await import('/titlebar.js');
      window.titlebar=mountTitlebar(document.getElementById('titlebar'),{account:()=>true,
        window:action=>{if(action==='maximize')queueMicrotask(()=>window.titlebar.update(true));return true;}});
    });
    const shown=()=>page.evaluate(()=>document.querySelector('[data-lume-shared-titlebar]').dataset.visible==='true');
    assert.equal(await shown(),true);
    await page.click('[data-window="maximize"]');
    await page.evaluate(()=>window.titlebar.update(true));
    await page.mouse.move(640,400);
    assert.equal(await page.evaluate(()=>document.activeElement?.dataset.window),'maximize','the click left focus on the button');
    assert.equal(await shown(),false,'fullscreen hides the bar although the button kept focus');
    await page.mouse.move(640,10);
    assert.equal(await shown(),true,'reaching the top brings it back');
    await page.mouse.move(640,400);
    await page.evaluate(()=>window.titlebar.update(false));
    assert.equal(await shown(),true);
    await page.evaluate(()=>window.titlebar.setImmersive(true));
    assert.equal(await shown(),false,'music immersive mode hides it');
  }finally{await browser.close();server.close();}
});
