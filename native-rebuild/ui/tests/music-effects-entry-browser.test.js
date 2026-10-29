import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile, mkdir, mkdtemp, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';

function installEntryProbe() {
  if (window !== window.top) return;
  const listeners = new Set();
  const probe = window.effectsEntryProbe = {
    contexts: [], videos: [], sceneCopies: 0, rainSceneCopies: 0, copyOwners: new WeakMap(), copiedScenes: new Set(), violations: [],
    frozen: false, heldFrames: new Map(), initialFrame: null,
  };
  window.harness = {
    commands: [],
    receive: message => listeners.forEach(listener => listener({data: message})),
  };
  window.chrome = {webview: {
    addEventListener: (_, listener) => listeners.add(listener),
    removeEventListener: (_, listener) => listeners.delete(listener),
    postMessage: command => {
      harness.commands.push(command);
      if (command?.scope === 'music-background' && command.action === 'snapshot')
        queueMicrotask(() => harness.receive({version: 1, type: 'music-background-state',
          revision: 0, request: 0, status: 'ready', error: '', media: null}));
      if (command?.scope === 'music' && command.action === 'loadLyrics')
        queueMicrotask(() => window.effectLyrics && harness.receive(effectLyrics));
    },
    postMessageWithAdditionalObjects: command => harness.commands.push(command),
  }};
  document.addEventListener('securitypolicyviolation', event => probe.violations.push({
    directive: event.violatedDirective, blockedURI: event.blockedURI,
  }));
  const create = Document.prototype.createElement;
  Document.prototype.createElement = function(name, options) {
    const element = create.call(this, name, options);
    if (name.toLowerCase() === 'video') probe.videos.push(element);
    return element;
  };
  const getContext = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function(type, options) {
    const context = getContext.call(this, type, options);
    if (context && /webgl/.test(type) && !probe.contexts.some(item => item.context === context))
      probe.contexts.push({canvas: this, context, effect: this.dataset.visualizerEffect || ''});
    return context;
  };
  const drawImage = CanvasRenderingContext2D.prototype.drawImage;
  CanvasRenderingContext2D.prototype.drawImage = function(source, ...args) {
    if (source instanceof HTMLCanvasElement && source.hasAttribute('data-music-background')) {
      probe.sceneCopies++;
      if (!probe.copyOwners.has(this)) probe.copyOwners.set(this, new Error().stack.includes('/rain-window.js:'));
      if (probe.copyOwners.get(this)) probe.rainSceneCopies++;
      probe.copiedScenes.add(source);
    }
    return drawImage.call(this, source, ...args);
  };
  const request = window.requestAnimationFrame.bind(window);
  const cancel = window.cancelAnimationFrame.bind(window);
  probe.nativeFrame = request;
  window.requestAnimationFrame = callback => {
    const id = request(now => {
      if (probe.frozen) probe.heldFrames.set(id, callback);
      else callback(now);
    });
    return id;
  };
  window.cancelAnimationFrame = id => { probe.heldFrames.delete(id); cancel(id); };
  probe.thaw = () => {
    probe.frozen = false;
    for (const callback of probe.heldFrames.values()) request(callback);
    probe.heldFrames.clear();
  };
  probe.releaseState = () => probe.contexts.filter(item => item.effect).map(item => ({
    effect: item.effect, connected: item.canvas.isConnected, lost: item.context.isContextLost(),
    width: item.canvas.width, height: item.canvas.height,
  }));
  probe.sample = selector => new Promise(resolve => request(() => {
    const source = document.querySelector(selector);
    if (!source) return resolve(null);
    const canvas = document.createElement('canvas');
    canvas.width = source.width; canvas.height = source.height;
    const context = canvas.getContext('2d');
    context.drawImage(source, 0, 0);
    const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
    let lit = 0, hash = 0;
    for (let i = 0; i < pixels.length; i += 4) {
      if (pixels[i + 3] > 8 && pixels[i] + pixels[i + 1] + pixels[i + 2] > 12) lit++;
      hash = (hash + (i + 1) * (pixels[i] + pixels[i + 1] * 3 +
        pixels[i + 2] * 7 + pixels[i + 3] * 11)) % 1000000007;
    }
    resolve({lit, hash, width: canvas.width, height: canvas.height});
  }));
}

