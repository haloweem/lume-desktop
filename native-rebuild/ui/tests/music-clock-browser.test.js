import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,mkdtemp,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';

function installClockProbe() {
  if(window!==window.top) {
    window.addEventListener('message',event=>{
      if(event.source!==parent || event.origin!==location.origin)return;
      const probe=parent.musicClockProbe;
      if(!probe)return;
      if(event.data?.type==='music-clock-test-barrier') {
        probe.barrierReceived=event.data.id;
        return;
      }
      if(event.data?.type!=='lume-lyric-stage' || event.data.mode!==location.hash.slice(1))return;
      const state=event.data.state;
      probe.messages.push({phase:probe.phase,mode:event.data.mode,parentNow:probe.now,
        currentTime:state.currentTime,activeLineIndex:state.activeLineIndex,
        playing:state.playing,visible:state.visible,
        lines:state.lines?.map(line=>line.fullText),current:state.current,
        preferences:state.preferences,
        lightweight:state.lines===undefined && state.current===undefined && state.preferences===undefined});
    });
    return;
  }
  const listeners=new Set();
  const probe=window.musicClockProbe={now:10000,phase:'setup',messages:[],holdAcks:false,
    pendingAcks:[],delivered:[],barrierSent:0,barrierReceived:0};
  // Freeze only the parent projection clock; iframe animation clocks and RAF remain real.
  Object.defineProperty(performance,'now',{configurable:true,value:()=>probe.now});
  const deliver=message=>{
    probe.delivered.push(structuredClone(message));
    listeners.forEach(listener=>listener({data:message}));
  };
  window.harness={commands:[],receive:message=>{
    if(probe.holdAcks && message.type==='music-preferences-state' && message.request>0)
      probe.pendingAcks.push(message);
    else deliver(message);
  }};
  probe.releaseAck=()=>{
    const message=probe.pendingAcks.shift();
    if(!message)throw new Error('No preference ACK to release');
    deliver(message);
  };
  window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
    removeEventListener:(_,listener)=>listeners.delete(listener),
    postMessage:message=>harness.commands.push(message)}};
}

