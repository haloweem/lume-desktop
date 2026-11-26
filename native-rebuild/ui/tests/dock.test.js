import test from 'node:test';
import assert from 'node:assert/strict';
import {validHostMessage,createBridge} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {modules,localizedItem} from '../catalog.js';
import {catalog,card,home,search} from '../pages.js';
import {setLanguage} from '../localization.js';
import {dockPage,dockStatus,validDockState,validDockSummonShortcut,dockSummonShortcutFromEvent,dockSummonShortcutDefault} from '../dock.js';

const snapshot=(fields={})=>({version:1,type:'dock-state',enabled:false,running:false,material:'liquid-glass',reflection:false,systemTray:false,
  summonEnabled:false,summonShortcutCode:0,summonHotkeyRegistered:false,iconScale:100,
  position:50,bottomOffset:0,iconSpacing:10,
  pinned:0,added:0,materialStatus:'inactive',loadStatus:'fresh',error:'',result:'snapshot',saved:false,...fields});

test('Dock native icon cache fields must not prevent close acknowledgements reaching the settings page',()=>{
  const message=snapshot({missingIcons:0,iconCachePaths:12,loadStatus:'loaded',result:'applied',saved:true});
  assert.equal(validDockState(message),true);assert.equal(validHostMessage(message),true);
  const pending=reduce(initialState(),{type:'request',scope:'dock',key:'enabled',value:false});
  const settled=reduce(pending,{type:'host',message});
  assert.notEqual(settled.dockStatus,'saving');assert.equal(settled.dock.enabled,false);
  for(const fields of [{missingIcons:-1},{missingIcons:'0'},{iconCachePaths:257},{iconCachePaths:1.5}])
    assert.equal(validDockState({...message,...fields}),false);
});

test('Dock state with launchpad window fields still reaches the settings page',()=>{
  // The native session reports these; rejecting them left the Dock toggle grey and unusable.
  const message=snapshot({enabled:true,running:true,launchpadScale:100,launchpadIconScale:100,launchpadMaximized:true,launchpadRemoved:3,loadStatus:'loaded',result:'applied',saved:true});
  assert.equal(validDockState(message),true);assert.equal(validHostMessage(message),true);
  const settled=reduce(initialState(),{type:'host',message});
  assert.equal(settled.dock.enabled,true);
  for(const fields of [{launchpadMaximized:'true'},{launchpadRemoved:-1},{launchpadRemoved:1.5}])
    assert.equal(validDockState({...message,...fields}),false);
});

test('Dock keeps its toggle and the removed menu bar has no entry',()=>{
  const index=modules.findIndex(item=>item.id==='dock');
  assert.ok(index>=0,'dock entry exists');
  assert.equal(modules[index+1].id,'dynamicIsland');
  assert.equal(modules[index].title,'Dock栏美化');assert.equal(modules[index].toggle,true);
  const state={...initialState(),dock:{...initialState().dock,enabled:true},dockStatus:'idle'};
  assert.match(card(modules[index],state),/aria-checked="true"[^>]*data-action="dock-toggle"/);
  assert.equal(modules.some(item=>item.id==='menuBar'),false);
  assert.match(catalog(state,false),/data-open="dock"/);
  for(const language of ['zh-CN','en']) {
    setLanguage(language);
    for(const html of [home(state),catalog(state,false),search({...state,query:''}),search({...state,query:'菜单栏'})])
      assert.doesNotMatch(html,/data-open="menuBar"|data-action="menuBar-toggle"/);
  }
  try {setLanguage('en');assert.equal(localizedItem(modules[index]).title,'Dock');}
  finally {setLanguage('zh-CN');}
});

test('removed menu bar cannot be opened by local or native navigation',()=>{
  const state=initialState();
  assert.equal(reduce(state,{type:'navigate',view:'menuBar'}),state);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'menuBar'}),false);
});

test('dock-state messages are validated strictly',()=>{
  assert.ok(validDockState(snapshot()));
  assert.ok(validHostMessage(snapshot({enabled:true,running:true,pinned:3,added:2})));
  assert.ok(!validDockState(snapshot({material:'glass'})));
  assert.ok(!validDockState(snapshot({pinned:-1})));
  assert.ok(!validDockState(snapshot({error:'other'})));
  const popup=snapshot({enabled:true,running:true,error:'collection-popup',loadStatus:'loaded'});
  assert.ok(validHostMessage(popup),'a failed group popup must not hide the Dock state');
  const shown=reduce(initialState(),{type:'host',message:popup});
  assert.equal(shown.dock.enabled,true);assert.match(dockStatus(shown),/收纳组/);
  assert.ok(!validDockState({...snapshot(),extra:true}));
  assert.ok(!validDockState(snapshot({saved:true,result:'snapshot'})));
  assert.ok(validDockState(snapshot({saved:true,result:'applied',loadStatus:'loaded'})));
});

