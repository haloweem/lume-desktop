import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir,tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';

const browserOptions={skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:120000};
const avatarUrl='https://q1.qlogo.cn/g?b=qq&nk=10001&s=100';
const vipIconUrl='https://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png';
const svipIconUrl='https://y.qq.com/mediastyle/lv-icon/v14/2x/svip5.png';
const qqProfile={avatarUrl,memberships:[{kind:'qq-green-vip',label:'VIP',level:5,active:true,iconUrl:vipIconUrl,expiresAt:1893456000000},
  {kind:'qq-super-vip',label:'SVIP',level:5,active:false,iconUrl:svipIconUrl,expiresAt:0}]};

async function withBrowser(run) {
  const ui=path.resolve(process.env.LUME_UI_ROOT||fileURLToPath(new URL('..',import.meta.url)));
  const evidence=path.join(tmpdir(),'lume-provider-account-'+Date.now());
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const require=createRequire(import.meta.url);
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const server=createServer(async(req,res)=>{
    const file=path.resolve(ui,'.'+new URL(req.url,'http://localhost').pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  try {
    await mkdir(evidence,{recursive:true});
    console.log(`Browser evidence: ${evidence}`);
    browser=await chromium.launch({headless:true,channel:'msedge'});
    await run({browser,PNG,evidence,url:`http://127.0.0.1:${server.address().port}/index.html`});
  } finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
}

for(const [width,height] of [[1440,900],[390,844]]) {
test(`historical active-account UI ${width}x${height}: public profile images, guest home entry, QR focus and stage continuity`,
  browserOptions,()=>withBrowser(async({browser,PNG,evidence,url})=>{
    const imageFixture=new PNG({width:40,height:40});
    imageFixture.data.fill(255);
    const profileImage=PNG.sync.write(imageFixture);
      const page=await browser.newPage({viewport:{width,height}}),errors=[],requests=[],popups=[];
      page.setDefaultTimeout(15000);
      page.on('pageerror',error=>errors.push(error.message));
      page.on('request',request=>requests.push(request.url()));
      page.on('popup',popup=>popups.push(popup));
      await page.route(value=>[avatarUrl,vipIconUrl,svipIconUrl].includes(value.href),
        route=>route.fulfill({contentType:'image/png',body:profileImage}));
      await page.addInitScript(()=>{
        const listeners=new Set();
        window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
        window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
          removeEventListener:(_,listener)=>listeners.delete(listener),postMessage:message=>harness.commands.push(message)}};
        window.providerCommands=()=>harness.commands.filter(value=>value?.scope==='music-provider-account');
        window.onlineCommands=()=>harness.commands.filter(value=>value?.scope==='music-online');
        window.replyOnline=()=>{
          const {request,provider,action}=onlineCommands().at(-1);
          harness.receive({version:1,type:'music-online-state',request,provider,action,status:'ready',error:'',
            data:{tracks:[],playlists:[],recommendations:[],more:false,likedIds:[]}});
        };
        window.providerState={revision:0,accounts:['netease','qq','kugou'].map(provider=>({
          provider,request:0,status:'signed-out',phase:'',userId:'',displayName:'',qrImage:'',qrText:'',error:''}))};
        window.reply=(provider,patch)=>{
          providerState.accounts=providerState.accounts.map(account=>account.provider===provider?{...account,...patch}:account);
          harness.receive({version:1,type:'music-provider-account-state',revision:++providerState.revision,
            accounts:structuredClone(providerState.accounts)});
        };
      });
      await page.addInitScript(installMusicPreferencesHarness);
      await page.goto(url);
      await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
      assert.equal(await page.evaluate(()=>providerCommands().length),0,'no auth storage restoration before music opens');
      await page.locator('[data-titlebar-music]').click();
      assert.equal(await page.evaluate(()=>providerCommands().filter(value=>value.action==='snapshot').length),1);
      await page.evaluate(()=>reply('netease',{}));
      const networkStart=requests.length;
      for(const provider of ['netease','qq','kugou']) {
        const opener=page.locator(`.MusicProviderButtons [data-music-provider-open="${provider}"]`);
        await opener.click();
        assert.equal(await page.locator('[data-provider-close]').evaluate(node=>node===document.activeElement),true);
        await page.evaluate(provider=>{
          const request=providerCommands().at(-1).request;
          reply(provider,{request,status:'authorizing',phase:'waiting',qrText:'LUME-TEST-ONLY-'+provider});
        },provider);
        const image=page.locator('.MusicProviderQr img');
        await image.waitFor({state:'visible'});
        const geometry=await image.evaluate(node=>({width:node.getBoundingClientRect().width,
          height:node.getBoundingClientRect().height,natural:node.naturalWidth,source:node.getAttribute('src').slice(0,22)}));
        assert.deepEqual(geometry,{width:160,height:160,natural:320,source:'data:image/png;base64,'});
        const png=PNG.sync.read(await image.screenshot());
        let dark=0,light=0;
        for(let i=0;i<png.data.length;i+=4) {
          if(png.data[i]<50)dark++;
          if(png.data[i]>240)light++;
        }
        assert.ok(dark>1000 && light>1000,'real locally encoded black/white QR modules');
        if(provider==='netease') {
          await page.evaluate(()=>{
            const qrImage=document.querySelector('.MusicProviderQr img').src;
            reply('netease',{qrImage,qrText:''});
          });
          await image.waitFor({state:'visible'});
          assert.equal(await image.evaluate(node=>node.naturalWidth),320,'native PNG path renders without a remote request');
        }
        assert.equal(await page.locator('dialog a, dialog iframe').count(),0);
        assert.equal((await page.locator('dialog').innerText()).includes('LUME-TEST-ONLY'),false);
        await page.evaluate(provider=>reply(provider,{phase:'scanned'}),provider);
        assert.match(await page.locator('[data-provider-message]').innerText(),/已扫码/);
        const revision=await page.evaluate(()=>providerState.revision);
        await page.evaluate(({provider,revision})=>{
          const accounts=structuredClone(providerState.accounts);
          accounts.find(account=>account.provider===provider).phase='waiting';
          harness.receive({version:1,type:'music-provider-account-state',revision:revision-1,accounts});
        },{provider,revision});
        assert.match(await page.locator('[data-provider-message]').innerText(),/已扫码/);
        if(provider==='qq') {
          await page.screenshot({path:path.join(evidence,`qr-fixture-${width}.png`)});
          const rect=await page.locator('dialog').evaluate(node=>node.getBoundingClientRect().toJSON());
          assert.ok(rect.x>=0 && rect.right<=width && rect.y>=0 && rect.bottom<=height);
        }
        await page.locator('[data-provider-select="kugou"]').focus();
        await page.keyboard.press('Tab');
        assert.equal(await page.locator('[data-provider-close]').evaluate(node=>node===document.activeElement),true);
        await page.keyboard.press('Escape');
        assert.equal(await page.locator('dialog').count(),0);
        assert.equal(await opener.evaluate(node=>node===document.activeElement),true);
        assert.equal(await page.evaluate(()=>providerCommands().at(-1).action),'cancel');
        await page.evaluate(provider=>reply(provider,{status:'signed-in',userId:'late-fixture',displayName:'Stale'}),provider);
        assert.equal(await page.locator('dialog').count(),0,'late success cannot reopen a closed modal');
      }
      assert.equal(requests.slice(networkStart).some(value=>!value.startsWith(new URL(url).origin+'/')),false);
      assert.equal(page.url(),url);assert.equal(popups.length,0);
      // Platform switching cancels the previous operation before beginning the next.
      await page.locator('.MusicProviderButtons [data-music-provider-open="netease"]').click();
      await page.locator('[data-provider-select="qq"]').click();
      assert.deepEqual(await page.evaluate(()=>providerCommands().slice(-2).map(value=>[value.action,value.provider])),
        [['cancel','netease'],['begin','qq']]);
      await page.evaluate(()=>harness.receive({version:1,type:'visibility',visible:false}));
      assert.equal(await page.locator('dialog').count(),0);
      await page.evaluate(()=>harness.receive({version:1,type:'visibility',visible:true}));
      await page.locator('.MusicProviderButtons [data-music-provider-open="kugou"]').click();
      await page.evaluate(()=>{
        Object.defineProperty(document,'hidden',{configurable:true,value:true});
        document.dispatchEvent(new Event('visibilitychange'));
      });
      assert.equal(await page.locator('dialog').count(),0);
      await page.evaluate(()=>{
        delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));
      });
      await page.locator('.MusicProviderButtons [data-music-provider-open="netease"]').click();
      await page.evaluate(()=>document.querySelector('[data-titlebar-music]').click());
      assert.equal(await page.locator('dialog').count(),0);
      assert.equal(await page.locator('[data-music-root]').count(),0);
      assert.equal(await page.evaluate(()=>providerCommands().at(-1).action),'cancel');
      await page.locator('[data-titlebar-music]').click();
      assert.equal(await page.evaluate(()=>providerCommands().filter(value=>value.action==='snapshot').length),2);
      await page.evaluate(()=>{
        providerState.accounts=providerState.accounts.map(account=>({...account,status:'signed-out',
          userId:'',displayName:'',phase:'',qrImage:'',qrText:'',error:''}));
        reply('netease',{});
      });
      await page.locator('.MusicProviderButtons [data-music-provider-open="qq"]').click();
      await page.evaluate(()=>{
        const request=providerCommands().at(-1).request;
        reply('qq',{request,status:'authorizing',phase:'waiting',qrText:'LUME-TEST-ONLY'});
      });
      await page.locator('.MusicProviderQr img').waitFor({state:'visible'});
      await page.evaluate(()=>reply('qq',{status:'expired',error:'expired',qrText:''}));
      await page.locator('[data-provider-retry]').click();
      await page.evaluate(()=>{
        const request=providerCommands().at(-1).request;
        reply('qq',{request,status:'signed-in',error:'',phase:'',userId:'10001',displayName:'Synthetic account'});
      });
      assert.equal(await page.locator('dialog').count(),0,'successful login dismisses its QR dialog');
      assert.equal(await page.evaluate(()=>providerCommands().at(-1).action),'begin',
        'successful login does not send cancel');
      assert.deepEqual(await page.evaluate(()=>{const {provider,action}=onlineCommands().at(-1);return [provider,action];}),
        ['qq','home'],'home login selects the same active online platform');
      await page.evaluate(()=>replyOnline());
      await page.evaluate(async()=>{
        const {initialMusicState}=await import('./music.js');
        const {query,nav,lyrics,...native}=initialMusicState();
        const track={id:'qr-local-fixture',title:'Local fixture',artist:'Fixture',album:'',
          artwork:'',source:'C:/fixture.mp3',duration:180};
        window.musicFixture={...native,version:1,type:'music-state',session:4,revision:1,pageOpen:true,pageVisible:true,
          current:track,queue:[track],status:'playing',playing:true,timeline:{position:10,duration:180},
          capabilities:{canToggle:true,canSeek:true,canNext:false,canPrevious:false}};
        harness.receive(musicFixture);
        harness.receive({version:1,type:'music-lyrics',session:4,trackId:track.id,
          lines:[{startTime:0,endTime:180,fullText:'Synthetic lyric fixture',words:[],translation:''}]});
      });
      await page.locator('[data-music-action="open-player"]').click();
      await page.frameLocator('.MusicReferenceStage').locator('#reference-stage[data-reference-state="ready"]').waitFor();
      const frame=page.frames().find(value=>value.url().endsWith('#classic'));
      await frame.evaluate(()=>window.authContinuity='retained');
      await page.locator('[data-music-action="lyrics"]').click();
      await page.locator('[data-music-panel-tab="account"]').click();
      await page.evaluate(()=>{
        window.stageNode=document.querySelector('[data-music-stage]');
        window.progressNode=document.querySelector('[data-music-progress]');
        window.authStart=harness.commands.length;
      });
      const frameRequests=requests.filter(value=>value.includes('/reference/index.html')).length;
      const accountPanel=page.locator('.MusicProviderAccounts');
      assert.equal(await page.locator('[data-music-provider-active]').count(),1);
      assert.equal(await accountPanel.locator('.MusicProviderSwitchers, .MusicProviderSwitcher, .MusicProviderAccountConnect').count(),0);
      assert.equal(await accountPanel.locator('[data-music-provider-select], [data-music-provider-open]').count(),0);
      const profileAccepted=await page.evaluate(async profile=>{
        const {validMusicProviderAccountMessage}=await import('./music-provider-account.js');
        reply('qq',profile);
        return validMusicProviderAccountMessage({version:1,type:'music-provider-account-state',
          revision:providerState.revision,accounts:structuredClone(providerState.accounts)});
      },qqProfile);
      assert.equal(profileAccepted,true,'the browser bridge must accept the C++ kind/level/epoch expiresAt profile');
      const accountRow=page.locator('[data-music-provider-active]');
      assert.equal(await accountRow.getAttribute('data-provider'),'qq');
      assert.equal(await accountRow.locator('[data-music-provider-active-name]').innerText(),'Synthetic account');
      assert.deepEqual(await accountRow.locator('img[src]').evaluateAll(nodes=>nodes.map(node=>node.getAttribute('src')).sort()),
        [avatarUrl,vipIconUrl,svipIconUrl].sort(),'avatar and both active/inactive memberships use real image elements');
      await page.waitForFunction(()=>[...document.querySelectorAll('[data-music-provider-active] img[src]')]
        .every(node=>node.complete&&node.naturalWidth>0));
      const avatar=accountRow.locator('[data-music-provider-avatar]');
      assert.deepEqual(await avatar.evaluate(node=>{
        const rect=node.getBoundingClientRect();return {width:rect.width,height:rect.height};
      }),{width:40,height:40});
      const logout=accountRow.locator('[data-music-provider-logout]');
      assert.equal(await logout.isVisible(),true);
      assert.ok(await logout.locator('svg').count()>0,'logout stays an icon button');
      const sync=accountPanel.locator('[data-music-provider-refresh]');
      assert.equal(await sync.isVisible(),true);
      assert.match(await sync.innerText(),/同步数据/);
      assert.equal(await sync.evaluate(node=>node.closest('[data-music-provider-active]')===null),true,
        'data sync is separate from the account header');
      const layout=await accountPanel.evaluate(node=>{
        const row=node.querySelector('[data-music-provider-active]').getBoundingClientRect();
        const sync=node.querySelector('[data-music-provider-refresh]').getBoundingClientRect();
        return {left:row.left,right:row.right,syncTop:sync.top,rowBottom:row.bottom};
      });
      assert.ok(layout.left>=0&&layout.right<=width&&layout.syncTop>=layout.rowBottom,JSON.stringify(layout));
      for (const field of ['avatarUrl','iconUrl']) {
        for (const source of ['javascript:alert(1)','file:///C:/fixture.png','data:image/svg+xml;base64,PHN2Zz4=',
          'https://user:secret@q1.qlogo.cn/fixture.png']) {
          await page.evaluate(({field,source})=>{
            const accounts=structuredClone(providerState.accounts),profile=accounts.find(account=>account.provider==='qq');
            if(field==='avatarUrl')profile.avatarUrl=source;else profile.memberships[0].iconUrl=source;
            harness.receive({version:1,type:'music-provider-account-state',revision:++providerState.revision,accounts});
          },{field,source});
          assert.deepEqual(await accountRow.locator('img[src]').evaluateAll(nodes=>nodes.map(node=>node.getAttribute('src')).sort()),
            [avatarUrl,vipIconUrl,svipIconUrl].sort(),`rejected ${field} never replaces safe images`);
        }
      }
      await page.locator('.MusicPlayerPanel').screenshot({path:path.join(evidence,`account-panel-${width}.png`)});
      assert.equal(await page.locator('.MusicProviderAccounts [data-music-provider-active]').count(),1);
      assert.equal(await frame.evaluate(()=>window.authContinuity),'retained');
      assert.equal(requests.filter(value=>value.includes('/reference/index.html')).length,frameRequests);
      assert.equal(await page.evaluate(()=>stageNode===document.querySelector('[data-music-stage]')&&
        progressNode===document.querySelector('[data-music-progress]')),true);
      assert.equal(await page.evaluate(()=>harness.commands.slice(authStart).some(value=>value?.scope==='music')),false);
      const readsBeforeSync=await page.evaluate(()=>onlineCommands().filter(value=>value.action==='home').length);
      await sync.click();
      assert.deepEqual(await page.evaluate(()=>{const {action,provider}=providerCommands().at(-1);return [action,provider];}),['refresh','qq']);
      assert.equal(await page.evaluate(()=>onlineCommands().filter(value=>value.action==='home').length),readsBeforeSync,
        'online data refresh waits for the profile reply');
      await page.evaluate(()=>reply('qq',{request:providerCommands().at(-1).request,status:'signed-in'}));
      assert.equal(await page.evaluate(()=>onlineCommands().filter(value=>value.action==='home').length),readsBeforeSync+1,
        'a successful profile refresh reloads the online list once');
      await page.evaluate(()=>replyOnline());
      assert.equal(await frame.evaluate(()=>window.authContinuity),'retained');
      assert.equal(await page.evaluate(()=>stageNode===document.querySelector('[data-music-stage]')&&
        progressNode===document.querySelector('[data-music-progress]')),true);
      await logout.click();
      assert.deepEqual(await page.evaluate(()=>{const {action,provider}=providerCommands().at(-1);return [action,provider];}),['logout','qq']);
      await page.evaluate(()=>reply('qq',{request:providerCommands().at(-1).request,status:'signed-out',
        userId:'',displayName:'',avatarUrl:'',memberships:[]}));
      assert.equal(await accountPanel.locator('[data-music-provider-active]').isVisible(),false);
      assert.equal(await sync.isVisible(),false);
      assert.match(await accountPanel.innerText(),/游客模式/);
      const commandCount=await page.evaluate(()=>providerCommands().length);
      await accountPanel.locator('[data-music-action="account-home"]').click();
      assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'home');
      await page.screenshot({path:path.join(evidence,`guest-home-${width}.png`)});
      assert.equal(await page.locator('.MusicProviderButtons').isVisible(),true,
        'returning home after logout keeps the login entry even with a local playback queue');
      assert.equal(await page.locator('dialog').count(),0,'guest entry navigates home instead of opening account-page QR login');
      assert.equal(await page.evaluate(()=>providerCommands().length),commandCount);
      assert.deepEqual(errors,[]);
      await page.close();
}));
}

