import test from 'node:test';
import assert from 'node:assert/strict';
import {createMusicFontAssets,createMusicFontGate,isMusicImportedFontFamily,validMusicFontLabel} from '../music-font-assets.js';
import {mountMusicVisualizer} from '../music-visualizer.js';
import {mountReference} from '../music-visualizers/reference-host.js';
import {DEFAULT_MUSIC_PREFERENCES} from '../music-preference-values.js';

const asset=value=>{const id=value.padStart(32,'0');return {id,family:`LumeImportedFont_${id}`,extension:'woff2',size:4};};
const response=()=>new Response(new Uint8Array(4));
const turn=()=>new Promise(resolve=>setImmediate(resolve));
function documentFixture(load=async face=>face) {
  const faces=new Set();
  return {fonts:faces,defaultView:{FontFace:class {
    constructor(family,bytes){this.family=family;this.bytes=bytes;}
    load(){return load(this);}
  }}};
}

test('labels use the existing asset map, remain bounded and never expose mutable metadata',()=>{
  const manager=createMusicFontAssets({fetch:()=>{throw new Error('display never fetches');}});
  const font={...asset('a'),label:'原始字体名'};
  manager.setAssets([font]);
  const {labelFor,listAssets}=manager;
  assert.equal(labelFor(font.family),'原始字体名');
  assert.equal(labelFor('Arial'),undefined);
  assert.deepEqual(listAssets(),[font]);
  font.label='changed input';
  const listed=listAssets();listed[0].label='changed output';listed[0].id='bad';listed.length=0;
  assert.equal(labelFor(font.family),'原始字体名');
  assert.equal(listAssets()[0].id,font.id);
  for(const label of [undefined,null,42,'',' ','a'.repeat(129),'字'.repeat(43),'\ud800','bad\nname',
    'bad\u007fname','bad\u0085name','bad/name','bad\\name','bad:name']) {
    assert.equal(validMusicFontLabel(label),false);
    manager.setAssets([{...font,label}]);
    assert.equal(labelFor(font.family),undefined);
    assert.equal(Object.hasOwn(listAssets()[0],'label'),false);
  }
  for(const label of ['a'.repeat(128),'字'.repeat(42)+'ab','𝄞'.repeat(32),'<img onerror=alert(1)>']) {
    assert.equal(validMusicFontLabel(label),true);
    manager.setAssets([{...font,label}]);
    assert.equal(labelFor(font.family),label);
  }
  manager.setAssets([]);assert.equal(labelFor(font.family),undefined);assert.deepEqual(listAssets(),[]);
  manager.setAssets([{...font,label:'Restored'}]);manager.dispose();
  assert.equal(labelFor(font.family),undefined);assert.deepEqual(listAssets(),[]);
});

test('metadata defines only bounded immutable local assets, never arbitrary fetch URLs',async()=>{
  const urls=[],manager=createMusicFontAssets({fetch:async(url,options)=>{urls.push([url,options]);return response();}});
  const good=asset('abc123');
  manager.setAssets([good]);
  for(const bad of [{...good,id:'../abc123'},{...good,family:'Arial'},{...good,extension:'svg'},
    {...good,size:0},{...good,size:50*1024*1024+1},{...good,size:2.5},
    {...good,size:5},{...good,extension:'ttf'}])
    assert.throws(()=>manager.setAssets([bad]),TypeError);
  assert.throws(()=>manager.setAssets([good,good]),TypeError);
  assert.throws(()=>manager.setAssets(Array(9).fill(good)),TypeError);
  assert.throws(()=>manager.setAssets(['a','b','c','d','e'].map(id=>({...asset(id),size:50*1024*1024}))),/storage limit/);
  const document=documentFixture();
  await manager.ensureFont(document,'Arial');
  assert.equal(urls.length,0);
  await assert.rejects(manager.ensureFont(document,asset('de').family),/unavailable/);
  await manager.ensureFont(document,good.family);
  assert.equal(urls[0][0],`https://lume-fonts.local/${good.id}.woff2`);
  assert.equal(urls[0][1].redirect,'error');
  assert.equal(document.fonts.size,1);
  manager.dispose();
  assert.equal(document.fonts.size,0);
  await assert.rejects(manager.ensureFont(document,good.family),{name:'AbortError'});
  assert.equal(isMusicImportedFontFamily(good.family),true);
  assert.equal(isMusicImportedFontFamily('LumeImportedFont_abc'),false);
  assert.equal(isMusicImportedFontFamily('LumeImportedFont_abc/evil'),false);
});

