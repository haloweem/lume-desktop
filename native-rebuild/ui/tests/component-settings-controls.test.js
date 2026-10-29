import test, {afterEach} from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync} from 'node:fs';
import {initialState} from '../state.js';
import {validHostMessage} from '../bridge.js';
import {setLanguage} from '../localization.js';
import {clockPage} from '../clock.js';
import {calendarPage} from '../calendar.js';
import {weatherPage} from '../weather.js';
import {almanacPage} from '../almanac.js';
import {performancePage} from '../performance.js';
import {organizerPage} from '../organizer.js';
import {desktopIconsPage} from '../desktop-icons.js';
import {taskbarPage} from '../taskbar.js';
import {minimizePage} from '../minimize.js';
import {installStartupBrowserApis} from './startup-dom.mjs';

const components=[
  ['clock',clockPage,['clock-style']],
  ['calendar',calendarPage,['calendar-theme','calendar-material','calendar-textColor','calendar-highlightColor']],
  ['weather',weatherPage,['weather-material','weather-city']],
  ['almanac',almanacPage,['almanac-material']],
  ['performance',performancePage,['performance-material']],
  ['organizer',organizerPage,['organizer-material']],
  ['icons',desktopIconsPage,['icons-autoHide','icons-delay','icons-restoreOnInput']],
  ['taskbar',taskbarPage,['taskbar-transparent','taskbar-autoHide']],
  ['minimize',minimizePage,['minimize-durationMs','minimize-restore','minimize-launch','minimize-multiMonitor',
    'minimize-unhideTaskbar','minimize-unhideDurationMs','minimize-engine']],
];
const action=scope=>scope==='clock'?'toggle':`${scope}-toggle`;
const reset=scope=>scope==='clock'?'reset':`${scope}-reset`;
const selector=key=>`[id="${key}"], [data-action="${key}"]`;
const jsdomPath=process.env.LUME_JSDOM_PATH;
afterEach(()=>setLanguage('zh-CN'));

for(const [scope,page,controls] of components) {
  test(`${scope}：设置和详情移除主开关，但保留参数、子开关、重置和导航`,()=>{
    for(const language of ['zh-CN','en']) {
      setLanguage(language);
      for(const enabled of [false,true]) {
        for(const status of ['idle','loading','saving','error','unavailable','saved']) {
          const state=initialState();
          state[scope].enabled=enabled;
          state[scope==='clock'?'status':`${scope}Status`]=status;
          state[scope==='clock'?'error':`${scope}Error`]=status==='error'?'runtime':'';
          for(const tab of ['settings','details']) {
            const html=page(state,tab);
            assert.equal(html.includes(`data-action="${action(scope)}"`),false,
              `${language}/${status}/${tab} 不应出现主开关`);
            assert.doesNotMatch(html,/>启用组件<|>启用功能<|>Enable widget<|>Enable feature</);
            assert.ok(html.includes(`data-settings-scope="${scope}"`));
            for(const key of ['data-action="back"','data-tab="settings"','data-tab="details"','class="status '])
              assert.ok(html.includes(key),`${scope} 保留 ${key}`);
            if(tab==='settings') {
              for(const control of controls)
                assert.match(html,new RegExp(`(?:id|data-action)="${control}"`),`${scope} 保留 ${control}`);
              assert.ok(html.includes(`data-action="${reset(scope)}"`),`${scope} 保留重置`);
            } else assert.ok(html.includes('<p>'),`${scope} 保留详情说明`);
          }
        }
      }
    }
  });
}

function hostMessage(scope,enabled,{error='',revision=0,applied=false}={}) {
  const preferences={...initialState()[scope],enabled};
  if(scope!=='clock')preferences.running=enabled&&!error;
  const common={version:1,error,loadStatus:'loaded',result:applied?'applied':'snapshot',saved:applied&&!error};
  if(scope==='clock')return {version:1,type:'settings',clock:preferences,error,saved:!error};
  if(scope==='calendar')return {...common,type:'calendar-settings',calendar:preferences,materialStatus:'inactive'};
  if(scope==='taskbar')return {version:1,type:'taskbar-state',...preferences,error,loadStatus:'loaded',revision};
  if(scope==='icons')return {...common,type:'icons-state',...preferences,desktops:preferences.running?1:0};
  if(scope==='minimize')return {...common,type:'minimize-state',...preferences,
    x64:preferences.running,x86:false,hosts:preferences.running?1:0,hooks:preferences.running?2:0,
    entries:0,pendingCleanup:0};
  if(scope==='organizer')preferences.collections=preferences.collections.map(item=>({
    ...item,enabled,running:preferences.running,
  }));
  const weather=scope==='weather'?{loading:false,weatherError:'',cacheWriteFailed:false,hasWeather:false}:{};
  return {...common,type:`${scope}-state`,...preferences,materialStatus:'inactive',...weather};
}

