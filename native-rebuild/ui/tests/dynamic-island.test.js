import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {modules} from '../catalog.js';
import {catalog,search} from '../pages.js';
import {islandPage,validIslandState} from '../dynamic-island.js';
import {setLanguage} from '../localization.js';
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
    {saved:true,result:'snapshot'},{loadStatus:'bad'},{error:'unknown'},{size:'huge'},{hover:1},{material:'glass'},{hideIdle:false}])assert.equal(validHostMessage(snapshot(fields)),false);
  for(const command of [['enabled',true],['locked',false],['expand'],['collapse'],['focus'],['stopFocus'],['reset'],
    ['hideFullscreen',true],['topmost',false],['hover',true],['size','large'],['material','liquid-glass'],['autoHide',true]])
    assert.ok(bridge.send('dynamicIsland',...command));
  for(const command of [['enabled','true'],['focus',true],['locked',1],['material','glass'],['path','C:/anything'],['size','huge'],['hover','yes'],['hideIdle',false]])
    assert.equal(bridge.send('dynamicIsland',...command),false);
  assert.equal(sent.length,13);
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
test('island settings include lock, expansion, timer and accurate details in both languages',()=>{
  const state={...initialState(),dynamicIslandStatus:'idle'};
  for(const language of ['zh-CN','en']){
    setLanguage(language);
    const settings=islandPage(state);
    assert.doesNotMatch(settings,/dynamicIsland-lock/);
    assert.match(settings,/data-action="dynamicIsland-expand" disabled/);
    assert.doesNotMatch(settings,/dynamicIsland-focus|25-minute|专注/);
    assert.match(islandPage(state,'details'),/音乐|music/i);
  }
  setLanguage('zh-CN');
  assert.equal(reduce(state,{type:'navigate',view:'dynamicIsland'}).view,'dynamicIsland');
  const active={...state,dynamicIsland:{...state.dynamicIsland,enabled:true,expanded:true}};
  assert.match(islandPage(active),/data-action="dynamicIsland-collapse"/);
  assert.doesNotMatch(islandPage(active),/dynamicIsland-hideIdle/);
  for(const action of ['autoHide','hideFullscreen','topmost','hover'])assert.match(islandPage(active),new RegExp(`data-action="dynamicIsland-${action}"`));
  assert.match(islandPage(active),/id="dynamicIsland-size"/);
  assert.match(islandPage(active),/id="dynamicIsland-material"/);
  const updated=reduce(state,{type:'host',message:snapshot({hover:true,size:'small',topmost:false})});
  assert.equal(updated.dynamicIsland.size,'small');assert.equal(updated.dynamicIsland.material,'none');assert.equal(updated.dynamicIsland.hover,true);assert.equal(updated.dynamicIsland.topmost,false);
});
