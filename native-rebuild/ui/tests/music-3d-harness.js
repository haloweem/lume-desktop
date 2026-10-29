import assert from 'node:assert/strict';
import {existsSync} from 'node:fs';
import {registerHooks} from 'node:module';

// Replace only the browser/GPU boundary; retain real Three geometry, textures and math.
const stageUrl = new URL('../music-visualizers/vendor/mineradio-stage.js', import.meta.url);
const threeUrl = new URL('../music-visualizers/vendor/three.module.js', import.meta.url);
const entryUrl = new URL('../music-visualizers/three-d.js', import.meta.url);
const starUrl = new URL('../music-effects/reference/star-river.js', import.meta.url);
let hook, active;
class Renderer {
  constructor(options) {
    this.options = options;
    this.domElement = active.makeCanvas();
    this.ratio = 1;
    this.renders = this.disposed = this.contextLost = 0;
    active.renderers.push(this);
  }
  setClearColor(color, alpha) { this.clear = [color, alpha]; }
  setPixelRatio(value) { this.ratio = value; }
  getPixelRatio() { return this.ratio; }
  setSize(width, height) {
    this.domElement.width = width * this.ratio;
    this.domElement.height = height * this.ratio;
  }
  render(scene, camera) { this.renders++; this.scene = scene; this.camera = camera; }
  dispose() { this.disposed++; }
  forceContextLoss() { this.contextLost++; }
}

export async function modules() {
  for (const url of [entryUrl, stageUrl, threeUrl]) {
    assert.ok(existsSync(url), `missing migrated runtime: ${url.pathname}`);
  }
  if (!hook) {
    globalThis[Symbol.for('lume.music3d.testRenderer')] = Renderer;
    const source = `export * from ${JSON.stringify(threeUrl.href)};\nexport const WebGLRenderer = globalThis[Symbol.for('lume.music3d.testRenderer')];`;
    const url = `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`;
    hook = registerHooks({resolve(specifier, context, next) {
      if (context.parentURL === stageUrl.href && specifier === './three.module.js') return {url, shortCircuit: true};
      if (context.parentURL === starUrl.href && specifier === '../../music-visualizers/vendor/three.module.js') return {url, shortCircuit: true};
      return next(specifier, context);
    }});
  }
  return {entry: await import(entryUrl.href), stage: await import(stageUrl.href), star: await import(starUrl.href)};
}

function eventTarget() {
  return {
    listeners: new Map(),
    addEventListener(type, listener) {
      if (!this.listeners.has(type)) this.listeners.set(type, new Set());
      this.listeners.get(type).add(listener);
    },
    removeEventListener(type, listener) {
      this.listeners.get(type)?.delete(listener);
      if (!this.listeners.get(type)?.size) this.listeners.delete(type);
    },
    fire(type, fields = {}) {
      for (const listener of this.listeners.get(type) ?? []) listener.call(this, {preventDefault() {}, ...fields});
    },
  };
}

export function environment(t) {
  const frames = new Map(), renderers = [], observers = [], canvases = [], images = [];
  let nextFrame = 0, now = 100;
  const makeCanvas = () => {
    const canvas = {...eventTarget(), width: 300, height: 150, style: {}, dataset: {},
      captured: null, focus() {}, setPointerCapture(id) { this.captured = id; },
      hasPointerCapture(id) { return this.captured === id; },
      releasePointerCapture(id) { if (id === this.captured) this.captured = null; },
      remove() {
        const index = this.parent?.children.indexOf(this);
        if (index >= 0) this.parent.children.splice(index, 1);
        this.parent = null;
      },
    };
    const context = {canvas, paints: 0, inks:[], measureText: text => ({width: [...text].length * 35}),
      clearRect() {}, fillText() { this.paints++; this.inks.push(this.fillStyle); }, fillRect() {}, drawImage() {},
      save() {}, restore() {}, beginPath() {}, rect() {}, clip() {},
      createLinearGradient: () => ({addColorStop() {}}),
    };
    canvas.getContext = type => type === '2d' ? context : null;
    canvases.push(canvas);
    return canvas;
  };
  const document = {...eventTarget(), hidden: false, baseURI: 'https://lume.local/', createElement: makeCanvas,
    createElementNS(namespace, tag) {
      assert.equal(tag, 'img');
      const image = {...eventTarget(), removeAttribute(name) { delete this[name]; }};
      images.push(image);
      return image;
    }};
  const globals = {document, window: {devicePixelRatio: 2}, ResizeObserver: class {
    constructor(callback) { this.callback = callback; observers.push(this); }
    observe() {}
    disconnect() { this.disconnected = true; }
  }, requestAnimationFrame: callback => {frames.set(++nextFrame, callback); return nextFrame;},
  cancelAnimationFrame: id => frames.delete(id)};
  const descriptors = Object.fromEntries(Object.keys(globals).map(key => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
  for (const [key, value] of Object.entries(globals)) Object.defineProperty(globalThis, key, {value, configurable: true, writable: true});
  t.mock.method(performance, 'now', () => now);
  const host = {clientWidth: 1000, clientHeight: 700, ownerDocument: document, children: [],
    appendChild(node) { node.parent = this; this.children.push(node); },
  };
  active = {makeCanvas, renderers};
  t.after(() => {
    for (const [key, descriptor] of Object.entries(descriptors)) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else delete globalThis[key];
    }
    active = null;
  });
  return {host, document, frames, renderers, observers, canvases, images,
    step(milliseconds = 1000 / 60) {
      now += milliseconds;
      const callbacks = [...frames.values()]; frames.clear();
      callbacks.forEach(callback => callback(now));
    },
  };
}

export const lines = Array.from({length: 1000}, (_, index) => ({
  fullText: `Lyric ${index}`, translation: `Translation ${index}`,
  startTime: index * 3, endTime: index * 3 + 3, words: [],
}));
export const stateAt = (index = 0, fields = {}) => ({lines, activeLineIndex: index,
  currentTime: index * 3 + 1, playing: true, visible: true, ...fields});

export function resources(root) {
  const items = new Set();
  root.traverse(node => {
    if (node.geometry) items.add(node.geometry);
    if (node.material) {
      items.add(node.material);
      if (node.material.map) items.add(node.material.map);
    }
  });
  return [...items];
}
