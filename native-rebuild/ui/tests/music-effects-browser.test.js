import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('Edge standalone effects: lazy, moving pixels, shared canvas, hidden release and recovery', {
  skip: process.env.LUME_EFFECTS_BROWSER_TESTS !== '1',
  timeout: 180000,
}, async () => {
  const ui = fileURLToPath(new URL('..', import.meta.url));
  const evidence = path.resolve(ui, '../../build-artifacts/native-rebuild/music-effects');
  const server = createServer(async (req, res) => {
    if (req.url === '/') {
      res.setHeader('Content-Type', 'text/html');
      res.end('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><style>html,body{margin:0;width:100%;height:100%;background:#111}#scene,#effects{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}</style><canvas id="scene"></canvas><div id="effects"></div>');
      return;
    }
    const name = decodeURIComponent(new URL(req.url, 'http://localhost').pathname).slice(1);
    if (!(name === 'music-effects.js' || name.startsWith('music-effects/') || name === 'music-visualizers/vendor/three.module.js') || name.split('/').includes('..')) {
      res.writeHead(404).end(); return;
    }
    try {
      const bytes = await readFile(path.join(ui, name));
      res.setHeader('Content-Type', name.endsWith('.png') ? 'image/png' : 'text/javascript');
      res.end(bytes);
    } catch { res.writeHead(404).end(); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const {chromium} = createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR ||
    path.join(homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
  let browser;
  const results = [];
  try {
    browser = await chromium.launch({headless: true, channel: 'msedge'});
    await mkdir(evidence, {recursive: true});
    for (const [width, height, deviceScaleFactor] of [[1440, 900, 1], [390, 844, 2]]) {
      const page = await browser.newPage({viewport: {width, height}, deviceScaleFactor});
      const errors = [], requests = [], assets = [];
      page.on('pageerror', error => errors.push(error.message));
      page.on('request', request => requests.push(request.url()));
      page.on('response', response => {
        if (/rain-window-drop-.*\.png/.test(response.url())) assets.push({url: response.url(), status: response.status()});
      });
      await page.goto(`http://127.0.0.1:${server.address().port}/`);
      await page.evaluate(async () => {
        const {mountMusicEffects} = await import('/music-effects.js');
        const host = document.querySelector('#effects'), canvas = document.querySelector('#scene');
        canvas.width = innerWidth; canvas.height = innerHeight;
        const context = canvas.getContext('2d');
        window.paint = (alternate = false) => {
          context.fillStyle = alternate ? '#146f20' : '#803438';
          context.fillRect(0, 0, canvas.width, canvas.height);
          for (let x = 0; x < canvas.width; x += 48) {
            context.fillStyle = alternate ? '#30cf55' : '#ecae52';
            context.fillRect(x, 0, 12, canvas.height);
          }
          canvas.dataset.glassFrameVersion = String(Number(canvas.dataset.glassFrameVersion || 0) + 1);
          canvas.dispatchEvent(new Event('lume-glass-media-frame'));
        };
        paint();
        window.backgroundCopies = 0;
        const draw = CanvasRenderingContext2D.prototype.drawImage;
        CanvasRenderingContext2D.prototype.drawImage = function(source, ...args) {
          if (source === canvas) backgroundCopies++;
          return draw.call(this, source, ...args);
        };
        const request = window.requestAnimationFrame.bind(window), cancel = window.cancelAnimationFrame.bind(window);
        window.pendingFrames = new Set();
        window.requestAnimationFrame = callback => {
          const id = request(now => {pendingFrames.delete(id); callback(now);});
          pendingFrames.add(id); return id;
        };
        window.cancelAnimationFrame = id => {pendingFrames.delete(id); cancel(id);};
        const NativeObserver = window.ResizeObserver;
        window.observers = new Set();
        window.ResizeObserver = class extends NativeObserver {
          constructor(callback) { super(callback); observers.add(this); }
          disconnect() { observers.delete(this); super.disconnect(); }
        };
        const getContext = HTMLCanvasElement.prototype.getContext;
        window.contexts = [];
        HTMLCanvasElement.prototype.getContext = function(type, options) {
          const gl = getContext.call(this, type, options);
          if (gl && /webgl/.test(type) && !contexts.includes(gl)) contexts.push(gl);
          return gl;
        };
        window.failures = [];
        window.effects = mountMusicEffects(host, {onError: error => failures.push(error.message)});
        window.fixture = {mode: 'none', visualizer: 'classic', visible: true, scene: {canvas, media: null, mediaSource: null}, current: {artwork: canvas.toDataURL()}};
        window.setEffect = overrides => effects.update({...fixture, ...overrides});
        window.sample = () => new Promise(resolve => request(() => {
          const effect = host.querySelector('canvas');
          if (!effect) return resolve(null);
          const copy = document.createElement('canvas');
          copy.width = effect.width; copy.height = effect.height;
          const ctx = copy.getContext('2d'); ctx.drawImage(effect, 0, 0);
          const pixels = ctx.getImageData(0, 0, copy.width, copy.height).data;
          let lit = 0, hash = 0, red = 0, green = 0;
          for (let i = 0; i < pixels.length; i += 4) {
            if (pixels[i + 3] > 8 && pixels[i] + pixels[i + 1] + pixels[i + 2] > 12) lit++;
            hash = (hash + (i + 1) * (pixels[i] + pixels[i + 1] * 3 + pixels[i + 2] * 7 + pixels[i + 3] * 11)) % 1000000007;
            red += pixels[i]; green += pixels[i + 1];
          }
          resolve({lit, hash, red, green, width: copy.width, height: copy.height});
        }));
        window.releaseState = () => ({
          canvases: host.querySelectorAll('canvas').length, frames: pendingFrames.size,
          observers: observers.size, liveContexts: contexts.filter(gl => !gl.isContextLost()).length,
          videoCount: document.querySelectorAll('video').length,
        });
        await setEffect({});
        await setEffect({mode: 'snow', visible: false});
        await setEffect({mode: 'star-river', visualizer: '3d'});
      });
      assert.ok(requests.every(url => !/\/music-effects\/|three\.module/.test(url)), 'none/hidden/shared-3d do not load leaves');
      assert.deepEqual(await page.evaluate(() => releaseState()), {canvases: 0, frames: 0, observers: 0, liveContexts: 0, videoCount: 0});
      for (const mode of ['snow', 'star-river', 'rain-window']) {
        await page.evaluate(mode => setEffect({mode, playing: false}), mode);
        await page.waitForTimeout(600);
        const first = await page.evaluate(() => sample());
        assert.ok(first?.lit > 100, `${mode}: nonblank`);
        const ratio = mode === 'rain-window' ? deviceScaleFactor : Math.min(deviceScaleFactor, 1.5);
        assert.equal(first.width, Math.floor(width * ratio));
        assert.equal(first.height, Math.floor(height * ratio));
        await page.waitForTimeout(700);
        const moved = await page.evaluate(() => sample());
        assert.notEqual(moved.hash, first.hash, `${mode}: moves even without playback`);
        await page.screenshot({path: path.join(evidence, `${mode}-${width}.png`)});
        if (mode === 'rain-window') {
          const copies = await page.evaluate(() => backgroundCopies);
          await page.waitForTimeout(250);
          assert.equal(await page.evaluate(() => backgroundCopies), copies, 'static forest needs no repeated background copies or blur');
          await page.evaluate(() => paint(true));
          await page.waitForTimeout(500);
          const changed = await page.evaluate(() => sample());
          assert.ok(changed.green > changed.red * 2, 'rain refreshes from canvas, not an independent image/video');
          const refreshed = await page.evaluate(() => backgroundCopies);
          await page.waitForTimeout(150);
          assert.equal(await page.evaluate(() => backgroundCopies), refreshed, 'final background frame settles after a change');
          assert.equal(assets.length, 2);
          assert.ok(assets.every(item => item.status === 200));
        }
        await page.evaluate(mode => setEffect({mode, visible: false}), mode);
        await page.waitForTimeout(80);
        const released = await page.evaluate(() => releaseState());
        assert.deepEqual(released, {canvases: 0, frames: 0, observers: 0, liveContexts: 0, videoCount: 0}, `${mode}: ${JSON.stringify(await page.evaluate(() => failures))}`);
        await page.evaluate(mode => setEffect({mode}), mode);
        await page.waitForTimeout(300);
        assert.ok((await page.evaluate(() => sample()))?.lit > 100);
        await page.evaluate(() => setEffect({mode: 'none'}));
        results.push({viewport: [width, height], deviceScaleFactor, mode, first, moved, released});
      }
      assert.deepEqual(await page.evaluate(() => failures), []);
      for (const mode of ['snow', 'star-river', 'rain-window']) {
        await page.evaluate(mode => setEffect({mode}), mode);
        await page.waitForTimeout(100);
        await page.evaluate(() => {
          const gl = contexts.find(gl => !gl.isContextLost());
          gl.getExtension('WEBGL_lose_context').loseContext();
        });
        await page.waitForFunction(() => document.querySelector('#effects').dataset.effectState === 'error');
        await page.evaluate(async mode => {for (let i = 0; i < 10; i++) await setEffect({mode});}, mode);
        assert.deepEqual(await page.evaluate(() => releaseState()), {canvases: 0, frames: 0, observers: 0, liveContexts: 0, videoCount: 0});
        await page.evaluate(async mode => {
          await setEffect({mode, visible: false});
          await setEffect({mode});
        }, mode);
        await page.waitForTimeout(150);
        assert.ok((await page.evaluate(() => sample()))?.lit > 100);
        await page.evaluate(() => setEffect({mode: 'none'}));
      }
      assert.equal((await page.evaluate(() => failures)).length, 3);
      await page.evaluate(async () => {
        failures.length = 0;
        const createBuffer = WebGL2RenderingContext.prototype.createBuffer;
        WebGL2RenderingContext.prototype.createBuffer = () => {throw new Error('Injected buffer allocation failure');};
        await setEffect({mode: 'snow'});
        await setEffect({mode: 'rain-window'});
        WebGL2RenderingContext.prototype.createBuffer = createBuffer;
        const observe = ResizeObserver.prototype.observe;
        ResizeObserver.prototype.observe = () => {throw new Error('Injected observer failure');};
        await setEffect({mode: 'star-river'});
        ResizeObserver.prototype.observe = observe;
        await setEffect({mode: 'none'});
        const getParameter = WebGL2RenderingContext.prototype.getParameter;
        WebGL2RenderingContext.prototype.getParameter = () => {throw new Error('Injected Three constructor failure');};
        await setEffect({mode: 'star-river'});
        WebGL2RenderingContext.prototype.getParameter = getParameter;
      });
      assert.equal((await page.evaluate(() => failures)).length, 4);
      assert.deepEqual(await page.evaluate(() => releaseState()), {canvases: 0, frames: 0, observers: 0, liveContexts: 0, videoCount: 0});
      await page.evaluate(async () => {
        failures.length = 0;
        const original = HTMLCanvasElement.prototype.getContext;
        HTMLCanvasElement.prototype.getContext = function(type, options) {return /webgl/.test(type) ? null : original.call(this, type, options);};
        await setEffect({mode: 'snow'});
        await setEffect({mode: 'rain-window'});
        await setEffect({mode: 'star-river'});
        HTMLCanvasElement.prototype.getContext = original;
        effects.dispose(); effects.dispose();
      });
      assert.equal((await page.evaluate(() => failures)).length, 3, 'all WebGL failures report, without escaping to page');
      assert.deepEqual(await page.evaluate(() => releaseState()), {canvases: 0, frames: 0, observers: 0, liveContexts: 0, videoCount: 0});
      assert.deepEqual(errors, []);
      await page.close();
    }
    await writeFile(path.join(evidence, 'results.json'), JSON.stringify({
      browser: browser.version(), results,
      checks: ['lazy none/hidden/shared-3d', 'nonblank and moving', 'rain PNG 200', 'shared canvas refresh',
        'hidden zero RAF/observer/live GL', 'restore rebuild', 'context loss error latch and recovery',
        'partial initialization cleanup', 'WebGL unavailable callbacks', 'no video', 'no page errors'],
    }, null, 2) + '\n');
    console.log(JSON.stringify(results));
  } finally {
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
  }
});