test('dock state reducer tracks saving, results and errors',()=>{
  let state=reduce(initialState(),{type:'request',scope:'dock',key:'enabled',value:true});
  assert.equal(state.dockStatus,'saving');
  state=reduce(state,{type:'host',message:snapshot({enabled:true,running:true,pinned:2,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.dockStatus,'saved');assert.deepEqual(state.dock,{screen:1,screenCount:1,enabled:true,running:true,material:'liquid-glass',reflection:false,reflectionRatio:50,reflectionTransparency:50,showRunning:true,runningPreview:false,animation:false,animateRestore:true,animateLaunch:true,animateClose:true,animationDuration:400,showStartMenu:true,launchpadEnabled:true,edge:'bottom',systemTray:false,summonEnabled:false,summonShortcutCode:0,summonHotkeyRegistered:false,iconScale:100,position:50,positionMode:'center',positionOffset:0,bottomOffset:0,iconSpacing:10,launchpadScale:100,launchpadIconScale:100,pinned:2,added:0});
  state=reduce(state,{type:'host',message:snapshot({error:'limit'})});
  assert.equal(state.dockStatus,'error');assert.match(dockStatus(state),/上限/);
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).dockStatus,'unavailable');
});

test('Dock running-area state defaults on for old snapshots and accepts only explicit booleans',()=>{
  assert.equal(initialState().dock.showRunning,true);
  assert.equal(validHostMessage(snapshot()),true,'an older host may omit the new field');
  assert.equal(reduce(initialState(),{type:'host',message:snapshot()}).dock.showRunning,true);
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  for(const showRunning of [true,false]){
    assert.equal(validDockState(snapshot({showRunning})),true);
    assert.equal(validHostMessage(snapshot({showRunning})),true);
    assert.equal(reduce(initialState(),{type:'host',message:snapshot({showRunning})}).dock.showRunning,showRunning);
    assert.equal(bridge.send('dock','showRunning',showRunning),true);
  }
  for(const value of [undefined,null,'true','false',0,1,{},[]]){
    assert.equal(validDockState(snapshot({showRunning:value})),false,'a present field must be boolean');
    assert.equal(validHostMessage(snapshot({showRunning:value})),false);
    assert.equal(bridge.send('dock','showRunning',value),false);
  }
  assert.equal(bridge.send('dock','showRunning'),false);
  assert.equal(bridge.send('dock','showRunning',false,'extra'),false);
  assert.deepEqual(sent,['v1|dock|showRunning|true','v1|dock|showRunning|false']);
});

test('Dock running-area changes wait for acknowledgement, preserve failures and reset on confirmation',()=>{
  let state=reduce(initialState(),{type:'host',message:snapshot({showRunning:true,reflection:true,systemTray:true})});
  state=reduce(state,{type:'request',scope:'dock',key:'showRunning',value:false});
  assert.equal(state.dockStatus,'saving');assert.equal(state.dock.showRunning,true);
  state=reduce(state,{type:'host',message:snapshot({showRunning:true,reflection:true,systemTray:true})});
  assert.equal(state.dockStatus,'saving');assert.equal(state.dock.showRunning,true,'a snapshot is not a save acknowledgement');
  const saved=snapshot({showRunning:false,reflection:true,systemTray:true,result:'applied',saved:true,loadStatus:'loaded'});
  state=reduce(state,{type:'host',message:saved});
  assert.equal(state.dockStatus,'saved');assert.equal(state.dock.showRunning,false);
  assert.equal(state.dock.reflection,true);assert.equal(state.dock.systemTray,true);
  const reopened=reduce(initialState(),{type:'host',message:{...saved,result:'snapshot',saved:false}});
  assert.equal(reopened.dock.showRunning,false,'the persisted host value wins after reopening');
  state=reduce(reopened,{type:'request',scope:'dock',key:'showRunning',value:true});
  state=reduce(state,{type:'host',message:{...saved,saved:false,error:'save'}});
  assert.equal(state.dockStatus,'error');assert.equal(state.dock.showRunning,false);
  state=reduce(state,{type:'request',scope:'dock',key:'reset'});
  assert.equal(state.dock.showRunning,false,'reset also waits for the host');
  state=reduce(state,{type:'host',message:snapshot({showRunning:true,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.dock.showRunning,true);assert.equal(state.dockStatus,'saved');
});

test('Dock running-area switch is localized and follows busy state',()=>{
  const state={...initialState(),dockStatus:'idle'};
  try {
    for(const language of ['zh-CN','en']){
      setLanguage(language);
      const html=dockPage(state,'settings');
      const material=html.indexOf('id="dock-material"'),running=html.indexOf('data-action="dock-show-running"'),reset=html.indexOf('data-action="dock-reset"');
      assert.ok(material>=0&&material<running&&running<reset);
      const control=html.match(/<button[^>]*data-action="dock-show-running"[^>]*>/)?.[0]||'';
      assert.match(control,/aria-checked="true"/);
      assert.match(control,language==='en'?/aria-label="Running apps"/:/aria-label="运行区"/);
      assert.doesNotMatch(html,/Show running apps in the Dock|在 Dock 中显示正在运行的应用|class="setting-help"/);
      for(const dockStatus of ['loading','saving'])
        assert.match(dockPage({...state,dockStatus},'settings'),/<button[^>]*data-action="dock-show-running"[^>]*disabled/);
      assert.match(dockPage({...state,dock:{...state.dock,showRunning:false}},'settings'),/<button[^>]*aria-checked="false"[^>]*data-action="dock-show-running"/);
      for(const tab of ['dock','control-center','details'])
        assert.doesNotMatch(dockPage(state,tab),/data-action="dock-show-running"/);
    }
  } finally {setLanguage('zh-CN');}
});

test('Dock settings place control center after running apps and omit launchpad counts',()=>{
  const state={...initialState(),dock:{...initialState().dock,enabled:true,running:true,material:'frosted-glass',pinned:4,added:1},dockStatus:'idle'};
  const settings=dockPage(state,'settings');
  assert.match(settings,/data-settings-scope="dock"/);
  assert.match(settings,/id="dock-material"/);
  assert.equal([...settings.matchAll(/class="settings-group__body"/g)].length,5);
  assert.doesNotMatch(settings,/data-action="dock-reflection"/);
  for(const value of ['liquid-glass','frosted-glass','none'])
    assert.ok(settings.includes(`data-dock-material="${value}" aria-pressed="${value==='frosted-glass'}"`));
  assert.match(settings,/data-action="dock-reset"/);
  assert.doesNotMatch(settings,/id="dock-icon-scale"|data-dock-summary/);
  const runAt=settings.indexOf('data-action="dock-show-running"'),centerAt=settings.indexOf('data-action="dock-system-tray"');
  const summonAt=settings.indexOf('data-action="dock-summon-enabled"');
  assert.ok(runAt>=0&&runAt<centerAt&&centerAt<summonAt,'control center follows the running-area setting');
  assert.match(settings,/class="dock-shortcut-field"/);
  assert.match(settings,/data-dock-summon-shortcut[^>]*>Win \+ Insert<\/button>/);
  assert.match(settings,/data-action="dock-summon-clear"/);
  assert.doesNotMatch(settings,/class="setting-help"|未设置快捷键，不会唤出 Dock|No shortcut set/);
  const dock=dockPage(state);
  for(const id of ['dock-icon-scale','dock-bottom-offset','dock-icon-spacing'])assert.match(dock,new RegExp(`id="${id}"`));
  assert.match(dock,/<h2>图标倒影<\/h2>/);
  assert.match(dock,/data-action="dock-reflection"/);
  assert.match(dock,/id="dock-reflection-ratio"[^>]+value="50"[^>]+disabled/);
  assert.match(dock,/id="dock-reflection-transparency"[^>]+value="50"[^>]+disabled/);
  assert.doesNotMatch(dock,/id="dock-material"|data-action="dock-reset"|id="dock-launchpad-scale"/);
  assert.match(settings,/id="dock-material"[\s\S]*data-action="dock-launchpad-enabled"[\s\S]*id="dock-launchpad-scale"[\s\S]*id="dock-launchpad-icon-scale"[\s\S]*id="dock-screen-label"/);
  assert.equal(dockPage(state,'launchpad'),settings);
  assert.equal(validDockState(snapshot({launchpadEnabled:0})),false);
  assert.equal(reduce(initialState(),{type:'host',message:snapshot({launchpadEnabled:false})}).dock.launchpadEnabled,false);
  const toggle=page=>page.match(/<[^>]*data-action="dock-system-tray"[^>]*>/)?.[0]||'';
  assert.ok(toggle(settings)&&!/aria-checked="true"/.test(toggle(settings)));
  assert.match(toggle(dockPage({...state,dock:{...state.dock,systemTray:true}},'settings')),/aria-checked="true"/);
  assert.match(dockPage(state,'details'),/启动台显示桌面上的图标/);
});

test('Dock sidebar has distinct localized navigation and the generated icon family',()=>{
  const state={...initialState(),dockStatus:'idle'};
  try {
    for(const language of ['zh-CN','en']){
      setLanguage(language);
      for(const tab of ['dock','settings','details']){
        const html=dockPage(state,tab);
        const nav=html.match(/<nav[^>]*role="tablist"[^>]*>([\s\S]*?)<\/nav>/)?.[1]||'';
        assert.deepEqual([...nav.matchAll(/data-tab="([^"]+)"/g)].map(match=>match[1]),
          ['dock','settings','details']);
        assert.deepEqual([...nav.matchAll(/aria-selected="true" data-tab="([^"]+)"/g)].map(match=>match[1]),[tab]);
        for(const [key,icon] of [['dock','dock'],['settings','settings']]){
          const button=nav.match(new RegExp(`<button[^>]*data-tab="${key}"[^>]*>([\\s\\S]*?)<\\/button>`))?.[1]||'';
          assert.match(button,new RegExp(`${icon}-${tab===key?'selected':'default'}-24\\.png`));
          assert.match(button,/-48\.png 2x, [^"]+-72\.png 3x/);
        }
        assert.match(nav,/data-tab="settings"[^>]*>[\s\S]*settings-(?:default|selected)-24\.png[\s\S]*data-tab="details"/,
          'the existing settings gear remains immediately above Details');
        assert.doesNotMatch(nav,/data-tab="launchpad"/);
        assert.doesNotMatch(nav,language==='en'?/Control center/:/控制中心/);
      }
    }
  } finally {setLanguage('zh-CN');}
});
test('Dock summon is opt-in and only accepts safe registered shortcuts',()=>{
  const state=initialState();
  assert.equal(state.dock.summonEnabled,false);
  assert.equal(dockSummonShortcutDefault,2093);
  assert.equal(state.dock.summonShortcutCode,dockSummonShortcutDefault);
  assert.equal(validDockSummonShortcut(dockSummonShortcutDefault),true);
  assert.equal(validDockState(snapshot({summonEnabled:true})),true,'enabled without a key remains inactive');
  assert.equal(validDockSummonShortcut(0),true);
  assert.equal(validDockSummonShortcut(800),true,'Ctrl+Alt+Space');
  assert.equal(validDockSummonShortcut(2592),true,'Win+Ctrl+Space');
  assert.equal(validDockSummonShortcut(544),true,'Ctrl+Space is a customizable global shortcut');
  assert.equal(validDockSummonShortcut(814),false,'Ctrl+Alt+Delete is reserved');
  assert.equal(validDockSummonShortcut(371),false,'Alt+F4 is reserved');
  assert.equal(dockSummonShortcutFromEvent({key:' ',keyCode:32,ctrlKey:true,altKey:true}),800);
  assert.equal(dockSummonShortcutFromEvent({key:' ',keyCode:32,ctrlKey:true}),544);
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(bridge.send('dock','summonEnabled',true),true);
  assert.equal(bridge.send('dock','summonCapture',true),true);
  assert.equal(bridge.send('dock','summonShortcut',800),true);
  assert.equal(bridge.send('dock','summonShortcut',544),true);
  assert.equal(bridge.send('dock','summonShortcut',0),true);
  assert.deepEqual(sent,['v1|dock|summonEnabled|true','v1|dock|summonCapture|true',
    'v1|dock|summonShortcut|800','v1|dock|summonShortcut|544','v1|dock|summonShortcut|0']);
});
test('Dock validates and restores both saved reflection percentages',()=>{
  const message=snapshot({material:'none',reflection:true,reflectionRatio:80,reflectionTransparency:18,iconScale:120});
  assert.equal(validHostMessage(message),true);
  const restored=reduce(initialState(),{type:'host',message}).dock;
  assert.equal(restored.reflectionRatio,80);assert.equal(restored.reflectionTransparency,18);
  for(const fields of [{iconScale:150},{reflection:'yes'},{material:'clear'},{reflectionRatio:101},{reflectionTransparency:-1},{reflectionRatio:'50'}])
    assert.equal(validDockState(snapshot(fields)),false,JSON.stringify(fields));
  const sent=[];const bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(bridge.send('dock','material','none'),true);
  assert.equal(bridge.send('dock','reflection',true),true);
  assert.equal(bridge.send('dock','reflectionRatio',80),true);
  assert.equal(bridge.send('dock','reflectionTransparency',50),true);
  assert.equal(bridge.send('dock','reflectionRatio',101),false);
  assert.equal(bridge.send('dock','reflectionTransparency',-1),false);
  assert.equal(bridge.send('dock','iconScale',120),true);
  assert.equal(bridge.send('dock','iconScale',50),true);
  assert.equal(bridge.send('dock','iconScale',121),false);
  assert.deepEqual(sent,['v1|dock|material|none','v1|dock|reflection|true','v1|dock|reflectionRatio|80','v1|dock|reflectionTransparency|50','v1|dock|iconScale|120','v1|dock|iconScale|50']);
});
test('launchpad size and icon size have independent native commands and numeric inputs',()=>{
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  for(const [key,min,max] of [['launchpadScale',60,140],['launchpadIconScale',50,120]]){
    for(const value of [min,83,100,max]){
      assert.equal(bridge.send('dock',key,value),true);assert.equal(validDockState(snapshot({[key]:value})),true);
      assert.equal(reduce(initialState(),{type:'host',message:snapshot({[key]:value})}).dock[key],value);
    }
    for(const value of [min-1,max+1,'100',90.5]){
      assert.equal(bridge.send('dock',key,value),false);assert.equal(validDockState(snapshot({[key]:value})),false);
    }
  }
  for(const [tab,ids] of [['dock',['dock-icon-scale','dock-bottom-offset','dock-icon-spacing']],
    ['settings',['dock-launchpad-scale','dock-launchpad-icon-scale']]]){
    const html=dockPage({...initialState(),dockStatus:'idle'},tab);
    for(const id of ids)assert.match(html,new RegExp(`id="${id}-number" type="number"`));
  }
});

test('Dock layout settings send validated values and keep their state',()=>{
  const sent=[];const bridge=createBridge({postMessage:value=>sent.push(value)});
  for(const [action,value] of [['positionMode','left'],['positionOffset',25],['bottomOffset',80],['iconSpacing',24]])
    assert.equal(bridge.send('dock',action,value),true);
  for(const [action,value] of [['positionMode','top'],['positionOffset',51],['bottomOffset',-20],['iconSpacing',25]])
    assert.equal(bridge.send('dock',action,value),false);
  assert.deepEqual(sent,['v1|dock|positionMode|left','v1|dock|positionOffset|25','v1|dock|bottomOffset|80','v1|dock|iconSpacing|24']);
  const message=snapshot({positionMode:'left',positionOffset:25,bottomOffset:80,iconSpacing:24});
  assert.ok(validDockState(message));
  assert.equal(validDockState(snapshot({position:101})),false);
  assert.equal(validDockState(snapshot({positionMode:'top'})),false);
  assert.equal(validDockState(snapshot({positionOffset:51})),false);
  assert.equal(validDockState(snapshot({iconSpacing:25})),false);
  assert.equal(validDockState(snapshot({position:12,iconSpacing:5,iconScale:105})),true);
  const state=reduce(initialState(),{type:'host',message});
  assert.equal(state.dock.positionMode,'left');assert.equal(state.dock.positionOffset,25);assert.equal(state.dock.bottomOffset,80);assert.equal(state.dock.iconSpacing,24);
  const page=dockPage(state);
  assert.match(page,/<h2>尺寸<\/h2>/);
  assert.match(page,/<h2>位置<\/h2>/);
  assert.match(page,/data-dock-position="center" aria-pressed="false"/);
  assert.match(page,/data-dock-position="left" aria-pressed="true"/);
  assert.match(page,/data-dock-position="right" aria-pressed="false"/);
  assert.ok(page.indexOf('id="dock-icon-scale"')<page.indexOf('id="dock-icon-spacing"'));
  assert.ok(page.indexOf('id="dock-icon-spacing"')<page.indexOf('data-dock-position="center"'));
  assert.ok(page.indexOf('id="dock-position-offset"')<page.indexOf('id="dock-bottom-offset"'));
  assert.match(page,/data-dock-position-offset-row[^>]*>/);
  assert.match(page,/id="dock-bottom-offset"/);
});
