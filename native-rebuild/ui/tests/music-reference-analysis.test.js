import test from 'node:test';
import assert from 'node:assert/strict';
import {analysisSnapshot,applyAnalysis,createStateReducer,EMPTY_ANALYSIS} from '../music-visualizers/reference/src/protocol.js';
import {mountReference} from '../music-visualizers/reference-host.js';

const sample=(energy=0.5)=>({energy,bins:Array.from({length:32},(_,index)=>index/31)});
const current={id:'fixture',source:'online:fixture',title:'Fixture'};

test('analysis snapshots are bounded, owned and atomic with the rest of the delta',()=>{
  const reducer=createStateReducer(lines=>lines);
  const input=sample();
  reducer.update({current,playing:true,analysis:input});
  const stable=reducer.state.analysis;
  assert.notEqual(stable,input);
  input.bins[0]=1;
  assert.equal(stable.bins[0],0);
  assert.throws(()=>{stable.bins[0]=1;},TypeError);
  assert.equal(analysisSnapshot(stable,sample()),stable);
  const sparse=Array(32);sparse[0]=0;
  for(const invalid of [{},[],true,{energy:0,bins:sparse},{energy:0,bins:new Uint8Array(32)},
    ...[-1,1.01,NaN,Infinity,'0.5'].map(energy=>({...sample(),energy})),
    ...[0,31,33,4096].map(length=>({energy:0,bins:Array(length).fill(0)})),
    ...[-1,1.01,NaN,Infinity,'0.5',null,undefined].map(value=>({energy:0,bins:[value,...Array(31).fill(0)]}))]){
    const before=reducer.state;
    assert.throws(()=>reducer.update({lines:[{fullText:'discard'}],currentTime:99,analysis:invalid}),TypeError);
    assert.equal(reducer.state,before);
  }
  assert.equal(reducer.update({lines:[{fullText:'discard'}]}).structural,true,'invalid delta did not commit its line key');
  assert.equal(analysisSnapshot(stable,null),EMPTY_ANALYSIS);
});

test('audio-only updates never root-render and clear on every playback lifetime boundary',()=>{
  const reducer=createStateReducer(lines=>lines);
  reducer.update({current,playing:true,analysis:sample()});
  for(let i=0;i<120;i++){
    assert.equal(reducer.update({currentTime:i/60,analysis:sample(i/120)}).structural,false);
    assert.equal(reducer.state.analysis.energy,i/120);
  }
  const last=reducer.state.analysis;
  reducer.update({current:{...current,title:'New metadata'}});
  assert.equal(reducer.state.analysis,last,'metadata is not a new track');
  for(const boundary of [{playing:false},{visible:false},{current:null},
    {current:{...current,id:'next'}},{current:{...current,source:'online:next'}}]){
    reducer.update({current,playing:true,visible:true});
    reducer.update({analysis:sample()});
    reducer.update({...boundary,analysis:sample(1)});
    assert.equal(reducer.state.analysis,EMPTY_ANALYSIS);
    reducer.update({current,playing:true,visible:true});
    reducer.update({currentTime:2});
    assert.equal(reducer.state.analysis,EMPTY_ANALYSIS,'clock/resume alone cannot resurrect samples');
    reducer.update({analysis:sample()});
    assert.equal(reducer.state.analysis.energy,0.5);
  }
  reducer.update({analysis:null});
  assert.equal(reducer.state.analysis,EMPTY_ANALYSIS);
});

test('the historical MotionValues receive deterministic scaled measurements, never random data',()=>{
  const value=()=>({set(next){this.current=next;}});
  const power=value(),bands=Object.fromEntries(['bass','lowMid','mid','vocal','treble','spectrum'].map(key=>[key,value()]));
  const identities=Object.values(bands);
  const analysis=sample();
  applyAnalysis(analysis,power,bands);
  assert.equal(power.current,127.5);
  for(const [key,start,end] of [['bass',0,1],['lowMid',1,2],['mid',2,4],['vocal',4,8],['treble',8,32]]){
    const mean=analysis.bins.slice(start,end).reduce((sum,bin)=>sum+bin,0)/(end-start);
    assert.equal(bands[key].current,mean*255);
  }
  assert.ok(bands.spectrum.current instanceof Uint8Array);
  assert.deepEqual([...bands.spectrum.current],analysis.bins.flatMap(bin=>Array(4).fill(Math.round(bin*255))));
  applyAnalysis(EMPTY_ANALYSIS,power,bands);
  assert.equal(power.current,0);
  for(const key of ['bass','lowMid','mid','vocal','treble'])assert.equal(bands[key].current,0);
  assert.deepEqual([...bands.spectrum.current],Array(128).fill(0));
  assert.deepEqual(Object.values(bands),identities);
});

