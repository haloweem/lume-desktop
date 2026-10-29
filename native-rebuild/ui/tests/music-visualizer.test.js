import test from 'node:test';
import assert from 'node:assert/strict';

const moduleUrl = new URL('../music-visualizer.js', import.meta.url);
const lines = [0, 5, 10].map(startTime => ({startTime,endTime:startTime+5,fullText:`Line ${startTime}`,words:[]}));
const fixture = () => {
  const frames = new Map(), handlers = new Map(), mounts = [], disposed = [], states = [];
  let clock=0, nextId=1;
  const document = {hidden:false, addEventListener:(name,fn)=>handlers.set(name,fn),removeEventListener:name=>handlers.delete(name),
    defaultView:{performance:{now:()=>clock},requestAnimationFrame:fn=>{const id=nextId++;frames.set(id,fn);return id;},cancelAnimationFrame:id=>frames.delete(id)}};
  const host = {ownerDocument:document, dataset:{}, textContent:'', replaceChildren(){}};
  const load = async mode => (element,state) => {
    mounts.push([mode,element]); states.push(state);
    return {update:value=>states.push(value),dispose:()=>disposed.push(mode)};
  };
  return {document,host,load,frames,handlers,mounts,disposed,states,
    advance(ms){clock+=ms;const callbacks=[...frames.values()];frames.clear();callbacks.forEach(fn=>fn(clock));}};
};
const state = overrides => ({lines,current:{id:'one'},currentTime:1,duration:30,playing:true,visible:true,...overrides});

test('available lyric styles are registered separately from backgrounds', async () => {
  const {MUSIC_VISUALIZERS,resolveMusicVisualizer,activeLyricIndex}=await import(moduleUrl);
  assert.deepEqual(MUSIC_VISUALIZERS.map(item=>item.id),['partita','fume','sonnet','gallery','3d','lumiere','cadenza','tilt','cappella','diorama','pendolo','classic','tempera']);
  assert.equal(resolveMusicVisualizer('partita'),'partita');
  assert.equal(resolveMusicVisualizer('classic'),'classic');
  assert.equal(resolveMusicVisualizer('motif'),'partita');
  assert.equal(resolveMusicVisualizer('monet'),'partita');
  assert.equal(activeLyricIndex(lines,5),1);
  assert.equal(activeLyricIndex(lines,0),0);
  assert.equal(activeLyricIndex(lines,-1),-1);
  assert.equal(activeLyricIndex([],3),-1);
});

test('switching and disposing cancel old renderer work, hidden state stops the clock', async () => {
  const {mountMusicVisualizer}=await import(moduleUrl),f=fixture();
  const view=mountMusicVisualizer(f.host,{load:f.load});
  await view.update(state(),'3d');
  assert.equal(f.mounts.length,1);
  f.advance(100); assert.equal(f.states.at(-1).currentTime,1.1);
  await view.update(state({visible:false}),'3d');
  assert.equal(f.frames.size,0); assert.equal(f.states.at(-1).visible,false);
  await view.update(state(),'gallery');
  assert.deepEqual(f.disposed,['3d']); assert.equal(f.mounts.length,2);
  f.document.hidden=true;f.handlers.get('visibilitychange')();
  assert.equal(f.frames.size,0);assert.equal(f.states.at(-1).visible,false);
  view.dispose();view.dispose();
  assert.deepEqual(f.disposed,['3d','gallery']);assert.equal(f.handlers.size,0);
});

test('a slow previous import cannot remount after a style switch or teardown', async () => {
  const {mountMusicVisualizer}=await import(moduleUrl),f=fixture();
  let finish;
  const view=mountMusicVisualizer(f.host,{load:mode=>mode==='3d'?new Promise(resolve=>finish=resolve):f.load(mode)});
  const pending=view.update(state(),'3d');
  await view.update(state(),'gallery');
  finish(await f.load('3d'));await pending;
  assert.deepEqual(f.mounts.map(([mode])=>mode),['gallery']);
  view.dispose();assert.equal(f.frames.size,0);
});

test('empty 3D lyrics allocate no canvas and progress updates reuse the lyric array', async () => {
  const {mountMusicVisualizer}=await import(moduleUrl),f=fixture();
  const view=mountMusicVisualizer(f.host,{load:f.load});
  await view.update(state({lines:[]}),'3d'); assert.equal(f.mounts.length,0);
  await view.update(state(),'3d');
  for(let i=0;i<40;i++) await view.update(state({currentTime:i/10}),'3d');
  assert.equal(f.mounts.length,1);
  assert.equal(f.states.at(-1).lines,lines);
  view.dispose();
});

test('empty or stopped lyrics mount nothing and a removed style falls back', async () => {
  const {mountMusicVisualizer}=await import(moduleUrl),f=fixture();
  const view=mountMusicVisualizer(f.host,{load:f.load});
  await view.update(state({lines:[],current:null}),'classic');
  assert.equal(f.mounts.length,0);
  await view.update(state({lines:[],visible:false}),'cadenza');
  assert.equal(f.mounts.length,0);
  await view.update(state({lines:[],playing:false}),'tilt');
  assert.equal(f.mounts.length,0);
  assert.equal(f.host.dataset.visualizerState,'empty');
  await view.update(state({lines:[],playing:false}),'monet');
  assert.equal(f.mounts.length,0);
  assert.equal(f.host.dataset.visualizerMode,'partita');
  view.dispose();
  assert.equal(f.handlers.size,0);
});

test('3D star-river remains reachable without lyric lines while ordinary empty 3D stays inert', async () => {
  const {mountMusicVisualizer}=await import(moduleUrl),f=fixture();
  const view=mountMusicVisualizer(f.host,{load:f.load});
  await view.update(state({lines:[],preferences:{effectMode:'none'}}),'3d');
  assert.equal(f.mounts.length,0);
  await view.update(state({lines:[],preferences:{effectMode:'star-river'}}),'3d');
  assert.equal(f.mounts.length,1);
  assert.equal(f.mounts[0][0],'3d');
  view.dispose();
});

test('presentation updates retain the native clock anchor across themes and mode changes', async () => {
  const {mountMusicVisualizer}=await import(moduleUrl),f=fixture();
  const view=mountMusicVisualizer(f.host,{load:f.load});
  const current=state({currentTime:4.8});
  await view.update(current,'classic');
  f.advance(500);
  assert.equal(f.states.at(-1).currentTime,5.3);
  assert.equal(f.states.at(-1).activeLineIndex,1);
  await view.update({...current,preferences:{lyricsFontColor:'#ff2200'}},'classic',{syncClock:false});
  assert.equal(f.states.at(-1).currentTime,5.3,'a theme must not resubmit the stale native position');
  assert.equal(f.states.at(-1).activeLineIndex,1);
  f.advance(300);
  await view.update(current,'sonnet',{syncClock:false});
  assert.equal(f.states.at(-1).currentTime,5.6,'loading a different mode preserves elapsed projection');
  f.advance(500);
  assert.equal(f.states.at(-1).currentTime,5.8,'presentation cannot extend the one-second projection budget');
  await view.update(state({currentTime:2}),'sonnet');
  assert.equal(f.states.at(-1).currentTime,2,'native backward seek still resets the anchor');
  view.dispose();
});
