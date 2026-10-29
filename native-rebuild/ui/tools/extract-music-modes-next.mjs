// Build-time only. Bundles the Folia 0.7 lyric renderers Lume adds on top of the
// historical reference bundle (cappella, diorama, pendolo) into
// music-visualizers/reference-next. The upstream checkout is read-only.
//
//   node tools/extract-music-modes-next.mjs <folia-major checkout with node_modules>
//
// esbuild comes from the checkout, or LUME_ESBUILD (path to esbuild/lib/main.js).
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync,existsSync,readdirSync,unlinkSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

if(!process.argv[2])throw new Error('Usage: node tools/extract-music-modes-next.mjs <read-only upstream>');
const upstream=path.resolve(process.argv[2]);
const output=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../music-visualizers/reference-next');
const require=createRequire(path.join(upstream,'package.json'));
let esbuild;
try{esbuild=require('esbuild');}catch{
  if(!process.env.LUME_ESBUILD)throw new Error('Set LUME_ESBUILD to esbuild/lib/main.js');
  esbuild=createRequire(import.meta.url)(process.env.LUME_ESBUILD);
}
const {parse}=require('@babel/parser');
const {compile}=require('tailwindcss');
const {Scanner}=require('@tailwindcss/oxide');
const postcss=require('postcss');

const modes={cappella:'cappella/VisualizerCappella.tsx',diorama:'diorama/VisualizerDiorama.tsx',pendolo:'pendolo/VisualizerPendolo.tsx',tempera:'tempera/VisualizerTempera.tsx'};
const slash=value=>value.replaceAll('\\','/');
const sha256=value=>createHash('sha256').update(value).digest('hex');
const sourceHashes=new Map();
const upstreamFile=relative=>path.join(upstream,relative);
const read=relative=>{
  const bytes=readFileSync(upstreamFile(relative));
  sourceHashes.set(slash(relative),sha256(bytes));
  return bytes.toString('utf8');
};
const visualizer=relative=>upstreamFile(`src/components/visualizer/${relative}`);
const referenceSrc=path.resolve(output,'../reference/src');

function declaration(relative,name) {
  const source=read(relative),ast=parse(source,{sourceType:'module',plugins:['typescript','jsx']});
  const statement=ast.program.body.map(node=>node.type==='ExportNamedDeclaration'?node.declaration:node)
    .find(node=>node?.type==='VariableDeclaration'&&node.declarations.some(item=>item.id.name===name));
  if(!statement)throw new Error(`Missing upstream declaration ${relative}:${name}`);
  return source.slice(statement.start,statement.end);
}

