import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';

const root = new URL('../music-effects/reference/', import.meta.url);
const read = name => readFile(new URL(name, root));
const hash = bytes => createHash('sha256').update(bytes).digest('hex');

test('all generated files, original sources and rain assets match recorded hashes', async () => {
  const manifest = JSON.parse(await read('manifest.json'));
  for (const [name, expected] of Object.entries(manifest.outputs)) assert.equal(hash(await read(name)), expected, name);
  for (const [name, expected] of Object.entries(manifest.sources)) assert.equal(hash(await read(`source/upstream/${name}`)), expected, name);
  assert.equal(hash(await read(manifest.sharedThree.path)), manifest.sharedThree.sha256, 'shared Three bundle');
  for (const name of ['rain-window-drop-color.png', 'rain-window-drop-alpha.png']) {
    assert.deepEqual([...await read(`assets/${name}`)].slice(0, 8), [137, 80, 78, 71, 13, 10, 26, 10]);
  }
  assert.ok(manifest.adaptations.length > 0);
});

test('runtime leaves contain no application state, background decoders or bundled Three copy', async () => {
  const manifest = JSON.parse(await read('manifest.json'));
  const inputs = Object.keys(manifest.sources);
  assert.ok(!inputs.some(name => /node_modules|backgroundMedia\.ts|Wallpaper|store/i.test(name)));
  for (const file of ['snow.js', 'star-river.js', 'rain-window.js']) {
    const text = (await read(file)).toString();
    assert.doesNotMatch(text, /electron|localStorage|sessionStorage|window\.app|useStore|createElement\(["']video|ImageDecoder|backgroundMedia/);
  }
  const star = (await read('star-river.js')).toString();
  assert.match(star, /from ["']\.\.\/\.\.\/music-visualizers\/vendor\/three\.module\.js["']/);
  assert.match(star, /PerspectiveCamera\(44, 1, 0\.1, 300\)/);
  assert.match(star, /forceContextLoss/);
  const snow = (await read('snow.js')).toString();
  assert.match(snow, /targetFps = 30/);
  assert.match(snow, /maxPixelRatio = 1\.5/);
  const rain = (await read('rain-window.js')).toString();
  assert.match(rain, /HTMLCanvasElement/);
  assert.match(rain, /cancelAnimationFrame/);
  assert.match(rain, /WEBGL_lose_context/);
});
