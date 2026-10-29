import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import {mkdirSync, readFileSync, writeFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

// Build-time only. Bundles a full Three.js and the post-processing add-ons
// 音符律动 uses into one local ESM file; the lyric styles' stripped build
// has no render targets, so bloom and depth of field cannot run on it.
// node build-motif-three.mjs <dir containing node_modules/three> [--check]
// Set LUME_ESBUILD_PATH to an esbuild 0.25.12 package directory.
const ui = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.join(ui, 'music-visualizers/motif-vendor');
const args = process.argv.slice(2);
const check = args.includes('--check');
const reference = path.resolve(args.find(arg => !arg.startsWith('--')) ?? '.');
const threeRoot = path.join(reference, 'node_modules/three');
const require = createRequire(import.meta.url);
const sha256 = value => createHash('sha256').update(value).digest('hex');
const esbuild = require(process.env.LUME_ESBUILD_PATH || 'esbuild');
if (esbuild.version !== '0.25.12') throw new Error(`Expected esbuild 0.25.12; found ${esbuild.version}`);
const threePackage = JSON.parse(readFileSync(path.join(threeRoot, 'package.json'), 'utf8'));
if (threePackage.version !== '0.185.1') throw new Error(`Expected three 0.185.1; found ${threePackage.version}`);

const entry = `
export * from 'three';
export {EffectComposer} from 'three/examples/jsm/postprocessing/EffectComposer.js';
export {RenderPass} from 'three/examples/jsm/postprocessing/RenderPass.js';
export {UnrealBloomPass} from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';
export {BokehPass} from 'three/examples/jsm/postprocessing/BokehPass.js';
export {OutputPass} from 'three/examples/jsm/postprocessing/OutputPass.js';
export {RoomEnvironment} from 'three/examples/jsm/environments/RoomEnvironment.js';
`;
const result = await esbuild.build({
  stdin: {contents: entry, resolveDir: reference, sourcefile: 'motif-three-entry.js'},
  bundle: true, format: 'esm', minify: true, legalComments: 'inline', write: false,
  target: 'es2022', logLevel: 'silent',
});
const bundle = Buffer.from(result.outputFiles[0].contents);
const license = readFileSync(path.join(threeRoot, 'LICENSE'));
const manifest = {
  three: threePackage.version, esbuild: esbuild.version, license: 'MIT',
  source: 'https://github.com/mrdoob/three.js', exports: entry.trim().split('\n'),
  files: {'three.module.js': sha256(bundle), 'LICENSE-three.txt': sha256(license)},
};
const files = {'three.module.js': bundle, 'LICENSE-three.txt': license,
  'manifest.json': Buffer.from(JSON.stringify(manifest, null, 2) + '\n')};
if (check) {
  for (const [name, data] of Object.entries(files))
    if (sha256(readFileSync(path.join(out, name))) !== sha256(data)) throw new Error(`${name} is stale; rebuild`);
  console.log('motif-vendor matches');
} else {
  mkdirSync(out, {recursive: true});
  for (const [name, data] of Object.entries(files)) writeFileSync(path.join(out, name), data);
  console.log(`motif-vendor: three ${threePackage.version}, ${(bundle.length / 1024).toFixed(0)} KB`);
}
