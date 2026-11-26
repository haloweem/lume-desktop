import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeExpandedLayout,encodeExpandedLayout,expandedWidgets} from '../expanded-widgets.js';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {islandPage} from '../dynamic-island.js';

const snapshot=(expandedLayout=1,fields={})=>({version:1,type:'dynamicIsland-state',
  enabled:true,running:true,locked:true,expanded:false,focusing:false,finished:false,remaining:0,
  hideFullscreen:true,topmost:false,hover:false,autoHide:true,size:'standard',material:'none',
  expandedLayout,loadStatus:'loaded',error:'',result:'snapshot',saved:false,...fields});
test('all bounded layouts are canonical and preserve existing layout codes',()=>{
  let count=0,legacyCount=0,clockCount=0,fiveCount=0,countdownCount=0,countdownFiveCount=0;
  const legacyDecode=value=>{
    const ids=[];
    while(value){const digit=value%6;value=Math.floor(value/6);if(!digit||ids.includes(digit-1))return null;ids.unshift(digit-1);}
    return ids;
  };
  const previousDecode=code=>{
    if(code<1296)return legacyDecode(code);
    code-=1296;const ids=[];
    while(code){const digit=code%7;code=Math.floor(code/7);if(!digit||ids.includes(digit-1)||ids.length===4)return null;ids.unshift(digit-1);}
    return ids.includes(5)?ids:null;
  };
  for(let code=0;code<=11032;++code){
    const ids=decodeExpandedLayout(code);if(ids===null)continue;
    assert.ok(ids.length<=5);assert.equal(new Set(ids).size,ids.length);
    assert.equal(encodeExpandedLayout(ids),code);++count;
    if(code<1296){assert.deepEqual(ids,legacyDecode(code));++legacyCount;}
    else if(code<3697){assert.ok(ids.includes(5));++clockCount;}
    else if(code<4417){assert.equal(ids.length,5);++fiveCount;}
    else if(code<8513){assert.ok(ids.includes(6));++countdownCount;}
    else{assert.equal(ids.length,5);assert.ok(ids.includes(6));++countdownFiveCount;}
  }
  for(let code=0;code<=3696;++code)assert.deepEqual(decodeExpandedLayout(code),previousDecode(code));
  assert.equal(count,3620);assert.equal(legacyCount,206);assert.equal(clockCount,311);assert.equal(fiveCount,720);
  assert.equal(countdownCount,583);assert.equal(countdownFiveCount,1800);
  assert.deepEqual(decodeExpandedLayout(1),[0]);
  assert.deepEqual(decodeExpandedLayout(0),[]);
  assert.deepEqual(decodeExpandedLayout(53),[0,1,4],'music, calendar and to-dos retain their stored layout');
  assert.equal(encodeExpandedLayout([5]),1302);assert.deepEqual(decodeExpandedLayout(1302),[5]);
  assert.equal(encodeExpandedLayout([0,1,2,3,4]),3697);assert.equal(encodeExpandedLayout([5,4,3,2,1]),4416);
  assert.equal(encodeExpandedLayout([6]),4424);assert.deepEqual(decodeExpandedLayout(4424),[6]);
  assert.equal(encodeExpandedLayout([6,5,4,3,2]),11032);assert.deepEqual(decodeExpandedLayout(11032),[6,5,4,3,2]);
  for(const value of [-1,6,7,12,1296,1297,1303,1344,3696,4417,1.2,'1',null])assert.equal(decodeExpandedLayout(value),null);
  for(const ids of [[0,0],[5,5],[0,1,2,3,4,5],[7],[-1],['1']])assert.equal(encodeExpandedLayout(ids),null);
});
test('the real bridge accepts only validated layouts and waits for a host acknowledgement',()=>{
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  const code=encodeExpandedLayout([5,4,1,0,3]);
  assert.equal(bridge.send('dynamicIsland','expandedLayout',code),true);
  assert.equal(sent.at(-1),`v1|dynamicIsland|expandedLayout|${code}`);
  for(const value of [6,7,1296,-1,'1']){
    assert.equal(bridge.send('dynamicIsland','expandedLayout',value),false);
    assert.equal(validHostMessage(snapshot(value)),false);
  }
  let state=reduce(initialState(),{type:'host',message:snapshot()});
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'expandedLayout',value:code});
  assert.equal(state.dynamicIsland.expandedLayout,1);assert.equal(state.dynamicIslandStatus,'saving');
  state=reduce(state,{type:'host',message:snapshot(1,{result:'applied',error:'save'})});
  assert.equal(state.dynamicIsland.expandedLayout,1);assert.equal(state.dynamicIslandStatus,'error');
  state=reduce(state,{type:'host',message:snapshot(code,{result:'applied',saved:true})});
  assert.equal(state.dynamicIsland.expandedLayout,code);assert.equal(state.dynamicIslandStatus,'saved');
  const old=snapshot();delete old.expandedLayout;
  assert.equal(validHostMessage(old),true);
  assert.equal(reduce(state,{type:'host',message:old}).dynamicIsland.expandedLayout,1);
});
test('island home exposes expanded widgets and style navigation without compact or live preview controls',()=>{
  const html=islandPage(initialState(),'home');
  assert.equal(expandedWidgets.length,7);
  assert.doesNotMatch(html,/data-island-compact|data-island-preview|data-island-native-preview/);
  assert.match(html,/data-island-expanded-panel>/);
  assert.match(html,/data-island-styles/);
  // Focus stays hidden; performance is now released.
  assert.match(html,/data-island-expanded-toggle="0"/);
  assert.match(html,/data-island-expanded-toggle="1"/);
  assert.match(html,/data-island-expanded-toggle="4"/);
  assert.match(html,/data-island-expanded-toggle="5"/);
  assert.match(html,/data-island-expanded-toggle="3"/);
  assert.match(html,/data-island-expanded-toggle="6"/);
  assert.equal((html.match(/data-island-expanded-toggle="\d+"/g)||[]).length,6);
  assert.doesNotMatch(html,/最多同时启用 5 个组件|按住已启用的胶囊拖动排序|Use the switches to enable/);
  const base=initialState();
  const layout=ids=>ids.reduce((code,id)=>code*6+id+1,0);
  const page=code=>islandPage({...base,dynamicIsland:{...base.dynamicIsland,expandedLayout:code}},'home');
  const mixed=page(layout([2,1,0,4]));
  assert.equal((mixed.match(/data-island-expanded-toggle="\d+"/g)||[]).length,6,'the new countdown choice does not change a stored selection');
  assert.match(mixed,/data-island-expanded-toggle="4" aria-checked="true"/);
  assert.match(mixed,/data-island-expanded-toggle="1" aria-checked="true"/);
  assert.match(mixed,/data-island-expanded-toggle="0" aria-checked="true"/);
  assert.match(mixed,/data-island-expanded-toggle="5" aria-checked="false"/);
  const clockPage=page(encodeExpandedLayout([2,5,4,1]));
  assert.match(clockPage,/data-island-expanded-toggle="5" aria-checked="true"/);
  assert.match(clockPage,/data-island-expanded-toggle="4" aria-checked="true"/);
  assert.match(clockPage,/data-island-expanded-toggle="1" aria-checked="true"/);
  assert.match(clockPage,/data-island-expanded-toggle="0" aria-checked="false"/,'clock does not fall back to music');
  assert.match(page(layout([2])),/data-island-expanded-toggle="0" aria-checked="true"/,'hidden-only layouts fall back to music');
  assert.match(page(layout([2,3])),/data-island-expanded-toggle="3" aria-checked="true"/,'stored system monitor layouts now show performance');
  const five=page(encodeExpandedLayout([0,1,4,5,3]));
  assert.equal((five.match(/data-island-expanded-toggle="\d+" aria-checked="true"/g)||[]).length,5,'all five published widgets can be enabled together');
  assert.match(five,/aria-label="性能"/);
  const countdown=page(encodeExpandedLayout([6,0,1]));
  assert.match(countdown,/data-island-expanded-toggle="6" aria-checked="true"/);
  assert.doesNotMatch(html,/data-expanded-remove|data-expanded-number|data-expanded-selected|data-expanded-available|岛内|已加入|未加入/);
  assert.doesNotMatch(html,/QQ|WeChat|微信|data-island-messages/);
  const tabs=islandPage(initialState(),'home');
  for(const key of ['home','todo','notifications','settings','details'])
    assert.match(tabs,new RegExp(`data-tab="${key}"`));
  for(const key of ['music','quick-launch','clock'])
    assert.doesNotMatch(tabs,new RegExp(`data-tab="${key}"`),'hidden module tabs stay out of navigation');
});