test('each document owns its face; concurrent leases share decoding and binary fetch',async()=>{
  let requests=0,decodes=0;
  const manager=createMusicFontAssets({fetch:async()=>{requests++;return response();}});
  const a=documentFixture(async face=>{decodes++;return face;}),b=documentFixture();
  const font=asset('aa'),first=new AbortController(),second=new AbortController(),child=new AbortController();
  manager.setAssets([font]);
  await Promise.all([
    manager.ensureFont(a,font.family,{signal:first.signal}),
    manager.ensureFont(a,font.family,{signal:second.signal}),
    manager.ensureFont(b,font.family,{signal:child.signal}),
  ]);
  assert.equal(requests,1);assert.equal(decodes,1);
  assert.equal(a.fonts.size,1);assert.equal(b.fonts.size,1);
  assert.notEqual([...a.fonts][0],[...b.fonts][0]);
  first.abort();assert.equal(a.fonts.size,1);
  second.abort();assert.equal(a.fonts.size,0);assert.equal(b.fonts.size,1);
  manager.releaseDocument(b);assert.equal(b.fonts.size,0);
  manager.dispose();
});

test('late fetch/decode cannot register after cancellation, and failures are retryable',async()=>{
  const font=asset('bb'),document=documentFixture();
  let finish,fetchSignal;
  const manager=createMusicFontAssets({fetch:(_,options)=>{
    fetchSignal=options.signal;return new Promise(resolve=>finish=resolve);
  }});
  manager.setAssets([font]);
  const controller=new AbortController();
  const pending=manager.ensureFont(document,font.family,{signal:controller.signal});
  const rejected=assert.rejects(pending,{name:'AbortError'});
  controller.abort();await rejected;
  assert.equal(fetchSignal.aborted,true);
  finish(response());await turn();
  assert.equal(document.fonts.size,0);
  manager.dispose();

  let decoded,attempt=0;
  const other=documentFixture(face=>++attempt===1?Promise.reject(new Error('bad font')):
    new Promise(resolve=>decoded=()=>resolve(face)));
  const retry=createMusicFontAssets({fetch:async()=>response()});
  retry.setAssets([font]);
  await assert.rejects(retry.ensureFont(other,font.family),/bad font/);
  const next=retry.ensureFont(other,font.family);
  const cancelled=assert.rejects(next,{name:'AbortError'});
  await turn();retry.releaseDocument(other);decoded();await cancelled;await turn();
  assert.equal(other.fonts.size,0);
  retry.dispose();
});

test('raw bytes are dropped after decode and streamed bounds reject wrong-sized responses',async()=>{
  const requested=[],manager=createMusicFontAssets({fetch:async url=>{requested.push(url);return response();}});
  const fonts=['a','b','c'].map(asset),document=documentFixture();
  manager.setAssets(fonts);
  for(const font of fonts) {
    await manager.ensureFont(document,font.family);
    manager.releaseDocument(document);
  }
  await manager.ensureFont(document,fonts[0].family);
  assert.equal(requested.length,4,'released documents refetch instead of retaining raw binaries');
  manager.releaseDocument(document);
  await manager.ensureFont(document,fonts[0].family);
  assert.equal(requested.length,5,'even the latest binary is dropped once decoding finishes');
  manager.dispose();
  for(const bad of [
    {ok:false},
    {ok:true,headers:{get:()=>String(51*1024*1024)}},
    new Response(new Uint8Array(5)),
    new Response(new Uint8Array(3)),
  ]) {
    const other=createMusicFontAssets({fetch:async()=>bad});
    other.setAssets([fonts[0]]);
    await assert.rejects(other.ensureFont(document,fonts[0].family));
    assert.equal(document.fonts.size,0);
    other.dispose();
  }
});

test('stream overflow cancels the reader before accepting a second oversized chunk',async()=>{
  let cancelled=false;
  const manager=createMusicFontAssets({fetch:async()=>new Response(new ReadableStream({
    start(controller){controller.enqueue(new Uint8Array(3));controller.enqueue(new Uint8Array(3));},
    cancel(){cancelled=true;},
  }))});
  manager.setAssets([asset('a')]);
  await assert.rejects(manager.ensureFont(documentFixture(),asset('a').family),/size mismatch/);
  assert.equal(cancelled,true);
  manager.dispose();
});