test('production Edge music clock retains display-only anchors and accepts native backward seeks',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:180000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const artifacts=path.resolve(ui,'../../build-artifacts/native-rebuild');
  await mkdir(artifacts,{recursive:true});
  const evidence=await mkdtemp(path.join(artifacts,`music-clock-${new Date().toISOString().replace(/[:.]/g,'-')}-`));
  const dependencies=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||path.join(dependencies,'playwright'));
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/favicon.ico')return res.writeHead(204).end();
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      const bytes=await readFile(file);
      res.writeHead(200,{'Cache-Control':'no-store','Content-Type':({
        '.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp',
        '.png':'image/png','.svg':'image/svg+xml','.woff2':'font/woff2',
      })[path.extname(file)]||'application/octet-stream'}).end(bytes);
    }catch{res.writeHead(404).end();}
  });
  const report={verifiedAt:new Date().toISOString(),status:'running',
    scope:{entry:'/index.html',productionMount:true,realReferenceIframe:true,
      bridge:'in-memory fixture with held preference ACKs',clock:'parent performance.now only',
      nativeAudioVerified:false,realConfigurationTouched:false},scenarios:[]};
  let browser;
  try {
    await new Promise((resolve,reject)=>{
      server.once('error',reject);
      server.listen(0,'127.0.0.1',resolve);
    });
    const origin=`http://127.0.0.1:${server.address().port}`;
    browser=await chromium.launch({channel:'msedge',headless:true});
    report.browser={channel:'msedge',version:browser.version(),headless:true};

    async function runScenario(result,faultSource) {
      const page=await browser.newPage({viewport:{width:1440,height:900}});
      const errors=[],remoteRequests=[];
      result.checks=[];result.screenshots=[];
      page.setDefaultTimeout(10000);
      page.on('pageerror',error=>errors.push(error.message));
      page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
      await page.route('**/*',route=>{
        const url=new URL(route.request().url());
        if(url.origin!==origin) {
          remoteRequests.push(url.href);
          return route.abort();
        }
        if(faultSource && url.pathname==='/music-visualizer.js') {
          result.faultServed=true;
          return route.fulfill({status:200,contentType:'text/javascript',body:faultSource});
        }
        return route.continue();
      });
      await page.addInitScript({content:`(${installClockProbe.toString()})();
        if(window===window.top)(${installMusicPreferencesHarness.toString()})();`});
      const screenshot=async name=>{
        const file=`${result.name}-${name}.png`;
        await page.screenshot({path:path.join(evidence,file)});
        result.screenshots.push(file);
      };
      const mark=phase=>page.evaluate(phase=>{
        musicClockProbe.phase=phase;
        return musicClockProbe.messages.length;
      },phase);
      const saves=()=>page.evaluate(()=>harness.commands.filter(command=>
        command?.scope==='music-preferences' && command.action==='apply'));
      const reference=()=>page.frames().find(frame=>frame.url().includes('/music-visualizers/reference/index.html#'));
      const waitClock=async(time,line,mode='classic')=>{
        await page.waitForFunction(({time,line,mode})=>{
          const last=musicClockProbe.messages.at(-1);
          return last?.mode===mode && Math.abs(last.currentTime-time)<1e-7 && last.activeLineIndex===line;
        },{time,line,mode});
        await reference().waitForSelector(`#reference-stage[data-reference-state="ready"][data-reference-line="${line}"]`);
      };
      const check=async(phase,after,time,line,mode='classic',{lightweight=false,retained=true}={})=>{
        await page.waitForFunction(({after,mode})=>musicClockProbe.messages.length>after &&
          musicClockProbe.messages.at(-1).mode===mode,{after,mode});
        // A FIFO barrier drains real host posts before inspection, including transient backward ticks.
        const barrier=await page.evaluate(()=>{
          const id=++musicClockProbe.barrierSent;
          document.querySelector('.MusicReferenceStage').contentWindow.postMessage(
            {type:'music-clock-test-barrier',id},location.origin);
          return id;
        });
        await page.waitForFunction(id=>musicClockProbe.barrierReceived===id,barrier);
        const sample=await page.evaluate(after=>{
          const iframe=document.querySelector('.MusicReferenceStage');
          const stage=iframe.contentDocument.getElementById('reference-stage');
          return {now:performance.now(),messages:musicClockProbe.messages.slice(after),
            retained:iframe===musicClockProbe.initialFrame,mode:stage.dataset.referenceMode,
            line:Number(stage.dataset.referenceLine),renders:Number(stage.dataset.referenceRenders),
            state:stage.dataset.referenceState};
        },after);
        result.checks.push({phase,expectedTime:time,expectedLine:line,...sample});
        assert.ok(sample.messages.length,`${phase}: actual iframe clock messages received`);
        for(const message of sample.messages) {
          assert.equal(Math.round(message.currentTime*1e7)/1e7,time,`${phase}: clock must stay at ${time}s`);
          assert.equal(message.activeLineIndex,line,`${phase}: lyric line must not rewind`);
          assert.equal(message.playing,true,`${phase}: playing is retained`);
          assert.equal(message.visible,true,`${phase}: stage remains visible`);
          if(lightweight)assert.equal(message.lightweight,true,`${phase}: clock-only delta`);
        }
        assert.equal(sample.mode,mode);
        assert.equal(sample.line,line,`${phase}: real renderer consumed the active line`);
        assert.equal(sample.state,'ready');
        if(retained)assert.equal(sample.retained,true,`${phase}: iframe retained`);
        return sample;
      };
      try {
        await page.goto(`${origin}/index.html`);
        await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
        await page.evaluate(async()=>{
          const {initialMusicState}=await import('./music.js');
          const {query,nav,lyrics,...native}=initialMusicState();
          const track={id:'clock-fixture',title:'Clock regression',artist:'Lume',album:'Test',
            source:'C:/fixture/music-clock.wav',artwork:'',duration:60};
          window.clockSnapshot={...native,version:1,type:'music-state',session:27,revision:1,
            pageOpen:true,pageVisible:true,status:'paused',playing:false,current:track,queue:[track],
            timeline:{position:4.8,duration:60},
            capabilities:{canToggle:true,canPrevious:true,canNext:true,canSeek:true}};
          harness.receive(clockSnapshot);
          harness.receive({version:1,type:'navigate',page:'music'});
          window.clockLyrics={version:1,type:'music-lyrics',session:27,trackId:track.id,
            lines:[[0,3,'Native seek at two seconds'],[3,5,'Before the five second boundary'],
              [5,5.7,'Projected lyric after five seconds'],[5.7,60,'One second extrapolation limit']]
              .map(([startTime,endTime,fullText])=>({startTime,endTime,fullText,translation:'',words:[]}))};
          harness.receive(clockLyrics);
        });
        await page.locator('[data-music-open-player]').click();
        await page.locator('[data-music-background][data-music-background-state="ready"]').waitFor();
        await waitClock(4.8,1);
        await page.locator('[data-music-action="lyrics"]').click();
        await page.locator('[data-music-panel-tab="controls"]').click();
        const color=page.locator('[data-music-preference="lyricsFontColor"]');
        await color.waitFor({state:'visible'});
        assert.equal(await color.isEnabled(),true);
        await page.evaluate(()=>{
          musicClockProbe.initialFrame=document.querySelector('.MusicReferenceStage');
          musicClockProbe.initialColor=document.querySelector('[data-music-preference="lyricsFontColor"]');
          clockSnapshot={...clockSnapshot,revision:2,status:'playing',playing:true};
          harness.receive(clockSnapshot);
        });
        await waitClock(4.8,1);
        let after=await mark('native-anchor');
        await check('native-anchor',after,4.8,1,'classic',{lightweight:true});
        await page.evaluate(()=>musicClockProbe.now=10500);
        await waitClock(5.3,2);
        after=await mark('interpolated-anchor');
        await check('interpolated-anchor',after,5.3,2,'classic',{lightweight:true});
        const frameTime=await reference().evaluate(()=>performance.now());
        await reference().waitForFunction(before=>performance.now()>before+1,frameTime);
        assert.equal(await page.evaluate(()=>performance.now()),10500,'iframe clock was not frozen');
        await screenshot('anchor-5.3');

        const beforeSaves=(await saves()).length;
        after=await mark('preference-input');
        await color.evaluate(input=>{
          input.value='#ff2200';
          input.dispatchEvent(new Event('input',{bubbles:true}));
        });
        await page.waitForFunction(()=>musicClockProbe.messages.some(message=>
          message.phase==='preference-input' && message.preferences?.lyricsFontColor==='#ff2200'));
        const preview=await check('preference-input',after,5.3,2);
        assert.equal((await saves()).length,beforeSaves,'input is preview-only');
        assert.equal(await page.evaluate(()=>musicClockProbe.initialColor===
          document.querySelector('[data-music-preference="lyricsFontColor"]')),true);

        await page.evaluate(()=>musicClockProbe.holdAcks=true);
        after=await mark('preference-commit-pending');
        await color.dispatchEvent('change');
        await page.waitForFunction(()=>musicClockProbe.pendingAcks.length===1 &&
          document.querySelector('[data-music-settings]')?.getAttribute('aria-busy')==='true');
        assert.equal((await saves()).length,beforeSaves+1);
        const pending=await check('preference-commit-pending',after,5.3,2,'classic',{lightweight:true});
        assert.equal(pending.renders,preview.renders,'equivalent pending changes only settings DOM');
        after=await mark('preference-ack');
        await page.evaluate(()=>{musicClockProbe.releaseAck();musicClockProbe.holdAcks=false;});
        await page.waitForFunction(()=>document.querySelector('[data-music-settings]')?.getAttribute('aria-busy')==='false');
        const ack=await check('preference-ack',after,5.3,2,'classic',{lightweight:true});
        assert.equal(ack.renders,pending.renders,'equivalent ACK does not rerender reference lyrics');

        after=await mark('panel-close');
        await page.locator('[data-music-panel-tab="controls"]').press('Escape');
        await page.locator('[data-music-panel]').waitFor({state:'detached'});
        await check('panel-close',after,5.3,2,'classic',{lightweight:true});
        after=await mark('panel-open');
        await page.locator('[data-music-action="lyrics"]').click();
        await color.waitFor({state:'visible'});
        await check('panel-open',after,5.3,2,'classic',{lightweight:true});

        after=await mark('account-state');
        await page.evaluate(()=>harness.receive({version:1,type:'account-state',revision:1,
          status:'signed-in',busy:false,error:'',profile:{id:'clock-account',displayName:'Clock Account',
            avatarUrl:'',vip:false,vipExpiresAt:''}}));
        await page.waitForFunction(()=>document.querySelector('[data-lume-account-label]')?.textContent==='Clock Account');
        await check('account-state',after,5.3,2,'classic',{lightweight:true});
        after=await mark('late-lyrics');
        await page.evaluate(()=>{
          clockLyrics={...clockLyrics,lines:clockLyrics.lines.map((line,index)=>
            index===2?{...line,fullText:'Late lyrics still at five seconds'}:line)};
          harness.receive(clockLyrics);
        });
        // Base glyphs and karaoke fills duplicate words in the historical DOM.
        await reference().waitForFunction(()=>document.getElementById('reference-stage')
          .textContent.includes('Late'));
        const late=await check('late-lyrics',after,5.3,2);
        assert.ok(late.messages.some(message=>message.lines?.includes('Late lyrics still at five seconds')));
        await screenshot('display-updates-5.3');

        await page.locator('[data-music-panel-tab="globalSettings"]').click();
        for(const mode of ['cadenza','classic']) {
          after=await mark(`mode-${mode}`);
          await page.locator('[data-music-visualizer-trigger]').click();
          await page.locator(`[data-music-visualizer="${mode}"]`).click();
          await page.frameLocator('.MusicReferenceStage').locator(
            `#reference-stage[data-reference-state="ready"][data-reference-mode="${mode}"]`).waitFor();
          await page.waitForFunction(()=>document.querySelector('[data-music-settings]')?.getAttribute('aria-busy')==='false');
          const switched=await check(`mode-${mode}`,after,5.3,2,mode,{retained:false});
          assert.equal(switched.retained,false,'mode switch mounts a real new reference iframe');
          assert.ok(switched.messages.some(message=>message.mode===mode && message.lines?.length===4 &&
            message.current?.id==='clock-fixture'),'new iframe receives the structural snapshot');
          assert.equal(await page.locator('.MusicReferenceStage').count(),1);
        }
        await page.evaluate(()=>musicClockProbe.initialFrame=document.querySelector('.MusicReferenceStage'));
        for(const now of [11000,16000]) {
          await page.evaluate(now=>musicClockProbe.now=now,now);
          await waitClock(5.8,3);
          after=await mark(`extrapolation-${now}`);
          await check(`extrapolation-${now}`,after,5.8,3,'classic',{lightweight:true});
        }

        after=await mark('native-backward-seek');
        await page.evaluate(()=>{
          clockSnapshot={...clockSnapshot,revision:3,account:{signedIn:true},
            timeline:{position:2,duration:60}};
          harness.receive(clockSnapshot);
        });
        await waitClock(2,0);
        // Pending pre-seek posts may be 5.8; the native snapshot must actually publish 2, not clamp it.
        const seekMessages=await page.evaluate(after=>musicClockProbe.messages.slice(after),after);
        assert.ok(seekMessages.some(message=>message.currentTime===2 && message.activeLineIndex===0));
        after=await mark('native-backward-seek-stable');
        await check('native-backward-seek-stable',after,2,0,'classic',{lightweight:true});
        await screenshot('native-seek-2');
        await page.evaluate(()=>musicClockProbe.now=16500);
        await waitClock(2.5,0);
        after=await mark('post-seek-interpolation');
        await check('post-seek-interpolation',after,2.5,0,'classic',{lightweight:true});
        const playback=await page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music' &&
          !['open','close','snapshot','loadHome','loadLyrics','visibility'].includes(command.action)));
        assert.deepEqual(playback,[],'display interactions never issue native playback commands');
        assert.deepEqual(errors,[]);
        assert.deepEqual(remoteRequests,[]);
        result.status='passed';
      }catch(error) {
        result.status='failed';
        result.failure={name:error.name,message:error.message,actual:error.actual,expected:error.expected};
        await screenshot('failure').catch(()=>{});
        throw error;
      }finally {
        result.pageErrors=errors;result.remoteRequests=remoteRequests;
        Object.assign(result,await page.evaluate(()=>({commands:harness.commands,
          receivedClockMessages:musicClockProbe.messages,deliveredHostMessages:musicClockProbe.delivered,
          pendingAcks:musicClockProbe.pendingAcks.length,
          renderedText:document.querySelector('.MusicReferenceStage')?.contentDocument
            ?.getElementById('reference-stage')?.textContent})).catch(()=>({})));
        await page.close();
      }
    }

    const production={name:'production'};
    report.scenarios.push(production);
    await runScenario(production);

    // Negative control: restore the old unconditional reset in this page's HTTP response only.
    const source=await readFile(path.join(ui,'music-visualizer.js'),'utf8');
    const fixed='if(!state || syncClock)receivedAt=window.performance.now();';
    assert.equal(source.split(fixed).length,2,'fault injection must target exactly one clock assignment');
    const negative={name:'old-reset-negative-control',fault:'unconditional receivedAt reset, response-only'};
    report.scenarios.push(negative);
    await assert.rejects(()=>runScenario(negative,source.replace(fixed,'receivedAt=window.performance.now();')),error=>{
      assert.equal(error.code,'ERR_ASSERTION');
      assert.match(error.message,/preference-input: clock/);
      assert.equal(error.actual,4.8);
      assert.equal(error.expected,5.3);
      assert.equal(negative.faultServed,true);
      assert.deepEqual(negative.pageErrors,[]);
      assert.deepEqual(negative.remoteRequests,[]);
      negative.status='regression-detected';
      return true;
    },'the same production-entry assertion must reject the old clock-reset behavior');
    report.status='passed';
  }catch(error) {
    report.status='failed';
    report.failure={name:error.name,message:error.message,stack:error.stack};
    throw error;
  }finally {
    await writeFile(path.join(evidence,'report.json'),JSON.stringify(report,null,2)+'\n');
    console.log(`Music clock evidence: ${evidence}`);
    await browser?.close();
    if(server.listening)await new Promise(resolve=>server.close(resolve));
  }
});
