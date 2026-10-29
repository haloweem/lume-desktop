import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import {existsSync, readFileSync, mkdirSync, writeFileSync, readdirSync} from 'node:fs';
import {fileURLToPath, pathToFileURL} from 'node:url';
import path from 'node:path';
import {patchStarRiverCover} from './patch-star-river-cover.mjs';

// Build-time only. The original checkout is always read-only; all outputs stay in vendor.
const ui = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const vendor = path.join(ui, 'music-visualizers/vendor');
const args = process.argv.slice(2);
const check = args.includes('--check');
const referenceArg = args.find(arg => !arg.startsWith('--'));
const reference = referenceArg ? path.resolve(referenceArg) : null;
const sourceRoot = reference ?? path.join(vendor, 'source');
const esbuildVersion = '0.25.12';
const require = createRequire(import.meta.url);
const sha256 = value => createHash('sha256').update(value).digest('hex');
const read = relative => readFileSync(path.join(sourceRoot, relative));
const stagePath = 'src/components/visualizer/mineradio3d/stageRuntime.ts';
const sourcePaths = [
  stagePath,
  'src/components/visualizer/colorMix.ts',
  'src/utils/lyrics/graphemeTiming.ts',
  'src/components/visualizer/effects/StarRiverParticles.ts',
  'src/components/visualizer/effects/starRiverPointShaders.ts',
  'src/components/visualizer/effects/definition.ts',
  'src/services/baseThemes.ts',
  'src/types.ts',
  'src/components/visualizer/mineradio3d/VisualizerMineradio3D.tsx',
  'package.json',
  'tsconfig.json',
  'node_modules/three/build/three.module.js',
  'node_modules/three/package.json',
];

function loadEsbuild() {
  const candidates = [process.env.LUME_ESBUILD_PATH];
  if (reference) {
    candidates.push(path.join(reference, 'node_modules/esbuild'));
    const pnpm = path.join(reference, 'node_modules/.pnpm');
    if (existsSync(pnpm)) candidates.push(...readdirSync(pnpm)
      .filter(name => name === `esbuild@${esbuildVersion}`)
      .map(name => path.join(pnpm, name, 'node_modules/esbuild')));
  }
  candidates.push('esbuild');
  for (const candidate of candidates.filter(Boolean)) {
    try {
      const loaded = require(candidate);
      if (loaded.version !== esbuildVersion) throw new Error(`Expected esbuild ${esbuildVersion}; found ${loaded.version}`);
      const root = path.dirname(require.resolve(`${candidate}/package.json`));
      return {...loaded, license: readFileSync(path.join(root, 'LICENSE.md'))};
    } catch (error) {
      if (candidate === process.env.LUME_ESBUILD_PATH || !['MODULE_NOT_FOUND', 'ERR_MODULE_NOT_FOUND'].includes(error.code)) throw error;
    }
  }
  throw new Error(`Set LUME_ESBUILD_PATH to an existing esbuild ${esbuildVersion} package directory; no installer is run.`);
}

