import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {createMusicFontQuery} from '../music-font-query.js';

function fixture() {
  const listeners=new Set(),sent=[];
  const bridge=createBridge({
    postMessage:value=>sent.push(value),
    addEventListener:(_,listener)=>listeners.add(listener),
    removeEventListener:(_,listener)=>listeners.delete(listener),
  });
  return {sent,listeners,query:createMusicFontQuery(bridge,{timeout:40}),
    reply:(request,families=['Arial'],error='')=>{
      for(const listener of listeners)listener({data:{version:1,type:'music-fonts-state',request,families,error}});
    }};
}
test('font bridge is bounded and rejects paths, extra fields and untrusted commands',()=>{
  const bridge=createBridge({postMessage(){}});
  assert.equal(bridge.musicFonts('list',{request:1}),true);
  for(const [action,fields] of [['choose',{request:1}],['list',{request:0}],['list',{request:1,path:'C:/font.ttf'}]])
    assert.equal(bridge.musicFonts(action,fields),false);
  const state={version:1,type:'music-fonts-state',request:1,families:['Arial'],error:''};
  assert.equal(validHostMessage(state),true);
  for(const patch of [{families:['a'.repeat(129)]},{families:['bad\nname']},{families:Array(4097).fill('Arial')},
    {families:[{}]},{error:'exception'},{paths:['private']},{request:-1}])
    assert.equal(validHostMessage({...state,...patch}),false);
});
test('font query is lazy, fences stale responses and frees subscription on success',async()=>{
  const f=fixture();
  assert.equal(f.sent.length,0);assert.equal(f.listeners.size,0);
  const query=f.query();
  assert.deepEqual(f.sent,[{version:1,scope:'music-fonts',action:'list',request:1}]);
  f.reply(99,['ignored']);assert.equal(f.listeners.size,1);
  f.reply(1,['Arial','SimSun']);
  assert.deepEqual(await query,[{family:'Arial'},{family:'SimSun'}]);assert.equal(f.listeners.size,0);
});
test('font query abort, replacement, failure and timeout all release listeners',async()=>{
  const f=fixture(),abort=new AbortController();
  let result=assert.rejects(f.query({signal:abort.signal}),/cancelled/);
  abort.abort();await result;assert.equal(f.listeners.size,0);
  result=assert.rejects(f.query(),/cancelled/);
  const next=f.query();await result;
  f.reply(2,['stale']);f.reply(3,['Arial']);await next;
  result=assert.rejects(f.query(),/unavailable/);f.reply(4,[],'unavailable');await result;
  await assert.rejects(f.query(),/timeout/);assert.equal(f.listeners.size,0);
  const offline=createMusicFontQuery(createBridge(null));
  await assert.rejects(offline(),/unavailable/);
});
