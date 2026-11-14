import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {islandPage} from '../dynamic-island.js';
import {setLanguage} from '../localization.js';

const snapshot=extra=>({version:1,type:'dynamicIsland-state',enabled:false,running:false,locked:false,
  expanded:false,focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:false,hover:false,
  autoHide:true,size:'standard',material:'none',loadStatus:'loaded',error:'',result:'snapshot',saved:false,...extra});

test('Chinese almanac has independent validated commands and native acknowledgement',()=>{
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value),addEventListener(){},removeEventListener(){}});
  for(const value of ['default','chinese-almanac']){
    assert.ok(bridge.send('dynamicIsland','calendarStyle',value));
    assert.ok(validHostMessage(snapshot({calendarStyle:value})));
  }
  for(const value of ['',null,1,'Chinese-Almanac','default ','chinese-almanac|extra']){
    assert.equal(bridge.send('dynamicIsland','calendarStyle',value),false);
    assert.equal(validHostMessage(snapshot({calendarStyle:value})),false);
  }
  assert.deepEqual(sent,['v1|dynamicIsland|calendarStyle|default','v1|dynamicIsland|calendarStyle|chinese-almanac']);
  let state=reduce(initialState(),{type:'host',message:snapshot()});
  assert.equal(state.dynamicIsland.calendarStyle,'default','older hosts retain the original calendar');
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'calendarStyle',value:'chinese-almanac'});
  assert.equal(state.dynamicIsland.calendarStyle,'default','selection waits for acknowledgement');
  state=reduce(state,{type:'host',message:snapshot({calendarStyle:'default',error:'save',result:'applied'})});
  assert.equal(state.dynamicIsland.calendarStyle,'default');
  state=reduce(state,{type:'host',message:snapshot({calendarStyle:'chinese-almanac',saved:true,result:'applied'})});
  assert.equal(state.dynamicIsland.calendarStyle,'chinese-almanac');
  assert.equal(state.dynamicIsland.clockStyle,'simple');
  assert.equal(state.dynamicIsland.expandedLayout,1,'choosing a style does not enable or reorder widgets');
});

test('calendar category offers default and Chinese almanac in both languages',()=>{
  const state={...initialState(),islandStyleWidget:'calendar-weather',dynamicIslandStatus:'idle'};
  for(const language of ['zh-CN','en']){
    setLanguage(language);
    const html=islandPage(state,'home');
    assert.match(html,/data-island-calendar-style="default" aria-pressed="true"/);
    assert.match(html,/data-island-calendar-style="chinese-almanac" aria-pressed="false"/);
    assert.equal((html.match(/data-island-calendar-style=/g)||[]).length,2);
    assert.ok(html.includes(language==='en'?'Chinese almanac':'中华万年历'));
    assert.ok(html.includes(`calendar-chinese-almanac${language==='en'?'-en':''}.png`));
    const chosen=islandPage({...state,dynamicIsland:{...state.dynamicIsland,calendarStyle:'chinese-almanac'}},'home');
    assert.match(chosen,/data-island-calendar-style="chinese-almanac" aria-pressed="true"/);
  }
  setLanguage('zh-CN');
});