// Exact, counted substitutions fail closed when the legacy implementation changes.
function patchStage(source) {
  let text = source.replaceAll('\r\n', '\n');
  const replace = (from, to, expected = 1) => {
    if (text.split(from).length - 1 !== expected) throw new Error(`Legacy patch no longer matches: ${from}`);
    text = text.split(from).join(to);
  };
  replace('    paused: boolean;', '    paused: boolean;\n    visible?: boolean;');
  replace('    private destroyed = false;', '    private destroyed = false;\n    private visible = true;\n    private karaokePaintPending = false;');
  replace('        this.state = state;', '        this.state = state;\n        this.visible = state.visible !== false;');
  replace('        this.renderer.setClearColor(0x000000, 0);', '        try {\n        this.renderer.setClearColor(0x000000, 0);');
  replace('        this.update(state);\n    }', `        this.update(state);
        } catch (error) {
            this.destroy();
            throw error;
        }
    }`);
  replace('        this.resizeObserver.disconnect();', '        this.resizeObserver?.disconnect();');
  replace('        this.updateCamera();', '        this.karaokePaintPending = false;\n        this.updateCamera();');
  replace('            if (karaokeChanged && canPaintKaraoke) {', '            if (karaokeChanged && !canPaintKaraoke) this.karaokePaintPending = true;\n            if (karaokeChanged && canPaintKaraoke) {');
  replace('                if (translationChanged && canPaintKaraoke) {', '                if (translationChanged && !canPaintKaraoke) this.karaokePaintPending = true;\n                if (translationChanged && canPaintKaraoke) {');
  replace('        return orbitMoving || rotationMoving || panMoving || trackMoving;', '        return orbitMoving || rotationMoving || panMoving || trackMoving || this.karaokePaintPending;');
  replace('    getCanvas() {', `    // Visibility suspends work without resetting the authored camera or lyric pose.
    setVisible(visible: boolean) {
        if (this.destroyed || this.visible === visible) return;
        this.visible = visible;
        if (!visible) {
            this.stop();
            this.releasePointer();
            this.starRiver?.dispose();
            this.starRiver = null;
        } else {
            this.lastStageFrameAt = performance.now();
            this.resize();
        }
    }

    private releasePointer() {
        const pointer = this.activePointerId;
        this.activePointerId = null;
        this.activePointerMode = null;
        const canvas = this.renderer.domElement;
        if (pointer !== null && canvas.hasPointerCapture?.(pointer)) canvas.releasePointerCapture(pointer);
        canvas.style.cursor = 'grab';
    }

    getCanvas() {`);
  replace('        if (this.destroyed) return;', '        if (this.destroyed || !this.visible) return;', 6);
  // destroy must remain callable while hidden.
  replace('    destroy() {\n        if (this.destroyed || !this.visible) return;', '    destroy() {\n        if (this.destroyed) return;\n        this.releasePointer();');
  replace('        if (this.destroyed || this.animationFrameId !== null) return;', '        if (this.destroyed || !this.visible || this.animationFrameId !== null) return;');
  replace('        if (this.animationFrameId !== null || this.destroyed || this.state.paused) return;', '        if (this.animationFrameId !== null || this.destroyed || !this.visible || this.state.paused) return;');
  for (const signature of [
    '    private readonly onPointerDown = (event: PointerEvent) => {',
    '    private readonly onPointerMove = (event: PointerEvent) => {',
    '    private readonly onWheel = (event: WheelEvent) => {',
    '    private readonly onDoubleClick = () => {',
  ]) replace(signature, `${signature}\n        if (this.destroyed || !this.visible) return;`);
  replace(`        this.renderer.domElement.releasePointerCapture?.(event.pointerId);
        this.activePointerId = null;
        this.activePointerMode = null;
        this.renderer.domElement.style.cursor = 'grab';`, '        this.releasePointer();');
  replace("        this.renderer.domElement.addEventListener('pointercancel', this.onPointerUp);", "        this.renderer.domElement.addEventListener('pointercancel', this.onPointerUp);\n        this.renderer.domElement.addEventListener('lostpointercapture', this.onPointerUp);");
  replace("        this.renderer.domElement.removeEventListener('pointercancel', this.onPointerUp);", "        this.renderer.domElement.removeEventListener('pointercancel', this.onPointerUp);\n        this.renderer.domElement.removeEventListener('lostpointercapture', this.onPointerUp);");
  replace('        this.renderer.dispose();', `        this.renderer.dispose();
        this.renderer.forceContextLoss();
        this.renderer.domElement.width = 1;
        this.renderer.domElement.height = 1;
        this.trackLines = null;
        this.trackVirtualIndices = [];
        this.state = { ...this.state, lines: [] };`);
  return text;
}