async function withApp(scope,id,exercise) {
    const {JSDOM,VirtualConsole}=createRequire(import.meta.url)(jsdomPath);
    const errors=[],virtualConsole=new VirtualConsole();
    virtualConsole.on('jsdomError',error=>errors.push(error.message));
    const dom=new JSDOM('<div id="app"><header id="titlebar"></header><main id="content"></main><div id="notice" hidden></div></div>',
      {url:'https://lume.local/',pretendToBeVisual:true,virtualConsole});
    const originals=Object.fromEntries(['window','document','matchMedia'].map(key=>
      [key,Object.getOwnPropertyDescriptor(globalThis,key)]));
    globalThis.window=dom.window;
    globalThis.document=dom.window.document;
    globalThis.matchMedia=()=>({matches:true});
    const finishStartup=installStartupBrowserApis(dom.window);
    const callbacks=new Set(),commands=[];
    dom.window.chrome={webview:{
      addEventListener:(_,fn)=>callbacks.add(fn),removeEventListener:(_,fn)=>callbacks.delete(fn),
      postMessage:command=>commands.push(command),
    }};
    const receive=message=>{
      assert.equal(validHostMessage(message),true,`${scope} 使用合法宿主消息`);
      callbacks.forEach(fn=>fn({data:message}));
    };
    const find=query=>{
      assert.deepEqual(errors,[],'页面事件不得抛出异常');
      const element=dom.window.document.querySelector(query);
      assert.ok(element,`${scope} 缺少 ${query}`);return element;
    };
    const click=query=>find(query).click();
    const cardToggle=()=>find(`.Card [data-action="${action(scope)}"]`);
    try {
      await import(`../app.js?component-settings-controls=${scope}-${id}`);
      await finishStartup(commands);
      await exercise({dom,receive,commands,find,click,cardToggle});
      assert.deepEqual(errors,[]);
    } finally {
      dom.window.dispatchEvent(new dom.window.Event('beforeunload'));
      assert.equal(callbacks.size,0,'卸载页面解除宿主订阅');
      dom.window.close();
      for(const [key,descriptor] of Object.entries(originals)) {
        if(descriptor)Object.defineProperty(globalThis,key,descriptor);
        else delete globalThis[key];
      }
    }
}

test('taskbar hiding behavior follows the switch, sends exact commands and preserves confirmation and focus',{skip:!jsdomPath},async()=>{
  await withApp('taskbar','hide-behavior',async({dom,receive,commands,find,click})=>{
    const message=(fields={})=>({...hostMessage('taskbar',true,{revision:1}),autoHideTaskbar:true,...fields});
    receive(message({autoHideTaskbar:false}));
    receive({version:1,type:'navigate',page:'taskbar'});
    const row=find('[data-taskbar-hide-behavior]');
    assert.equal(row.hidden,true);
    click('[data-action="taskbar-autoHide"]');
    assert.equal(commands.at(-1),'v1|taskbar|autoHide|true');
    assert.equal(row.hidden,false,'switch immediately shows the setting');
    assert.equal(find('#taskbar-hide-behavior').disabled,true,'pending switch blocks selection');
    receive(message({revision:2}));
    const trigger=find('#taskbar-hide-behavior');
    assert.equal(trigger.disabled,false);
    click('#taskbar-hide-behavior');
    assert.equal(trigger.nextElementSibling.hidden,false);
    click('[data-select-owner="taskbar-hide-behavior"][data-select-value="fully-hidden"]');
    assert.equal(commands.at(-1),'v1|taskbar|hideBehavior|fully-hidden');
    assert.equal(trigger.querySelector('span').textContent,'完全隐藏');
    assert.equal(trigger.getAttribute('aria-disabled'),'true');
    assert.equal(dom.window.document.activeElement,trigger,'selection retains keyboard focus');
    const count=commands.length;
    trigger.click();
    assert.equal(commands.length,count);
    receive(message({revision:2,applying:true,hideBehavior:'fully-hidden'}));
    assert.equal(trigger.getAttribute('aria-disabled'),'true','intermediate snapshot cannot acknowledge the change');
    receive(message({revision:3,hideBehavior:'fully-hidden'}));
    assert.equal(find('#taskbar-hide-behavior'),trigger,'acknowledgement updates without rebuilding controls');
    assert.equal(trigger.getAttribute('aria-disabled'),'false');
    click('[data-action="taskbar-autoHide"]');
    assert.equal(row.hidden,true);
    receive(message({revision:4,hideBehavior:'fully-hidden',autoHideTaskbar:false}));
    click('[data-action="taskbar-autoHide"]');
    receive(message({revision:5,hideBehavior:'fully-hidden'}));
    assert.equal(row.hidden,false);
    assert.equal(trigger.querySelector('span').textContent,'完全隐藏','mode survives toggling hiding off and on');
    click('#taskbar-hide-behavior');
    click('[data-select-owner="taskbar-hide-behavior"][data-select-value="auto-hide"]');
    assert.equal(commands.at(-1),'v1|taskbar|hideBehavior|auto-hide');
    receive(message({revision:6,hideBehavior:'fully-hidden',error:'apply-visibility'}));
    assert.equal(trigger.querySelector('span').textContent,'完全隐藏','failure returns to confirmed behavior');
    assert.match(find('.status').textContent,/完全隐藏失败/);
  });
});

