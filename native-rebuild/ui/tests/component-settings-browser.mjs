import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {setTimeout as sleep} from 'node:timers/promises';
import {initialState} from '../state.js';
import {validHostMessage} from '../bridge.js';

export const viewports=[
  {width:1536,height:864},{width:1280,height:720},{width:960,height:540},
  {width:800,height:600},{width:680,height:864},
];
export const components=[
  {scope:'clock',controls:['clock-style']},
  {scope:'calendar',controls:['calendar-theme','calendar-material','calendar-textColor','calendar-highlightColor']},
  {scope:'weather',controls:['weather-material','weather-city']},
  {scope:'almanac',controls:['almanac-material']},
  {scope:'performance',controls:['performance-material']},
  {scope:'organizer',controls:['organizer-material']},
  {scope:'icons',controls:['icons-autoHide','icons-delay','icons-restoreOnInput']},
  {scope:'taskbar',controls:['taskbar-transparent','taskbar-autoHide']},
  {scope:'minimize',controls:['minimize-durationMs','minimize-restore','minimize-launch',
    'minimize-multiMonitor','minimize-unhideTaskbar','minimize-unhideDurationMs','minimize-engine']},
];
const stylesheets=['reference.css','titlebar.css','app.css','clock.css','calendar.css','application-settings.css','music.css'];
const ui=path.resolve(fileURLToPath(new URL('../',import.meta.url)));
const action=scope=>scope==='clock'?'toggle':`${scope}-toggle`;
const reset=scope=>scope==='clock'?'reset':`${scope}-reset`;
const controlSelector=key=>`[id="${key}"],[data-action="${key}"]`;
const masterSelectors=components.map(({scope})=>`[data-action="${action(scope)}"]`).join(',');

function componentMessage(scope,enabled,{error='',applied=false}={}) {
  const preferences={...initialState()[scope],enabled};
  if(scope!=='clock')preferences.running=enabled&&!error;
  const common={version:1,error,loadStatus:'loaded',result:applied?'applied':'snapshot',saved:applied&&!error};
  if(scope==='clock')return {version:1,type:'settings',clock:preferences,error,saved:!error};
  if(scope==='calendar')return {...common,type:'calendar-settings',calendar:preferences,materialStatus:'inactive'};
  if(scope==='taskbar')return {version:1,type:'taskbar-state',...preferences,error,loadStatus:'loaded',revision:0};
  if(scope==='icons')return {...common,type:'icons-state',...preferences,desktops:preferences.running?1:0};
  if(scope==='minimize')return {...common,type:'minimize-state',...preferences,
    x64:preferences.running,x86:false,hosts:preferences.running?1:0,hooks:preferences.running?2:0,
    entries:0,pendingCleanup:0};
  if(scope==='organizer')preferences.collections=preferences.collections.map(item=>
    ({...item,enabled,running:preferences.running}));
  const weather=scope==='weather'?{loading:false,weatherError:'',cacheWriteFailed:false,hasWeather:false,systemLocation:'unknown'}:{};
  return {...common,type:`${scope}-state`,...preferences,materialStatus:'inactive',...weather};
}

function nativePayload() {
  const messages=Object.fromEntries(components.map(({scope})=>[scope,Object.fromEntries(
    [false,true].flatMap(enabled=>[
      [`${enabled}-snapshot`,componentMessage(scope,enabled)],
      [`${enabled}-success`,componentMessage(scope,enabled,{applied:true})],
      [`${enabled}-error`,componentMessage(scope,enabled,{applied:true,error:scope==='taskbar'?'core-start':'runtime'})],
    ]),
  )]));
  const initial=[
    {version:1,type:'app-settings-state',revision:1,language:'zh-CN',balancedMode:true,autostart:false,
      busy:false,error:'',update:{status:'idle',version:'',progress:0}},
    {version:1,type:'account-state',revision:1,status:'signed-out',busy:false,error:'',profile:null},
    {version:1,type:'wallpaper-state',revision:1,status:'idle',error:'',loadStatus:'fresh',
      playerAvailable:true,catalogStatus:'ready',catalogError:'',likedIds:[],catalog:[],library:[],displays:[],
      download:{id:'',status:'idle',received:0,total:0,error:''},pauseFullscreen:false},
  ];
  for(const message of [...initial,...Object.values(messages).flatMap(Object.values)])
    assert.equal(validHostMessage(message),true,`模拟宿主消息必须通过真实协议校验：${JSON.stringify(message)}`);
  return {messages,initial};
}

