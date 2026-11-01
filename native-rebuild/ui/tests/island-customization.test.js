import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {islandPage} from '../dynamic-island.js';
import {setLanguage} from '../localization.js';
import {defaultIslandCustomization,validIslandCustomization,validIslandCustomizationCommand,
  islandMiniWidgets,islandExpandedWidgets} from '../island-customization.js';

const snapshot=(customization=defaultIslandCustomization(),fields={})=>({
 version:1,type:'dynamicIsland-state',enabled:true,running:true,locked:false,expanded:false,
 focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:true,hover:false,
 autoHide:true,size:'standard',material:'none',loadStatus:'loaded',error:'',result:'snapshot',saved:false,
 customization,...fields,
});
test('customization validates complete independent slots and canonical card orders',()=>{
 assert.equal(islandMiniWidgets.length,31);assert.equal(islandExpandedWidgets.length,9);
 const c=defaultIslandCustomization();assert.equal(validIslandCustomization(c),true);
 assert.equal(validHostMessage(snapshot(c)),true);
 c.left.widget='cat';c.left.style='capsule';c.right.widget='none';c.order.reverse();
 assert.equal(validIslandCustomization(c),true);
 assert.equal(validHostMessage(snapshot(c)),true);
 for(const patch of [{version:2},{customIcons:1},{overview:'true'},{extra:true},
  {left:{widget:'cat',style:'square'}},{right:{widget:'unknown',style:'circle'}},
  {enabled:[true]},{order:Array(9).fill('music')},{order:c.order.slice(1)}])
  assert.equal(validIslandCustomization({...c,...patch}),false,JSON.stringify(patch));
 const fresh=defaultIslandCustomization();assert.equal(fresh.left.widget,'date');
 assert.equal(fresh.order[0],'music','defaults must not share mutable arrays');
});
test('customization bridge enforces command ranges and keeps data until native acknowledgement',()=>{
 const sent=[],bridge=createBridge({postMessage:m=>sent.push(m)});
 for(const [action,value] of [['customIcons',true],['overview',false],['leftWidget',0],
  ['rightWidget',30],['leftStyle','circle'],['rightStyle','capsule'],['card',35],['cardMove',80]])
  assert.equal(bridge.send('dynamicIsland',action,value),true,action);
 for(const [action,value] of [['customIcons','true'],['overview',0],['leftWidget',-1],
  ['leftWidget',31],['rightWidget',1.5],['leftStyle','square'],['card',36],['card',-1],
  ['cardMove',81],['cardMove',-1],['cardMove',8.5]]) {
  assert.equal(validIslandCustomizationCommand(action,value),false,action);
  assert.equal(bridge.send('dynamicIsland',action,value),false,action);
 }
 assert.equal(sent.length,8);
 let state=reduce(initialState(),{type:'host',message:snapshot()});
 const prior=structuredClone(state.dynamicIsland.customization);
 state=reduce(state,{type:'request',scope:'dynamicIsland',key:'leftWidget',value:29});
 assert.equal(state.dynamicIslandStatus,'saving');assert.deepEqual(state.dynamicIsland.customization,prior);
 state=reduce(state,{type:'host',message:snapshot(prior,{result:'applied',error:'save'})});
 assert.equal(state.dynamicIslandStatus,'error');assert.deepEqual(state.dynamicIsland.customization,prior);
 const next=structuredClone(prior);next.left.widget='cat';next.customIcons=true;
 state=reduce(state,{type:'host',message:snapshot(next,{result:'applied',saved:true})});
 assert.equal(state.dynamicIslandStatus,'saved');assert.equal(state.dynamicIsland.customization.left.widget,'cat');
 assert.equal(state.dynamicIsland.customization.right.widget,'battery');
});
test('both languages expose two full galleries and nine cards without removed controls',()=>{
 for(const language of ['zh-CN','en']) {
  setLanguage(language);
  const html=islandPage({...initialState(),dynamicIslandStatus:'idle'});
  assert.equal((html.match(/class="island-mini-choice"/g)||[]).length,60);
  assert.equal((html.match(/data-island-card=/g)||[]).length,9);
  assert.match(html,/data-island-slot="left"/);assert.match(html,/data-island-slot="right"/);
  assert.doesNotMatch(html,/dynamicIsland-(?:toggle|hover|expand|collapse)\b|island-preview|Now playing|正在播放/);
  assert.match(html,language==='en'?/Quick launch/:/快捷启动/);
 }
 setLanguage('zh-CN');
});
