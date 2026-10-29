import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('font picker delegates queries and commits, handles lifecycle and keeps the stage mounted',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:90000},async()=>{
  const ui=fileURLToPath(new URL('..',import.meta.url));
  const dependencies=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||path.join(dependencies,'playwright'));
  const {PNG}=createRequire(import.meta.url)(path.join(dependencies,'pngjs'));
  const evidence=fileURLToPath(new URL('../../../build-artifacts/panel-fidelity/',import.meta.url));
  await mkdir(evidence,{recursive:true});
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/') {
      res.setHeader('Content-Type','text/html; charset=utf-8');
      return res.end('<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/music.css"><style>body{margin:0}main{height:100dvh}</style><main></main>');
    }
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(path.resolve(ui)+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    browser=await chromium.launch({channel:'msedge',headless:true});
    for(const width of [1440,390]) {
      const page=await browser.newPage({viewport:{width,height:844}});
      const errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      async function mount(kind='injected') {
        await page.goto(`http://127.0.0.1:${server.address().port}/`);
        await page.evaluate(async kind=>{
          const {mountMusicView}=await import('./music-view.js');
          const {initialMusicState}=await import('./music.js');
          const {DEFAULT_MUSIC_PREFERENCES}=await import('./music-preference-values.js');
          const root=document.querySelector('main');
          window.harness={queries:0,commits:[],commands:[],mounts:0,updates:0,disposals:0};
          const query=({signal}={})=>{
            harness.queries++;
            harness.signal=signal;
            return new Promise((resolve,reject)=>{harness.resolve=resolve;harness.reject=reject;});
          };
          if(kind==='browser')window.queryLocalFonts=function(){harness.bound=this===window;harness.argCount=arguments.length;return query();};
          else window.queryLocalFonts=undefined;
          harness.state={...initialMusicState(),session:1,pageVisible:true,pageOpen:true};
          harness.preferences={loaded:true,pending:false,preferences:DEFAULT_MUSIC_PREFERENCES};
          harness.controller=mountMusicView(root,{
            snapshot:harness.state,preferences:harness.preferences,
            background:{loaded:true,pending:false,media:null,error:''},
            viewState:{screen:'player',panel:'lyrics',panelTab:'globalSettings'},
            ...(kind==='injected'?{queryFonts:query}:{}),
            createBackground:()=>({update(){},dispose(){}}),
            createEffects:()=>({update(){},dispose(){}}),
            createVisualizer:()=>{harness.mounts++;return {update(){harness.updates++;},dispose(){harness.disposals++;}};},
            send:(...args)=>{harness.commands.push(args);return true;},
            onPreferenceCommit:patch=>{
              harness.commits.push(patch);
              harness.preferences={...harness.preferences,preferences:{...harness.preferences.preferences,...patch}};
              harness.controller.updatePreferences(harness.preferences);
            },
          });
          harness.stage=root.querySelector('[data-music-stage]');
          harness.baseline=harness.updates;
        },kind);
      }
      const trigger=page.locator('.MusicFontChange');
      const quick=page.getByRole('button',{name:'常用字体',exact:true});
      const dialog=page.getByRole('dialog',{name:'选择字体'});
      const search=page.getByRole('searchbox',{name:'搜索字体'});
      const status=page.locator('[data-music-font-status]');
      const rows=page.locator('[data-music-font-list] [data-music-font-family]');
      const settle=fonts=>page.evaluate(fonts=>harness.resolve(fonts),fonts);
      await mount();
      assert.equal(await page.locator('#music-font option').count(),8);
      assert.equal(await page.evaluate(()=>harness.queries),0,'enumeration requires opening');
      assert.equal(await page.locator('#music-font').isVisible(),false,'native select is only a compatibility proxy');
      await quick.click();
      assert.equal(await page.evaluate(()=>harness.queries),0,'quick fonts do not enumerate system fonts');
      assert.equal(await page.getByRole('listbox',{name:'常用字体'}).getByRole('option').count(),8);
      await page.keyboard.press('End');
      assert.equal(await page.evaluate(()=>document.activeElement.textContent),'Segoe UI');
      await page.keyboard.press('Home');
      await page.keyboard.press('Enter');
      assert.deepEqual(await page.evaluate(()=>harness.commits),[{fontFamily:''}]);
      await page.evaluate(()=>harness.commits=[]);
      await quick.click();
      const panelBefore=await page.locator('.MusicPlayerPanel').evaluate(node=>node.getBoundingClientRect().toJSON());
      await page.locator('[data-music-background-trigger]').click();
      assert.equal(await page.getByRole('listbox',{name:'常用字体'}).count(),0,'outside pointer closes quick picker');
      const backgroundGeometry=await page.locator('[data-music-background-picker]').evaluate(node=>{
        const trigger=node.querySelector('[data-music-background-trigger]').getBoundingClientRect();
        const menu=node.querySelector('[data-music-background-menu]').getBoundingClientRect();
        const panel=node.closest('.MusicPlayerPanel').getBoundingClientRect();
        return {width:Math.round(trigger.width),right:Math.abs(trigger.right-menu.right),
          gap:Math.round(trigger.top-menu.bottom),contained:menu.top>=panel.top&&menu.bottom<=panel.bottom&&menu.right<=panel.right,
          position:getComputedStyle(node.querySelector('[data-music-background-menu]')).position};
      });
      assert.deepEqual(backgroundGeometry,{width:274,right:0,gap:8,contained:true,position:'absolute'});
      assert.deepEqual(await page.locator('.MusicPlayerPanel').evaluate(node=>node.getBoundingClientRect().toJSON()),panelBefore,
        'absolute source menu never grows or moves its panel');
      await page.screenshot({path:path.join(evidence,`background-${width}.png`)});
      await page.locator('[data-music-background-trigger]').click();
      await page.evaluate(()=>{
        // Independent historical material reference, over the same solid synthetic backdrop.
        document.querySelector('[data-music-root]').style.background='rgb(48, 60, 76)';
        const reference=document.createElement('div');
        reference.id='historical-material';
        reference.style.cssText='position:fixed;left:0;top:0;width:32px;height:32px;background:rgba(0,0,0,.2);backdrop-filter:blur(24px)';
        document.querySelector('[data-music-root]').append(reference);
      });
      const panelImage=PNG.sync.read(await page.locator('.MusicPlayerPanel').screenshot({path:path.join(evidence,`settings-${width}.png`)}));
      const referenceImage=PNG.sync.read(await page.locator('#historical-material').screenshot());
      const pixel=(image,x,y)=>[...image.data.subarray((y*image.width+x)*4,(y*image.width+x)*4+3)];
      const actual=pixel(panelImage,10,Math.floor(panelImage.height/2)),expected=pixel(referenceImage,16,16);
      assert.ok(actual.every((value,index)=>Math.abs(value-expected[index])<=2),
        `panel material pixels ${actual} must match historical reference ${expected}`);
      assert.deepEqual(await quick.evaluate(node=>({font:getComputedStyle(node).fontSize,
        height:node.getBoundingClientRect().height})),{font:'10px',height:23});
      await page.locator('#historical-material').evaluate(node=>node.remove());
      await trigger.click();
      assert.equal(await page.evaluate(()=>harness.signal.aborted),false);
      assert.equal(await dialog.getAttribute('aria-busy'),'true');
      assert.equal(await status.innerText(),'正在读取字体');
      assert.equal(await search.evaluate(node=>node===document.activeElement),true);
      await search.fill(' sans ');
      await settle([{family:'Zulu'},{family:' Test Sans '},{family:'test sans'},{family:'Arial'},null,{family:' '}]);
      assert.deepEqual(await rows.allTextContents(),['Test Sans']);
      await search.fill('');
      assert.deepEqual(await rows.allTextContents(),['Arial','Test Sans','Zulu']);
      const style=await dialog.evaluate(node=>{
        const css=getComputedStyle(node);
        return {padding:css.padding,borderRadius:css.borderRadius,background:css.backgroundColor,
          blur:css.backdropFilter,title:getComputedStyle(node.querySelector('h2')).fontSize,
          listHeight:getComputedStyle(node.querySelector('[data-music-font-list]')).maxHeight};
      });
      assert.deepEqual(style,{padding:'12px',borderRadius:'12px',background:'rgba(0, 0, 0, 0.2)',
        blur:'blur(24px)',title:'12px',listHeight:'160px'},'historical font picker material and sizing');
      await search.fill('不存在');
      assert.equal(await status.innerText(),'没有找到匹配字体。');
      await search.fill('');
      await search.press('Control+k');
      await search.press('ArrowRight');
      assert.equal(await dialog.isVisible(),true,'player shortcuts cannot escape the modal');
      await page.evaluate(()=>harness.controller.update({...harness.state,error:'fixture'}));
      assert.equal(await search.evaluate(node=>node===document.activeElement),true,'structural snapshot retains editor focus');
      const geometry=await dialog.evaluate(node=>{
        const rect=node.getBoundingClientRect();
        return {fits:rect.left>=0&&rect.right<=innerWidth&&rect.top>=0&&rect.bottom<=innerHeight,
          overflow:node.scrollWidth>node.clientWidth};
      });
      assert.deepEqual(geometry,{fits:true,overflow:false});
      const placement=await dialog.evaluate(node=>{
        const rect=node.getBoundingClientRect(),row=node.previousElementSibling.getBoundingClientRect();
        const next=node.nextElementSibling.getBoundingClientRect();
        return {inline:node.parentElement.classList.contains('MusicGlobalSettings'),
          gap:Math.round(rect.top-row.bottom),nextGap:Math.round(next.top-rect.bottom),
          width:Math.round(rect.width),modal:node.getAttribute('aria-modal')};
      });
      assert.deepEqual(placement,{inline:true,gap:16,nextGap:16,width:274,modal:null});
      await page.screenshot({path:path.join(evidence,`font-popup-${width}.png`)});
      await rows.last().focus();
      await page.keyboard.press('Tab');
      assert.equal(await dialog.evaluate(node=>node.contains(document.activeElement)),true,'modal traps focus');
      await page.keyboard.press('Shift+Tab');
      assert.equal(await rows.last().evaluate(node=>node===document.activeElement),true,'reverse Tab wraps too');
      await page.getByRole('button',{name:'Test Sans',exact:true}).click();
      assert.equal(await dialog.count(),0);
      assert.equal(await page.evaluate(()=>harness.signal.aborted),true);
      assert.equal(await trigger.evaluate(node=>node===document.activeElement),true);
      assert.equal(await page.locator('#music-font').inputValue(),'Test Sans');
      assert.deepEqual(await page.evaluate(()=>harness.commits),[{fontFamily:'Test Sans'}]);
      await trigger.click();
      assert.equal(await page.locator('[data-music-font-reset]').innerText(),'恢复默认');
      await page.locator('[data-music-font-reset]').click();
      assert.deepEqual(await page.evaluate(()=>harness.commits),[{fontFamily:'Test Sans'},{fontFamily:''}]);
      await quick.click();
      await page.getByRole('option',{name:'默认字体',exact:true}).click();
      assert.deepEqual(await page.evaluate(()=>harness.commits),[{fontFamily:'Test Sans'},{fontFamily:''},{fontFamily:''}]);
      await trigger.click();
      assert.equal(await page.evaluate(()=>harness.queries),1,'nonempty result cached');
      await search.press('Escape');
      assert.equal(await dialog.count(),0);
      assert.equal(await trigger.evaluate(node=>node===document.activeElement),true);
      assert.equal(await page.locator('[data-music-player-panel]').isVisible(),true);
      await trigger.click();
      await page.getByRole('button',{name:'关闭字体选择'}).click();
      assert.equal(await trigger.evaluate(node=>node===document.activeElement),true);
      assert.deepEqual(await page.evaluate(()=>({
        commands:harness.commands,mounts:harness.mounts,disposals:harness.disposals,
        retained:harness.stage===document.querySelector('[data-music-stage]'),
      })),{commands:[],mounts:1,disposals:0,retained:true});

      await mount('browser');
      await trigger.click();
      assert.equal(await page.evaluate(()=>harness.bound),true);
      assert.equal(await page.evaluate(()=>harness.argCount),0,'queryLocalFonts receives no adapter arguments');
      await page.evaluate(()=>harness.reject(new Error('permission denied')));
      assert.equal(await status.innerText(),'读取本机字体失败，请检查权限。');
      assert.equal(await status.getAttribute('role'),'alert');
      await search.press('Escape');
      await trigger.click();
      await settle([]);
      assert.equal(await status.innerText(),'没有读取到可用字体。');
      await search.press('Escape');
      await trigger.click();
      await settle([{family:'<img src=x onerror=alert(1)>'},{family:'A'.repeat(129)},{family:'Valid'}]);
      assert.equal(await dialog.locator('img').count(),0,'family names are text, never markup');
      assert.equal(await rows.count(),2,'unrepresentable DTO families are omitted');
      await search.press('Escape');

      await mount('unavailable');
      await trigger.click();
      assert.equal(await status.innerText(),'当前环境无法读取本机字体。');
      assert.equal(await dialog.getAttribute('aria-busy'),'false');
      await search.fill('Arial');
      assert.equal(await status.innerText(),'当前环境无法读取本机字体。');
      assert.equal(await rows.count(),0,'no fabricated font list');
      await search.press('Escape');

      await mount();
      await trigger.click();
      await page.evaluate(()=>{harness.oldResolve=harness.resolve;harness.oldSignal=harness.signal;});
      await search.press('Escape');
      assert.equal(await page.evaluate(()=>harness.oldSignal.aborted),true);
      await trigger.click();
      await page.evaluate(()=>harness.oldResolve([{family:'Stale'}]));
      assert.equal(await dialog.getAttribute('aria-busy'),'true');
      assert.equal(await rows.count(),0,'closed request cannot populate reopened picker');
      await settle([{family:'Fresh'}]);
      assert.deepEqual(await rows.allTextContents(),['Fresh']);
      await page.evaluate(()=>harness.controller.setVisible(false));
      assert.equal(await dialog.count(),0);
      await page.evaluate(()=>harness.controller.setVisible(true));
      await trigger.click();
      await page.evaluate(()=>harness.controller.updatePreferences({...harness.preferences,loaded:false}));
      assert.equal(await dialog.count(),0);
      assert.equal(await trigger.isDisabled(),true);
      await page.evaluate(()=>harness.controller.updatePreferences(harness.preferences));
      assert.equal(await trigger.isEnabled(),true);

      await mount();
      await trigger.click();
      await page.evaluate(()=>{harness.controller.dispose();harness.resolve([{family:'Late'}]);});
      assert.equal(await page.evaluate(()=>harness.signal.aborted),true);
      assert.equal(await dialog.count(),0,'dispose removes modal and ignores late results');
      assert.equal(await page.evaluate(()=>harness.updates===harness.baseline),true,'opening/closing does not update playback');
      assert.deepEqual(errors,[]);
      await page.close();
    }
    console.log(`Panel fidelity screenshots: ${evidence}`);
  } finally {
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
  }
});
