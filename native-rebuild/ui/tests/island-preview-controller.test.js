import test from 'node:test';
import assert from 'node:assert/strict';
import {createIslandPreviewController} from '../island-preview-controller.js';
import {validHostMessage,createBridge} from '../bridge.js';
import {initialState,reduce} from '../state.js';
test('sample preview renders only on changes, serializes requests and releases on exit',()=>{
  const commands=[],frames=[],timers=new Map();let id=0,mode='compact';
  const controller=createIslandPreviewController({send:m=>{commands.push(m);return true;},getMode:()=>mode,
    onFrame:m=>frames.push(m),setTimer:fn=>{timers.set(++id,fn);return id;},clearTimer:id=>timers.delete(id)});
  const tick=()=>{const [key,fn]=timers.entries().next().value;timers.delete(key);fn();};
  controller.update(true);controller.update(true);controller.refresh();
  assert.deepEqual(commands,['compact']);assert.equal(timers.size,1);
  controller.receive({image:'first'});assert.equal(frames.length,1);assert.equal(timers.size,1);
  mode='expanded';tick();assert.deepEqual(commands,['compact','expanded']);
  controller.receive({image:'second'});assert.equal(timers.size,0,'no polling after a frame');
  controller.refresh();tick();assert.equal(commands.length,3);
  controller.update(false);assert.equal(timers.size,0);assert.equal(commands.at(-1),'stop');
  controller.receive({image:'late'});assert.equal(frames.length,2);
  controller.update(true);assert.equal(commands.at(-1),'expanded');
  controller.dispose();assert.equal(commands.at(-1),'stop');assert.equal(timers.size,0);
  controller.update(true);assert.equal(timers.size,0);
});
test('desktop expansion and playback do not change the selected preview mode',()=>{
  const state=initialState();
  assert.equal(state.dynamicIslandPreviewMode,'compact');
  const message={version:1,type:'dynamicIsland-state',...state.dynamicIsland,expanded:true,
    loadStatus:'loaded',result:'snapshot',error:'',saved:false};
  assert.equal(reduce(state,{type:'host',message}).dynamicIslandPreviewMode,'compact');
});
test('preview follows native transition frames and stops as soon as the shape settles',()=>{
  const commands=[],timers=new Map();let id=0;
  const controller=createIslandPreviewController({send:mode=>{commands.push(mode);return true;},getMode:()=> 'expanded',
    onFrame:()=>{},setTimer:fn=>{timers.set(++id,fn);return id;},clearTimer:key=>timers.delete(key)});
  controller.update(true);
  controller.receive({animating:true,error:''});assert.equal(timers.size,1);
  const [key,tick]=timers.entries().next().value;timers.delete(key);tick();
  assert.equal(commands.length,2);
  controller.receive({animating:false,error:''});assert.equal(timers.size,0);
  controller.update(false);assert.equal(commands.at(-1),'stop');
});
test('native preview protocol permits bounded image frames and exact modes',()=>{
  const frame={version:1,type:'dynamicIsland-preview',mode:'compact',shape:'standard',material:'liquid-glass',size:'standard',width:960,height:300,image:'data:image/png;base64,YQ==',error:''};
  assert.ok(validHostMessage(frame));
  assert.ok(validHostMessage({...frame,image:'data:image/jpeg;base64,YQ=='}));
  assert.ok(validHostMessage({...frame,animating:true}));
  assert.equal(validHostMessage({...frame,animating:'true'}),false);
  for(const patch of [{width:20000},{image:'https://example.com/a.png'},{image:'data:image/svg+xml,abc'},{mode:'anything'},{extra:true},{image:'a'.repeat(2800001)}])
    assert.equal(validHostMessage({...frame,...patch}),false);
  assert.equal(reduce(initialState(),{type:'host',message:frame}).dynamicIslandPreview,frame);
  const sent=[],bridge=createBridge({postMessage:m=>sent.push(m)});
  for(const value of ['compact','expanded','stop'])assert.ok(bridge.send('dynamicIsland','preview',value));
  assert.equal(bridge.send('dynamicIsland','preview','start'),false);
  assert.equal(sent.length,3);
});