// Lume-side replacements for app modules the renderers reach. Each keeps the
// imported names; behaviour that needs the Folia app (track decoding, i18n,
// remote settings) is absent rather than emulated.
const virtual=new Map();
const parser='src/utils/lyrics/parserCore.ts';
virtual.set('next:parser',`${declaration(parser,'INTERLUDE_FULL_TEXT')}
${declaration(parser,'buildTimedWords')}
export const isInterludeLine = line => line.fullText === INTERLUDE_FULL_TEXT;
export {INTERLUDE_FULL_TEXT, buildTimedWords};`);
virtual.set('next:drum-hits',`// No separated drum stem exists in Lume: every track reports no drum hits,
// which is the upstream path for a track without drums.
export const DRUM_KICK = 0, DRUM_SNARE = 1, DRUM_HAT = 2;
export const getDrumHits = () => null;`);
virtual.set('next:visualizer-settings',`import {DEFAULT_SMART_BEAT_SETTINGS} from '${slash(upstreamFile('src/types.ts'))}';
// Lume has no Folia visualizer settings store; renderers see the default settings.
const state = {smartBeat: DEFAULT_SMART_BEAT_SETTINGS};
export const useVisualizerSettingsStore = selector => selector ? selector(state) : state;
useVisualizerSettingsStore.getState = () => state;
useVisualizerSettingsStore.subscribe = () => () => {};`);
virtual.set('next:tempera-images',`// Tempera's canvas image pool lives in Folia's IndexedDB; Lume has none, so the pool is empty.
export const loadTemperaLayerImageBlobs = async () => new Map();`);
virtual.set('next:i18n',`const i18n = {language: 'zh-CN', t: key => key, on() {}, off() {}, use() { return i18n; }, init() {}};
export default i18n;`);
virtual.set('reference:defaults',`
import {buildTimedWords} from 'next:parser';
import {annotateLyricLines} from '${slash(upstreamFile('src/utils/lyrics/renderHints.ts'))}';
export function prepareLines(lines) {
  return annotateLyricLines(lines.map(line=>{
    if(!line||typeof line.fullText!=='string'||!Number.isFinite(line.startTime)||!Number.isFinite(line.endTime)||line.startTime<0||line.endTime<line.startTime)
      throw new TypeError('Invalid lyric line');
    const authored=Array.isArray(line.words)&&line.words.length>0;
    if(authored && line.words.some(word=>!word||typeof word.text!=='string'||!Number.isFinite(word.startTime)||!Number.isFinite(word.endTime)||word.endTime<word.startTime))
      throw new TypeError('Invalid lyric word');
    return {...line,words:authored?line.words:buildTimedWords(line.fullText,line.startTime,line.endTime),
      referenceTiming:authored?'authored-words':'estimated-from-line',isWordByWord:authored};
  }));
}`);
// Same colour mapping the historical reference bundle uses for Lume's lyric
// preferences, applied to the current upstream default theme.
virtual.set('reference:preference-theme',`
import {DEFAULT_THEME} from '${slash(upstreamFile('src/services/baseThemes.ts'))}';
export {resolveThemeFontStack} from 'reference:fonts';
function build(mode,preferences) {
  const color=preferences?.lyricsFontColor||'#ffffff';
  return Object.freeze({...DEFAULT_THEME,
    animationIntensity:preferences?.animationIntensity??DEFAULT_THEME.animationIntensity,
    fontFamily:preferences?.fontFamily||undefined,
    // secondaryColor stays the upstream default: Cappella draws other senders'
    // bubbles from it under primary-coloured text, which a shared colour hides.
    primaryColor:color,accentColor:color,
    karaokeFillColor:preferences?.lyricsKaraokeFillColor||'#ffffff',
    ...(mode==='cappella'?cappellaBubbles(color):{})});
}
// Cappella paints other singers' bubbles between secondaryColor and accentColor
// under primary-coloured text. Upstream derives both from the cover; here they
// are a fixed grey ramp on the far side of the lyric colour, so every singer's
// line stays readable whatever lyric colour is chosen.
function cappellaBubbles(color) {
  const hex=/^#[0-9a-f]{6}$/i.test(color)?color:'#ffffff';
  const [r,g,b]=[1,3,5].map(i=>parseInt(hex.slice(i,i+2),16)/255).map(v=>v<=0.03928?v/12.92:((v+0.055)/1.055)**2.4);
  const light=0.2126*r+0.7152*g+0.0722*b>0.4;
  return light?{secondaryColor:'#27272a',accentColor:'#71717a'}:{secondaryColor:'#f4f4f5',accentColor:'#a1a1aa'};
}
export const themes=Object.freeze(Object.fromEntries(${JSON.stringify(Object.keys(modes))}.map(mode=>[mode,build(mode)])));
export function resolvePreferenceTheme(mode,preferences) {return preferences?build(mode,preferences):themes[mode];}`);
virtual.set('reference:modes',`export const loaders={${Object.entries(modes).map(([name,file])=>
  `${JSON.stringify(name)}:()=>import(${JSON.stringify(slash(visualizer(file)))})`).join(',')}};`);

const adaptations=[
  'VisualizerShell resolves to the reference stage Shell: original classes, ref, opacity and font stack; the Folia app background, stage layers, player chrome and hotspots are not part of the stage.',
  'LiquidGlass and topVisual compositor imports resolve to no-op registration bridges; renderer canvases and effects are retained.',
  'react-i18next and src/i18n/config resolve to a key-returning translator; on-stage text uses the reference stage labels.',
  'stores/useVisualizerSettingsStore is replaced by the upstream default settings (smart beat off); the Folia settings registry is not bundled.',
  'services/temperaLayerImages is replaced by an empty image pool (Folia stores user canvas images in IndexedDB).',
  'services/analysis/drumHits is replaced by a module reporting no drum hits (Lume has no separated drum stem); Pendolo beat effects use the smart beat path or stay idle.',
  'parserCore is reduced to INTERLUDE_FULL_TEXT, buildTimedWords (exact AST declarations) and isInterludeLine; line-timed words are marked estimated-from-line.',
  'The lyric theme is the upstream DEFAULT_THEME (secondary colour kept) with Lume lyric colour as primary and accent, karaoke colour, font family and animation intensity preferences, using the reference bundle mapping.',
  'Vite import.meta.glob for Cappella avatars and emoji is expanded at build time into static imports of the same files.',
];

