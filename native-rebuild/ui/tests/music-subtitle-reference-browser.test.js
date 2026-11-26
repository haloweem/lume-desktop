import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {readFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {fileURLToPath} from 'node:url';
import {DEFAULT_MUSIC_PREFERENCES} from '../music-preference-values.js';

test('subtitle tracks render in order and react to the selected mode', {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:180000},async()=>{
  const ui=fileURLToPath(new URL('../',import.meta.url));
  const server=createServer(async(req,res)=>{try{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/')return res.setHeader('content-type','text/html').end('<html><body></body></html>');
    const file=path.resolve(ui,'.'+pathname); if(!file.startsWith(ui))return res.writeHead(403).end();
    res.setHeader('content-type',({'.html':'text/html','.js':'text/javascript','.css':'text/css'})[path.extname(file)]||'application/octet-stream');res.end(await readFile(file));
  }catch{res.writeHead(404).end();}});
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const origin=`http://127.0.0.1:${server.address().port}`;
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
  const browser=await chromium.launch({channel:'msedge'});
  try{for(const mode of ['nebbia','machi','hanabi','cappella','partita']){
    const page=await browser.newPage();page.on('pageerror',e=>console.error(mode,e.message));page.on('console',m=>{if(m.type()==='error')console.error(mode,m.text())});
    await page.goto(origin);await page.evaluate(mode=>{const f=document.createElement('iframe');f.width=900;f.height=550;document.body.append(f);
      window.addEventListener('message',e=>{if(e.data.type==='lume-lyric-stage-ready')window.ready=true});f.src='/music-visualizers/reference'+(mode==='partita'?'':'-next')+'/index.html#'+mode;
      window.send=state=>f.contentWindow.postMessage({type:'lume-lyric-stage',mode,state},location.origin);},mode);
    await page.waitForFunction(()=>window.ready);const frame=page.frames()[1];
    const lines=[{fullText:'Original words',translation:'Translated words',romanization:'Romanized words',startTime:0,endTime:12,words:[]}];
    for(const [choice,expected] of [['both',['Romanized words','Translated words']],['romanization',['Romanized words']],['translation',['Translated words']],['none',[]],['both',['Translated words']]]){
      if(expected.length===1&&choice==='both')lines[0].romanization='';
      await page.evaluate(state=>window.send(state),{lines,current:{id:'fixture',title:'title',artist:'artist'},duration:12,currentTime:4,activeLineIndex:0,playing:true,visible:true,preferences:{...DEFAULT_MUSIC_PREFERENCES,mode,subtitleContentMode:choice}});
      await frame.waitForFunction(({expected})=>{const text=document.body.innerText;const all=['Romanized words','Translated words'];return document.querySelector('#reference-stage').dataset.referenceState==='ready'&&all.every(w=>text.includes(w)===expected.includes(w))&&(expected.length!==2||text.indexOf(all[0])<text.indexOf(all[1]));},{expected},{timeout:6500}).catch(async e=>{throw new Error(`${mode}/${choice} visible text: ${await frame.locator('body').innerText()}`,{cause:e})});
    }
    if(['nebbia','machi','hanabi'].includes(mode)){
      await page.waitForFunction(async()=>{
        const f=document.querySelector('iframe'),stage=f.contentDocument.querySelector('canvas[data-lumenic-liquid-glass-scene-effect]');
        if(!stage)return false;
        const {createMusicMaterialScene}=await import('/music-material/scene.js');
        const probe=document.createElement('div');probe.style.cssText='position:absolute;left:120px;top:425px;width:650px;height:60px';document.body.append(probe);
        const scene=createMusicMaterialScene(document);
        try{const result=scene.capture(probe,document.body,null),ctx=result.canvas.getContext('2d');
          const data=ctx.getImageData(0,0,result.canvas.width,result.canvas.height).data;
          return result.canvases>0&&!result.issues.length&&data.some((alpha,i)=>i%4===3&&alpha>4);
        }finally{scene.dispose();probe.remove();}
      },undefined,{timeout:18000});
    }
    assert.equal(await frame.locator('#reference-stage[data-reference-state="error"]').count(),0);console.log(mode,'verified');await page.close();
  }}finally{await browser.close();server.close();}
});