function installNativeFixture({messages,initial}) {
  const listeners=new Set(),commands=[],received=[],unexpected=[],pending=new Map();
  const enabled=Object.fromEntries(Object.keys(messages).map(scope=>[scope,false]));
  let revision=0;
  const receive=message=>{
    received.push(message);
    for(const listener of listeners)listener({data:message});
  };
  const emit=(scope,kind)=>{
    const message=structuredClone(messages[scope][`${enabled[scope]}-${kind}`]);
    if(scope==='taskbar')message.revision=++revision;
    receive(message);
  };
  window.componentFixture={
    commands,received,unexpected,ready:false,
    prepare(scope,value=false) {
      pending.delete(scope);
      enabled[scope]=value;
      emit(scope,'snapshot');
    },
    navigate(scope) {receive({version:1,type:'navigate',page:scope});},
    reply(scope,fail=false) {
      if(!pending.has(scope))throw new Error(`${scope} 没有待确认的卡片命令`);
      if(!fail)enabled[scope]=pending.get(scope);
      pending.delete(scope);
      emit(scope,fail?'error':'success');
    },
    error(scope) {emit(scope,'error');},
  };
  window.chrome={webview:{
    addEventListener(type,listener) {if(type==='message')listeners.add(listener);},
    removeEventListener(type,listener) {if(type==='message')listeners.delete(listener);},
    postMessage(command) {
      commands.push(command);
      if(command==='v1|ready') {
        queueMicrotask(()=>{
          initial.forEach(receive);
          Object.keys(messages).forEach(scope=>emit(scope,'snapshot'));
          window.componentFixture.ready=true;
        });
        return;
      }
      const match=typeof command==='string'&&/^v1\|([^|]+)\|enabled\|(true|false)$/.exec(command);
      if(match&&Object.hasOwn(messages,match[1])) {
        if(pending.has(match[1]))throw new Error(`${match[1]} 重复发送忙碌中的启停命令`);
        pending.set(match[1],match[2]==='true');
      } else if(command?.action==='snapshot'&&['app-settings','account'].includes(command.scope)) {
        queueMicrotask(()=>receive(initial.find(message=>message.type===`${command.scope}-state`)));
      } else if(!['v1|window|dropzone-none','v1|window|drag'].includes(command))unexpected.push(command);
    },
  }};
}

export async function createBrowserHarness() {
  const output=path.resolve(ui,'../../build-artifacts/native-rebuild/component-settings-20260930/browser');
  await mkdir(output,{recursive:true});
  const server=createServer(async(request,response)=>{
    try {
      const pathname=decodeURIComponent(new URL(request.url,'http://127.0.0.1').pathname);
      if(pathname==='/favicon.ico') {response.writeHead(204).end();return;}
      const target=path.resolve(ui,pathname==='/'?'index.html':'.'+pathname);
      if(!target.startsWith(ui+path.sep)) {response.writeHead(403).end();return;}
      const body=await readFile(target);
      const type={'.html':'text/html;charset=utf-8','.js':'text/javascript;charset=utf-8','.css':'text/css',
        '.webp':'image/webp','.png':'image/png','.svg':'image/svg+xml','.ttf':'font/ttf','.otf':'font/otf',
        '.woff':'font/woff','.woff2':'font/woff2'}[path.extname(target)]||'application/octet-stream';
      response.writeHead(200,{'Content-Type':type}).end(body);
    } catch {response.writeHead(404).end();}
  });
  await new Promise((resolve,reject)=>{
    server.once('error',reject);
    server.listen(0,'127.0.0.1',resolve);
  });
  let browser;
  try {
    const dependency=process.env.LUME_PLAYWRIGHT_DIR||
      path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
    const {chromium}=createRequire(import.meta.url)(dependency);
    browser=await chromium.launch({
      headless:true,...(process.env.LUME_BROWSER_EXECUTABLE?
        {executablePath:process.env.LUME_BROWSER_EXECUTABLE}:{channel:'msedge'}),
      args:['--disable-background-networking','--disable-component-update','--no-first-run',
        '--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE 127.0.0.1'],
    });
    const report={entry:'index.html + app.js',browser:browser.version(),stylesheets,viewports:[]};
    return {browser,output,report,origin:`http://127.0.0.1:${server.address().port}`,
      async close() {
        try {
          await writeFile(path.join(output,'results.json'),JSON.stringify(report,null,2));
        } finally {
          await browser.close();
          await new Promise(resolve=>server.close(resolve));
        }
      },
    };
  } catch(error) {
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
    throw error;
  }
}