test('the actual host bridge deduplicates samples, rejects foreign readiness and blocks stale samples',()=>{
  const listeners=new Map(),documentListeners=new Map(),messages=[];
  const window={addEventListener:(type,fn)=>listeners.set(type,fn),removeEventListener:type=>listeners.delete(type)};
  const frame={setAttribute(){},contentWindow:{postMessage:message=>messages.push(message)},remove(){this.removed=true;}};
  const document={defaultView:window,hidden:false,createElement:()=>frame,
    addEventListener:(type,fn)=>documentListeners.set(type,fn),removeEventListener:type=>documentListeners.delete(type)};
  const host={ownerDocument:document,replaceChildren(node){this.child=node;},dataset:{}};
  let state={lines:[],current,currentTime:0,duration:30,playing:true,visible:true,analysis:sample()};
  const mounted=mountReference(host,state,'monet');
  const event={source:frame.contentWindow,origin:new URL(frame.src).origin,data:{type:'lume-lyric-stage-ready'}};
  listeners.get('message')({...event,source:{}});
  listeners.get('message')({...event,origin:'https://invalid.example'});
  assert.equal(messages.length,0);
  listeners.get('message')(event);
  assert.deepEqual(messages.at(-1).state.analysis,state.analysis);
  const update=delta=>{state={...state,...delta};mounted.update(state);return messages.at(-1).state;};
  for(let i=0;i<120;i++)assert.equal(update({currentTime:i/60}).analysis,undefined);
  assert.equal(update({analysis:sample()}).analysis,undefined,'equal native samples are not retransmitted');
  assert.equal(update({analysis:sample(0.75)}).analysis.energy,0.75);
  assert.equal(update({current:{...current,title:'Metadata'}}).analysis,undefined);
  assert.deepEqual(update({playing:false}).analysis,EMPTY_ANALYSIS);
  assert.equal(update({playing:true}).analysis,undefined);
  assert.equal(update({analysis:sample(0.75)}).analysis.energy,0.75,'fresh native packet can resume');
  assert.deepEqual(update({visible:false}).analysis,EMPTY_ANALYSIS);
  update({analysis:sample(1)});
  assert.equal(update({visible:true}).analysis,undefined,'samples received while hidden remain blocked');
  assert.equal(update({analysis:sample(1)}).analysis.energy,1);
  document.hidden=true;documentListeners.get('visibilitychange')();
  assert.equal(messages.at(-1).state.visible,false);
  assert.deepEqual(messages.at(-1).state.analysis,EMPTY_ANALYSIS);
  document.hidden=false;documentListeners.get('visibilitychange')();
  assert.equal(messages.at(-1).state.analysis,undefined);
  assert.equal(update({analysis:sample(1)}).analysis.energy,1);
  assert.deepEqual(update({current:{...current,id:'next'}}).analysis,EMPTY_ANALYSIS);
  assert.equal(update({currentTime:0}).analysis,undefined);
  assert.equal(update({analysis:sample(1)}).analysis.energy,1);
  assert.deepEqual(update({current:{...current,id:'next',source:'online:other'}}).analysis,EMPTY_ANALYSIS);
  update({analysis:sample(0.25)});
  listeners.get('message')(event);
  assert.equal(messages.at(-1).state.analysis.energy,0.25,'new iframe realm receives the retained sample');
  assert.deepEqual(update({current:null}).analysis,EMPTY_ANALYSIS);
  assert.equal(update({current}).analysis,undefined);
  assert.equal(update({analysis:sample(0.25)}).analysis.energy,0.25);
  const receive=listeners.get('message'),publish=documentListeners.get('visibilitychange');
  mounted.dispose();const count=messages.length;
  mounted.update(state);receive(event);publish();mounted.dispose();
  assert.equal(messages.length,count);
  assert.equal(listeners.size,0);
  assert.equal(documentListeners.size,0);
  assert.equal(frame.removed,true);
});

test('playback boundaries invalidate samples even before iframe readiness',()=>{
  for(const boundary of ['paused','hidden','track','initially-paused']){
    const messages=[],listeners=new Map(),documentListeners=new Map();
    const window={addEventListener:(type,fn)=>listeners.set(type,fn),removeEventListener:type=>listeners.delete(type)};
    const frame={setAttribute(){},contentWindow:{postMessage:message=>messages.push(message)},remove(){}};
    const document={defaultView:window,hidden:false,createElement:()=>frame,
      addEventListener:(type,fn)=>documentListeners.set(type,fn),removeEventListener:type=>documentListeners.delete(type)};
    const host={ownerDocument:document,replaceChildren(){},dataset:{}};
    let state={current,lines:[],playing:boundary!=='initially-paused',visible:true,analysis:sample()};
    const mounted=mountReference(host,state,'monet');
    if(boundary==='paused')mounted.update({...state,playing:false});
    if(boundary==='hidden'){
      document.hidden=true;documentListeners.get('visibilitychange')();
      document.hidden=false;documentListeners.get('visibilitychange')();
    }
    if(boundary==='track')state={...state,current:{...current,id:'next'}};
    state={...state,playing:true};
    mounted.update(state);
    assert.equal(messages.length,0);
    listeners.get('message')({source:frame.contentWindow,origin:new URL(frame.src).origin,data:{type:'lume-lyric-stage-ready'}});
    assert.deepEqual(messages.at(-1).state.analysis,EMPTY_ANALYSIS,boundary);
    mounted.update({...state,analysis:sample()});
    assert.equal(messages.at(-1).state.analysis.energy,0.5);
    mounted.dispose();
  }
});
