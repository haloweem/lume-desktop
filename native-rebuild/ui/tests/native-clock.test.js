import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge, validHostMessage} from '../bridge.js';
import {initialState, reduce, settingsNotice} from '../state.js';
import {clockPage} from '../clock.js';

test('native clock reset is one atomic command and host routing is allowlisted',()=>{
  const sent=[]; const bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(bridge.send('clock','reset'),true);
  assert.deepEqual(sent,['v1|clock|reset']);
  assert.equal(bridge.send('clock','reset','everything'),false);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'clock'}),true);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'file:///bad'}),false);
});
test('native runtime state and error survive acknowledgement without false success copy',()=>{
  const clock={enabled:true,style:'mond',running:false,scale:100,x:10,y:20,alpha:255,color:0xffffff,locked:false,customColor:false};
  const message={version:1,type:'settings',clock,saved:false,error:'runtime'};
  assert.equal(validHostMessage(message),true);
  const state=reduce(initialState(),{type:'host',message});
  assert.equal(state.error,'runtime');
  assert.match(clockPage(state),/桌面时钟启动失败/);
  assert.doesNotMatch(clockPage(state),/桌面显示尚未接入/);
  assert.equal(settingsNotice('saved',{previousStatus:'saving'}),'已应用并保存');
});
