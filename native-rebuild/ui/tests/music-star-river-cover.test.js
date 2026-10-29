import test from 'node:test';
import assert from 'node:assert/strict';
import {modules, environment, stateAt} from './music-3d-harness.js';
import {mountMusicEffects} from '../music-effects.js';
import {DEFAULT_MUSIC_PREFERENCES} from '../music-preference-values.js';
import {TextureLoader} from '../music-visualizers/vendor/three.module.js';

async function fixture(t, kind) {
  let controller;
  t.after(() => controller?.dispose());
  const f = environment(t), {entry, star} = await modules();
  const preferences = {...DEFAULT_MUSIC_PREFERENCES, mode: '3d', effectMode: 'star-river'};
  if (kind === '3d') {
    controller = entry.mountThreeD(f.host, stateAt(0, {lines: [], preferences, playing: false}));
  } else {
    Object.assign(f.host, {dataset: {}, replaceChildren() {}});
    controller = mountMusicEffects(f.host, {load: async () => (host, state) =>
      star.mountStarRiverCanvas({clientWidth: 800, clientHeight: 600}, state)});
  }
  const update = async (artwork, visible = true) => {
    const current = {artwork};
    if (kind === '3d') controller.update(stateAt(0, {lines: [], preferences, current, playing: false, visible}));
    else await controller.update({mode: 'star-river', visualizer: 'classic', current, visible});
    f.step(40);
  };
  const river = () => f.renderers.at(-1).scene.children.find(node => node.name === 'mineradio-star-river');
  return {...f, controller, update, river};
}

const cancelled = image => {
  assert.equal(image.src, undefined, 'pending request source removed');
  assert.equal(image.listeners.size, 0, 'callbacks no longer retain the old river');
};

for (const kind of ['3d', 'independent']) {
  test(`${kind} star river cancels pending covers on replacement, hide and dispose`, async t => {
    const f = await fixture(t, kind);
    await f.update('https://lume.local/a.png');
    const first = f.images.at(-1), oldLoad = [...first.listeners.get('load')][0];
    let releases = 0;
    f.river().pendingCover.addEventListener('dispose', () => releases++);
    await f.update('https://lume.local/b.png');
    cancelled(first);
    assert.equal(releases, 1);
    const second = f.images.at(-1), pending = f.river().pendingCover;
    oldLoad.call(first);
    assert.equal(f.river().pendingCover, pending, 'late callback cannot replace the new cover');
    assert.equal(releases, 1, 'late callback cannot dispose the cancelled texture twice');
    pending.addEventListener('dispose', () => releases++);
    await f.update('https://lume.local/b.png', false);
    cancelled(second);
    assert.equal(releases, 2);
    assert.equal(f.frames.size, 0);
    await f.update('https://lume.local/b.png');
    const restored = f.images.at(-1);
    assert.notEqual(restored, second);
    f.river().pendingCover.addEventListener('dispose', () => releases++);
    f.controller.dispose();
    cancelled(restored);
    assert.equal(releases, 3);
  });

  test(`${kind} star river keeps original TextureLoader parameters and releases failed covers`, async t => {
    const f = await fixture(t, kind);
    for (const url of ['https://lume.local/art.png', 'https://lume.local/art.JPG?x=1', 'data:image/jpeg;base64,AA==']) {
      const expected = new TextureLoader().load(url);
      const originalImage = f.images.at(-1);
      originalImage.fire('load');
      await f.update(url);
      const image = f.images.at(-1), texture = f.river().pendingCover;
      let releases = 0;
      texture.addEventListener('dispose', () => releases++);
      assert.equal(image.crossOrigin, originalImage.crossOrigin);
      image.fire('load');
      assert.equal(image.listeners.size, 0);
      assert.equal(image.src, url, 'successful image remains available for GPU upload');
      assert.equal(f.river().pendingCover, null);
      assert.equal(f.river().children[0].material.uniforms.uCoverTex.value, texture);
      for (const key of ['format', 'type', 'mapping', 'wrapS', 'wrapT', 'magFilter', 'minFilter',
        'anisotropy', 'generateMipmaps', 'premultiplyAlpha', 'flipY', 'unpackAlignment', 'encoding', 'version']) {
        assert.equal(texture[key], expected[key], key);
      }
      assert.equal(texture.image, image);
      await f.update(null);
      assert.equal(releases, 1);
      expected.dispose();
    }
    await f.update('https://lume.local/error.png');
    const image = f.images.at(-1), river = f.river(), pending = river.pendingCover;
    let releases = 0;
    pending.addEventListener('dispose', () => releases++);
    image.fire('error');
    cancelled(image);
    assert.equal(releases, 1);
    assert.equal(river.pendingCover, null);
    assert.equal(river.children[0].material.uniforms.uCoverTex.value, river.fallback);
  });
}
