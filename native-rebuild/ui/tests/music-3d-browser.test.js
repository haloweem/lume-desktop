import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile, mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('standalone real WebGL stage draws, drags, hides and releases its context', {
  skip: process.env.LUME_3D_BROWSER_TESTS !== '1',
}, async () => {
  const ui = fileURLToPath(new URL('..', import.meta.url));
  const evidence = path.resolve(ui, '../../build-artifacts/native-rebuild/music-visual-20261002/three-d-runtime');
  const files = new Map();
  const collect=async name=>{
    if(files.has(name))return;
    const file=path.join(ui,name);files.set(name,file);
    for(const match of (await readFile(file,'utf8')).matchAll(/\b(?:from\s*|import\s*)['"]([^'"]+)['"]/g)) {
      assert.ok(match[1].startsWith('./') || match[1]==='../music-material/registry.js','only local renderer dependencies');
      await collect(path.posix.join(path.posix.dirname(name),match[1]));
    }
  };
  await collect('/music-visualizers/three-d.js');
  const server = createServer(async (req, res) => {
    if (req.url === '/') {
      res.setHeader('Content-Type', 'text/html');
      res.end('<!doctype html><meta charset="utf-8"><style>html,body{margin:0;width:100%;height:100%;background:#09090b}#stage{width:100%;height:100%}</style><div id="stage"></div>');
    } else if (files.has(req.url)) {
      res.setHeader('Content-Type', 'text/javascript'); res.end(await readFile(files.get(req.url)));
    } else res.writeHead(404).end();
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const {chromium} = createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR || path.join(homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
  let browser;
  try {
    browser = await chromium.launch({headless: true, channel: 'msedge'});
    await mkdir(evidence, {recursive: true});
    for (const [width, height] of [[1440, 900], [390, 844]]) {
      const page = await browser.newPage({viewport: {width, height}}), errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.goto(`http://127.0.0.1:${server.address().port}/`);
      const initial = await page.evaluate(async () => {
        const {mountThreeD} = await import('/music-visualizers/three-d.js');
        const labels = ['Before the dawn', 'We find our way', 'Across the sky', 'A quiet moment', 'The light returns'];
        const lines = labels.map((fullText, i) => ({fullText, translation: '\u4e00\u6bb5\u5b89\u9759\u7684\u65f6\u5149', startTime: i * 3, endTime: i * 3 + 3, words: []}));
        window.fixture = {lines, activeLineIndex: 2, currentTime: 7.5, playing: true, visible: true};
        window.stage = mountThreeD(document.querySelector('#stage'), window.fixture);
        window.canvas = document.querySelector('canvas');
        window.gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
        window.pixelSample = () => new Promise(resolve => requestAnimationFrame(() => {
          const sample = document.createElement('canvas'); sample.width = canvas.width; sample.height = canvas.height;
          const context = sample.getContext('2d'); context.drawImage(canvas, 0, 0);
          const pixels = context.getImageData(0, 0, sample.width, sample.height).data;
          let lit = 0, center = 0, hash = 0;
          for (let i = 0; i < pixels.length; i += 4) {
            if (pixels[i + 3] > 20) {
              lit++; hash = (hash + i * pixels[i + 3]) % 1000000007;
              const x = (i / 4) % sample.width, y = Math.floor(i / 4 / sample.width);
              if (x > sample.width * .1 && x < sample.width * .9 && y > sample.height * .25 && y < sample.height * .75) center++;
            }
          }
          resolve({lit, center, hash, width: sample.width, height: sample.height});
        }));
        return {webgl: !!gl, ...(await pixelSample())};
      });
      assert.equal(initial.webgl, true); assert.ok(initial.lit > 300); assert.ok(initial.center > 300);
      await page.screenshot({path: path.join(evidence, `three-d-${width}.png`)});
      await page.mouse.move(width * .5, height * .5); await page.mouse.down();
      await page.mouse.move(width * .65, height * .4, {steps: 8}); await page.mouse.up();
      const moved = await page.evaluate(() => pixelSample());
      assert.notEqual(moved.hash, initial.hash); assert.ok(moved.lit > 100);
      await page.screenshot({path: path.join(evidence, `three-d-${width}-drag.png`)});
      const final = await page.evaluate(async () => {
        stage.update({...fixture, visible: false});
        stage.update({...fixture, visible: true});
        const resumed = await pixelSample();
        const lost = new Promise(resolve => canvas.addEventListener('webglcontextlost', () => resolve(true), {once: true}));
        stage.dispose(); stage.dispose();
        return {resumed, lost: await lost, canvases: document.querySelectorAll('canvas').length};
      });
      assert.ok(final.resumed.lit > 100); assert.equal(final.lost, true); assert.equal(final.canvases, 0);
      assert.deepEqual(errors, []);
      console.log(JSON.stringify({viewport: [width, height], initial, moved, released: final.lost}));
      await page.close();
    }
  } finally {
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
  }
});
