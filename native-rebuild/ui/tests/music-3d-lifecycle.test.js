import test from 'node:test';
import assert from 'node:assert/strict';
import {modules, environment, stateAt, resources} from './music-3d-harness.js';
import {DEFAULT_MUSIC_PREFERENCES} from '../music-preference-values.js';

test('star river shares the 3d scene without recreating lyric textures or a second renderer',async t=>{
  const f=environment(t),{entry}=await modules();
  const preferences={...DEFAULT_MUSIC_PREFERENCES,mode:'3d'};
  const mounted=entry.mountThreeD(f.host,stateAt(0,{preferences,playing:false}));f.step();
  const renderer=f.renderers[0],root=renderer.scene.children[0],lyrics=root.children[0];
  mounted.update(stateAt(0,{preferences:{...preferences,effectMode:'star-river'},playing:false}));f.step();
  assert.equal(f.renderers.length,1);
  assert.equal(root.children[0],lyrics,'effect does not rebuild unchanged text textures');
  assert.ok(renderer.scene.children.some(node=>node.name==='mineradio-star-river'));
  mounted.update(stateAt(0,{preferences:{...preferences,effectMode:'snow'},playing:false}));f.step();
  assert.equal(renderer.scene.children.some(node=>node.name==='mineradio-star-river'),false);
  assert.equal(root.children[0],lyrics);
  mounted.dispose();assert.equal(f.frames.size,0);assert.equal(renderer.contextLost,1);
});

test('hiding 3d releases the shared star river while keeping the lyric renderer recoverable',async t=>{
  const f=environment(t),{entry}=await modules();
  const preferences={...DEFAULT_MUSIC_PREFERENCES,mode:'3d',effectMode:'star-river'};
  const mounted=entry.mountThreeD(f.host,stateAt(0,{preferences,playing:false}));f.step();
  const renderer=f.renderers[0],star=renderer.scene.children.find(node=>node.name==='mineradio-star-river');
  assert.ok(star);
  const owned=resources(star),released=new Set();
  owned.forEach(item=>item.addEventListener('dispose',()=>released.add(item)));
  mounted.update(stateAt(0,{preferences,playing:false,visible:false}));f.step();
  assert.equal(renderer.scene.children.includes(star),false);
  assert.equal(released.size,owned.length);
  assert.equal(f.frames.size,0);
  const lyrics=renderer.scene.children[0].children[0];
  mounted.update(stateAt(0,{preferences,playing:false,visible:true}));f.step();
  assert.ok(renderer.scene.children.some(node=>node.name==='mineradio-star-river'));
  assert.equal(f.renderers.length,1);
  assert.equal(renderer.scene.children[0].children[0],lyrics);
  f.document.hidden=true;f.document.fire('visibilitychange');
  assert.equal(renderer.scene.children.some(node=>node.name==='mineradio-star-river'),false);
  mounted.update(stateAt(0,{preferences:{...preferences,effectMode:'none'},playing:false}));f.step();
  assert.equal(f.frames.size,0);
  f.document.hidden=false;f.document.fire('visibilitychange');f.step();
  assert.equal(renderer.scene.children.some(node=>node.name==='mineradio-star-river'),false);
  mounted.dispose();
});

test('preferences refresh textures and reset fonts without replacing the renderer or camera',async t=>{
  const f=environment(t),{entry}=await modules();
  const defaults={version:1,mode:'3d',fontFamily:'',animationIntensity:'normal',lyricsFontColor:'#ffffff',lyricsKaraokeFillColor:'#ffffff'};
  const mounted=entry.mountThreeD(f.host,stateAt(0,{playing:false}));f.step();
  const renderer=f.renderers[0],camera=renderer.camera,root=renderer.scene.children[0],old=root.children[0];
  assert.ok(old.children.find(node=>node.material?.map?.image?.height===270).material.map.image.getContext('2d').inks.includes('#ffffff'),
    'default 3D theme includes the original stored-default white lyric override');
  const owned=resources(old),released=new Set();owned.forEach(item=>item.addEventListener('dispose',()=>released.add(item)));
  root.rotation.y=.4;
  const preferences={...defaults,fontFamily:'A"B\\C, D',lyricsFontColor:'#12ab34',lyricsKaraokeFillColor:'#ab1234'};
  mounted.update(stateAt(0,{playing:false,preferences}));
  assert.equal(f.renderers.length,1);assert.equal(renderer.camera,camera);assert.equal(root.rotation.y,.4);
  assert.equal(released.size,owned.length);assert.equal(old.parent,null);
  const current=root.children[0],text=current.children.find(node=>node.material?.map?.image?.height===270).material.map.image;
  assert.ok(text.getContext('2d').font.includes('"A\\"B\\\\C, D", '));
  assert.ok(text.getContext('2d').inks.includes('#12ab34'));
  assert.ok(text.getContext('2d').inks.includes('#ab1234'));
  mounted.update(stateAt(0,{playing:false,preferences:{...preferences}}));
  assert.equal(root.children[0],current,'equivalent preferences retain textures');
  mounted.update(stateAt(0,{playing:false}));assert.equal(root.children[0],current);
  assert.throws(()=>mounted.update(stateAt(0,{preferences:{...preferences,fontFamily:'bad\nfont'}})),TypeError);
  assert.equal(root.children[0],current);
  mounted.update(stateAt(0,{playing:false,preferences:defaults}));
  const reset=root.children[0].children.find(node=>node.material?.map?.image?.height===270).material.map.image;
  assert.equal(reset.getContext('2d').font,'750 102px system-ui, sans-serif');
  assert.ok(reset.getContext('2d').inks.includes('#ffffff'));
  mounted.dispose();assert.equal(renderer.disposed,1);assert.equal(f.document.listeners.size,0);
});