// Vite globs in the renderer graph become explicit imports at build time.
function expandGlobs(contents,file) {
  return contents.replace(/import\.meta\.glob(?:<[^>]*>)?\(\s*'([^']+)'\s*,\s*\{\s*eager:\s*true\s*,?\s*\}\s*,?\s*\)/g,(match,pattern)=>{
    const m=pattern.match(/^\.\/([\w-]+)\/\*\.\{([\w,]+)\}$/);
    if(!m)throw new Error(`Unsupported glob ${pattern} in ${file}`);
    const directory=path.join(path.dirname(file),m[1]),extensions=m[2].split(',');
    const files=readdirSync(directory).filter(name=>extensions.includes(name.split('.').pop().toLowerCase())).sort();
    const entries=files.map((name,i)=>`${JSON.stringify(`./${m[1]}/${name}`)}:{default:__glob_${m[1]}_${i}}`);
    globImports.push(...files.map((name,i)=>`import __glob_${m[1]}_${i} from ${JSON.stringify(`./${m[1]}/${name}`)};`));
    return `({${entries.join(',')}})`;
  });
}
let globImports=[];

// Small source edits, each checked to apply exactly once.
const patches={
  // Lume shows no upcoming-line strip under any lyric style; translations stay.
  'src/components/visualizer/VisualizerSubtitleOverlay.tsx':[
    ['        recentCompletedLine,\n        nextLines,\n        hideTranslationSubtitle,',
     '        recentCompletedLine,\n        nextLines: [],\n        hideTranslationSubtitle,'],
  ],
  // The liquid glass samples the stage canvas on its own frames. A preserved
  // drawing buffer would let Tempera's back-buffer blit pile frames up and fill
  // the gaps that show the wallpaper, so instead the glass redraws the canvas
  // synchronously just before sampling it (see music-material/registry.js). The
  // redraw only re-renders the current stage: running the frame logic again would
  // advance song handover and scene work twice per frame.
  'src/components/visualizer/tempera/createTemperaPixiRuntime.ts':[
    ["        this.options.host.appendChild(this.app.canvas);",
     "        this.app.canvas.setAttribute('data-lumenic-liquid-glass-scene-effect', 'true');\n        (this.app.canvas as HTMLCanvasElement & { lumeMaterialSceneRefresh?: () => void }).lumeMaterialSceneRefresh = () => { if (this.lumeRefreshed || this.destroyed || !this.app.canvas.isConnected) return; this.lumeRefreshed = true; requestAnimationFrame(() => { this.lumeRefreshed = false; }); this.app.renderer.render(this.app.stage); };\n        this.options.host.appendChild(this.app.canvas);"],
    // At most one extra render per animation frame, whoever asks for it (the glass
    // and the stage keep-alive in reference-host.js). The guard clears on the next
    // animation frame, not in the frame step: a paused Tempera stops its ticker,
    // and the glass must still get a fresh picture of the paused stage.
    ["    private renderFrame = () => {\n        if (this.destroyed) return;",
     "    private lumeRefreshed = false;\n    private renderFrame = () => {\n        this.lumeRefreshed = false;\n        if (this.destroyed) return;"],
  ],
};

const forbidden=/(?:^|\/)(?:App\.[tj]sx?|electron|providers?|accounts?)(?:\/|$)|components\/app\/|services\/(?:onlineMusic|db|appDatabase|repositories|analysis)\b|(?:^|\/)node:/i;
const graphSource=new Map();
const plugin={name:'reference-next-boundary',setup(build){
  build.onResolve({filter:/.*/},args=>{
    if(virtual.has(args.path))return {path:args.path,namespace:'reference'};
    if(args.path==='reference:fonts')return {path:upstreamFile('src/utils/fontStacks.ts')};
    if(args.path==='react-i18next')return {path:path.join(referenceSrc,'bridges.js')};
    if(/(?:^|\/)VisualizerShell$/.test(args.path))return {path:path.join(referenceSrc,'Shell.tsx')};
    if(/(?:lumeTopVisualCompositor|LiquidGlassWebGLSceneBridge)$/.test(args.path))return {path:path.join(referenceSrc,'bridges.js')};
    if(/i18n\/config$/.test(args.path))return {path:'next:i18n',namespace:'reference'};
    if(/services\/temperaLayerImages$/.test(args.path))return {path:'next:tempera-images',namespace:'reference'};
    if(/stores\/useVisualizerSettingsStore$/.test(args.path))return {path:'next:visualizer-settings',namespace:'reference'};
    if(/analysis\/drumHits$/.test(args.path))return {path:'next:drum-hits',namespace:'reference'};
    if(/lyrics\/parserCore$/.test(args.path))return {path:'next:parser',namespace:'reference'};
    if(args.path.startsWith('virtual:'))throw new Error(`Unhandled Vite virtual module ${args.path} from ${args.importer}`);
    if(args.path.startsWith('node:'))throw new Error(`Node runtime import forbidden: ${args.path}`);
  });
  build.onLoad({filter:/.*/,namespace:'reference'},args=>({contents:virtual.get(args.path),loader:'ts',resolveDir:upstream}));
  build.onLoad({filter:/\.[cm]?[jt]sx?$/},args=>{
    const relative=slash(path.relative(upstream,args.path));
    if(relative.startsWith('../'))return;
    if(relative.startsWith('src/')&&forbidden.test(relative))throw new Error(`App surface forbidden: ${relative}`);
    let contents=read(relative);
    if(relative.startsWith('src/')){
      globImports=[];
      contents=expandGlobs(contents,args.path);
      if(globImports.length)contents=globImports.join('\n')+'\n'+contents;
      graphSource.set(relative,contents);
    }
    for(const [before,after,count=1] of patches[relative]??[]){
      contents=contents.replaceAll('\r\n','\n');
      if(contents.split(before).length!==count+1)throw new Error(`Upstream patch drift: ${relative}`);
      contents=contents.replaceAll(before,after);
    }
    return {contents,loader:path.extname(args.path).endsWith('tsx')?'tsx':path.extname(args.path)==='.ts'?'ts':'js'};
  });
}};

