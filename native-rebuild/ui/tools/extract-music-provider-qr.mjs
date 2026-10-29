// Build-time only. No installs; historical dependencies remain read-only.
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync,existsSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const argument = process.argv.slice(2).find(value=>!value.startsWith('--'));
if (!argument) throw new Error('Usage: node tools/extract-music-provider-qr.mjs <upstream> [--check]');
const upstream = path.resolve(argument), check = process.argv.includes('--check');
const output = fileURLToPath(new URL('../music-provider-qr/',import.meta.url));
const require = createRequire(path.join(upstream,'package.json'));
const esbuild = require('./node_modules/.pnpm/esbuild@0.25.12/node_modules/esbuild/lib/main.js');
const qrPackage = require('qrcode/package.json');
if (qrPackage.version !== '1.5.4' || esbuild.version !== '0.25.12') throw new Error('Historical dependency version drift');
const result = await esbuild.build({
  absWorkingDir:upstream,entryPoints:['node_modules/qrcode/lib/browser.js'],bundle:true,
  platform:'browser',format:'esm',target:'es2020',minify:true,write:false,metafile:true,
  banner:{js:'// qrcode 1.5.4 + dijkstrajs. See LICENSE.txt and SOURCES.json. Generated; do not edit.'},
});
const sha256 = value=>createHash('sha256').update(value).digest('hex');
const files = new Map([['encoder.js',Buffer.from(result.outputFiles[0].contents)]]);
files.set('LICENSE.txt',Buffer.from(['qrcode','dijkstrajs'].map(name=>{
  const dir = path.dirname(require.resolve(`${name}/package.json`));
  const license = name === 'qrcode' ? 'license' : 'LICENSE.md';
  return `${name}\n${readFileSync(path.join(dir,license),'utf8')}`;
}).join('\n\n')));
const sources = {};
for (const file of Object.keys(result.metafile.inputs).sort())
  sources[file.replaceAll('\\','/')] = sha256(readFileSync(path.join(upstream,file)));
files.set('SOURCES.json',Buffer.from(JSON.stringify({upstream:'lume1.0.1/third_party/folia-major/upstream',
  command:'node tools/extract-music-provider-qr.mjs <upstream>',esbuild:esbuild.version,
  packages:{qrcode:qrPackage.version,dijkstrajs:require('dijkstrajs/package.json').version},
  sources,encoderSha256:sha256(files.get('encoder.js'))},null,2)+'\n'));
// Extract the same Lucide glyphs used by the historical login and account tabs.
const React = require('react'), {renderToStaticMarkup} = require('react-dom/server'), icons = require('lucide-react');
const iconFile = fileURLToPath(new URL('../music-icons.js',import.meta.url));
const iconSource = readFileSync(iconFile,'utf8');
const iconMap = JSON.parse(iconSource.slice(iconSource.indexOf('{'),iconSource.lastIndexOf('}')+1));
for (const [key,name] of [['logout','LogOut'],['loader','LoaderCircle']])
  iconMap[key] = renderToStaticMarkup(React.createElement(icons[name],{className:'MusicIcon','aria-hidden':true}));
const iconBytes = Buffer.from('// Generated from the reference Lucide icons; see assets/music/SOURCES.md.\nexport const MUSIC_ICONS = '+JSON.stringify(iconMap,null,2)+';\n');
function emit(file,bytes) {
  if (check) {
    if (!existsSync(file) || !readFileSync(file).equals(bytes)) throw new Error(`Generated asset drift: ${file}`);
  } else {mkdirSync(path.dirname(file),{recursive:true});writeFileSync(file,bytes);}
}
for (const [name,bytes] of files) emit(path.join(output,name),bytes);
emit(iconFile,iconBytes);
console.log(`${check ? 'Verified' : 'Generated'} local QR bundle and Lucide account glyphs`);