test('import is inert; mount alone creates one transparent WebGL renderer', async t => {
  const f = environment(t), {entry} = await modules();
  assert.deepEqual(Object.keys(entry), ['mountThreeD']);
  assert.equal(f.renderers.length, 0);
  const mounted = entry.mountThreeD(f.host, stateAt());
  const renderer = f.renderers[0];
  assert.equal(f.renderers.length, 1);
  assert.deepEqual(renderer.options, {alpha: true, antialias: true, powerPreference: 'high-performance'});
  assert.equal(renderer.ratio, 1.5); assert.deepEqual(renderer.clear, [0, 0]);
  f.step();
  assert.equal(renderer.camera.fov, 44); assert.equal(renderer.camera.far, 300);
  assert.equal(renderer.scene.children[0].children.length, 6);
  assert.equal(renderer.scene.children.length, 1, 'default effect is none');
  mounted.dispose();
});

test('hidden mount and updates own no RAF; resize and pointer events cannot restart it', async t => {
  const {entry} = await modules(), f = environment(t);
  const mounted = entry.mountThreeD(f.host, stateAt(0, {visible: false}));
  assert.equal(f.frames.size, 0);
  mounted.update(stateAt(20)); f.step();
  const renderer = f.renderers[0], canvas = renderer.domElement;
  canvas.fire('pointerdown', {button: 0, pointerId: 1, clientX: 1, clientY: 1});
  mounted.update(stateAt(20, {visible: false}));
  assert.equal(canvas.captured, null);
  const renders = renderer.renders;
  mounted.update(stateAt(300, {visible: false}));
  f.observers[0].callback();
  canvas.fire('wheel', {deltaY: 120});
  canvas.fire('pointermove', {pointerId: 1, clientX: 50, clientY: 50}); f.step();
  assert.equal(f.frames.size, 0); assert.equal(renderer.renders, renders);
  mounted.update(stateAt(300)); f.step();
  assert.ok(renderer.renders > renders); assert.equal(f.renderers.length, 1);
  mounted.dispose();
});

test('document visibility cancels RAF and respects the host visible flag', async t => {
  const {entry} = await modules(), f = environment(t);
  const mounted = entry.mountThreeD(f.host, stateAt());
  f.document.hidden = true; f.document.fire('visibilitychange');
  assert.equal(f.frames.size, 0);
  mounted.update(stateAt(1)); assert.equal(f.frames.size, 0);
  f.document.hidden = false; f.document.fire('visibilitychange'); assert.equal(f.frames.size, 1);
  mounted.update(stateAt(1, {visible: false})); f.document.fire('visibilitychange');
  assert.equal(f.frames.size, 0);
  mounted.dispose();
});

test('dispose releases resources, listeners, canvas and context exactly once', async t => {
  const {entry} = await modules(), f = environment(t);
  const mounted = entry.mountThreeD(f.host, stateAt(20)); f.step();
  const renderer = f.renderers[0], canvas = renderer.domElement;
  const owned = resources(renderer.scene), counts = new Map();
  owned.forEach(resource => resource.addEventListener('dispose', () => counts.set(resource, (counts.get(resource) ?? 0) + 1)));
  const images = new Set(owned.filter(item => item.image).map(item => item.image));
  canvas.fire('pointerdown', {button: 0, pointerId: 1, clientX: 0, clientY: 0});
  mounted.dispose(); mounted.dispose(); mounted.update(stateAt(500));
  f.observers[0].callback(); f.document.fire('visibilitychange'); f.step();
  assert.ok(owned.length > 0); assert.ok(owned.every(resource => counts.get(resource) === 1));
  assert.ok([...images].every(image => image.width === 1 && image.height === 1));
  assert.equal(renderer.disposed, 1); assert.equal(renderer.contextLost, 1);
  assert.equal(canvas.captured, null); assert.equal(canvas.listeners.size, 0);
  assert.equal(f.document.listeners.size, 0); assert.equal(f.observers[0].disconnected, true);
  assert.equal(f.host.children.length, 0); assert.equal(f.frames.size, 0);
});