mkdirSync(output,{recursive:true});
const entryPoints={entry:path.join(output,'src/entry.tsx'),
  ...Object.fromEntries(Object.entries(modes).map(([name,file])=>[`mode-${name}`,visualizer(file)]))};
const result=await esbuild.build({absWorkingDir:upstream,entryPoints,outdir:output,
  bundle:true,splitting:true,format:'esm',platform:'browser',target:['es2022'],jsx:'automatic',
  minify:true,metafile:true,write:false,legalComments:'eof',
  chunkNames:'chunks/[name]-[hash]',entryNames:'[name]',assetNames:'assets/[name]-[hash]',
  loader:{'.png':'file','.jpg':'file','.jpeg':'file','.gif':'file','.webp':'file','.svg':'file'},
  nodePaths:[path.join(upstream,'node_modules')],
  define:{'process.env.NODE_ENV':'"production"','import.meta.env.DEV':'false','import.meta.env.PROD':'true'},
  plugins:[plugin],logLevel:'warning'});

const inputPaths=Object.keys(result.metafile.inputs).map(slash);
const forbiddenImportGraph=inputPaths.filter(name=>name.startsWith('src/')&&forbidden.test(name));
if(forbiddenImportGraph.length)throw new Error(`Forbidden import graph: ${forbiddenImportGraph.join(', ')}`);

const generated=[];
function emit(relative,bytes){
  const destination=path.resolve(output,relative);
  if(!destination.startsWith(output+path.sep))throw new Error('Output escapes reference-next');
  mkdirSync(path.dirname(destination),{recursive:true});writeFileSync(destination,bytes);
  generated.push({path:slash(relative),bytes:Buffer.byteLength(bytes),sha256:sha256(bytes)});
}
const manifestFile=path.join(output,'manifest.json');
if(existsSync(manifestFile)){
  for(const item of JSON.parse(readFileSync(manifestFile,'utf8')).generated??[]){
    const destination=path.resolve(output,item.path);
    if(destination.startsWith(output+path.sep)&&!item.path.startsWith('src/')&&item.path!=='index.html'&&existsSync(destination))unlinkSync(destination);
  }
}
for(const file of result.outputFiles)emit(path.relative(output,file.path),file.contents);

// Tailwind utilities from only the renderer graph.
const tailwindBase=path.dirname(require.resolve('tailwindcss/package.json'));
const tailwindCss={};
for(const name of ['theme.css','preflight.css'])tailwindCss[name]=readFileSync(path.join(tailwindBase,name),'utf8');
const compiler=await compile(`@layer theme, base, components, utilities;\n@layer theme {${tailwindCss['theme.css']}}\n@layer base {${tailwindCss['preflight.css']}}\n@layer utilities {@tailwind utilities;}`);
const scanner=new Scanner({sources:[]});
const candidates=scanner.scanFiles([...graphSource.values(),readFileSync(path.join(referenceSrc,'Shell.tsx'),'utf8')].map(content=>({content,extension:'tsx'})));
const custom=[];
postcss.parse(read('src/index.css')).walkRules(rule=>{
  if(/^(?:\.hide-scrollbar|\.visualizer-overlay-scrollbar)(?:$|\b|:)/.test(rule.selector))custom.push(rule.toString());
});
emit('reference.css',`${compiler.build(candidates)}\n${custom.join('\n')}\n:root{color-scheme:dark;}\nhtml,body,#reference-stage{width:100%;height:100%;margin:0;overflow:hidden;background:transparent;}\nbody{color:#e4e4e7;font-family:Inter,sans-serif;}\n`);