export async function createFixturePage(harness,viewport) {
  const context=await harness.browser.newContext({viewport,reducedMotion:'no-preference',serviceWorkers:'block'});
  const report={viewport,errors:[],consoleErrors:[],remoteRequests:[],failedRequests:[],httpErrors:[],
    localRequests:[],cases:[],layouts:[],notices:[]};
  harness.report.viewports.push(report);
  await context.route('**/*',route=>{
    if(new URL(route.request().url()).origin===harness.origin)return route.continue();
    report.remoteRequests.push(route.request().url());
    return route.abort('blockedbyclient');
  });
  const page=await context.newPage();
  page.setDefaultTimeout(10000);
  page.on('pageerror',error=>report.errors.push(error.message));
  page.on('console',message=>{if(message.type()==='error')report.consoleErrors.push(message.text());});
  page.on('request',request=>{
    if(new URL(request.url()).origin===harness.origin)report.localRequests.push(new URL(request.url()).pathname);
  });
  page.on('requestfailed',request=>report.failedRequests.push({url:request.url(),error:request.failure()?.errorText}));
  page.on('response',response=>{
    if(response.status()>=400)report.httpErrors.push({url:response.url(),status:response.status()});
  });
  try {
    await page.clock.install({time:new Date('2026-09-30T07:00:00Z')});
    await page.addInitScript(installNativeFixture,nativePayload());
    await page.goto(harness.origin);
    await page.waitForFunction(()=>window.componentFixture?.ready);
    await page.evaluate(()=>document.fonts.ready);
    await page.clock.pauseAt(new Date('2026-09-30T08:00:00Z'));
    return {page,context,report,output:harness.output,prefix:`${viewport.width}x${viewport.height}`};
  } catch(error) {
    await context.close();
    throw error;
  }
}

export async function recordCase(fixture,name,exercise) {
  const result={name,passed:false};
  fixture.report.cases.push(result);
  try {
    await exercise();
    result.passed=true;
  } catch(error) {
    result.error=error.message;
    await fixture.page.screenshot({path:path.join(fixture.output,`${fixture.prefix}-${name}-failed.png`)})
      .catch(screenshotError=>{result.screenshotError=screenshotError.message;});
    throw error;
  }
}

async function screenshot(fixture,name) {
  await fixture.page.screenshot({path:path.join(fixture.output,`${fixture.prefix}-${name}.png`)});
}