test('aborting a blocked stream cancels its reader and releases the document immediately',async()=>{
  let cancelled=false;
  const manager=createMusicFontAssets({fetch:async()=>new Response(new ReadableStream({
    cancel(){cancelled=true;},
  }))});
  manager.setAssets([asset('d')]);
  const controller=new AbortController(),document=documentFixture();
  const result=manager.ensureFont(document,asset('d').family,{signal:controller.signal});
  const rejection=assert.rejects(result,{name:'AbortError'});
  await turn();controller.abort();await rejection;await turn();
  assert.equal(cancelled,true);assert.equal(document.fonts.size,0);
  manager.dispose();
});

test('no more than two families can hold pending binary reads',async()=>{
  const requests=[];
  const manager=createMusicFontAssets({fetch:async(url,{signal})=>{
    requests.push({url,signal});
    return new Response(new ReadableStream());
  }});
  const fonts=['a','b','c'].map(asset);
  manager.setAssets(fonts);
  const a=manager.ensureFont(documentFixture(),fonts[0].family);
  const cancelledA=assert.rejects(a,{name:'AbortError'});
  const b=manager.ensureFont(documentFixture(),fonts[1].family);
  const cancelledB=assert.rejects(b,{name:'AbortError'});
  await turn();
  const c=manager.ensureFont(documentFixture(),fonts[2].family);
  const cancelledC=assert.rejects(c,{name:'AbortError'});
  await turn();await cancelledA;
  assert.equal(requests[0].signal.aborted,true);
  assert.equal(requests.filter(request=>!request.signal.aborted).length,2);
  manager.dispose();await Promise.all([cancelledB,cancelledC]);
});

test('candidate lease hands off synchronously to a gate; hide releases and show reloads',async()=>{
  let requests=0,decodes=0;
  const manager=createMusicFontAssets({fetch:async()=>{requests++;return response();}});
  const document=documentFixture(async face=>{decodes++;return face;});
  const font=asset('a'),candidate=new AbortController();
  manager.setAssets([font]);
  await manager.ensureFont(document,font.family,{signal:candidate.signal});
  const gate=createMusicFontGate(document,{ensureFont:manager.ensureFont});
  const preferences={fontFamily:font.family};
  gate.preferences(preferences);
  candidate.abort();
  assert.equal(document.fonts.size,1,'committing before picker close preserves decoded face');
  await turn();
  assert.equal(gate.preferences(preferences),preferences);
  assert.equal(requests,1);assert.equal(decodes,1);
  gate.preferences(preferences,{visible:false});
  assert.equal(document.fonts.size,0);
  assert.equal(gate.preferences(preferences,{visible:false}).fontFamily,'');
  await turn();assert.equal(requests,1,'hidden views never enumerate or fetch fonts');
  assert.equal(gate.preferences(preferences).fontFamily,'');
  await turn();
  assert.equal(gate.preferences(preferences),preferences);
  assert.equal(requests,2,'resume refetches instead of retaining raw bytes while hidden');
  gate.dispose();manager.dispose();
});

test('gate retains old family, supersedes loads, applies latest preferences and owns leases',async()=>{
  const pending=[],errors=[],ready=[];
  const gate=createMusicFontGate({},{
    ensureFont:(_,family,{signal})=>new Promise((resolve,reject)=>pending.push({family,signal,resolve,reject})),
    onReady:()=>ready.push(true),onError:error=>errors.push(error.message),
  });
  assert.equal(gate.preferences({fontFamily:'Arial'}).fontFamily,'Arial');
  const a=asset('a').family,b=asset('b').family;
  assert.equal(gate.preferences({fontFamily:a}).fontFamily,'Arial');
  await turn();
  const current={fontFamily:b,lyricsFontColor:'#ff0000'};
  assert.deepEqual(gate.preferences(current),{...current,fontFamily:'Arial'});
  assert.equal(pending[0].signal.aborted,true);
  await turn();pending[0].resolve();await turn();assert.equal(ready.length,0);
  pending[1].resolve();await turn();
  assert.equal(gate.preferences(current),current);
  assert.equal(ready.length,1);
  gate.preferences({fontFamily:a});await turn();
  assert.equal(pending[1].signal.aborted,false,'old ready face stays while next family loads');
  pending[2].reject(new Error('decode'));await turn();
  assert.deepEqual(errors,['decode']);
  assert.equal(gate.preferences({fontFamily:a}).fontFamily,b);
  gate.preferences({fontFamily:''});
  assert.equal(pending[1].signal.aborted,true);
  gate.dispose();assert.equal(pending.every(item=>item.signal.aborted),true);
});

