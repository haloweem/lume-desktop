import test from 'node:test';
import assert from 'node:assert/strict';
import {mountMusicEffects} from '../music-effects.js';

function environment() {
  const frames = new Map();
  let nextFrame = 1;
  const document = {
    hidden: false,
    defaultView: null,
    listeners: new Map(),
    addEventListener(type, listener) {
      this.listeners.set(type, listener);
    },
    removeEventListener(type, listener) {
      if (this.listeners.get(type) === listener) this.listeners.delete(type);
    },
  };
  const window = {
    document,
    performance: {now: () => 100},
    requestAnimationFrame(callback) {
      const id = nextFrame++;
      frames.set(id, callback);
      return id;
    },
    cancelAnimationFrame(id) {
      frames.delete(id);
    },
    devicePixelRatio: 2,
  };
  document.defaultView = window;
  const host = {
    ownerDocument: document,
    children: [],
    dataset: {},
    append(node) {
      this.children.push(node);
    },
    replaceChildren(...nodes) {
      this.children = nodes;
    },
  };
  return {host, document, window, frames};
}

const state = (overrides = {}) => ({
  mode: 'none',
  visualizer: 'classic',
  visible: true,
  scene: null,
  current: null,
  ...overrides,
});

test('none and hidden states do not invoke loaders or retain frames', async () => {
  const f = environment();
  let loads = 0;
  const mounted = mountMusicEffects(f.host, {
    load: async () => {
      loads++;
      throw new Error('loader should not run');
    },
  });
  await mounted.update(state());
  await mounted.update(state({mode: 'snow', visible: false}));
  assert.equal(loads, 0);
  assert.equal(f.frames.size, 0);
  mounted.dispose();
  mounted.dispose();
});

test('stale async loads never mount after a newer mode or dispose', async () => {
  const f = environment();
  const pending = new Map();
  const mounted = mountMusicEffects(f.host, {
    load: mode => new Promise(resolve => pending.set(mode, resolve)),
  });
  const first = mounted.update(state({mode: 'snow'}));
  const second = mounted.update(state({mode: 'star-river'}));
  pending.get('snow')?.(() => { throw new Error('stale mount'); });
  pending.get('star-river')?.(() => ({update() {}, dispose() {}}));
  await Promise.all([first, second]);
  assert.equal(f.host.children.length, 0);
  mounted.dispose();

  const pendingAfterDispose = mounted.update(state({mode: 'rain-window'}));
  await pendingAfterDispose;
  assert.equal(f.host.children.length, 0);
});

test('webgl loader errors report through onError and leave host usable', async () => {
  const f = environment();
  const errors = [];
  const mounted = mountMusicEffects(f.host, {
    onError: error => errors.push(error),
    load: async () => {
      throw new Error('webgl unavailable');
    },
  });
  await mounted.update(state({mode: 'snow'}));
  assert.equal(errors.length, 1);
  assert.match(errors[0].message, /webgl unavailable/);
  assert.equal(f.host.children.length, 0);
  mounted.dispose();
});

test('rain-window receives the shared scene canvas and canvas-sized background source', async () => {
  const f = environment();
  const canvas = {ownerDocument: f.document, width: 100, height: 50};
  const media = {kind: 'image', url: 'https://lume-music.local/art.png'};
  let received;
  const mounted = mountMusicEffects(f.host, {
    load: async mode => {
      assert.equal(mode, 'rain-window');
      return (host, next) => {
        received = next;
        host.append({kind: 'rain-effect'});
        return {update() {}, dispose() {}};
      };
    },
  });
  await mounted.update(state({mode: 'rain-window', scene: {canvas, media, mediaSource: {width: 100, height: 50}}}));
  assert.equal(received.scene.canvas, canvas);
  assert.equal(received.scene.mediaSource.width, 100);
  assert.deepEqual(f.host.children, [{kind: 'rain-effect'}]);
  mounted.dispose();
});

test('hide disposes immediately, resume rebuilds; playback fields never pause effects', async () => {
  const f = environment(), runtimes = [];
  const mounted = mountMusicEffects(f.host, {load: async () => (host, next) => {
    const runtime = {states: [next], disposed: 0, update(next) {this.states.push(next);}, dispose() {this.disposed++;}};
    runtimes.push(runtime);
    host.append(runtime);
    return runtime;
  }});
  await mounted.update(state({mode: 'snow', playing: false}));
  await mounted.update(state({mode: 'snow', playing: true}));
  assert.equal(runtimes.length, 1);
  await mounted.update(state({mode: 'snow', visible: false}));
  assert.equal(runtimes[0].disposed, 1);
  assert.equal(f.host.children.length, 0);
  await mounted.update(state({mode: 'snow'}));
  assert.equal(runtimes.length, 2);
  f.document.hidden = true;
  f.document.listeners.get('visibilitychange')();
  assert.equal(runtimes[1].disposed, 1);
  f.document.hidden = false;
  await f.document.listeners.get('visibilitychange')();
  assert.equal(runtimes.length, 3);
  mounted.dispose(); mounted.dispose();
  assert.equal(runtimes[2].disposed, 1);
  assert.equal(f.document.listeners.size, 0);
});