async function measureLayout(fixture,name) {
  const layout=await fixture.page.evaluate(()=>{
    const width=document.documentElement.clientWidth,issues=[];
    const describe=element=>element.id?`#${element.id}`:
      `${element.tagName.toLowerCase()}${element.classList.length?'.'+[...element.classList].join('.'):''}`;
    const rendered=element=>{
      const style=getComputedStyle(element);
      return element.getClientRects().length&&style.visibility!=='hidden'&&style.visibility!=='collapse';
    };
    const add=(kind,element,details)=>issues.push({kind,element:describe(element),...details});
    for(const element of [document.documentElement,...document.querySelectorAll('body,body *')]) {
      if(!(element instanceof HTMLElement)||!rendered(element))continue;
      const rect=element.getBoundingClientRect();
      if(element.clientWidth>0&&element.scrollWidth>element.clientWidth+1)
        add('内容宽于容器',element,{clientWidth:element.clientWidth,scrollWidth:element.scrollWidth});
      if(rect.width>0&&(rect.left< -1||rect.right>width+1))
        add('元素超出视口',element,{left:rect.left,right:rect.right,width});
      for(const node of element.childNodes) {
        if(node.nodeType!==Node.TEXT_NODE||!node.textContent.trim())continue;
        const range=document.createRange();
        range.selectNodeContents(node);
        for(const textRect of range.getClientRects()) {
          if(textRect.width>0&&(textRect.left< -1||textRect.right>width+1))
            add('文本超出视口',element,{text:node.textContent.trim(),left:textRect.left,right:textRect.right,width});
        }
      }
    }
    return {width,documentWidth:document.documentElement.scrollWidth,
      bodyWidth:document.body.scrollWidth,issues};
  });
  fixture.report.layouts.push({name,...layout});
  assert.deepEqual(layout.issues,[],`${fixture.prefix}/${name} 存在横向溢出：${JSON.stringify(layout.issues)}`);
}

async function openCatalog(fixture,scope) {
  await fixture.page.emulateMedia({reducedMotion:'no-preference'});
  await fixture.page.evaluate(scope=>{
    componentFixture.prepare(scope);
    componentFixture.navigate(scope);
  },scope);
  await fixture.page.locator('[data-action="back"]').click();
  const toggle=fixture.page.locator(`.Card [data-action="${action(scope)}"]`);
  assert.equal(await toggle.count(),1,'外部卡片保留唯一的主开关');
  assert.equal(await toggle.getAttribute('aria-checked'),'false');
  assert.equal(await toggle.isEnabled(),true);
  return toggle;
}

export async function checkSettingsPages(fixture,{scope,controls}) {
  const {page}=fixture;
  await openCatalog(fixture,scope);
  await measureLayout(fixture,`${scope}-catalog`);
  await page.locator(`[data-open="${scope}"]`).click();
  for(const enabled of [false,true]) {
    if(enabled)await page.evaluate(scope=>componentFixture.prepare(scope,true),scope);
    for(const tab of ['settings','details']) {
      await page.locator(`[data-tab="${tab}"]`).click();
      const panel=page.locator(`[data-settings-scope="${scope}"]`);
      assert.equal(await panel.count(),1,'真实页面必须显示目标组件');
      assert.equal(await panel.locator(masterSelectors).count(),0,'内页不能保留任何组件主开关');
      assert.equal(await panel.getByRole('switch',{name:/^(启用组件|启用功能|Enable widget|Enable feature)$/}).count(),0);
      assert.ok((await panel.locator('.settings-body').textContent()).trim(),'详情和参数区域仍有内容');
      assert.equal(await panel.locator('.status').count(),1,'错误和忙碌状态容器保留');
      if(tab==='settings') {
        for(const key of [...controls,reset(scope)]) {
          const control=panel.locator(controlSelector(key));
          assert.equal(await control.count(),1,`${scope} 保留 ${key}`);
          assert.equal(await control.isEnabled(),true,`${key} 正常状态可以操作`);
        }
      }
      const name=`${scope}-${tab}-${enabled?'on':'off'}`;
      if(!enabled)await screenshot(fixture,name);
      await measureLayout(fixture,name);
      if(!enabled&&tab==='settings') {
        const scroll=await panel.evaluate(element=>{
          element.scrollTop=element.scrollHeight;
          return element.scrollTop;
        });
        if(scroll>0) {
          await screenshot(fixture,`${name}-bottom`);
          await measureLayout(fixture,`${name}-bottom`);
        }
        await panel.evaluate(element=>{element.scrollTop=0;});
      }
    }
  }
}

async function noticeState(page) {
  return page.locator('#notice').evaluate(element=>{
    const style=getComputedStyle(element);
    return {at:Date.now(),hidden:element.hidden,hiding:element.classList.contains('is-hiding'),
      text:element.textContent,opacity:Number(style.opacity),transitionProperty:style.transitionProperty,
      transitionDuration:style.transitionDuration,transitionTimingFunction:style.transitionTimingFunction};
  });
}