test('paused stage redraws seconds and keeps drag, inertia, pan and zoom', async t => {
  const {entry} = await modules(), f = environment(t);
  const mounted = entry.mountThreeD(f.host, stateAt(0, {playing: false})); f.step();
  const renderer = f.renderers[0], canvas = renderer.domElement, root = renderer.scene.children[0];
  assert.equal(f.frames.size, 0);
  const textureCanvas = root.children[0].children.find(node => node.material?.map?.image?.height === 270).material.map.image;
  const paints = textureCanvas.getContext('2d').paints;
  mounted.update(stateAt(0, {playing: false, currentTime: 2})); f.step(40);
  assert.ok(textureCanvas.getContext('2d').paints > paints);
  canvas.fire('pointerdown', {button: 0, pointerId: 1, clientX: 0, clientY: 0});
  canvas.fire('pointermove', {pointerId: 1, clientX: 100, clientY: -50}); f.step();
  assert.ok(root.rotation.y > 0); assert.ok(root.rotation.x < 0);
  canvas.fire('pointerup', {pointerId: 1});
  const rotation = root.rotation.y; f.step(); assert.ok(root.rotation.y > rotation);
  canvas.fire('pointerdown', {button: 1, pointerId: 2, clientX: 0, clientY: 0});
  canvas.fire('pointermove', {pointerId: 2, clientX: 100, clientY: 100});
  canvas.fire('pointerup', {pointerId: 2}); f.step();
  assert.ok(root.position.x > 0); assert.ok(root.position.y < 0);
  canvas.fire('wheel', {deltaY: -120}); f.step(); assert.ok(renderer.camera.position.length() < 16);
  canvas.fire('dblclick');
  for (let i = 0; i < 500 && f.frames.size; i++) f.step();
  assert.equal(f.frames.size, 0);
  assert.ok(Math.abs(root.rotation.x) < 0.0002 && Math.abs(root.rotation.y) < 0.0002);
  mounted.dispose();
});

test('window eviction and seeks release old resources but preserve nearby rows', async t => {
  const {entry} = await modules(), f = environment(t);
  const mounted = entry.mountThreeD(f.host, stateAt()); f.step();
  const root = f.renderers[0].scene.children[0], first = root.children[0], nearby = root.children[2];
  const owned = resources(first), disposed = new Set();
  owned.forEach(item => item.addEventListener('dispose', () => disposed.add(item)));
  mounted.update(stateAt(1)); assert.ok(root.children.includes(nearby)); assert.equal(disposed.size, 0);
  mounted.update(stateAt(6)); assert.equal(disposed.size, owned.length); assert.equal(first.parent, null);
  for (const index of [800, 20, 999, 0, 600, 5, 800, 0]) {
    mounted.update(stateAt(index)); f.step();
    assert.ok(root.children.length <= 11);
    assert.ok(root.children.filter(child => child.visible).length <= 5);
  }
  mounted.update(stateAt(-1, {lines: []})); f.step(); assert.equal(root.children.length, 0);
  mounted.dispose();
});

test('a paused seek inside the 30Hz paint interval eventually displays the final time', async t => {
  const {entry} = await modules(), f = environment(t);
  const mounted = entry.mountThreeD(f.host, stateAt(0, {playing: false})); f.step();
  const root = f.renderers[0].scene.children[0];
  const text = root.children[0].children.find(node => node.material?.map?.image?.height === 270).material.map.image;
  mounted.update(stateAt(0, {playing: false, currentTime: 2})); f.step(40);
  const paints = text.getContext('2d').paints;
  mounted.update(stateAt(0, {playing: false, currentTime: 0.5})); f.step(5);
  assert.equal(text.getContext('2d').paints, paints, 'preserve the original 30Hz upload throttle');
  assert.equal(f.frames.size, 1, 'finish the deferred repaint even while paused');
  f.step(40);
  assert.ok(text.getContext('2d').paints > paints); assert.equal(f.frames.size, 0);
  mounted.dispose();
});

test('failed mount releases the partially initialized renderer and listeners', async t => {
  const {entry} = await modules(), f = environment(t);
  t.mock.method(globalThis.ResizeObserver.prototype, 'observe', () => {throw new Error('test resize failure');});
  assert.throws(() => entry.mountThreeD(f.host, stateAt()), /test resize failure/);
  const renderer = f.renderers[0];
  assert.equal(renderer.disposed, 1); assert.equal(renderer.contextLost, 1);
  assert.equal(renderer.domElement.listeners.size, 0);
  assert.equal(f.host.children.length, 0); assert.equal(f.frames.size, 0);
});

test('repeated mounts do not retain old stages or RAF callbacks', async t => {
  const {entry} = await modules(), f = environment(t);
  for (let i = 0; i < 20; i++) {
    const mounted = entry.mountThreeD(f.host, stateAt(i * 10)); f.step(); mounted.dispose();
    assert.equal(f.frames.size, 0); assert.equal(f.host.children.length, 0);
    assert.equal(f.document.listeners.size, 0);
  }
  assert.ok(f.renderers.every(renderer => renderer.disposed === 1 && renderer.contextLost === 1));
});