for(const [scope,,controls] of components) {
  test(`${scope}：外部卡片经应用事件链启停、失败重试并返回详情`,{skip:!jsdomPath},async()=>{
    await withApp(scope,'card',async({dom,receive,commands,find,click,cardToggle})=>{
      const checkSettings=busy=>{
        assert.ok(find(`[data-settings-scope="${scope}"]`));
        assert.equal(dom.window.document.querySelector(`[data-action="${action(scope)}"]`),null,'内页没有主开关');
        for(const key of [...controls,reset(scope)])assert.equal(find(selector(key)).disabled,busy,key);
      };
      receive(hostMessage(scope,false));
      receive({version:1,type:'navigate',page:scope});
      checkSettings(false);
      click('[data-tab="details"]');
      assert.equal(dom.window.document.querySelector(`[data-action="${action(scope)}"]`),null);
      assert.ok(find('.settings-body').textContent.trim(),'详情仍有内容');
      click('[data-tab="settings"]');
      checkSettings(false);
      click('[data-action="back"]');
      assert.equal(cardToggle().getAttribute('aria-checked'),'false');
      assert.equal(cardToggle().disabled,false);
      cardToggle().click();
      assert.equal(commands.at(-1),`v1|${scope}|enabled|true`);
      assert.equal(cardToggle().disabled,true,'等待确认时阻止重复启用');
      if(scope==='organizer') {
        assert.equal(cardToggle().getAttribute('aria-checked'),'true','点击后立即移动开关');
        assert.equal(cardToggle().getAttribute('aria-busy'),'true');
        assert.equal(find('[data-organizer-pending]').hidden,false);
        assert.match(find('[data-organizer-pending]').textContent,/正在开启/);
        receive(hostMessage(scope,false));
        assert.equal(cardToggle().getAttribute('aria-checked'),'true','中间快照不能让开关跳回');
      }
      const count=commands.length;
      cardToggle().click();
      assert.equal(commands.length,count);
      receive(hostMessage(scope,false,{error:scope==='taskbar'?'core-start':'runtime',revision:1,applied:true}));
      assert.equal(cardToggle().getAttribute('aria-checked'),'false','启用失败仍显示真实关闭状态');
      assert.equal(cardToggle().disabled,false,'失败后仍可重试');
      if(scope==='organizer')assert.equal(find('[data-organizer-pending]').hidden,true);
      assert.equal(find('#notice').hidden,false);
      assert.doesNotMatch(find('#notice').textContent,/已应用|已保存/);
      click(`[data-open="${scope}"]`);
      checkSettings(false);
      assert.equal(find('.status').getAttribute('role'),'alert');
      assert.equal(find('.status').hidden,false);
      click('[data-action="back"]');
      cardToggle().click();
      assert.equal(commands.at(-1),`v1|${scope}|enabled|true`,'重试确实再次发送启用命令');
      click(`[data-open="${scope}"]`);
      checkSettings(true);
      const surface=find(`[data-settings-scope="${scope}"]`);
      receive(hostMessage(scope,true,{revision:2,applied:true}));
      checkSettings(false);
      assert.equal(find(`[data-settings-scope="${scope}"]`),surface,'确认不重建详情页');
      click('[data-action="back"]');
      assert.equal(cardToggle().getAttribute('aria-checked'),'true');
      cardToggle().click();
      assert.equal(commands.at(-1),`v1|${scope}|enabled|false`);
      assert.equal(cardToggle().disabled,true);
      if(scope==='organizer') {
        assert.equal(cardToggle().getAttribute('aria-checked'),'false');
        assert.match(find('[data-organizer-pending]').textContent,/正在关闭/);
      }
      receive(hostMessage(scope,false,{revision:3,applied:true}));
      assert.equal(cardToggle().getAttribute('aria-checked'),'false');
      assert.equal(cardToggle().disabled,false);
      click(`[data-open="${scope}"]`);
      checkSettings(false);
      assert.deepEqual(commands.filter(command=>typeof command==='string'&&command.startsWith(`v1|${scope}|`)),
        [`v1|${scope}|enabled|true`,`v1|${scope}|enabled|true`,`v1|${scope}|enabled|false`]);
    });
  });
}