async function readNotice(fixture,scope,phase) {
  const value=await noticeState(fixture.page);
  fixture.report.notices.push({scope,phase,...value});
  return value;
}

function assertVisible(value,hiding=false) {
  assert.equal(value.hidden,false,'提示仍应显示');
  assert.equal(value.hiding,hiding,'提示淡出状态应与期限一致');
  assert.ok(value.text.trim(),'可见提示必须保留文本');
}

function assertHidden(value) {
  assert.equal(value.hidden,true,'到期后应隐藏提示');
  assert.equal(value.hiding,false,'清理淡出标记');
  assert.equal(value.text,'','清理过期文本');
}

async function clickAndReply(fixture,scope,enabled,fail=false) {
  const {page}=fixture,toggle=page.locator(`.Card [data-action="${action(scope)}"]`);
  await toggle.click();
  assert.equal(await page.evaluate(()=>componentFixture.commands.at(-1)),`v1|${scope}|enabled|${enabled}`,
    '必须通过真实卡片事件发送正确原生命令');
  assert.equal(await toggle.evaluate(element=>element.disabled||element.getAttribute('aria-disabled')==='true'),
    true,'等待宿主确认时应阻止重复启停');
  assertHidden(await noticeState(page));
  await page.evaluate(({scope,fail})=>componentFixture.reply(scope,fail),{scope,fail});
  assert.equal(await toggle.getAttribute('aria-checked'),String(fail?!enabled:enabled),'卡片以宿主回执为准');
  assert.equal(await toggle.isEnabled(),true,'回执后卡片恢复操作');
  assert.notEqual(await toggle.getAttribute('aria-disabled'),'true');
  const value=await readNotice(fixture,scope,fail?'failed-reply':'success-reply');
  assertVisible(value);
  if(fail)assert.match(value.text,/失败/,'失败不能误显示成功');
  else assert.match(value.text,/已.*(?:应用|保存|恢复)/,'成功回执显示应用或保存结果');
  return value;
}

async function checkErrorReplacement(fixture,scope,delay) {
  const {page}=fixture;
  await page.clock.fastForward(delay);
  const before=await readNotice(fixture,scope,`replace-at-${delay}`);
  assertVisible(before,delay===2500);
  await page.evaluate(scope=>componentFixture.error(scope),scope);
  const error=await readNotice(fixture,scope,`error-at-${delay}`);
  assertVisible(error);
  assert.match(error.text,/失败/);
  assert.notEqual(error.text,before.text,'错误替换旧成功提示');
  await page.clock.fastForward(2680-delay);
  await page.clock.fastForward(10000);
  await sleep(220);
  const retained=await readNotice(fixture,scope,`error-retained-${delay}`);
  assertVisible(retained);
  assert.equal(retained.text,error.text,'旧成功提示的定时器不能关闭新错误');
  assert.equal(retained.opacity,1,'错误提示恢复不透明且持续保留');
  await measureLayout(fixture,`${scope}-error-${delay}`);
  if(delay===2500)await screenshot(fixture,`${scope}-error-retained`);
}

