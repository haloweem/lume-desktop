import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {modules} from '../catalog.js';
import {catalog,search} from '../pages.js';
import {islandPage,validIslandState} from '../dynamic-island.js';
import {setLanguage} from '../localization.js';
import {compactWidgets,compactWidgetPicker} from '../compact-widgets.js';
const snapshot=(fields={})=>({version:1,type:'dynamicIsland-state',enabled:false,running:false,locked:false,
  expanded:false,focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:true,hover:false,autoHide:false,size:'standard',material:'none',
  loadStatus:'fresh',error:'',result:'snapshot',saved:false,...fields});

test('island appears immediately after Dock in the styles catalog and search',()=>{
  const state=initialState();const index=modules.findIndex(item=>item.id==='dock');
  assert.equal(modules[index+1].id,'dynamicIsland');
  const html=catalog(state,false);
  assert.ok(html.indexOf('data-open="dynamicIsland"')>html.indexOf('data-open="dock"'));
  assert.match(html,/data-action="dynamicIsland-toggle" disabled/);
  assert.match(search({...state,query:'灵动岛'}),/data-open="dynamicIsland"/);
});
test('island bridge rejects malformed commands and snapshots',()=>{
  const sent=[];const bridge=createBridge({postMessage:m=>sent.push(m)});
  assert.ok(validIslandState(snapshot()));assert.ok(validHostMessage(snapshot()));
  for(const fields of [{enabled:'true'},{remaining:1501},{remaining:NaN},{running:true},{extra:true},
    {saved:true,result:'snapshot'},{loadStatus:'bad'},{error:'unknown'},{size:'huge'},{hover:1},{material:'glass'},{hideIdle:false},{shape:'circle'},{shape:1}])assert.equal(validHostMessage(snapshot(fields)),false);
  for(const command of [['enabled',true],['locked',false],['expand'],['collapse'],['focus'],['stopFocus'],['reset'],
    ['hideFullscreen',true],['topmost',false],['hover',true],['size','large'],['material','liquid-glass'],['autoHide',true],['shape','standard'],['shape','capsule'],['resetAppearance'],
    ['leftWidget',20],['rightWidget',14]])
    assert.ok(bridge.send('dynamicIsland',...command));
  for(const command of [['enabled','true'],['focus',true],['locked',1],['material','glass'],['path','C:/anything'],['size','huge'],['hover','yes'],['hideIdle',false]])
    assert.equal(bridge.send('dynamicIsland',...command),false);
  for(const command of [['customIcons',true],['overview',true],['leftWidget',-1],['rightWidget',31],['leftWidget','4'],
    ['leftStyle','circle'],['rightStyle','capsule'],['card',0],['cardMove',0]])
    assert.equal(bridge.send('dynamicIsland',...command),false);
  assert.equal(bridge.send('dynamicIsland','shape','circle'),false);
  assert.equal(sent.length,18);
  assert.ok(validHostMessage({version:1,type:'navigate',page:'dynamicIsland'}));
});
test('native acknowledgement settles requests while preserving confirmed preferences on failure',()=>{
  let state=reduce(initialState(),{type:'request',scope:'dynamicIsland',key:'enabled',value:true});
  assert.equal(state.dynamicIslandStatus,'saving');assert.equal(state.dynamicIsland.enabled,false);
  state=reduce(state,{type:'host',message:snapshot()});assert.equal(state.dynamicIslandStatus,'saving');
  state=reduce(state,{type:'host',message:snapshot({enabled:true,running:true,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.dynamicIslandStatus,'saved');assert.equal(state.dynamicIsland.enabled,true);
  state=reduce(state,{type:'host',message:snapshot({enabled:true,running:true,error:'save',result:'applied'})});
  assert.equal(state.dynamicIslandStatus,'error');assert.equal(state.dynamicIsland.enabled,true);
  assert.equal(reduce(state,{type:'unavailable',scope:'all'}).dynamicIslandStatus,'unavailable');
});
test('island settings show only display preferences and accurate details in both languages',()=>{
  const state={...initialState(),dynamicIslandStatus:'idle'};
  for(const language of ['zh-CN','en']){
    setLanguage(language);
    const home=islandPage(state,'home');
    assert.match(home,/settings--island/);
    assert.match(home,/data-tab="home"/);
    assert.match(home,/data-tab="settings"/);
    for(const key of ['music','todo','quick-launch','clock','notifications'])assert.match(home,new RegExp(`data-tab="${key}"`));
    assert.match(home,/assets\/icons\/lume-rounded-v1\/home-selected-24.png/);
    assert.match(home,/data-island-home/);
    assert.match(home,/data-island-shape="standard"/);
    assert.match(home,/data-island-shape="capsule"/);
    const settings=islandPage(state);
    assert.doesNotMatch(settings,/dynamicIsland-lock/);
    assert.doesNotMatch(settings,/dynamicIsland-(?:toggle|hover|expand|collapse)|island-preview|正在播放|Now playing/);
    assert.doesNotMatch(settings,/dynamicIsland-focus|25-minute|专注/);
    assert.doesNotMatch(settings,/dynamicIsland-(?:customIcons|overview|leftStyle|rightStyle|card)|data-island-card|多组件工作台/);
    assert.doesNotMatch(islandPage(state,'details'),/workspace|customiz|自选左右|组件排序/i);
    assert.match(islandPage(state,'details'),/音乐|music/i);
  }
  setLanguage('zh-CN');
  assert.equal(reduce(state,{type:'navigate',view:'dynamicIsland'}).view,'dynamicIsland');
  const active={...state,dynamicIsland:{...state.dynamicIsland,enabled:true,expanded:true}};
  assert.doesNotMatch(islandPage(active),/dynamicIsland-(?:toggle|hover|expand|collapse)|island-preview/);
  assert.doesNotMatch(islandPage(active),/dynamicIsland-hideIdle/);
  assert.match(islandPage(active),/data-action="dynamicIsland-autoHide"/);
  assert.match(islandPage(active),/data-action="dynamicIsland-hideFullscreen"/);
  assert.match(islandPage(active),/id="dynamicIsland-layer"/);
  assert.match(islandPage(active),/data-select-value="above"/);
  assert.match(islandPage(active),/全屏时隐藏/);
  assert.match(islandPage(active),/空闲时隐藏/);
  assert.doesNotMatch(islandPage(active),/无活动时自动隐藏/);
  assert.doesNotMatch(islandPage(active),/id="dynamicIsland-(?:size|material)"|data-island-preference/);
  const home=islandPage(active,'home');
  assert.equal(compactWidgets.length,31);
  assert.equal((home.match(/data-island-compact-widget="\d+"/g)||[]).length,31);
  assert.match(home,/data-island-compact-side="left"/);
  assert.match(home,/data-island-compact-side="right"/);
  for(const id of [17,18,20,25])assert.match(home,new RegExp(`data-island-compact-widget="${id}"`));
  const before=compactWidgetPicker({...state,dynamicIslandPreview:{readings:[{id:14,text:'3%',available:true}]}});
  const after=compactWidgetPicker({...state,dynamicIslandPreview:{readings:[{id:14,text:'99%',available:true}]}});
  assert.equal(before,after,'chooser samples do not use live native readings');
  assert.match(before,/data-island-compact-widget="14"[\s\S]*?48%/);
  assert.doesNotMatch(before,/data-compact-reading|99%/);
  assert.doesNotMatch(home,/data-island-layout|data-island-membership|data-island-components-title|点击标签加入|Click a tag to add/);
  for(const [key,values] of [['material',['none','liquid-glass','frosted-glass']],['size',['small','standard','large']]])
    for(const value of values)assert.match(home,new RegExp(`data-island-preference="${key}" data-value="${value}"`));
  assert.match(home,/data-action="dynamicIsland-resetAppearance"/);
  const updated=reduce(state,{type:'host',message:snapshot({hover:true,size:'small',topmost:false})});
  assert.equal(updated.dynamicIsland.size,'small');assert.equal(updated.dynamicIsland.material,'none');assert.equal(updated.dynamicIsland.hover,true);assert.equal(updated.dynamicIsland.topmost,false);
});
test('shape waits for native confirmation, survives rollback and accepts older snapshots',()=>{
  let state=reduce(initialState(),{type:'host',message:snapshot({shape:'capsule'})});
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'shape',value:'standard'});
  assert.equal(state.dynamicIsland.shape,'capsule');
  state=reduce(state,{type:'host',message:snapshot({shape:'standard',saved:true,result:'applied',loadStatus:'loaded'})});
  assert.equal(state.dynamicIsland.shape,'standard');assert.equal(state.dynamicIslandStatus,'saved');
  state=reduce(state,{type:'host',message:snapshot({shape:'standard',error:'save',result:'applied'})});
  assert.equal(state.dynamicIsland.shape,'standard');assert.equal(state.dynamicIslandStatus,'error');
  assert.equal(reduce(state,{type:'host',message:snapshot()}).dynamicIsland.shape,'capsule');
});

test('display policy lists connected screens and preserves confirmed selection across save failures',()=>{
  const sent=[],bridge=createBridge({postMessage:message=>sent.push(message)});
  for(const screen of [0,1,64])assert.ok(bridge.send('dynamicIsland','screen',screen));
  for(const screen of [-1,65,1.5,'1',null,NaN])assert.equal(bridge.send('dynamicIsland','screen',screen),false);
  assert.deepEqual(sent,['v1|dynamicIsland|screen|0','v1|dynamicIsland|screen|1','v1|dynamicIsland|screen|64']);
  for(const fields of [{screen:0,screens:[1]},{screen:2,screens:[1,2]},{screen:2,screens:[1]}])assert.ok(validHostMessage(snapshot(fields)));
  for(const fields of [{screen:-1,screens:[1]},{screen:65,screens:[1]},{screen:'1',screens:[1]},
    {screen:1,screens:[]},{screen:1,screens:[1,1]},{screen:1,screens:[2]},{screen:1},{screens:[1]}])
    assert.equal(validHostMessage(snapshot(fields)),false);
  for(const language of ['zh-CN','en']){
    setLanguage(language);
    for(const screens of [[1],[1,2],[1,2,3,4]]){
      const state=reduce(initialState(),{type:'host',message:snapshot({screen:0,screens})});
      const html=islandPage(state,'home');
      assert.match(html,/class="island-display"/);
      assert.ok(html.indexOf('class="island-display"')>html.indexOf('id="island-size-label"'));
      assert.equal((html.match(/data-island-screen="\d+"/g)||[]).length,screens.length+1);
      for(const id of screens)assert.match(html,new RegExp(`data-island-screen="${id}"`));
      assert.match(html,/data-island-screen="0" aria-pressed="true"/);
      assert.match(html,/显示策略|Display policy/);
      assert.doesNotMatch(html,new RegExp(`data-island-screen="${screens.length+1}"`));
    }
  }
  setLanguage('zh-CN');
  let state=reduce(initialState(),{type:'host',message:snapshot({screen:2,screens:[1,2]})});
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'screen',value:0});
  assert.equal(state.dynamicIsland.screen,2,'selection waits for native confirmation');
  state=reduce(state,{type:'host',message:snapshot({screen:2,screens:[1,2],result:'applied',error:'save'})});
  assert.equal(state.dynamicIsland.screen,2);assert.equal(state.dynamicIslandStatus,'error');
  state=reduce(state,{type:'host',message:snapshot({screen:0,screens:[1,2],result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.dynamicIsland.screen,0);assert.equal(state.dynamicIslandStatus,'saved');
  state=reduce(state,{type:'host',message:snapshot({screen:2,screens:[1]})});
  assert.match(islandPage(state,'home'),/data-island-screen-unavailable >所选屏幕未连接/);
  assert.equal(reduce(state,{type:'host',message:snapshot()}).dynamicIsland.screen,1,'older hosts retain the primary-screen default');
});