// Corresponding sources, generated adapters and licences.
for(const relative of graphSource.keys())emit(`source/upstream/${relative}`,readFileSync(upstreamFile(relative)));
for(const [name,contents] of virtual)emit(`source/generated/${name.replace(/^[a-z]+:/,'')}.ts`,contents);
emit('licenses/Folia-AGPL-3.0.txt',read('LICENSE'));
const packages=new Map();
for(const input of inputPaths){
  const match=input.match(/(?:^|\/)node_modules\/((?:@[^/]+\/)?[^/]+)/);
  if(!match||packages.has(match[1]))continue;
  const name=match[1],base=upstreamFile(`node_modules/${name}`);
  const metadata=JSON.parse(readFileSync(path.join(base,'package.json'),'utf8'));
  const licenses=readdirSync(base).filter(file=>/^(?:LICEN[SC]E|COPYING|NOTICE)(?:\.|$)/i.test(file));
  // Packages published without their licence file: the text is kept beside the adapter.
  const supplied={'@react-three/fiber':['react-three-fiber-MIT.txt','https://github.com/pmndrs/react-three-fiber/blob/master/LICENSE'],
    '@pixi/colord':['colord-MIT.txt','https://raw.githubusercontent.com/omgovich/colord/master/LICENSE.md']}[name];
  if(!licenses.length&&!supplied)throw new Error(`Review missing license: ${name}`);
  if(!licenses.length){
    const text=readFileSync(path.join(output,'src/licenses',supplied[0]));
    emit(`licenses/${name}/LICENSE`,text);
    packages.set(name,{version:metadata.version,license:metadata.license,licenses:[`${name}/LICENSE`],
      licenseSource:supplied[1],licenseSha256:sha256(text)});
    continue;
  }
  packages.set(name,{version:metadata.version,license:metadata.license,licenses:licenses.map(file=>`${name}/${file}`)});
  for(const file of licenses)emit(`licenses/${name}/${file}`,readFileSync(path.join(base,file)));
}
for(const relative of ['src/components/visualizer/cappella/avatar/README.md','src/components/visualizer/cappella/emo/README.md'])
  emit(`licenses/cappella-assets/${path.basename(path.dirname(relative))}-README.md`,read(relative));

const upstreamRevision=(()=>{try{
  const head=readFileSync(upstreamFile('.git/HEAD'),'utf8').trim();
  return head.startsWith('ref:')?readFileSync(upstreamFile(`.git/${head.slice(5)}`),'utf8').trim():head;
}catch{return 'unknown';}})();
const adapters=Object.fromEntries(['src/entry.tsx','src/protocol.js','index.html','../../music-preference-values.js','../reference/src/protocol.js'].map(file=>[file,sha256(readFileSync(path.join(output,file)))]));
const manifest={schema:1,builder:'esbuild',builderVersion:esbuild.version,
  upstream:'https://github.com/chthollyphile/folia-major',upstreamVersion:JSON.parse(read('package.json')).version,upstreamRevision,
  modes:Object.keys(modes),
  entries:Object.fromEntries(Object.entries(modes).map(([mode,file])=>[mode,`src/components/visualizer/${file}`])),
  dependencies:Object.fromEntries([...packages].sort(([a],[b])=>a.localeCompare(b,'en'))),
  sources:Object.fromEntries([...sourceHashes].sort(([a],[b])=>a.localeCompare(b,'en'))),
  adapters,buildScriptSha256:sha256(readFileSync(fileURLToPath(import.meta.url))),adaptations,
  modeBytes:Object.fromEntries(Object.keys(modes).map(mode=>[mode,result.metafile.outputs[slash(path.relative(upstream,path.join(output,`mode-${mode}.js`)))]?.bytes??null])),
  javascriptBytes:generated.filter(file=>file.path.endsWith('.js')).reduce((total,file)=>total+file.bytes,0),
  generated:generated.sort((a,b)=>a.path.localeCompare(b.path,'en'))};
writeFileSync(manifestFile,JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({modes:manifest.modes,upstreamVersion:manifest.upstreamVersion,upstreamRevision,
  sources:sourceHashes.size,packages:packages.size,javascriptBytes:manifest.javascriptBytes,modeBytes:manifest.modeBytes},null,2));
