// 实际 index.html + 全量 CSS + app.js 的 Edge 验证；只替换 native bridge，不写真实设置。
// node tests/application-settings-browser.mjs <playwright目录> <build-artifacts输出目录>
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {DIAGNOSTICS_ENABLED} from '../diagnostics.js';

const {chromium}=createRequire(import.meta.url)(path.resolve(process.argv[2]));
const ui=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const output=path.resolve(process.argv[3]);
const artifacts=path.resolve(ui,'../../build-artifacts');
assert.ok(output.startsWith(artifacts+path.sep),'测试产物仅写项目 build-artifacts');
fs.mkdirSync(output,{recursive:true});
function installNativeFixture(initialSettings={}) {
  const listeners=new Set(),commands=[];
  let settings={version:1,type:'app-settings-state',revision:1,language:'zh-CN',balancedMode:true,autostart:false,
    busy:false,error:'',update:{status:'idle',version:'',progress:0},...initialSettings};
  let account={version:1,type:'account-state',revision:1,status:'signed-in',busy:false,error:'',
    profile:{id:'test-user',displayName:'测试用户',avatarUrl:'',vip:true,vipExpiresAt:'2030-01-01T00:00:00Z'}};
  const wallpaper={version:1,type:'wallpaper-state',revision:1,status:'idle',error:'',loadStatus:'fresh',
    playerAvailable:true,catalogStatus:'ready',catalogError:'',likedIds:[],catalog:[],library:[],displays:[],
    download:{id:'',status:'idle',received:0,total:0,error:''},pauseFullscreen:false};
  const receive=message=>listeners.forEach(listener=>listener({data:message}));
  let diagnostic={version:1,type:'diagnostics-state',schemaVersion:1,revision:1,
    available:true,storage:'ready',events:[],health:{},mismatches:[]};
  window.harness={commands,receive,navigate:section=>receive({version:1,type:'navigate',page:'applicationSettings',section}),
    diagnostics(fields={}) {diagnostic={...diagnostic,...fields,revision:diagnostic.revision+1};receive(diagnostic);},
    setAvatar(avatarUrl) {account={...account,revision:account.revision+1,profile:{...account.profile,avatarUrl}};receive(account);},
    settingsSnapshot(fields={}) {settings={...settings,...fields,revision:settings.revision+1};receive(settings);},
    accountSnapshot(fields={}) {account={...account,...fields,revision:account.revision+1};receive(account);}};
  window.chrome={webview:{
    addEventListener(type,listener) {if(type==='message')listeners.add(listener);},
    removeEventListener(type,listener) {if(type==='message')listeners.delete(listener);},
    postMessage(command) {
      commands.push(command);
      if(command==='v1|ready')queueMicrotask(()=>{receive(settings);receive(account);receive(wallpaper);});
      if(command?.scope==='app-settings' && command.action==='snapshot')queueMicrotask(()=>receive(settings));
      if(command?.scope==='account' && command.action==='snapshot')queueMicrotask(()=>receive(account));
      if(command?.scope==='diagnostics') {
        if(command.action==='clear')diagnostic={...diagnostic,revision:diagnostic.revision+1,events:[],health:{},mismatches:[]};
        queueMicrotask(()=>receive(diagnostic));
      }
    },
  }};
}
const server=http.createServer((req,res)=>{
  const route=new URL(req.url,'http://localhost').pathname;
  const file=path.resolve(ui,'.'+(route==='/'?'/index.html':route));
  if(!file.startsWith(ui+path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {res.writeHead(404).end();return;}
  res.setHeader('Content-Type',({'.css':'text/css','.js':'text/javascript','.html':'text/html;charset=utf-8',
    '.webp':'image/webp','.ttf':'font/ttf','.otf':'font/otf'})[path.extname(file)] || 'application/octet-stream');
  res.end(fs.readFileSync(file));
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
const results=[],errors=[],remoteRequests=[];
try {
  browser=await chromium.launch({headless:true,channel:'msedge'});
  for(const viewport of [{width:1536,height:864},{width:1280,height:720},{width:960,height:540},{width:800,height:600},{width:680,height:864}]) {
    const page=await browser.newPage({viewport});
    page.setDefaultTimeout(10000);
    page.on('pageerror',e=>errors.push(e.message));
    page.on('request',request=>{if(!request.url().startsWith('http://127.0.0.1:')&&!request.url().startsWith('data:'))remoteRequests.push(request.url());});
    await page.addInitScript(installNativeFixture);
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
    await page.evaluate(()=>{
      window.accountMenuEvents=[];
      for(const type of ['pointerdown','mousedown','focusout','focusin','mouseup','click'])
        document.addEventListener(type,event=>{
          if(!event.target.closest?.('#titlebar'))return;
          const record=()=>accountMenuEvents.push({type,target:event.target.dataset.accountMenuAction ||
            event.target.className,related:event.relatedTarget?.className,active:document.activeElement?.className,
            menuHidden:document.querySelector('[data-lume-account-menu]').hidden});
          record();
          if(type==='focusout')queueMicrotask(record);
        },true);
    });
    await page.locator('.lume-titlebar__login').click();
    await page.locator('[data-account-menu-action="logout"]').click();
    const firstLogoutCommands=await page.evaluate(()=>harness.commands.filter(command=>
      command?.scope==='account' && command.action==='logout'));
    assert.equal(firstLogoutCommands.length,1,`真实鼠标点击顶部退出登录必须发送一次账号退出命令：${
      JSON.stringify(await page.evaluate(()=>accountMenuEvents))}`);
    assert.equal(await page.locator('[data-lume-account-label]').textContent(),'测试用户','发送退出命令不等于退出成功');
    await page.evaluate(()=>harness.accountSnapshot({error:'storage-failed'}));
    assert.equal(await page.locator('[data-lume-account-label]').textContent(),'测试用户','退出失败保留已确认用户资料');
    await page.locator('.lume-titlebar__login').click();
    await page.locator('[data-account-menu-action="logout"]').click();
    assert.equal(await page.evaluate(()=>harness.commands.filter(command=>
      command?.scope==='account' && command.action==='logout').length),2,'退出失败后可以再次点击重试');
    await page.evaluate(()=>harness.accountSnapshot({status:'signed-out',profile:null,error:'local-logout'}));
    assert.equal(await page.locator('[data-lume-account-label]').textContent(),'登录');
    await page.evaluate(()=>harness.accountSnapshot({status:'signed-in',error:'',
      profile:{id:'test-user',displayName:'测试用户',avatarUrl:'',vip:true,vipExpiresAt:'2030-01-01T00:00:00Z'}}));
    assert.deepEqual(await page.locator('link[rel="stylesheet"]').evaluateAll(links=>links.map(link=>link.getAttribute('href'))),
      ['reference.css','titlebar.css','app.css','clock.css','calendar.css','radial.css','application-settings.css','music.css']);
    await page.locator('[data-titlebar-settings]').click();
    for(const section of ['general','user','autostart',...(DIAGNOSTICS_ENABLED?['diagnostics']:[]),'about']) {
      await page.locator(`button[data-settings-section="${section}"]`).click();
      const geometry=await page.evaluate(section=>{
        const box=selector=>{const r=document.querySelector(selector).getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height};};
        const panel=document.querySelector(`[data-settings-panel="${section}"]`);
        return {nav:box('.application-settings__nav'),titlebar:box('[data-lume-shared-titlebar]'),
          panel:box(`[data-settings-panel="${section}"]`),horizontalOverflow:panel.scrollWidth>panel.clientWidth,
          avatar:section==='user'?box('.application-account__avatar-frame'):null};
      },section);
      assert.equal(geometry.nav.width,viewport.width<=800?190:220);
      assert.equal(geometry.titlebar.height,35);
      assert.equal(geometry.horizontalOverflow,false,`${viewport.width}/${section} 横向溢出`);
      if(section==='user') {
        assert.equal(geometry.avatar.width,88);assert.equal(geometry.avatar.height,88);
        await page.screenshot({path:path.join(output,`${viewport.width}-account-avatar.png`),
          clip:{x:geometry.avatar.x-10,y:geometry.avatar.y-10,width:116,height:116}});
        assert.ok(geometry.avatar.x-5>=geometry.panel.x,
          `${viewport.width}：头像外侧 5px VIP 圆框不能被左侧滚动边界裁切`);
        await page.waitForFunction(()=>[...document.querySelectorAll('.application-account__avatar-badge img,.lume-titlebar__vip-badge img')].every(img=>img.complete&&img.naturalWidth===48));
        const avatarGeometry=await page.locator('[data-lume-avatar-fallback] svg').boundingBox();
        assert.equal(avatarGeometry.width,18);assert.equal(avatarGeometry.height,18);
        assert.equal(await page.locator('[data-lume-avatar-fallback]').innerHTML(),
          await page.locator('[data-account-avatar-fallback]').innerHTML(),'顶部和设置页共用 Lume 图标');
        await page.locator('.lume-titlebar__login').click();
        await page.screenshot({path:path.join(output,`${viewport.width}-account-menu.png`)});
        await page.keyboard.press('Escape');
      }
      if(section==='diagnostics') {
        assert.equal(await page.locator('[data-diagnostic-field="summary"]').isVisible(),true);
        assert.match(await page.locator('[data-diagnostic-field="summary"]').getAttribute('placeholder'),/现象|context/i);
        await page.locator('[data-diagnostic-field="summary"]').fill('测试问题');
        assert.match(await page.locator('[data-diagnostic-events]').textContent(),/0|recorded|记录/);
        await page.waitForFunction(()=>document.querySelector('[data-diagnostic-native]').textContent.includes('正常记录中'));
        await page.evaluate(()=>harness.diagnostics({
          events:[{at:1700000000000,firstAt:1700000000000,repeatCount:1,module:'dock',kind:'effect-mismatch',
            error:'dock-layers-hidden',expected:'layers-visible',actual:'hidden'}],
          health:{dock:{enabled:true,running:true,layersVisible:false}},
          mismatches:[{component:'dock',expected:'layers-visible',actual:'hidden',confidence:'confirmed',reason:'dock-layers-hidden'}],
        }));
        assert.match(await page.locator('[data-diagnostic-evidence]').textContent(),/Dock.*layers-visible.*hidden/);
        assert.match(await page.locator('#notice').textContent(),/Dock/);
        await page.evaluate(()=>{document.querySelector('#notice').textContent='unchanged';harness.diagnostics();});
        assert.equal(await page.locator('#notice').textContent(),'unchanged','重复快照不得重复提示');
        await page.evaluate(()=>harness.diagnostics({mismatches:[],health:{dock:{enabled:true,running:true,layersVisible:true}}}));
        assert.equal(await page.locator('[data-diagnostic-mismatches]').textContent(),'','恢复后清除当前不一致，历史保留');
        page.once('dialog',dialog=>dialog.accept());
        await page.locator('[data-diagnostic-action="clear"]').click();
        await page.waitForFunction(()=>document.querySelector('[data-settings-message]').textContent.includes('已清除'));
        assert.match(await page.locator('[data-diagnostic-events]').textContent(),/0/);
        assert.equal(await page.locator('[data-diagnostic-field="summary"]').inputValue(),'');
      }
      results.push({viewport,section,...geometry});
      if(viewport.width===1536 || viewport.width===960 || viewport.width===680)
        await page.screenshot({path:path.join(output,`${viewport.width}-${section}.png`)});
    }
    if(!DIAGNOSTICS_ENABLED) {
      assert.equal(await page.locator('[data-settings-section="diagnostics"]').count(),0);
      await page.evaluate(()=>{harness.navigate('diagnostics');document.querySelector('#notice').textContent='unchanged';
        harness.diagnostics({events:[{at:1700000000000,module:'dock',kind:'error',error:'test-fault'}],
          mismatches:[{component:'dock',expected:'visible',actual:'hidden',confidence:'confirmed',reason:'test'}]});});
      assert.equal(await page.locator('[data-settings-panel="diagnostics"]').isVisible(),false);
      assert.equal(await page.locator('[data-settings-panel="general"]').isVisible(),true);
      assert.equal(await page.locator('#notice').textContent(),'unchanged','disabled diagnostics do not show notices');
      const recorded=await page.evaluate(()=>Object.keys(localStorage).filter(key=>key.startsWith('lume-diagnostic')));
      assert.deepEqual(recorded,[],'disabled diagnostics do not persist runtime data');
    }
    await page.evaluate(avatarUrl=>harness.setAvatar(avatarUrl),
      'data:image/webp;base64,'+fs.readFileSync(path.join(ui,'assets/black-texture.webp')).toString('base64'));
    await page.waitForFunction(()=>{const avatar=document.querySelector('.lume-titlebar__avatar');
      return !avatar.hidden && avatar.complete && avatar.naturalWidth>0;});
    assert.equal(await page.locator('[data-lume-avatar-fallback]').isVisible(),false);
    await page.evaluate(()=>harness.setAvatar('data:image/png;base64,YWJj'));
    await page.locator('[data-lume-avatar-fallback]').waitFor({state:'visible'});
    assert.equal(await page.locator('.lume-titlebar__avatar').getAttribute('src'),null);
    await page.evaluate(()=>harness.setAvatar(''));
    await page.evaluate(()=>harness.navigate('user'));
    const field=page.locator('[data-account-name-input]');
    await field.fill('保留草稿😀');
    await field.evaluate(e=>{e.focus();e.setSelectionRange(2,4);});
    await page.evaluate(()=>harness.accountSnapshot({error:'network-unavailable'}));
    assert.deepEqual(await field.evaluate(e=>({value:e.value,start:e.selectionStart,end:e.selectionEnd,focused:document.activeElement===e})),
      {value:'保留草稿😀',start:2,end:4,focused:true});
    const scroll=await page.locator('[data-settings-panel="user"]').evaluate(e=>{e.scrollTop=160;e.dispatchEvent(new Event('scroll'));return e.scrollTop;});
    await page.locator('[data-settings-back]').click();
    await page.evaluate(()=>harness.navigate('user'));
    assert.equal(await field.inputValue(),'保留草稿😀');
    assert.equal(await page.locator('[data-settings-panel="user"]').evaluate(e=>e.scrollTop),scroll);
    await page.evaluate(()=>{harness.navigate('general');harness.settingsSnapshot({language:'en'});});
    assert.equal(await page.locator('[data-titlebar-settings]').getAttribute('aria-label'),'Settings');
    await page.locator('.lume-titlebar__login').click();
    await page.keyboard.press('End');
    assert.equal(await page.locator('[data-account-menu-action="logout"]').evaluate(e=>document.activeElement===e),true);
    await page.keyboard.press('Escape');
    assert.equal(await page.locator('.lume-titlebar__login').evaluate(e=>document.activeElement===e),true);
    await page.locator('.lume-titlebar__login').click();
    await page.locator('.application-settings__title-row h1').click();
    assert.equal(await page.locator('[data-lume-account-menu]').isVisible(),false);
    await page.evaluate(()=>harness.navigate('user'));
    await page.locator('[data-account-action="logout"]').click();
    assert.equal(await page.locator('.lume-titlebar__login').getAttribute('aria-disabled'),'true',
      '设置页请求发出后，顶部立即同步等待状态');
    assert.doesNotMatch(await page.locator('.lume-titlebar__login').getAttribute('title'),/disconnected|宿主未连接/);
    await page.evaluate(()=>harness.accountSnapshot({error:'storage-failed'}));
    assert.equal(await page.locator('.lume-titlebar__login').getAttribute('aria-disabled'),'false');
    assert.equal(await page.locator('[data-account-action="logout"]').getAttribute('aria-disabled'),'false');
    await page.evaluate(()=>harness.receive({version:1,type:'window',fullscreen:true}));
    await field.click();
    await page.mouse.move(viewport.width-20,viewport.height-20);
    await page.waitForFunction(()=>document.querySelector('[data-lume-shared-titlebar]').dataset.visible==='false');
    await page.keyboard.press('F6');
    assert.equal(await page.locator('.lume-titlebar__login').evaluate(e=>document.activeElement===e),true);
    assert.equal(await page.locator('[data-lume-shared-titlebar]').getAttribute('data-visible'),'true');
    await page.keyboard.press('ArrowDown');
    await page.keyboard.press('End');
    const keyboardLogoutCount=await page.evaluate(()=>harness.commands.filter(command=>
      command?.scope==='account' && command.action==='logout').length);
    await page.evaluate(()=>harness.accountSnapshot({busy:true,error:''}));
    assert.equal(await page.locator('[data-account-menu-action="logout"]').evaluate(e=>document.activeElement===e),true,
      '菜单聚焦期间收到忙碌快照，不丢失键盘焦点');
    await page.keyboard.press('Enter');
    assert.equal(await page.evaluate(()=>harness.commands.filter(command=>
      command?.scope==='account' && command.action==='logout').length),keyboardLogoutCount,'忙碌时回车不能重复发送');
    await page.evaluate(()=>harness.accountSnapshot({busy:false,error:'network-unavailable'}));
    await page.keyboard.press('Enter');
    assert.equal(await page.evaluate(()=>harness.commands.filter(command=>
      command?.scope==='account' && command.action==='logout').length),keyboardLogoutCount+1,'失败恢复后回车可退出');
    await page.evaluate(()=>harness.accountSnapshot({error:'storage-failed'}));
    await page.keyboard.press('Space');
    await page.keyboard.press('End');
    await page.keyboard.press('Space');
    assert.equal(await page.evaluate(()=>harness.commands.filter(command=>
      command?.scope==='account' && command.action==='logout').length),keyboardLogoutCount+2,'空格可打开菜单并退出');
    await page.evaluate(()=>harness.accountSnapshot({error:''}));
    await page.keyboard.press('ArrowDown');
    await page.keyboard.press('Tab');
    assert.equal(await page.locator('[data-lume-account-menu]').isVisible(),false);
    assert.equal(await page.locator('[data-titlebar-settings]').evaluate(e=>document.activeElement===e),true,
      'Tab 关闭菜单并自然前进到设置按钮');
    await page.keyboard.press('Shift+Tab');
    await page.keyboard.press('ArrowUp');
    await page.keyboard.press('Shift+Tab');
    assert.equal(await page.locator('[data-lume-account-menu]').isVisible(),false);
    await page.waitForFunction(()=>document.querySelector('[data-lume-shared-titlebar]').dataset.visible==='false');
    await page.evaluate(()=>harness.receive({version:1,type:'window',fullscreen:false}));
    await page.evaluate(()=>{
      harness.accountSnapshot({status:'signed-out',profile:null});
      harness.settingsSnapshot({language:'en',update:{status:'incompatible',version:'2.0.0',progress:0}});
      harness.navigate('about');
    });
    assert.equal(await page.locator('[data-settings-action="installUpdate"]').isVisible(),false);
    assert.equal(await page.locator('[data-settings-action="downloadUpdate"]').isVisible(),false);
    assert.equal(await page.locator('[data-lume-account-label]').textContent(),'Sign in');
    await page.screenshot({path:path.join(output,`${viewport.width}-en-incompatible.png`)});
    await page.close();
  }
  assert.deepEqual(errors,[]);
  assert.deepEqual(remoteRequests,[],'UI 不得直接访问 HTTP 账号服务');
  const restored=await browser.newPage({viewport:{width:960,height:540}});
  restored.on('pageerror',e=>errors.push(e.message));
  await restored.addInitScript(installNativeFixture,{language:'en',balancedMode:false,error:'balanced-runtime'});
  await restored.goto(`http://127.0.0.1:${server.address().port}/`);
  await restored.waitForFunction(()=>document.documentElement.lang==='en');
  assert.equal(await restored.locator('[data-view="home"]').textContent(),'Home');
  await restored.locator('[data-titlebar-settings]').click();
  assert.equal(await restored.locator('[data-settings-heading]').textContent(),'General');
  assert.equal(await restored.locator('[data-setting="balancedMode"]').isChecked(),false);
  assert.match(await restored.locator('[data-settings-message]').textContent(),/wallpaper.*policy/i);
  await restored.screenshot({path:path.join(output,'960-en-restored-error.png')});
  await restored.close();
  assert.deepEqual(errors,[]);
  fs.writeFileSync(path.join(output,'layout.json'),JSON.stringify({entry:'index.html + app.js',results,errors,remoteRequests},null,2));
  console.log(`实际 app.js Edge 验证通过：${results.length} 组四栏几何，完整 index/CSP/6 个样式表，5 个视口，中英切换，VIP 位图、草稿/焦点/滚动、返回、键盘菜单、注销及不兼容更新。`);
} finally {
  await browser?.close();
  await new Promise(resolve=>server.close(resolve));
}