test('visualizer font readiness never resets the clock, mounts twice or applies an unresolved alias',async()=>{
  let clock=0,resolveFont,signal,mounts=0;
  const frames=new Map(),states=[],errors=[];
  const document={hidden:false,addEventListener(){},removeEventListener(){},
    defaultView:{performance:{now:()=>clock},requestAnimationFrame:fn=>{frames.set(1,fn);return 1;},
      cancelAnimationFrame:id=>frames.delete(id)}};
  const host={ownerDocument:document,dataset:{},replaceChildren(){}};
  const controller=mountMusicVisualizer(host,{
    ensureFont:(_,family,options)=>{signal=options.signal;return new Promise(resolve=>resolveFont=resolve);},
    onError:error=>errors.push(error),
    load:async()=>(host,state)=>{mounts++;states.push(state);return {update:state=>states.push(state),dispose(){}};},
  });
  const state={current:{id:'track'},lines:[{startTime:0,endTime:10}],currentTime:4,duration:30,
    playing:true,visible:true,preferences:{fontFamily:'Arial'}};
  await controller.update(state,'3d');
  clock=500;
  const family=asset('c').family;
  await controller.update({...state,preferences:{fontFamily:family}},'3d',{syncClock:false});
  await turn();
  assert.equal(states.at(-1).preferences.fontFamily,'Arial');
  assert.equal(states.at(-1).currentTime,4.5);
  clock=800;
  await controller.update({...state,preferences:{fontFamily:family,lyricsFontColor:'#ff0000'}},'3d',{syncClock:false});
  resolveFont();await turn();
  assert.equal(states.at(-1).preferences.fontFamily,family);
  assert.equal(states.at(-1).preferences.lyricsFontColor,'#ff0000');
  assert.equal(states.at(-1).currentTime,4.8);
  assert.equal(mounts,1);assert.deepEqual(errors,[]);
  controller.dispose();assert.equal(signal.aborted,true);
});

test('reference font readiness publishes latest state in place and discards completion after teardown',async()=>{
  const listeners=new Map(),messages=[],pending=[];
  const window={addEventListener:(name,fn)=>listeners.set(name,fn),removeEventListener:name=>listeners.delete(name)};
  const child=documentFixture();
  const frame={contentDocument:child,setAttribute(){},remove(){},
    contentWindow:{postMessage:message=>messages.push(message)}};
  const host={ownerDocument:{defaultView:window,createElement:()=>frame},dataset:{},replaceChildren(){}};
  const font=asset('a'),initial={lines:[],current:null,currentTime:0,visible:true,
    preferences:{...DEFAULT_MUSIC_PREFERENCES,fontFamily:'Arial'}};
  const controller=mountReference(host,initial,'sonnet',{
    ensureFont:(document,family,{signal})=>new Promise(resolve=>pending.push({document,family,signal,resolve})),
  });
  listeners.get('message')({source:frame.contentWindow,origin:new URL(frame.src).origin,
    data:{type:'lume-lyric-stage-ready'}});
  const preferences={...initial.preferences,fontFamily:font.family};
  controller.update({...initial,currentTime:10,preferences});
  assert.equal(messages.at(-1).state.preferences,undefined,'old family stays applied during decode');
  assert.equal(pending[0].document,child);
  controller.update({...initial,currentTime:20,preferences:{...preferences,lyricsFontColor:'#ff0000'}});
  assert.equal(messages.at(-1).state.preferences.fontFamily,'Arial');
  pending[0].resolve();await turn();
  assert.equal(messages.at(-1).state.currentTime,20);
  assert.equal(messages.at(-1).state.preferences.fontFamily,font.family);
  assert.equal(messages.at(-1).state.preferences.lyricsFontColor,'#ff0000');
  controller.update({...initial,preferences:{...preferences,fontFamily:asset('b').family}});
  const count=messages.length;
  controller.dispose();pending[1].resolve();await turn();
  assert.equal(messages.length,count);
  assert.equal(pending.every(item=>item.signal.aborted),true);
});