test('3d star river delegates without loading; switching back mounts independently', async () => {
  const f = environment();
  let loads = 0, disposals = 0;
  const mounted = mountMusicEffects(f.host, {load: async () => {
    loads++;
    return () => ({update() {}, dispose() {disposals++;}});
  }});
  await mounted.update(state({mode: 'star-river', visualizer: '3d'}));
  assert.equal(loads, 0);
  await mounted.update(state({mode: 'star-river', visualizer: 'sonnet'}));
  assert.equal(loads, 1);
  await mounted.update(state({mode: 'star-river', visualizer: '3d'}));
  assert.equal(disposals, 1);
  mounted.dispose();
});

test('pending loads use latest same-mode state and are aborted on hide/dispose', async () => {
  const f = environment(), requests = [], mounts = [];
  const mounted = mountMusicEffects(f.host, {load: (mode, host, {signal}) => new Promise(resolve => requests.push({resolve, signal}))});
  const first = mounted.update(state({mode: 'star-river', current: {artwork: 'old'}}));
  const latest = mounted.update(state({mode: 'star-river', current: {artwork: 'new'}}));
  assert.equal(requests.length, 1);
  requests[0].resolve((host, next) => {
    mounts.push(next);
    return {update() {}, dispose() {}};
  });
  await Promise.all([first, latest]);
  assert.equal(mounts[0].current.artwork, 'new');
  await mounted.update(state({mode: 'none'}));
  const hidden = mounted.update(state({mode: 'snow'}));
  await mounted.update(state({mode: 'snow', visible: false}));
  assert.equal(requests[1].signal.aborted, true);
  requests[1].resolve(() => {throw new Error('hidden mount');});
  await hidden;
  const dead = mounted.update(state({mode: 'snow'}));
  mounted.dispose();
  assert.equal(requests[2].signal.aborted, true);
  requests[2].resolve(() => {throw new Error('disposed mount');});
  await dead;
});

test('rain waits for a nonzero shared canvas and replaces runtime on canvas change', async () => {
  const f = environment();
  let loads = 0, disposals = 0;
  const mounted = mountMusicEffects(f.host, {load: async () => {
    loads++;
    return () => ({update() {}, dispose() {disposals++;}});
  }});
  await mounted.update(state({mode: 'rain-window'}));
  await mounted.update(state({mode: 'rain-window', scene: {canvas: {width: 0, height: 0}}}));
  assert.equal(loads, 0);
  const canvas = {width: 10, height: 10};
  await mounted.update(state({mode: 'rain-window', scene: {canvas}}));
  await mounted.update(state({mode: 'rain-window', scene: {canvas}}));
  assert.equal(loads, 1);
  await mounted.update(state({mode: 'rain-window', scene: {canvas: {...canvas}}}));
  assert.equal(loads, 2);
  assert.equal(disposals, 1);
  mounted.dispose();
});

for (const kind of ['load', 'mount', 'frame']) {
  test(`${kind} failure is latched across playback ticks and only a material change retries`, async () => {
    const f = environment(), errors = [];
    let loads = 0, callback;
    const canvas = {width: 10, height: 10};
    const mounted = mountMusicEffects(f.host, {
      onError: error => errors.push(error),
      load: async () => {
        loads++;
        if (kind === 'load') throw new Error('load failure');
        return (host, state, {onError}) => {
          if (kind === 'mount') throw new Error('mount failure');
          callback = onError;
          return {update() {}, dispose() {}};
        };
      },
    });
    const next = state({mode: 'snow', scene: {canvas}});
    await mounted.update(next);
    if (kind === 'frame') callback(new Error('frame failure'));
    assert.equal(f.host.dataset.effectMode, 'snow');
    assert.equal(f.host.dataset.effectState, 'error');
    for (let i = 0; i < 10; i++) await mounted.update({...next, scene: {...next.scene}, current: {artwork: `${i}`}, currentTime: i});
    assert.equal(loads, 1);
    assert.equal(errors.length, 1);
    await mounted.update({...next, visible: false});
    assert.equal(f.host.dataset.effectMode, 'none');
    assert.equal(f.host.dataset.effectState, 'idle');
    await mounted.update(next);
    assert.equal(loads, 2);
    if (kind === 'frame') callback(new Error('frame failure'));
    await mounted.update({...next, scene: {canvas: {...canvas}}});
    assert.equal(loads, 3);
    if (kind === 'frame') callback(new Error('frame failure'));
    await mounted.update({...next, mode: 'star-river'});
    assert.equal(loads, 4);
    mounted.dispose();
  });
}

test('dataset reflects idle, loading, ready, delegated 3d and disposed lifecycle', async () => {
  const f = environment();
  let resolve;
  const mounted = mountMusicEffects(f.host, {load: () => new Promise(done => {resolve = done;})});
  assert.equal(f.host.dataset.effectState, 'idle');
  const pending = mounted.update(state({mode: 'snow'}));
  assert.equal(f.host.dataset.effectMode, 'snow');
  assert.equal(f.host.dataset.effectState, 'loading');
  resolve(() => ({update() {}, dispose() {}}));
  await pending;
  assert.equal(f.host.dataset.effectState, 'ready');
  await mounted.update(state({mode: 'star-river', visualizer: '3d'}));
  assert.equal(f.host.dataset.effectMode, 'none');
  assert.equal(f.host.dataset.effectState, 'idle');
  mounted.dispose();
  assert.equal(f.host.dataset.effectMode, 'none');
  assert.equal(f.host.dataset.effectState, 'disposed');
});