const fixtureCases=[
  ['icons','desktop_icons_manager_test.cpp',[['enabled',true],['delay',9],['restoreOnInput',false]]],
  ['taskbar','desktop_taskbar_test.cpp',[['enabled',true],['autoHide',true],['transparent',false],['hideBehavior','fully-hidden'],['hideBehavior','auto-hide'],['autoHide',false]]],
  ['minimize','desktop_minimize_test.cpp',[['enabled',true],['durationMs',600],['engine','classic'],['restore',false]]],
  ['almanac','desktop_almanac_test.cpp',[['enabled',true],['material','liquid-glass']]],
  ['performance','desktop_performance_test.cpp',[['enabled',true],['material','liquid-glass']]],
  ['organizer','desktop_organizer_test.cpp',[['enabled',true],['material','liquid-glass']]],
  ['weather','desktop_weather_test.cpp',[['city','shanghai'],['enabled',true],['material','liquid-glass']]],
];
const flushMutations=async()=>{
  for(let index=0;index<20;index++)await Promise.resolve();
};
for(const [scope,file,requests] of fixtureCases) {
  test(`${scope}：原生夹具脚本在应用 DOM 中仍走卡片和详情，保持命令及重开顺序`,{skip:!jsdomPath},async()=>{
    const source=readFileSync(new URL(`../../tests/unit/${file}`,import.meta.url),'utf8');
    const script=source.match(/options\.readyScript=LR"JS\(([\s\S]*?)\)JS";/)?.[1];
    assert.ok(script,`${file} 保留 WebView 操作脚本`);
    const startScript=dom=>new Function('document','window','MutationObserver','Event',script)(
      dom.window.document,dom.window,dom.window.MutationObserver,dom.window.Event);
    let preferences={},enabled=false;
    const reply=(applied,revision)=>({...hostMessage(scope,enabled,{applied,revision}),...preferences});
    await withApp(scope,'fixture-enable',async({dom,receive,commands,find})=>{
      receive(reply(false,0));
      receive({version:1,type:'navigate',page:scope});
      const before=commands.length;
      startScript(dom);
      for(const [index,[key,value]] of requests.entries()) {
        await flushMutations();
        assert.equal(commands.length,before+index+1,'夹具应逐项等待宿主确认，不能停在已移除的主开关');
        assert.equal(commands.at(-1),`v1|${scope}|${key}|${value}`,`${file} 第 ${index+1} 条命令`);
        if(key==='enabled') {
          assert.ok(find(`.Card [data-action="${action(scope)}"]`),'只能通过外部卡片启用');
          enabled=value;
        } else {
          assert.ok(find(`[data-settings-scope="${scope}"]`),'参数仍通过详情页设置');
          const property={delay:'autoHideDelay',restoreOnInput:'showIconsOnAnyInput',
            transparent:'transparentTaskbar',autoHide:'autoHideTaskbar'}[key] || key;
          preferences[property]=value;
        }
        await flushMutations();
        assert.equal(commands.length,before+index+1,'忙碌时不跳过当前确认');
        receive(reply(true,index+1));
      }
      await flushMutations();
      assert.equal(commands.length,before+requests.length,'确认结束后不得重复提交');
      assert.ok(find(`[data-settings-scope="${scope}"]`),'启用后回到详情页');
    });
    await withApp(scope,'fixture-reopen',async({dom,receive,commands,find})=>{
      receive(reply(false,requests.length));
      receive({version:1,type:'navigate',page:scope});
      const before=commands.length;
      startScript(dom);
      await flushMutations();
      if(['icons','taskbar','minimize'].includes(scope)) {
        assert.equal(commands.length,before+1);
        assert.equal(commands.at(-1),`v1|${scope}|enabled|false`,'重开管理页仍从卡片关闭服务');
        assert.ok(find(`.Card [data-action="${action(scope)}"]`));
        enabled=false;
        receive(reply(true,requests.length+1));
        await flushMutations();
        assert.equal(commands.length,before+1,'关闭确认后不重复提交');
      } else assert.equal(commands.length,before,'重开组件页保留已经启用和保存的参数');
      assert.ok(find(`[data-settings-scope="${scope}"]`),'重开操作结束后仍回到详情页');
    });
  });
}
