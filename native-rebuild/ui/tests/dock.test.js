import test from 'node:test';
import assert from 'node:assert/strict';
import {validHostMessage,createBridge} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {modules,localizedItem} from '../catalog.js';
import {catalog,card,home,search} from '../pages.js';
import {setLanguage} from '../localization.js';
import {dockPage,dockStatus,validDockState} from '../dock.js';

const snapshot=(fields={})=>({version:1,type:'dock-state',enabled:false,running:false,material:'liquid-glass',reflection:false,iconScale:100,
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
  assert.equal(state.dockStatus,'saved');assert.deepEqual(state.dock,{enabled:true,running:true,material:'liquid-glass',reflection:false,iconScale:100,position:50,bottomOffset:0,iconSpacing:10,launchpadScale:100,launchpadIconScale:100,pinned:2,added:0});
  state=reduce(state,{type:'host',message:snapshot({error:'limit'})});
  assert.equal(state.dockStatus,'error');assert.match(dockStatus(state),/上限/);
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).dockStatus,'unavailable');
});

test('Dock settings page renders material, counts and details',()=>{
  const state={...initialState(),dock:{enabled:true,running:true,material:'frosted-glass',pinned:4,added:1},dockStatus:'idle'};
  const settings=dockPage(state,'settings');
  assert.match(settings,/data-settings-scope="dock"/);
  assert.match(settings,/id="dock-material"/);
  assert.match(settings,/已固定 4 项，启动台已添加 1 项/);
  assert.match(settings,/data-action="dock-reset"/);
  assert.match(dockPage(state,'details'),/启动台显示桌面上的图标/);
});
test('dock offers no material, icon reflection and icon size',()=>{
  assert.equal(validDockState(snapshot({material:'none',reflection:true,iconScale:120})),true);
  for(const fields of [{iconScale:150},{reflection:'yes'},{material:'clear'}])
    assert.equal(validDockState(snapshot(fields)),false,JSON.stringify(fields));
  const sent=[];const bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(bridge.send('dock','material','none'),true);
  assert.equal(bridge.send('dock','reflection',true),true);
  assert.equal(bridge.send('dock','iconScale',120),true);
  assert.equal(bridge.send('dock','iconScale',50),true);
  assert.equal(bridge.send('dock','iconScale',121),false);
  assert.deepEqual(sent,['v1|dock|material|none','v1|dock|reflection|true','v1|dock|iconScale|120','v1|dock|iconScale|50']);
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
  const html=dockPage({...initialState(),dockStatus:'idle'});
  for(const id of ['dock-icon-scale','dock-bottom-offset','dock-icon-spacing','dock-launchpad-scale','dock-launchpad-icon-scale']){
    assert.match(html,new RegExp(`id="${id}-number" type="number"`));
  }
});

test('Dock layout settings send validated values and keep their state',()=>{
  const sent=[];const bridge=createBridge({postMessage:value=>sent.push(value)});
  for(const [action,value] of [['position',75],['bottomOffset',80],['iconSpacing',24]])
    assert.equal(bridge.send('dock',action,value),true);
  for(const [action,value] of [['position',101],['bottomOffset',-20],['iconSpacing',25]])
    assert.equal(bridge.send('dock',action,value),false);
  assert.deepEqual(sent,['v1|dock|position|75','v1|dock|bottomOffset|80','v1|dock|iconSpacing|24']);
  const message=snapshot({position:75,bottomOffset:80,iconSpacing:24});
  assert.ok(validDockState(message));
  assert.equal(validDockState(snapshot({position:101})),false);
  assert.equal(validDockState(snapshot({iconSpacing:25})),false);
  assert.equal(validDockState(snapshot({position:12,iconSpacing:5,iconScale:105})),true);
  const state=reduce(initialState(),{type:'host',message});
  assert.equal(state.dock.position,75);assert.equal(state.dock.bottomOffset,80);assert.equal(state.dock.iconSpacing,24);
  const page=dockPage(state);
  assert.ok(!page.includes('id="dock-position"'),'horizontal position is no longer offered');
  assert.ok(page.indexOf('id="dock-icon-scale"')<page.indexOf('id="dock-bottom-offset"')&&page.indexOf('id="dock-bottom-offset"')<page.indexOf('id="dock-icon-spacing"'));
  assert.match(page,/id="dock-bottom-offset"/);
});