for(const [width,height] of [[1440,900],[390,844]]) {
test(`QQ audio quality ${width}x${height}: four official VIP options, session commands, dismiss and viewport bounds`,
  browserOptions,()=>withBrowser(async({browser,PNG,evidence,url})=>{
    const page=await browser.newPage({viewport:{width,height}}),errors=[];
    page.setDefaultTimeout(15000);
    page.on('pageerror',error=>errors.push(error.message));
    const image=new PNG({width:40,height:16});
    image.data.fill(255);
    await page.route(value=>[avatarUrl,vipIconUrl,svipIconUrl].includes(value.href),
      route=>route.fulfill({contentType:'image/png',body:PNG.sync.write(image)}));
    await page.addInitScript(()=>{
      const listeners=new Set();
      window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
      window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
        removeEventListener:(_,listener)=>listeners.delete(listener),postMessage:message=>harness.commands.push(message)}};
      window.qualityCommands=()=>harness.commands.filter(value=>value?.scope==='music'&&value.action==='setQuality');
    });
    await page.addInitScript(installMusicPreferencesHarness);
    await page.goto(url);
    await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
    await page.locator('[data-titlebar-music]').click();
    await page.evaluate(async profile=>{
      const accounts=['netease','qq','kugou'].map(provider=>({provider,request:0,
        status:provider==='qq'?'signed-in':'signed-out',phase:'',userId:provider==='qq'?'10001':'',
        displayName:provider==='qq'?'QQ fixture':'',qrImage:'',qrText:'',error:'',
        ...(provider==='qq'?profile:{})}));
      harness.receive({version:1,type:'music-provider-account-state',revision:1,accounts});
      const {initialMusicState}=await import('./music.js');
      const {query,nav,lyrics,...initial}=initialMusicState();
      const current={id:'qq:quality-fixture',title:'QQ quality fixture',artist:'Fixture',album:'',
        artwork:'',duration:180,vipRequired:true,membershipBadge:'qq-green-vip'};
      window.qualityFixture={...initial,version:1,type:'music-state',session:12,revision:1,pageOpen:true,pageVisible:true,
        current,queue:[current],status:'paused',timeline:{position:18,duration:180},
        capabilities:{canPrevious:false,canNext:false,canToggle:true,canSeek:true},
        audio:{requestedQuality:'standard',quality:'high',bitrate:320000}};
      harness.receive(qualityFixture);
    },qqProfile);
    await page.locator('[data-music-action="open-player"]').click();
    const trigger=page.locator('[data-music-quality-trigger]'),menu=page.locator('[data-music-quality-menu]');
    assert.equal(await trigger.isEnabled(),true);
    assert.equal(await trigger.innerText(),'HQ高品质','the label comes from the decoded quality, not the requested quality');
    assert.match(await trigger.getAttribute('title'),/320kbps/);
    await trigger.click();
    assert.equal(await trigger.getAttribute('aria-expanded'),'true');
    assert.deepEqual(await menu.locator('[data-music-quality-option]').evaluateAll(nodes=>
      nodes.map(node=>node.dataset.musicQualityOption)),['hires','lossless','high','standard']);
    assert.deepEqual(await menu.locator('.MusicQualityName').allTextContents(),
      ['臻品音质','SQ无损品质','HQ高品质','标准品质']);
    assert.deepEqual(await menu.locator('img').evaluateAll(nodes=>nodes.map(node=>node.getAttribute('src'))),
      [svipIconUrl,vipIconUrl,vipIconUrl,vipIconUrl],'VIP-required QQ tracks use official images in all four rows');
    await page.waitForFunction(()=>[...document.querySelectorAll('[data-music-quality-menu] img')]
      .every(node=>node.complete&&node.naturalWidth>0));
    const bounds=await menu.evaluate(node=>{
      const rect=value=>value.getBoundingClientRect().toJSON();
      return {menu:rect(node),items:[...node.querySelectorAll('button, img, .MusicQualityName, small')].map(value=>({
        ...rect(value),clientWidth:value.clientWidth,scrollWidth:value.scrollWidth}))};
    });
    assert.ok(bounds.menu.left>=0&&bounds.menu.right<=width&&bounds.menu.top>=0&&bounds.menu.bottom<=height,JSON.stringify(bounds));
    for(const item of bounds.items)assert.ok(item.left>=bounds.menu.left&&item.right<=bounds.menu.right&&
      item.top>=bounds.menu.top&&item.bottom<=bounds.menu.bottom&&item.scrollWidth<=item.clientWidth,JSON.stringify(item));
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,'no horizontal page overflow');
    await page.screenshot({path:path.join(evidence,`qq-quality-${width}.png`)});
    await menu.locator('[data-music-quality-option="hires"]').focus();
    await page.keyboard.press('Escape');
    assert.equal(await menu.isVisible(),false);
    assert.equal(await trigger.getAttribute('aria-expanded'),'false');
    assert.equal(await trigger.evaluate(node=>node===document.activeElement),true);
    assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'player','Escape only closes the quality menu');
    await trigger.click();
    const bar=page.locator('.MusicPlayerBar'),barBounds=await bar.boundingBox();
    await bar.click({position:{x:barBounds.width/2,y:barBounds.height-6}});
    assert.equal(await menu.isVisible(),false,'clicking outside closes the quality menu');
    assert.equal(await page.evaluate(()=>qualityCommands().length),0,'opening and dismissing never sends setQuality');
    for(const [value,label] of [['hires','臻品音质'],['lossless','SQ无损品质'],['high','HQ高品质'],['standard','标准品质']]) {
      await trigger.click();
      assert.equal(await menu.locator('[aria-checked="true"]').count(),1);
      await menu.locator(`[data-music-quality-option="${value}"]`).click();
      assert.deepEqual(await page.evaluate(()=>qualityCommands().at(-1)),
        {version:1,scope:'music',action:'setQuality',value,session:12});
      assert.equal(await menu.isVisible(),false);
      assert.equal(await trigger.evaluate(node=>node===document.activeElement),true);
      await page.evaluate(value=>{
        qualityFixture={...qualityFixture,revision:qualityFixture.revision+1,
          audio:{requestedQuality:value,quality:value,bitrate:value==='standard'?128000:320000}};
        harness.receive(qualityFixture);
      },value);
      assert.equal(await trigger.innerText(),label,'native audio snapshots update the quality label');
    }
    assert.equal(await page.evaluate(()=>qualityCommands().length),4);
    await trigger.click();
    await menu.locator('[data-music-quality-option="standard"]').click();
    assert.equal(await page.evaluate(()=>qualityCommands().length),4,'the requested quality is not sent twice');
    assert.deepEqual(errors,[]);
    await page.close();
}));
}

