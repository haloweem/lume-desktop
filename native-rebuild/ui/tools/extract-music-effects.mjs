// Build-time only: historical input is read-only; emitted files stay under music-effects.
import {createRequire} from 'node:module';
import {existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {patchStarRiverCover} from './patch-star-river-cover.mjs';

const argument = process.argv.slice(2).find(value => !value.startsWith('--'));
if (!argument) throw new Error('Usage: node tools/extract-music-effects.mjs <read-only upstream> [--check]');
const upstream = path.resolve(argument);
const check = process.argv.includes('--check');
const output = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../music-effects/reference');
const effects = 'src/components/visualizer/effects/';
const require = createRequire(path.join(upstream, 'package.json'));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
const slash = value => value.replaceAll('\\', '/');
const sources = new Map(), outputs = new Map(), adaptations = [];

function original(relative) {
  const bytes = readFileSync(path.join(upstream, relative));
  sources.set(relative, sha256(bytes));
  outputs.set(`source/upstream/${relative}`, bytes);
  return bytes.toString('utf8').replaceAll('\r\n', '\n');
}
function replace(text, before, after, expected = 1) {
  const count = text.split(before).length - 1;
  if (count !== expected) throw new Error(`Historical patch drift (${count}, expected ${expected}): ${before.slice(0, 100)}`);
  return text.split(before).join(after);
}
function section(text, from, to) {
  if (text.split(from).length !== 2 || text.split(to).length !== 2) throw new Error(`Historical section drift: ${from}`);
  const start = text.indexOf(from) + from.length, end = text.indexOf(to, start);
  if (end < start) throw new Error(`Historical section order: ${from}`);
  return text.slice(start, end);
}
function removeSection(text, from, to) {
  return replace(text, from + section(text, from, to), '');
}
function esbuildPackage() {
  try { return require.resolve('esbuild/package.json'); } catch (error) {
    const pnpm = path.join(upstream, 'node_modules/.pnpm');
    if (!existsSync(pnpm)) throw error;
    const name = readdirSync(pnpm).find(name => name === 'esbuild@0.25.12');
    if (!name) throw error;
    return path.join(pnpm, name, 'node_modules/esbuild/package.json');
  }
}
const esbuildPath = esbuildPackage();
const esbuild = createRequire(esbuildPath)('./lib/main.js');
if (esbuild.version !== '0.25.12') throw new Error(`Unexpected esbuild: ${esbuild.version}`);
const layer = original(`${effects}VisualizerEffectLayer.tsx`);
const starHost = original(`${effects}StarRiverCanvas.tsx`);
original('src/components/visualizer/VisualizerShell.tsx');

// Keep the original snow WebGL setup/frame loop. Remove unused media decoding and app bridges.
function snowSource() {
  const vertex = section(layer, 'const VERTEX_SHADER_SOURCE = ', '\n\nconst compileShader');
  let compile = 'const compileShader' + section(layer, '\n\nconst compileShader', '\n\ninterface ShaderCanvasProps');
  compile = replace(compile, 'if (!shader) return null;', "if (!shader) throw new Error('Snow shader allocation failed');");
  compile = replace(compile, "        console.warn('[VisualizerEffectLayer] Shader compile failed:', gl.getShaderInfoLog(shader));",
    '        const message = gl.getShaderInfoLog(shader);');
  compile = replace(compile, '        return null;', "        throw new Error('Snow shader compile failed: ' + message);");
  const shader = section(layer, 'const ShaderCanvas: React.FC<ShaderCanvasProps>', '\nconst StarsCanvas:');
  let body = section(shader, '        const gl = canvas.getContext', '\n    }, [');
  body = '        const gl = canvas.getContext' + body;
  body = replace(body, '        if (!gl) return;', "        if (!gl) throw new Error('Snow WebGL2 unavailable');\n        try {");
  body = replace(body, '        const vertexShader =', '        vertexShader =');
  body = replace(body, '        const pixelShader =', '        pixelShader =');
  body = replace(body, '        const program =', '        program =');
  body = replace(body, '        if (!program) return;', "        if (!program) throw new Error('Snow program allocation failed');");
  const badLink = "            console.warn('[VisualizerEffectLayer] Program link failed:', gl.getProgramInfoLog(program));";
  body = replace(body, badLink + section(body, badLink, '\n        const topVisualLayer'),
    "            throw new Error('Snow program link failed: ' + gl.getProgramInfoLog(program));\n        }\n");
  body = removeSection(body, '        const topVisualLayer =', '\n        const positionBuffer');
  body = replace(body, '        const positionBuffer =', '        positionBuffer =');
  body = replace(body, '        const backgroundTexture =', '        backgroundTexture =');
  body = removeSection(body, "        const uploadCanvas = document.createElement('canvas');", '\n        let disposed = false;');
  body = replace(body, '        let disposed = false;', '');
  body = replace(body, '        let animationFrame = 0;', '');
  body = replace(body, '                textureDirty = true;', '');
  body = removeSection(body, '        const uploadBackgroundTexture =', '        const render =');
  body = replace(body, '            if (disposed) return;', '            if (disposed) return;\n            try {');
  body = replace(body, '                const hasBackgroundTexture = uploadBackgroundTexture();', '');
  body = replace(body, '                if (!requiresBackground || hasBackgroundTexture) {', '                {');
  body = replace(body, '                    publishLiquidGlassWebGLSceneFrame(canvas, now);\n                    topVisualLayer.requestFrame();', '');
  body = replace(body, '            if (!paused || !didRender) animationFrame = requestAnimationFrame(render);',
    `            if (!paused || !didRender) animationFrame = requestAnimationFrame(render);
            } catch (error) { dispose(); onError?.(error); }`);
  const cleanupStart = '        return () => {\n            disposed = true;';
  body = replace(body, cleanupStart + body.slice(body.indexOf(cleanupStart) + cleanupStart.length), `
        return { update() {}, dispose };
        } catch (error) { dispose(); throw error; }
        function dispose() {
            if (disposed) return;
            disposed = true;
            cancelAnimationFrame(animationFrame);
            if (positionBuffer) gl.deleteBuffer(positionBuffer);
            if (backgroundTexture) gl.deleteTexture(backgroundTexture);
            if (program) gl.deleteProgram(program);
            if (vertexShader) gl.deleteShader(vertexShader);
            if (pixelShader) gl.deleteShader(pixelShader);
            gl.getExtension('WEBGL_lose_context')?.loseContext();
            canvas.width = canvas.height = 1;
        }`);
  adaptations.push('Snow: extract ShaderCanvas setup, uniforms, resize and frame loop verbatim; remove React, app frame bridges and unused background decoding. Preserve 30fps, DPR cap 1.5, staticMode=false and transparent overlay shader. Add error propagation and complete GL cleanup.');
  return `import {SNOW_OVERLAY_SHADER_SOURCE} from './overlayShaders';
const VERTEX_SHADER_SOURCE = ${vertex}
${compile}
export function mountSnowCanvas(canvas, {onError} = {}) {
  const fragmentShader = SNOW_OVERLAY_SHADER_SOURCE, paused = false, usesMipmaps = false;
  const maxPixelRatio = 1.5, targetFps = 30;
  let disposed = false, animationFrame = 0;
  let vertexShader = null, pixelShader = null, program = null, positionBuffer = null, backgroundTexture = null;
${body}
}`;
}

function starSource() {
  let body = section(starHost, '        const renderer =', '\n    }, []);');
  body = '        renderer =' + body;
  body = replace(body, '        const river = new StarRiverParticles();\n        riverRef.current = river;', '        river = new StarRiverParticles();');
  body = replace(body, "        const layer = registerLumeTopVisualLayer({ canvas, order: 10, role: 'effect' });", '');
  body = replace(body, '        const observer =', '        observer =');
  body = replace(body, '        let frame = 0;', '');
  body = replace(body, '        const draw = (now: number) => {', '        const draw = (now: number) => {\n            if (disposed) return;\n            try {');
  body = replace(body, 'pausedRef.current', 'false');
  body = replace(body, '                publishLiquidGlassWebGLSceneFrame(canvas);\n                layer.requestFrame();', '');
  body = replace(body, '            frame = requestAnimationFrame(draw);', '            frame = requestAnimationFrame(draw);\n            } catch (error) { dispose(); onError?.(error); }');
  const cleanup = '        return () => {';
  body = replace(body, cleanup + body.slice(body.indexOf(cleanup) + cleanup.length), `
        const update = state => river.setCoverUrl(state.current?.artwork || null);
        update(state);
        return {update, dispose};`);
  adaptations.push('Star river: extract StarRiverCanvas fixed-camera mount and unchanged point shaders. Adapt StarRiverParticles to cancel pending cover images on replacement/disposal, preserving Three 0.128 texture defaults and JPEG format selection. Remove React/global compositor bridges; use current.artwork, keep staticMode=false, camera 44deg/16/0.06, DPR cap 1.5 and dt cap 0.08. Add constructor/frame failure cleanup and forceContextLoss. Externalize Three to existing music-visualizers/vendor.');
  return `import * as THREE from 'three';
import {StarRiverParticles} from './StarRiverParticles';
export function mountStarRiverCanvas(canvas, state, {onError} = {}) {
    let renderer = null, river = null, observer = null, frame = 0, disposed = false;
    try {
${body}
    } catch (error) { dispose(); throw error; }
    function dispose() {
        if (disposed) return;
        disposed = true;
        cancelAnimationFrame(frame);
        observer?.disconnect();
        river?.dispose();
        renderer?.dispose();
        renderer?.forceContextLoss();
        if (!renderer) {
            const context = canvas.getContext('webgl2') || canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
            context?.getExtension('WEBGL_lose_context')?.loseContext();
        }
        canvas.width = canvas.height = 1;
    }
}`;
}

function rainSource() {
  let text = original(`${effects}rainWindowPhysics.js`);
  // Scope sprite canvases per runtime so two hosts cannot dispose each other's sprites.
  text = replace(text, 'const createRainWindowEffect = (function() {', 'const createRainWindowEffect = function(canvas, resources, settings) { return (function() {');
  text = replace(text, '})()\n;', '})()(canvas, resources, settings);\n};');
  text = replace(text, '    renderDropsGfx(resources.dropColor, resources.dropAlpha);',
    '    var frame = 0;\n    try {\n    renderDropsGfx(resources.dropColor, resources.dropAlpha);');
  text = replace(text, "    if (!gl) { console.error('[RainWindow] WebGL2 not supported'); return null; }",
    "    if (!gl) throw new Error('Rain Window WebGL2 unavailable');");
  text = replace(text, '    if (!prog) return null;', "    if (!prog) throw new Error('Rain Window shader program failed');");
  text = replace(text, '      if (resources.background instanceof HTMLVideoElement && resources.background.readyState >= 2) {',
    `      var backgroundVersion = resources.background.dataset?.glassFrameVersion;
      var backgroundChanged = backgroundDirty || backgroundVersion !== lastBackgroundVersion;
      if ((resources.background instanceof HTMLVideoElement && resources.background.readyState >= 2)
          || (resources.background instanceof HTMLCanvasElement && backgroundChanged && resources.background.width > 0 && resources.background.height > 0)) {
        backgroundDirty = false;
        backgroundDirtyBlur = true;
        lastBackgroundVersion = backgroundVersion;`);
  text = replace(text, '        if (_videoFrameCounter % 3 === 0) {',
    '        if (now - lastBackgroundBlur >= 50) {\n          lastBackgroundBlur = now;\n          backgroundDirtyBlur = false;');
  text = replace(text, '    var _videoFrameCounter = 0;', `    var _videoFrameCounter = 0;
    var lastBackgroundVersion = resources.background.dataset?.glassFrameVersion;
    var backgroundDirty = false, backgroundDirtyBlur = false;
    var lastBackgroundBlur = 0;
    var onBackgroundFrame = function() { backgroundDirty = true; backgroundDirtyBlur = true; };
    resources.background.addEventListener?.('lume-glass-media-frame', onBackgroundFrame);`);
  text = replace(text, '      if (!lastTime) lastTime = now;', `      // Flush the final changed background even when a video pauses between blur updates.
      if (backgroundDirtyBlur && now - lastBackgroundBlur >= 50) {
        backgroundDirtyBlur = false;
        lastBackgroundBlur = now;
        drawImageCover(texBgCtx, resources.background, BG_W, BG_H);
        var finalBlurRadius = Math.max(1, Math.round(7 * Math.max(W / BG_W, H / BG_H) * (0.1 + newBlur * 1.8)));
        blurCanvas(texBgCanvas, finalBlurRadius);
        updateTextureFromCanvas(gl, texBgCanvas, 3);
      }
      if (!lastTime) lastTime = now;`);
  text = replace(text, '      if (!running || cleanup) { running = false; return; }',
    '      if (!running || cleanup) { running = false; return; }\n      try {');
  text = replace(text, '      requestAnimationFrame(render);',
    '      frame = requestAnimationFrame(render);\n      } catch (error) { release(); resources.onError?.(error); }');
  text = replace(text, '    requestAnimationFrame(render);', '    frame = requestAnimationFrame(render);');
  text = replace(text, `    function onResize() {
      resizePending = true;
    }`, `    var onResize = function() {
      resizePending = true;
    };`);
  text = replace(text, `    return function() {
      running = false;
      cleanup = true;
      if (resizeObserver) resizeObserver.disconnect();
      window.removeEventListener('resize', onResize);
    };`, `    return release;
    } catch (error) { release(); throw error; }
    function release() {
      if (cleanup) return;
      running = false;
      cleanup = true;
      cancelAnimationFrame(frame);
      resources.background.removeEventListener?.('lume-glass-media-frame', onBackgroundFrame);
      if (resizeObserver) resizeObserver.disconnect();
      if (onResize) window.removeEventListener('resize', onResize);
      if (gl) gl.getExtension('WEBGL_lose_context')?.loseContext();
      [liquidCanvas, dropletsCanvas, texFgCanvas, texBgCanvas, shineCanvas, clearDropletsGfx, ...dropsGfx].forEach(function(item) {
        if (item) item.width = item.height = 1;
      });
      dropsGfx = [];
      clearDropletsGfx = null;
      drops = [];
      canvas.width = canvas.height = 1;
    }`);
  // Defaults belong to the actual historical overlay call, not the physics fallback values.
  const settings = 'rain_intensity: 0.6,' + section(layer, 'rain_intensity: 0.6,', '\n            });\n        }).catch');
  text += `\nexport const RAIN_WINDOW_SETTINGS = Object.freeze({${settings}});\n`;
  adaptations.push('Rain: preserve physics/shaders and overlay defaults. Shared canvas textures refresh only on compositor frame events/version changes; static backgrounds avoid repeated copies, blur and GPU uploads. Preserve video refresh. Isolate sprite storage, cancel RAF/listeners and release owned canvases/GL on cleanup/failure. Never load backgroundMedia.');
  return text;
}

const entries = new Map([
  ['snow', snowSource()],
  ['star-river', starSource()],
  ['rain-window', rainSource()],
]);
const options = {
  bundle: true, format: 'esm', platform: 'browser', target: 'es2022',
  write: false, minify: false, charset: 'ascii', legalComments: 'eof', logLevel: 'warning',
  absWorkingDir: upstream, tsconfigRaw: {compilerOptions: {useDefineForClassFields: false}},
  banner: {js: '// Generated by tools/extract-music-effects.mjs. Do not edit; see manifest.json and licenses/.'},
  plugins: [{name: 'read-only-effect-leaves', setup(build) {
    build.onResolve({filter: /^three$/}, () => ({path: '../../music-visualizers/vendor/three.module.js', external: true}));
    build.onLoad({filter: /\.[jt]s$/}, args => {
      const relative = slash(path.relative(upstream, args.path));
      if (!relative.startsWith(effects)) throw new Error(`Unexpected effect dependency: ${relative}`);
      let contents = original(relative);
      if (relative === `${effects}StarRiverParticles.ts`) {
        contents = patchStarRiverCover(contents);
        outputs.set('source/StarRiverParticles.native.ts', Buffer.from(contents));
      }
      return {contents, loader: args.path.endsWith('.ts') ? 'ts' : 'js', resolveDir: path.dirname(args.path)};
    });
  }}],
};
for (const [name, contents] of entries) {
  outputs.set(`source/${name}.native.ts`, Buffer.from(contents));
  const result = await esbuild.build({...options, stdin: {
    contents, loader: 'ts', resolveDir: path.join(upstream, effects), sourcefile: `${name}.native.ts`,
  }});
  if (result.outputFiles.length !== 1) throw new Error('Expected one ESM leaf');
  outputs.set(`${name}.js`, result.outputFiles[0].contents);
}
for (const name of ['rain-window-drop-color.png', 'rain-window-drop-alpha.png']) {
  const relative = `src/assets/visualizer-effects/${name}`;
  original(relative);
  outputs.set(`assets/${name}`, readFileSync(path.join(upstream, relative)));
}
for (const [name, source] of [
  ['Folia-AGPL-3.0.txt', path.join(upstream, 'LICENSE')],
  ['Folia-NOTICE.md', path.resolve(upstream, '../NOTICE.md')],
  ['Mineradio-GPL-3.0.txt', path.resolve(upstream, '../../../LICENSE')],
  ['Three-MIT.txt', path.join(upstream, 'node_modules/three/LICENSE')],
  ['esbuild-MIT.txt', path.join(path.dirname(esbuildPath), 'LICENSE.md')],
]) outputs.set(`licenses/${name}`, readFileSync(source));
outputs.set('.gitattributes', Buffer.from('* -text\n'));
const manifest = {
  schema: 1, builder: 'esbuild', builderVersion: esbuild.version,
  reference: 'lume1.0.1/third_party/folia-major/upstream',
  sources: Object.fromEntries([...sources].sort(([a], [b]) => a.localeCompare(b, 'en'))),
  sharedThree: {
    path: '../../music-visualizers/vendor/three.module.js',
    sha256: sha256(readFileSync(path.resolve(output, '../../music-visualizers/vendor/three.module.js'))),
  },
  adaptations,
  outputs: Object.fromEntries([...outputs].sort(([a], [b]) => a.localeCompare(b, 'en')).map(([name, bytes]) => [name, sha256(bytes)])),
};
outputs.set('manifest.json', Buffer.from(JSON.stringify(manifest, null, 2) + '\n'));
const mismatches = [];
for (const [name, bytes] of outputs) {
  const destination = path.resolve(output, name);
  if (!destination.startsWith(output + path.sep)) throw new Error('Output escapes music-effects/reference');
  if (check) {
    if (!existsSync(destination) || !readFileSync(destination).equals(Buffer.from(bytes))) mismatches.push(name);
  } else {
    mkdirSync(path.dirname(destination), {recursive: true});
    writeFileSync(destination, bytes);
  }
}
if (mismatches.length) throw new Error(`Reproduction mismatch: ${mismatches.join(', ')}`);
console.log(`${check ? 'Verified' : 'Generated'} ${outputs.size} effect files; ${sources.size} original sources; esbuild ${esbuild.version}.`);