test('production Edge music effects entry: pixels, ownership, controls and wallpaper recovery',
  {skip: process.env.LUME_UI_BROWSER_TESTS !== '1', timeout: 240000}, async t => {
  const ui = path.resolve(fileURLToPath(new URL('..', import.meta.url)));
  const artifacts = path.resolve(ui, '../../build-artifacts/native-rebuild');
  await mkdir(artifacts, {recursive: true});
  const evidence = await mkdtemp(path.join(artifacts,
    `music-effects-entry-${new Date().toISOString().replace(/[:.]/g, '-')}-`));
  const runtime = path.join(homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const require = createRequire(import.meta.url);
  const {chromium} = require(process.env.LUME_PLAYWRIGHT_DIR || path.join(runtime, 'playwright'));
  const {PNG} = require(path.join(runtime, 'pngjs'));
  const server = createServer(async (req, res) => {
    const pathname = new URL(req.url, 'http://localhost').pathname;
    if (pathname === '/favicon.ico') return res.writeHead(204).end();
    const file = path.resolve(ui, '.' + decodeURIComponent(pathname));
    if (!file.startsWith(ui + path.sep)) return res.writeHead(403).end();
    try {
      res.writeHead(200, {'Cache-Control': 'no-store', 'Content-Type': ({
        '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css',
        '.webp': 'image/webp', '.png': 'image/png', '.svg': 'image/svg+xml',
        '.woff': 'font/woff', '.woff2': 'font/woff2',
      })[path.extname(file)] || 'application/octet-stream'}).end(await readFile(file));
    } catch { res.writeHead(404).end(); }
  });
  const report = {verifiedAt: new Date().toISOString(), status: 'running',
    scope: {entry: '/index.html', productionCodeUnmodified: true, bridge: 'isolated in-memory fixture',
      nativeAudioVerified: false, realAccountOrConfigurationTouched: false,
      compositingProbe: 'freeze parent RAF briefly, compare same page with effect visible/hidden'},
    scenarios: []};
  let browser;
  try {
    await new Promise((resolve, reject) => {
      server.once('error', reject); server.listen(0, '127.0.0.1', resolve);
    });
    const origin = `http://127.0.0.1:${server.address().port}`;
    browser = await chromium.launch({channel: 'msedge', headless: true});
    report.browser = {channel: 'msedge', version: browser.version(), headless: true};

    for (const [width, height, deviceScaleFactor] of [[1440, 900, 1], [390, 844, 2]]) {
      await t.test(`${width}x${height} @${deviceScaleFactor}`, async () => {
        const result = {viewport: {width, height}, deviceScaleFactor, checks: [], screenshots: [],
          pageErrors: [], cspErrors: [], consoleErrors: [], remoteRequests: [], failedResponses: [], leaves: []};
        report.scenarios.push(result);
        const page = await browser.newPage({viewport: {width, height}, deviceScaleFactor});
        page.setDefaultTimeout(12000);
        page.on('pageerror', error => result.pageErrors.push(error.message));
        page.on('console', message => {
          if (message.type() !== 'error') return;
          result.consoleErrors.push(message.text());
          if (/Content Security Policy|Refused to/i.test(message.text())) result.cspErrors.push(message.text());
        });
        page.on('request', request => {
          const url = new URL(request.url());
          if (url.pathname.startsWith('/music-effects/')) result.leaves.push(url.pathname);
        });
        page.on('response', response => {
          if (response.status() >= 400) result.failedResponses.push({url: response.url(), status: response.status()});
        });
        await page.route('**/*', route => {
          const url = new URL(route.request().url());
          if (url.origin !== origin) {
            result.remoteRequests.push(url.href);
            return route.abort();
          }
          return route.continue();
        });
        await page.addInitScript({content: `(${installEntryProbe.toString()})();
          if(window===window.top)(${installMusicPreferencesHarness.toString()})();`});
        const screenshot = async name => {
          const file = `${width}-${name}.png`;
          const bytes = await page.screenshot({path: path.join(evidence, file), animations: 'disabled'});
          result.screenshots.push(file);
          return bytes;
        };
        const check = async (name, run) => {
          const entry = {name};
          result.checks.push(entry);
          try { entry.evidence = await run(); entry.status = 'passed'; }
          catch (error) {
            entry.status = 'failed';
            entry.failure = {name: error.name, message: error.message, actual: error.actual, expected: error.expected};
            await screenshot(`${name}-failure`).catch(() => {});
          }
          console.log(`${width} ${name}: ${entry.status}${entry.failure ? `: ${entry.failure.message}` : ''}`);
        };
        const panel = page.locator('[data-music-player-panel]');
        const effectSelect = page.locator('select[data-music-preference="effectMode"]');
        const effectTrigger = page.locator('[data-music-effect-trigger]');
        const settings = async () => {
          if (!await panel.count()) await page.locator('[data-music-action="lyrics"]').click();
          await page.locator('[data-music-panel-tab="globalSettings"]').click();
          await effectTrigger.waitFor({state: 'visible'});
        };
        const closePanel = async () => {
          if (await panel.count()) {
            await page.locator('[data-music-panel-tab="globalSettings"]').press('Escape');
            await panel.waitFor({state: 'detached'});
          }
        };
        const setEffect = async mode => {
          await settings();
          await effectTrigger.click();
          await page.locator(`[data-music-effect="${mode}"]`).click();
          await page.waitForFunction(mode => document.querySelector('[data-music-settings]')?.getAttribute('aria-busy') === 'false' &&
            document.querySelector('[data-music-preference="effectMode"]')?.value === mode, mode);
          await closePanel();
          const selected = mode === 'star-river' &&
            await page.locator('[data-music-stage]').getAttribute('data-visualizer-mode') === '3d' ? 'none' : mode;
          await page.locator(`.MusicEffects[data-effect-mode="${selected}"][data-effect-state="${selected === 'none' ? 'idle' : 'ready'}"]`).waitFor();
        };
        const setMode = async mode => {
          await settings();
          await page.locator('[data-music-visualizer-trigger]').click();
          await page.locator(`[data-music-visualizer="${mode}"]`).click();
          await page.locator(`[data-music-stage][data-visualizer-mode="${mode}"][data-visualizer-state="ready"]`).waitFor();
          if (mode === 'partita')
            await page.frameLocator('.MusicReferenceStage').locator('#reference-stage[data-reference-state="ready"]').waitFor();
          await closePanel();
        };
        const pixels = async (mode, selector = `canvas[data-visualizer-effect="${mode}"]`) => {
          await page.waitForTimeout(600);
          const first = await page.evaluate(selector => effectsEntryProbe.sample(selector), selector);
          await page.waitForTimeout(700);
          const second = await page.evaluate(selector => effectsEntryProbe.sample(selector), selector);
          assert.ok(first?.lit > 100 && second?.lit > 100, `${mode}: actual canvas pixels must be nonblank`);
          assert.notEqual(first.hash, second.hash, `${mode}: actual canvas pixels must move`);
          return {first, second};
        };
        const foregroundVisible = async selector => {
          const controls=page.locator(selector);
          const bounds=await controls.boundingBox();
          await page.evaluate(() => new Promise(resolve => {
            effectsEntryProbe.frozen=true;
            effectsEntryProbe.nativeFrame(() => effectsEntryProbe.nativeFrame(resolve));
          }));
          try {
            const shown=PNG.sync.read(await page.screenshot({clip:bounds}));
            await controls.evaluate(node=>node.style.visibility='hidden');
            const hidden=PNG.sync.read(await page.screenshot({clip:bounds}));
            let changed=0;
            for(let i=0;i<shown.data.length;i+=4)
              if(Math.max(...[0,1,2].map(c=>Math.abs(shown.data[i+c]-hidden.data[i+c])))>20)changed++;
            assert.ok(changed>100,`${selector} must contribute visible pixels above rain, not merely pass hit testing`);
            return {changedPixels:changed};
          } finally {
            await controls.evaluate(node=>node.style.removeProperty('visibility'));
            await page.evaluate(()=>effectsEntryProbe.thaw());
          }
        };
        const released = async () => {
          await page.waitForFunction(() => effectsEntryProbe.releaseState().every(item =>
            item.connected || item.lost && item.width === 1 && item.height === 1));
          const states = await page.evaluate(() => effectsEntryProbe.releaseState());
          assert.ok(states.length, 'at least one effect context was observed');
          assert.ok(states.filter(item => !item.connected).every(item => item.lost),
            'detached effect WebGL contexts must be lost, not merely removed from DOM');
          return states;
        };
        const composited = async name => {
          await page.evaluate(() => new Promise(resolve => {
            effectsEntryProbe.frozen = true;
            effectsEntryProbe.nativeFrame(() => effectsEntryProbe.nativeFrame(resolve));
          }));
          try {
            const visible = PNG.sync.read(await screenshot(`${name}-visible`));
            await page.locator('.MusicEffects').evaluate(host => host.style.visibility = 'hidden');
            const hidden = PNG.sync.read(await screenshot(`${name}-hidden-control`));
            let changedPixels = 0, maximumDelta = 0;
            // Exclude the titlebar and lower playback controls from the visible-scene comparison.
            for (let y = Math.ceil(100 * deviceScaleFactor); y < (height - 180) * deviceScaleFactor; y++) {
              for (let x = 0; x < visible.width; x++) {
                const offset = (y * visible.width + x) * 4;
                const delta = Math.max(...[0, 1, 2].map(c => Math.abs(visible.data[offset + c] - hidden.data[offset + c])));
                if (delta > 5) changedPixels++;
                maximumDelta = Math.max(maximumDelta, delta);
              }
            }
            const sample = {changedPixels, maximumDelta};
            result.compositingSamples ??= {};
            result.compositingSamples[name] = sample;
            assert.ok(changedPixels > 100, `${name}: effect must contribute visible page pixels, not be occluded (${changedPixels})`);
            return sample;
          } finally {
            await page.locator('.MusicEffects').evaluate(host => host.style.removeProperty('visibility')).catch(() => {});
            await page.evaluate(() => effectsEntryProbe.thaw());
          }
        };
        try {
          await page.goto(`${origin}/index.html`);
          await page.waitForFunction(() => harness.commands.includes('v1|ready'));
          await page.evaluate(async () => {
            const {initialMusicState} = await import('./music.js');
            const {query, nav, lyrics, ...native} = initialMusicState();
            const track = {id: 'effects-entry', title: 'Effects entry', artist: 'Lume', album: 'Fixture',
              source: 'C:/fixture/effects-entry.wav', artwork: '', duration: 180};
            window.effectSnapshot = {...native, version: 1, type: 'music-state', session: 36, revision: 1,
              pageOpen: true, pageVisible: true, status: 'playing', playing: true, current: track, queue: [track],
              timeline: {position: 24, duration: 180},
              capabilities: {canToggle: true, canSeek: true, canPrevious: true, canNext: true}};
            window.effectLyrics = {version: 1, type: 'music-lyrics', session: 36, trackId: track.id,
              lines: [{startTime: 0, endTime: 180, fullText: 'Effects continuity', translation: '', words: []}]};
            harness.receive(effectSnapshot);
            harness.receive({version: 1, type: 'navigate', page: 'music'});
            harness.receive(effectLyrics);
          });
          await page.locator('[data-music-root][data-screen="home"]').waitFor();
          await page.locator('[data-music-background][data-music-background-state="ready"]').waitFor();
          await check('home-lazy', async () => {
            assert.deepEqual(result.leaves, [], 'home/none must not fetch effect leaves');
            assert.equal(await page.locator('canvas[data-visualizer-effect]').count(), 0);
            return {leaves: [...result.leaves]};
          });
          await page.locator('[data-music-open-player]').click();
          await setMode('partita');
          await check('player-none-lazy', async () => {
            await settings();
            assert.deepEqual(await effectSelect.locator('option').evaluateAll(options => options.map(option => option.value)),
              ['none', 'rain-window', 'snow', 'star-river']);
            await closePanel();
            assert.deepEqual(result.leaves, [], 'player/none must not fetch effect leaves');
            await page.evaluate(() => {
              effectsEntryProbe.initialFrame = document.querySelector('.MusicReferenceStage');
              effectsEntryProbe.initialWindow = effectsEntryProbe.initialFrame.contentWindow;
              effectsEntryProbe.background = document.querySelector('[data-music-background]');
              effectsEntryProbe.videoCount = effectsEntryProbe.videos.length;
            });
            assert.equal(await page.locator('.MusicEffects').evaluate(host =>
              !host.closest('[data-music-stage]') && getComputedStyle(host).pointerEvents === 'none'), true);
          });
          for (const mode of ['snow', 'rain-window', 'star-river']) {
            await check(`partita-${mode}`, async () => {
              await setEffect(mode);
              const sample = await pixels(mode);
              const identity = await page.evaluate(() => ({
                iframe: effectsEntryProbe.initialFrame === document.querySelector('.MusicReferenceStage'),
                window: effectsEntryProbe.initialWindow === document.querySelector('.MusicReferenceStage')?.contentWindow,
                background: effectsEntryProbe.background === document.querySelector('[data-music-background]'),
              }));
              assert.deepEqual(identity, {iframe: true, window: true, background: true},
                'effect preferences must preserve the lyric browsing context and background owner');
              assert.equal(await page.locator('canvas[data-visualizer-effect]').count(), 1);
              const composition = await composited(`partita-${mode}`);
              if (mode === 'rain-window') {
                await foregroundVisible('.MusicPlayerBar');
                sample.lyrics=await foregroundVisible('[data-music-stage]');
              }
              if (mode === 'rain-window') {
                const shared = await page.evaluate(() => ({
                  copies: effectsEntryProbe.sceneCopies,
                  rainCopies: effectsEntryProbe.rainSceneCopies,
                  exactCanvas: effectsEntryProbe.copiedScenes.has(effectsEntryProbe.background),
                  videosBefore: effectsEntryProbe.videoCount, videosAfter: effectsEntryProbe.videos.length,
                }));
                assert.ok(shared.copies >= 2 && shared.exactCanvas, 'rain samples the shared background without creating a second media owner');
                assert.ok(shared.rainCopies >= 2, 'rain texture copies must be observed independently of glass consumers');
                assert.equal(shared.videosAfter, shared.videosBefore, 'rain must not create a second video, including detached elements');
                sample.sharedBackground = shared;
              }
              return {...sample, identity, composition, contexts: await released()};
            });
          }
          await check('sonnet-rain-frame-sampling', async () => {
            const measurements = {};
            for (const [name, visualizer, effect] of [['rain', 'partita', 'rain-window'], ['sonnet', 'sonnet', 'none'], ['combined', 'sonnet', 'rain-window']]) {
              await setMode(visualizer);
              await setEffect(effect);
              await page.waitForTimeout(350);
              measurements[name] = await page.evaluate(() => new Promise(resolve => {
                const intervals = [];
                const copiesBefore = effectsEntryProbe.rainSceneCopies;
                let last = 0, start = 0;
                function frame(now) {
                  if (!start) start = now;
                  if (last) intervals.push(now - last);
                  last = now;
                  if (now - start < 1000) return requestAnimationFrame(frame);
                  intervals.sort((a,b) => a-b);
                  resolve({frames: intervals.length, p50: intervals[Math.floor(intervals.length * .5)], p95: intervals[Math.floor(intervals.length * .95)], backgroundCopies: effectsEntryProbe.rainSceneCopies - copiesBefore});
                }
                requestAnimationFrame(frame);
              }));
              assert.ok(measurements[name].frames > 0);
              assert.equal(measurements[name].backgroundCopies, 0, `${name}: static forest must not be copied every frame`);
            }
            await screenshot('sonnet-rain-combined');
            await setMode('partita');
            return measurements;
          });
          await check('pause-and-control-hit-testing', async () => {
            await setEffect('snow');
            const count = await page.evaluate(() => harness.commands.filter(c => c?.scope === 'music' && c.action === 'toggle').length);
            await page.locator('[data-music-action="toggle"]').click();
            assert.equal(await page.evaluate(() => harness.commands.filter(c => c?.scope === 'music' && c.action === 'toggle').length),
              count + 1, 'real control click must reach the playback bridge through effects');
            await page.evaluate(() => {
              effectSnapshot = {...effectSnapshot, revision: effectSnapshot.revision + 1, playing: false, status: 'paused'};
              harness.receive(effectSnapshot);
            });
            const samples = {};
            for (const mode of ['snow', 'rain-window', 'star-river']) {
              await setEffect(mode);
              samples[mode] = await pixels(mode);
            }
            await screenshot('paused-star-river');
            return samples;
          });
          await check('three-d-shared-star-river', async () => {
            await setEffect('none');
            await setMode('3d');
            assert.equal(await page.locator('[data-music-stage] canvas').count(), 1);
            await page.evaluate(() => effectsEntryProbe.threeCanvas = document.querySelector('[data-music-stage] canvas'));
            await setEffect('star-river');
            assert.equal(await page.locator('[data-music-stage] canvas').count(), 1);
            assert.equal(await page.locator('canvas[data-visualizer-effect="star-river"]').count(), 0);
            assert.equal(await page.evaluate(() => effectsEntryProbe.threeCanvas === document.querySelector('[data-music-stage] canvas')), true,
              '3d must keep its original canvas when star-river is selected');
            const shared = await pixels('3d-star-river', '[data-music-stage] canvas');
            await screenshot('three-d-shared-star-river');
            await page.evaluate(()=>harness.receive({...effectLyrics,lines:[]}));
            await page.locator('[data-music-stage][data-visualizer-state="ready"]').waitFor();
            const noLyrics=await pixels('3d-star-river-without-lyrics','[data-music-stage] canvas');
            assert.equal(await page.locator('[data-music-stage] canvas').count(),1);
            assert.equal(await page.locator('canvas[data-visualizer-effect="star-river"]').count(),0);
            await page.evaluate(()=>harness.receive(effectLyrics));
            await setEffect('rain-window');
            await pixels('rain-window');
            const rainLyrics=await foregroundVisible('[data-music-stage]');
            await screenshot('three-d-rain-window');
            await setEffect('star-river');
            await setMode('partita');
            await page.locator('canvas[data-visualizer-effect="star-river"]').waitFor();
            assert.equal(await page.locator('[data-music-stage] canvas').count(), 0);
            const independent = await pixels('star-river');
            return {shared, noLyrics, rainLyrics, independent, contexts: await released()};
          });
          await check('wallpaper-roundtrip', async () => {
            await setMode('partita');
            await setEffect('rain-window');
            await page.evaluate(() => effectsEntryProbe.beforeNavigation = document.querySelector('.MusicReferenceStage'));
            await page.locator('[data-titlebar-music]').click();
            await page.locator('[data-music-root]').waitFor({state: 'detached'});
            const oldContexts = await released();
            assert.ok(oldContexts.every(item => !item.connected && item.lost),
              'leaving music must release every old independent effect context');
            await page.locator('[data-titlebar-music]').click();
            await page.locator('[data-music-root][data-screen="home"]').waitFor();
            assert.equal(await page.locator('.MusicEffects').count(), 0);
            await page.locator('[data-music-open-player]').click();
            await page.evaluate(() => harness.receive(effectLyrics));
            await page.locator('.MusicEffects[data-effect-mode="rain-window"][data-effect-state="ready"]').waitFor();
            await settings();
            assert.equal(await effectSelect.inputValue(), 'rain-window');
            await closePanel();
            assert.equal(await page.evaluate(() => effectsEntryProbe.beforeNavigation !== document.querySelector('.MusicReferenceStage')), true);
            assert.equal(await page.locator('.MusicReferenceStage').count(), 1);
            const restored = await pixels('rain-window');
            await screenshot('wallpaper-return-rain-window');
            return {oldContexts, restored};
          });
          await check('back-home-and-return', async () => {
            const before = result.leaves.length;
            await page.locator('[data-music-return]').click();
            await page.locator('[data-music-root][data-screen="home"]').waitFor();
            assert.equal(await page.locator('.MusicEffects').count(), 0);
            await page.waitForTimeout(200);
            assert.equal(result.leaves.length, before, 'home must not load effect leaves even with a persisted selection');
            const contexts = await released();
            assert.ok(contexts.every(item => !item.connected && item.lost));
            await page.locator('[data-music-open-player]').click();
            await page.locator('.MusicEffects[data-effect-mode="rain-window"][data-effect-state="ready"]').waitFor();
            return {contexts};
          });
          // A nonblank raw canvas alone cannot detect an opaque Gallery background above it.
          for (const mode of ['snow', 'rain-window', 'star-river']) {
            await check(`gallery-${mode}-composition`, async () => {
              await setMode('gallery');
              await setEffect(mode);
              const raw = await pixels(mode);
              const composition = await composited(`gallery-${mode}`);
              const lyrics=mode==='rain-window'?await foregroundVisible('[data-music-stage]'):null;
              return {raw, composition, lyrics};
            });
          }
          await check('final-release-and-errors', async () => {
            await setEffect('none');
            assert.equal(await page.locator('canvas[data-visualizer-effect]').count(), 0);
            const contexts = await released();
            assert.ok(contexts.every(item => !item.connected && item.lost));
            const violations = await page.evaluate(() => effectsEntryProbe.violations);
            assert.deepEqual(violations, []);
            assert.deepEqual(result.pageErrors, []);
            assert.deepEqual(result.cspErrors, []);
            assert.deepEqual(result.consoleErrors, []);
            assert.deepEqual(result.failedResponses, []);
            assert.deepEqual(result.remoteRequests, []);
            const wallpaperMutations = await page.evaluate(() => harness.commands.filter(c =>
              c?.scope === 'wallpaper' && ['apply', 'stop', 'import'].includes(c.action)));
            assert.deepEqual(wallpaperMutations, []);
            return {contexts, violations};
          });
          const failed = result.checks.filter(check => check.status === 'failed');
          result.status = failed.length ? 'failed' : 'passed';
          assert.deepEqual(failed.map(check => `${check.name}: ${check.failure.message}`), []);
        } catch (error) {
          result.status = 'failed';
          result.failure = {name: error.name, message: error.message, stack: error.stack};
          await screenshot('failure').catch(() => {});
          throw error;
        } finally {
          Object.assign(result, await page.evaluate(() => ({
            commands: harness.commands, contexts: effectsEntryProbe.releaseState(),
            securityPolicyViolations: effectsEntryProbe.violations,
          })).catch(() => ({})));
          await page.close();
          await writeFile(path.join(evidence, 'report.json'), JSON.stringify(report, null, 2) + '\n');
        }
      });
    }
    report.status = report.scenarios.every(result => result.status === 'passed') ? 'passed' : 'failed';
  } catch (error) {
    report.status = 'failed';
    report.failure = {name: error.name, message: error.message, stack: error.stack};
    throw error;
  } finally {
    await writeFile(path.join(evidence, 'report.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(`Music effects entry evidence: ${evidence}`);
    await browser?.close();
    if (server.listening) await new Promise(resolve => server.close(resolve));
  }
});