for(const [width,height] of [[1440,900],[390,844]]) {
test(`provider selection and online scroll capture ${width}x${height}: 120px, unique appended rows and no pagination`,
  browserOptions,()=>withBrowser(async({browser,evidence,url})=>{
    const page=await browser.newPage({viewport:{width,height}}),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.goto(url);
    const selected=await page.evaluate(async()=>{
      const {createMusicProviderAccount,mountMusicProviderAccountView,musicProviderButtons}=await import('./music-provider-account.js');
      const root=document.createElement('main'),selected=[];
      root.innerHTML=musicProviderButtons();
      document.body.append(root);
      const controller=createMusicProviderAccount({send:()=>true});
      const view=mountMusicProviderAccountView(root,controller,{onSelect:provider=>selected.push(provider)});
      root.querySelector('[data-music-provider-open="qq"]').click();
      document.querySelector('[data-provider-select="netease"]').click();
      view.dispose();controller.dispose();root.remove();
      return selected;
    });
    assert.deepEqual(selected,['qq','netease'],'home buttons and modal tabs both notify active-provider selection');
    await page.evaluate(async()=>{
      const {createMusicOnline}=await import('./music-online.js');
      const {mountMusicView}=await import('./music-view.js');
      const {initialMusicState}=await import('./music.js');
      const root=document.createElement('main');
      root.style.cssText='position:fixed;inset:0;display:flex;overflow:hidden';
      document.body.replaceChildren(root);
      const commands=[],updates=[];
      let view;
      const online=createMusicOnline({now:()=>10,send:(action,fields)=>{commands.push({action,...fields});return true;},
        onChange:value=>view?.updateLibrary(value)});
      const track=id=>({id,title:id,artist:'Fixture',album:'',source:`online:${id}`,artwork:'',duration:180});
      const reply=(ids,more)=>{
        const {request,action,provider}=commands.at(-1);
        return online.receive({version:1,type:'music-online-state',request,action,provider,status:'ready',error:'',
          data:{tracks:ids.map(track),playlists:[{id:'list-fixture',providerId:provider,type:'playlist',
            title:'Fixture playlist',artwork:'',description:'',owned:true}],recommendations:[],more,
            likedIds:[...Array.from({length:32},(_,index)=>`netease:row-${index}`),'netease:extra-a','netease:extra-b']}});
      };
      online.accounts({accounts:[{provider:'netease',status:'signed-in',userId:'fixture'}]});
      online.setVisible(true);
      reply([],false);
      online.request('favorites');
      const song=track('netease:playing');
      view=mountMusicView(root,{online,library:online.snapshot(),
        snapshot:{...initialMusicState(),pageVisible:true,current:song,queue:[song],playing:true,
          timeline:{position:10,duration:180}},viewState:{screen:'home',nav:'favorites'},
        createBackground:()=>({update(){},dispose(){}}),createEffects:()=>({update(){},dispose(){}}),
        createVisualizer:()=>({update:value=>updates.push(value),dispose(){}})});
      window.appendFixture={online,view,commands,updates,reply};
      reply(Array.from({length:32},(_,index)=>`netease:row-${index}`),true);
    });
    const scroll=async(selector,distance)=>page.locator(selector).evaluate((node,distance)=>{
      node.scrollTop=node.scrollHeight-node.clientHeight-distance;
      node.dispatchEvent(new Event('scroll',{bubbles:false}));
      return {height:node.clientHeight,overflow:node.scrollHeight-node.clientHeight};
    },distance);
    const checkAppend=async(selector,rowSelector)=>{
      const count=await page.evaluate(()=>appendFixture.commands.length);
      const geometry=await scroll(selector,121);
      assert.ok(geometry.height>0&&geometry.overflow>121,JSON.stringify(geometry));
      assert.equal(await page.evaluate(()=>appendFixture.commands.length),count,'121px remaining does not load');
      await scroll(selector,120);
      assert.equal(await page.evaluate(()=>appendFixture.commands.length),count+1,'120px remaining loads the next batch');
      assert.equal(await page.evaluate(()=>appendFixture.commands.at(-1).offset),50);
      assert.equal(await page.locator(rowSelector).count(),32,'loading keeps the existing rows');
      await scroll(selector,0);
      assert.equal(await page.evaluate(()=>appendFixture.commands.length),count+1,'loading prevents repeated scroll requests');
      assert.equal(await page.evaluate(()=>appendFixture.reply(['netease:row-31','netease:extra-a','netease:extra-b'],false)),true);
      const ids=await page.locator(rowSelector).evaluateAll(nodes=>nodes.map(node=>node.dataset.musicTrackId));
      assert.equal(ids.length,34);
      assert.equal(new Set(ids).size,34,'the overlapping batch is deduplicated');
      await scroll(selector,0);
      assert.equal(await page.evaluate(()=>appendFixture.commands.length),count+1,'no request after the final batch');
      assert.equal(await page.locator('.MusicPagination, [data-music-action="online-more"], [data-music-action="online-previous"]').count(),0);
    };
    await checkAppend('.MusicContent','.MusicContent .MusicTrackRow');
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.screenshot({path:path.join(evidence,`online-content-${width}.png`)});
    await page.locator('[data-music-action="open-player"]').click();
    await page.locator('.MusicPlayerActions [data-music-action="queue"]').click();
    await page.locator('[data-music-rail-tab="playlists"]').click();
    await page.locator('[data-music-rail-playlist]').click();
    assert.equal(await page.evaluate(()=>appendFixture.commands.at(-1).action),'playlist');
    await page.evaluate(()=>appendFixture.reply(Array.from({length:32},(_,index)=>`netease:row-${index}`),true));
    await page.evaluate(()=>{
      window.appendStage=document.querySelector('[data-music-stage]');
      window.appendProgress=document.querySelector('[data-music-progress]');
      window.appendClockCount=appendFixture.updates.length;
    });
    await checkAppend('[data-music-rail] .MusicPanelBody','[data-music-rail] .MusicTrackRow');
    assert.equal(await page.evaluate(()=>appendStage===document.querySelector('[data-music-stage]')&&
      appendProgress===document.querySelector('[data-music-progress]')),true);
    assert.equal(await page.evaluate(()=>appendFixture.updates.length),await page.evaluate(()=>appendClockCount),
      'appending a collection does not restart the playback clock');
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.screenshot({path:path.join(evidence,`online-scroll-${width}.png`)});
    await page.evaluate(()=>{appendFixture.view.dispose();appendFixture.online.dispose();});
    assert.deepEqual(errors,[]);
    await page.close();
}));
}
