import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {createMusicFontAssetQuery} from '../music-font-asset-query.js';
const id='a'.repeat(32);
const asset={id,family:`LumeImportedFont_${id}`,label:'Fixture Font',extension:'otf',size:100};
const state=(request=0,patch={})=>({version:1,type:'music-font-assets-state',request,error:'',assets:[asset],candidate:null,...patch});
function fixture(now=()=>0) {
  const listeners=new Set(),sent=[],assets=[];
  const bridge=createBridge({postMessage:value=>sent.push(value),
    addEventListener:(_,listener)=>listeners.add(listener),removeEventListener:(_,listener)=>listeners.delete(listener)});
  const query=createMusicFontAssetQuery(bridge,{onAssets:value=>assets.push(value),timeout:40,now});
  return {query,sent,listeners,assets,reply:value=>{for(const listener of listeners)listener({data:value});}};
}
test('font assets expose bounded immutable metadata, never caller URLs or paths',()=>{
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(validHostMessage(state()),true);
  assert.equal(validHostMessage(state(1,{candidate:asset})),true);
  for(const patch of [{assets:[{...asset,url:'https://evil.invalid'}]},{assets:[{...asset,id:'../file'}]},
    {assets:[{...asset,size:50*1024*1024+1}]},{assets:Array(9).fill(asset)},
    {candidate:{...asset,id:'b'.repeat(32)}},{assets:[{...asset,label:'bad\nname'}]},
    {error:'raw exception'},{path:'C:/secret'}])assert.equal(validHostMessage(state(1,patch)),false);
  assert.equal(bridge.musicFontAssets('choose',{request:1}),true);
  assert.equal(bridge.musicFontAssets('cancel',{request:1}),true);
  assert.equal(bridge.musicFontAssets('cancel',{request:0}),false);
  assert.equal(bridge.musicFontAssets('drop',{request:2,id}),true);
  assert.equal(bridge.musicFontAssets('choose',{request:1,path:'C:/font.ttf'}),false);
  assert.equal(bridge.musicFontAssets('drop',{request:1,id:'../file'}),false);
});
test('a recreated page uses fresh upload IDs and a backward clock stays monotonic within a page',async()=>{
  let now=100;
  const first=fixture(()=>now);
  const load=first.query.load();first.reply(state());await load;
  const choose=first.query.choose();await Promise.resolve();
  assert.equal(first.sent.at(-1).request,101);
  first.reply(state(101,{candidate:asset}));await choose;
  now=50;
  const next=first.query.choose();await Promise.resolve();
  assert.equal(first.sent.at(-1).request,102);
  first.reply(state(102,{error:'cancelled'}));await next;
  first.query.dispose();now=200;
  const reopened=fixture(()=>now);
  const restore=reopened.query.load();reopened.reply(state());await restore;
  const upload=reopened.query.choose();await Promise.resolve();
  assert.equal(reopened.sent.at(-1).request,201);
  reopened.reply(state(201,{candidate:asset}));await upload;
  reopened.query.dispose();
});
test('asset query loads only on demand, chooses correlated candidates and reports cancellation',async()=>{
  const f=fixture();
  assert.equal(f.sent.length,0);
  const load=f.query.load();assert.equal(f.query.load(),load);
  f.reply(state());await load;
  const choose=f.query.choose();await Promise.resolve();
  assert.equal(f.sent.at(-1).action,'choose');
  f.reply(state(9,{candidate:asset}));assert.equal(f.listeners.size,1);
  f.reply(state(1,{candidate:asset}));assert.deepEqual(await choose,asset);
  const cancelled=f.query.choose();await Promise.resolve();
  f.reply(state(2,{error:'cancelled'}));assert.equal(await cancelled,null);
  const drop=f.query.drop(id);await Promise.resolve();
  f.reply(state(3,{assets:[]}));await drop;
  assert.equal(f.listeners.size,0);assert.deepEqual(f.assets.at(-1),[]);
});
test('asset query abort/dispose/timeout clear subscriptions',async()=>{
  const f=fixture(),abort=new AbortController();
  const load=f.query.load();f.reply(state());await load;
  const result=assert.rejects(f.query.choose({signal:abort.signal}),/cancelled/);
  await Promise.resolve();abort.abort();await result;assert.equal(f.listeners.size,0);
  assert.deepEqual(f.sent.at(-1),{version:1,scope:'music-font-assets',action:'cancel',request:1});
  await assert.rejects(f.query.choose(),/load/);assert.equal(f.listeners.size,0);
  assert.equal(f.sent.at(-1).action,'cancel');
  const last=assert.rejects(f.query.choose(),/cancelled/);await Promise.resolve();
  f.query.dispose();await last;assert.equal(f.listeners.size,0);
  await assert.rejects(f.query.load(),/cancelled/);
});
