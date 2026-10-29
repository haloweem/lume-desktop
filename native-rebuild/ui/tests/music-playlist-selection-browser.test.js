import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('A4 playlist menu matches historical anchor/material and fences closed writes without changing create dialog',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const dependencies=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const require=createRequire(import.meta.url);
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(dependencies,'playwright'));
  const {PNG}=require(path.join(dependencies,'pngjs'));
  const evidence=fileURLToPath(new URL('../../../build-artifacts/panel-fidelity/',import.meta.url));
  await mkdir(evidence,{recursive:true});
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/') {
      res.setHeader('Content-Type','text/html; charset=utf-8');
      return res.end(`<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/music.css">
        <style>body{margin:0;background:rgb(48,60,76);height:200vh}
        #anchor{position:absolute;right:40px;top:100px;width:32px;height:32px}
        #outside{position:fixed;left:8px;top:8px}
        #reference{position:fixed;left:0;bottom:0;width:32px;height:32px;background:rgba(0,0,0,.2);backdrop-filter:blur(24px)}</style>
        <button id="anchor" aria-label="添加到歌单">+</button><button id="outside">其他</button>
        <div id="reference"></div><dialog class="MusicCollectionDialog"><form><h2>新建歌单</h2><input><button>创建</button></form></dialog>`);
    }
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    browser=await chromium.launch({channel:'msedge',headless:true});
    for(const width of [1440,390,280]) {
      const page=await browser.newPage({viewport:{width,height:844}});
      const errors=[],requests=[];
      page.on('pageerror',error=>errors.push(error.message));
      page.on('request',request=>requests.push(request.url()));
      await page.goto(`http://127.0.0.1:${server.address().port}/`);
      await page.evaluate(async()=>{
        const {openMusicPlaylistSelection}=await import('/music-playlist-selection.js');
        const anchor=document.querySelector('#anchor');
        window.harness={calls:[],closed:0,created:0,mode:'hold',handle:null};
        harness.open=(count=3)=>{
          harness.handle?.close(false);
          harness.handle=openMusicPlaylistSelection({
            anchor,songName:'Synthetic song <img src=x onerror=alert(1)>',
            playlists:Array.from({length:count},(_,index)=>({
              id:`test-${index}`,name:index===0?'测试歌单 <img src=x>':`歌单 ${index}`,
              description:'Fixture only',
            })),
            onSelect:id=>{
              harness.calls.push(id);
              if(harness.mode==='reject')return false;
              return new Promise((resolve,reject)=>{harness.resolve=resolve;harness.reject=reject;});
            },
            onCreate:()=>{harness.created++;document.querySelector('dialog').showModal();},
            onClose:()=>harness.closed++,
          });
        };
        anchor.addEventListener('click',()=>harness.open());
        const css=getComputedStyle(document.querySelector('dialog'));
        harness.createStyle=[css.backgroundColor,css.borderRadius,css.backdropFilter];
      });
      await page.locator('#anchor').click();
      const menu=page.getByRole('menu',{name:'添加到歌单'});
      const metrics=await menu.evaluate(node=>{
        const css=getComputedStyle(node),rect=node.getBoundingClientRect();
        const anchor=document.querySelector('#anchor').getBoundingClientRect();
        const row=node.querySelector('[data-playlist-id]').getBoundingClientRect();
        return {width:rect.width,left:rect.left,top:rect.top,anchorRight:anchor.right,anchorBottom:anchor.bottom,
          rowHeight:row.height,radius:css.borderRadius,background:css.backgroundColor,blur:css.backdropFilter,
          title:getComputedStyle(node.querySelector('h2')).fontSize,
          listMax:getComputedStyle(node.querySelector('.MusicPlaylistSelectionList')).maxHeight,
          fits:rect.left>=8&&rect.right<=innerWidth-8&&rect.bottom<=innerHeight-8};
      });
      const menuWidth=Math.min(336,width-16);
      assert.deepEqual(metrics,{width:menuWidth,left:Math.min(Math.max(8,metrics.anchorRight+16-menuWidth),width-menuWidth-8),
        top:metrics.anchorBottom+8,anchorRight:width-40,anchorBottom:132,rowHeight:52,radius:'16px',
        background:'rgba(0, 0, 0, 0.2)',blur:'blur(24px)',title:'14px',listMax:'320px',fits:true});
      assert.equal(await menu.locator('img,select').count(),0,'text-only list, no native select');
      assert.equal(await menu.getByRole('menuitem').count(),4);
      await page.locator('#anchor').focus();
      const screenshot=PNG.sync.read(await menu.screenshot({path:path.join(evidence,`playlist-menu-${width}.png`)}));
      const reference=PNG.sync.read(await page.locator('#reference').screenshot());
      const pixel=(image,x,y)=>[...image.data.subarray((image.width*y+x)*4,(image.width*y+x)*4+3)];
      const actual=pixel(screenshot,3,80),expected=pixel(reference,16,16);
      assert.ok(actual.every((value,index)=>Math.abs(value-expected[index])<=2),
        `frosted material pixels ${actual} match historical formula ${expected}`);
      await menu.getByRole('menuitem').first().focus();
      await page.keyboard.press('End');
      assert.equal(await page.evaluate(()=>document.activeElement.dataset.playlistId),'test-2');
      await page.keyboard.press('ArrowDown');
      assert.equal(await page.evaluate(()=>document.activeElement.hasAttribute('data-playlist-create')),true);
      await menu.locator('[data-playlist-id="test-0"]').click();
      assert.equal(await menu.getAttribute('aria-busy'),'true');
      await page.evaluate(()=>document.querySelector('[data-playlist-id="test-1"]').click());
      assert.deepEqual(await page.evaluate(()=>harness.calls),['test-0'],'single-flight while submitting');
      await page.evaluate(()=>harness.reject(new Error('synthetic rejection')));
      await menu.getByRole('alert').waitFor({state:'visible'});
      assert.equal(await menu.locator('[data-playlist-id="test-1"]').isEnabled(),true);
      await menu.locator('[data-playlist-id="test-1"]').click();
      await page.evaluate(()=>harness.resolve(true));
      await menu.waitFor({state:'detached'});
      assert.equal(await page.locator('#anchor').evaluate(node=>node===document.activeElement),true);
      await page.locator('#anchor').click();
      await page.evaluate(()=>harness.mode='reject');
      await menu.locator('[data-playlist-id="test-0"]').click();
      await menu.getByRole('alert').waitFor({state:'visible'});
      assert.equal(await menu.getAttribute('aria-busy'),'false');
      await page.keyboard.press('Escape');
      assert.equal(await menu.count(),0);
      await page.locator('#anchor').click();
      await menu.locator('[data-playlist-create]').click();
      assert.equal(await menu.count(),0);
      assert.equal(await page.locator('dialog').isVisible(),true,'create keeps the existing modal');
      assert.deepEqual(await page.locator('dialog').evaluate(node=>{
        const css=getComputedStyle(node);return [css.backgroundColor,css.borderRadius,css.backdropFilter];
      }),await page.evaluate(()=>harness.createStyle));
      await page.evaluate(()=>document.querySelector('dialog').close());
      await page.locator('#anchor').click();
      await page.locator('#outside').click();
      assert.equal(await menu.count(),0);
      await page.locator('#anchor').click();
      await page.evaluate(()=>{
        harness.stale=document.querySelector('[data-playlist-id="test-0"]');
        harness.handle.close(false);harness.stale.click();
        harness.mode='hold';harness.open();
      });
      const callCount=await page.evaluate(()=>harness.calls.length);
      await menu.locator('[data-playlist-id="test-0"]').click();
      await page.evaluate(()=>{
        harness.oldResolve=harness.resolve;harness.handle.close(false);harness.open();
        harness.oldResolve(true);
      });
      assert.equal(await menu.count(),1,'late success cannot close the next menu');
      assert.equal(await page.evaluate(()=>harness.calls.length),callCount+1);
      await page.evaluate(()=>{
        harness.handle.close(false);window.scrollTo(0,0);
        document.querySelector('#anchor').style.top='760px';harness.open(20);
      });
      const above=await menu.evaluate(node=>{
        const rect=node.getBoundingClientRect(),anchor=document.querySelector('#anchor').getBoundingClientRect();
        const list=node.querySelector('.MusicPlaylistSelectionList');
        return {gap:anchor.top-rect.bottom,fits:rect.top>=8&&rect.bottom<=innerHeight-8,
          scrolls:list.scrollHeight>list.clientHeight};
      });
      assert.deepEqual(above,{gap:8,fits:true,scrolls:true});
      await page.screenshot({path:path.join(evidence,`playlist-menu-above-${width}.png`)});
      await page.evaluate(()=>window.scrollTo(0,200));
      await page.waitForFunction(()=>{
        const menu=document.querySelector('[data-playlist-selection-dropdown]').getBoundingClientRect();
        const anchor=document.querySelector('#anchor').getBoundingClientRect();
        return Math.abs(anchor.top-menu.bottom-8)<1;
      });
      await page.setViewportSize({width,height:240});
      await page.waitForFunction(()=>{
        const rect=document.querySelector('[data-playlist-selection-dropdown]').getBoundingClientRect();
        return innerHeight===240&&rect.top>=8&&rect.bottom<=innerHeight-8;
      });
      const short=await menu.evaluate(node=>{
        const rect=node.getBoundingClientRect();
        return {top:rect.top,bottom:rect.bottom,height:rect.height,viewport:innerHeight,
          fits:rect.top>=8&&rect.bottom<=innerHeight-8};
      });
      assert.equal(short.fits,true,`short viewport clamps within edges: ${JSON.stringify(short)}`);
      await page.evaluate(()=>{
        Object.defineProperty(document,'hidden',{configurable:true,value:true});
        document.dispatchEvent(new Event('visibilitychange'));
      });
      assert.equal(await menu.count(),0);
      assert.equal(requests.every(url=>url.startsWith(`http://127.0.0.1:${server.address().port}/`)),true);
      assert.deepEqual(errors,[]);
      await page.close();
    }
    console.log(`A4 isolated screenshots: ${evidence}`);
  } finally {
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
  }
});