export async function checkCardNotices(fixture,{scope}) {
  const {page}=fixture;
  await openCatalog(fixture,scope);
  const beforeCommands=await page.evaluate(()=>componentFixture.commands.length);
  const shown=await clickAndReply(fixture,scope,true);
  await screenshot(fixture,`${scope}-success`);
  await measureLayout(fixture,`${scope}-success`);
  await page.clock.fastForward(2499);
  const held=await readNotice(fixture,scope,'held-2499');
  assertVisible(held);
  assert.equal(held.at-shown.at,2499);
  assert.equal(held.opacity,1,'2500 毫秒之前不应提前变淡');
  await page.clock.fastForward(1);
  const fading=await readNotice(fixture,scope,'fade-start-2500');
  assertVisible(fading,true);
  assert.equal(fading.at-shown.at,2500);
  assert.equal(fading.transitionProperty,'opacity');
  assert.equal(fading.transitionDuration,'0.18s','使用生产 CSS 的 180 毫秒过渡');
  assert.equal(fading.transitionTimingFunction,'ease-out');
  // 浏览器时钟只推进业务定时器；CSS 合成器使用真实帧采样，不注入或替换样式。
  const samples=[fading.opacity];
  for(let index=0;index<10;index++) {
    await sleep(22);
    const sample=await readNotice(fixture,scope,`fade-sample-${index}`);
    assertVisible(sample,true);
    samples.push(sample.opacity);
  }
  assert.ok(samples.some(value=>value>0&&value<1),`必须观察到真实中间透明度：${samples.join(',')}`);
  assert.equal(samples.at(-1),0,'真实 CSS 淡出结束后透明度为零');
  for(let index=1;index<samples.length;index++)
    assert.ok(samples[index]<=samples[index-1]+0.001,'淡出透明度不能回升');
  await page.clock.fastForward(179);
  assertVisible(await readNotice(fixture,scope,'fade-2679'),true);
  await page.clock.fastForward(1);
  assertHidden(await readNotice(fixture,scope,'hidden-2680'));

  await clickAndReply(fixture,scope,false);
  await checkErrorReplacement(fixture,scope,1000);
  await clickAndReply(fixture,scope,true,true);
  await page.clock.fastForward(10000);
  assertVisible(await readNotice(fixture,scope,'failed-reply-retained'));
  await clickAndReply(fixture,scope,true);
  await checkErrorReplacement(fixture,scope,2500);

  await page.emulateMedia({reducedMotion:'reduce'});
  assert.equal(await page.evaluate(()=>matchMedia('(prefers-reduced-motion: reduce)').matches),true);
  const reduced=await clickAndReply(fixture,scope,false);
  assert.equal(reduced.transitionDuration,'0s','减少动态效果必须来自真实媒体查询样式');
  await page.clock.fastForward(2499);
  assertVisible(await readNotice(fixture,scope,'reduced-held-2499'));
  await page.clock.fastForward(1);
  const hidden=await readNotice(fixture,scope,'reduced-hidden-2500');
  assertHidden(hidden);
  assert.equal(hidden.at-reduced.at,2500,'减少动态效果不再等待额外 180 毫秒');
  await screenshot(fixture,`${scope}-reduced-hidden`);
  const commands=await page.evaluate(({scope,before})=>componentFixture.commands.slice(before)
    .filter(command=>typeof command==='string'&&command.startsWith(`v1|${scope}|`)),
  {scope,before:beforeCommands});
  assert.deepEqual(commands,[true,false,true,true,false].map(value=>`v1|${scope}|enabled|${value}`),
    '保留启用、关闭、失败、重试及最终关闭的完整事件链');
}

export async function checkBrowserEnvironment({page,report}) {
  const entry=await page.evaluate(()=>({
    module:[...document.querySelectorAll('script[type="module"]')].map(script=>script.getAttribute('src')),
    css:[...document.querySelectorAll('link[rel="stylesheet"]')].map(link=>link.getAttribute('href')),
    ruleCounts:[...document.styleSheets].map(sheet=>sheet.cssRules.length),
    injectedStyles:document.querySelectorAll('style').length,
    unexpected:componentFixture.unexpected,
    messages:componentFixture.received,
  }));
  assert.deepEqual(entry.module,['app.js']);
  assert.deepEqual(entry.css,stylesheets);
  assert.equal(entry.ruleCounts.length,stylesheets.length);
  assert.ok(entry.ruleCounts.every(count=>count>0),'入口声明的真实 CSS 均成功加载');
  assert.equal(entry.injectedStyles,0,'测试不能注入替代 CSS');
  for(const asset of ['/','/app.js',...stylesheets.map(name=>'/'+name)])
    assert.ok(report.localRequests.includes(asset),`真实页面加载 ${asset}`);
  assert.deepEqual(entry.unexpected,[],'不得发出真实桌面或非预期模块命令');
  for(const message of entry.messages)
    assert.equal(validHostMessage(message),true,'浏览器中回送的宿主消息始终合法');
  for(const key of ['errors','consoleErrors','remoteRequests','failedRequests','httpErrors'])
    assert.deepEqual(report[key],[],`${key} 必须为空`);
}