export async function extract() {
  const esbuild = loadEsbuild();
  const outputs = new Map();
  outputs.set('.gitattributes', Buffer.from('# Preserve source, license and generated artifact byte hashes on every checkout.\n* -text\n'));
  outputs.set('licenses/esbuild-MIT.txt', esbuild.license);
  const inputs = sourcePaths.map(relative => {
    const bytes = read(relative);
    outputs.set(`source/${relative}`, bytes);
    return {path: relative, sha256: sha256(bytes)};
  });
  const licenseSources = [
    ['Folia-AGPL-3.0.txt', reference ? path.join(reference, 'LICENSE') : path.join(vendor, 'licenses/Folia-AGPL-3.0.txt')],
    ['Folia-NOTICE.md', reference ? path.resolve(reference, '../NOTICE.md') : path.join(vendor, 'licenses/Folia-NOTICE.md')],
    ['Mineradio-GPL-3.0.txt', reference ? path.resolve(reference, '../../../LICENSE') : path.join(vendor, 'licenses/Mineradio-GPL-3.0.txt')],
    ['Three-MIT.txt', reference ? path.join(reference, 'node_modules/three/LICENSE') : path.join(vendor, 'licenses/Three-MIT.txt')],
  ];
  for (const [name, filename] of licenseSources) outputs.set(`licenses/${name}`, readFileSync(filename));
  const source = patchStage(read(stagePath).toString('utf8'));
  outputs.set('source/stageRuntime.native.ts', Buffer.from(source));
  const starSource = patchStarRiverCover(read('src/components/visualizer/effects/StarRiverParticles.ts').toString('utf8'));
  outputs.set('source/StarRiverParticles.native.ts', Buffer.from(starSource));
  const options = {bundle: true, format: 'esm', platform: 'browser', target: 'es2020',
    write: false, legalComments: 'inline', charset: 'ascii', logLevel: 'silent',
    absWorkingDir: sourceRoot, minify: false,
    tsconfigRaw: {compilerOptions: {useDefineForClassFields: false, experimentalDecorators: true}}};
  const stage = await esbuild.build({...options,
    stdin: {contents: source, loader: 'ts', resolveDir: path.join(sourceRoot, path.dirname(stagePath)), sourcefile: 'stageRuntime.ts'},
    banner: {js: '// Generated from Folia Major / Mineradio. See ../SOURCES.md and licenses/.'},
    plugins: [{name: 'local-three', setup(build) {
      build.onResolve({filter: /^three$/}, () => ({path: './three.module.js', external: true}));
      build.onLoad({filter: /[/\\]StarRiverParticles\.ts$/}, args => ({
        contents: starSource, loader: 'ts', resolveDir: path.dirname(args.path),
      }));
    }}],
  });
  const stageText = stage.outputFiles[0].text;
  outputs.set('mineradio-stage.js', Buffer.from(stageText));
  // Export only the Three symbols used by the two original runtime modules.
  const names = [...new Set([...source.matchAll(/THREE\.(\w+)/g),
    ...starSource.matchAll(/THREE\.(\w+)/g),
    ...read('src/components/visualizer/effects/StarRiverParticles.ts').toString().matchAll(/THREE\.(\w+)/g)]
    .map(match => match[1]))].sort();
  const three = await esbuild.build({...options,
    stdin: {contents: `export {${names.join(',')}} from './node_modules/three/build/three.module.js';`,
      resolveDir: sourceRoot, sourcefile: 'three-entry.js'},
    banner: {js: '// Three.js 0.128.0, MIT; see licenses/Three-MIT.txt.'},
  });
  outputs.set('three.module.js', Buffer.from(three.outputFiles[0].text));
  const defaults = await esbuild.build({...options,
    stdin: {contents: "export {DEFAULT_THEME} from './src/services/baseThemes.ts';", resolveDir: sourceRoot, sourcefile: 'default-theme-entry.js'},
    banner: {js: '// Generated from Folia Major baseThemes.ts; AGPL-3.0, see licenses/.'},
  });
  outputs.set('default-theme.js', Buffer.from(defaults.outputFiles[0].text));
  const manifest = {schema: 1, esbuild: esbuild.version, three: JSON.parse(read('node_modules/three/package.json')).version,
    foliaRevision: 'fa057895f916d0c2482785a150dce51ad6fc928c',
    mineradioRevision: '328a087a14e79d5267a3de54d29108c34ed6cf60',
    inputs, outputs: Object.fromEntries([...outputs].map(([name, bytes]) => [name, sha256(bytes)]))};
  outputs.set('manifest.json', Buffer.from(`${JSON.stringify(manifest, null, 2)}\n`));
  const mismatches = [];
  for (const [relative, bytes] of outputs) {
    const filename = path.join(vendor, relative);
    if (check) {
      if (!existsSync(filename) || !readFileSync(filename).equals(bytes)) mismatches.push(relative);
    } else {
      mkdirSync(path.dirname(filename), {recursive: true});
      writeFileSync(filename, bytes);
    }
  }
  if (mismatches.length) throw new Error(`Reproduction mismatch: ${mismatches.join(', ')}`);
  console.log(`${check ? 'Verified' : 'Generated'} ${outputs.size} files; esbuild ${esbuild.version}, Three ${manifest.three}.`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) await extract();
