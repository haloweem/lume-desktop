// Build-time only. The historical checkout is read-only; all outputs stay in reference/.
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync,existsSync,readdirSync,unlinkSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

if(!process.argv[2])throw new Error('Usage: node tools/extract-music-modes.mjs <read-only upstream>');
const upstream=path.resolve(process.argv[2]);
const output=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../music-visualizers/reference');
const require=createRequire(path.join(upstream,'package.json'));
let esbuild;
try{esbuild=require('esbuild');}catch{
  esbuild=require('./node_modules/.pnpm/esbuild@0.25.12/node_modules/esbuild');
}
const {parse}=require('@babel/parser');
const {compile}=require('tailwindcss');
const {Scanner}=require('@tailwindcss/oxide');
const postcss=require('postcss');
const modes={classic:'classic/Visualizer.tsx',cadenza:'cadenza/VisualizerCadenza.tsx',
  partita:'partita/VisualizerPartita.tsx',fume:'fume/VisualizerFume.tsx',tilt:'tilt/VisualizerTilt.tsx',
  monet:'monet/VisualizerMonet.tsx',sonnet:'sonnet/VisualizerSonnet.tsx'};
const slash=value=>value.replaceAll('\\','/');
const sha256=value=>createHash('sha256').update(value).digest('hex');
const sourceHashes=new Map(),adaptations=[];
const historyFile=relative=>path.join(upstream,relative);
const historical=relative=>{
  const bytes=readFileSync(historyFile(relative));
  sourceHashes.set(slash(relative),sha256(bytes));
  return bytes.toString('utf8');
};
const virtual=new Map();
const visualizer=relative=>historyFile(`src/components/visualizer/${relative}`);
const aliases=new Map([
  [slash(visualizer('VisualizerShell')),path.join(output,'src/Shell.tsx')],
  [slash(visualizer('lumeTopVisualCompositor')),path.join(output,'src/bridges.js')],
  [slash(historyFile('src/components/liquid-glass/LiquidGlassWebGLSceneBridge')),path.join(output,'src/bridges.js')],
]);

function statementNamed(relative,name) {
  const source=historical(relative),ast=parse(source,{sourceType:'module',plugins:['typescript']});
  const statement=ast.program.body.map(node=>node.type==='ExportNamedDeclaration'?node.declaration:node)
    .find(node=>node?.type==='VariableDeclaration'&&node.declarations.some(item=>item.id.name===name));
  if(!statement)throw new Error(`Missing historical declaration ${relative}:${name}`);
  return {statement:source.slice(statement.start,statement.end),declaration:statement.declarations.find(item=>item.id.name===name)};
}
const parser='src/utils/lyrics/parserCore.ts';
const buildTimedWords=statementNamed(parser,'buildTimedWords').statement;
virtual.set('reference:timing',`${buildTimedWords}\nexport {buildTimedWords};`);
const defaultColor=statementNamed('src/stores/useSettingsUiStore.ts','DEFAULT_LYRICS_COLOR').declaration.init.value;
if(defaultColor!=='#ffffff')throw new Error('Review changed historical default lyric color');
virtual.set('reference:defaults',`
import {buildTimedWords} from 'reference:timing';
import {annotateLyricLines} from '${slash(historyFile('src/utils/lyrics/renderHints.ts'))}';
export {themes} from 'reference:preference-theme';
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
}
`);
const buildVisualizerTheme=statementNamed('src/components/app/presentation/buildVisualizerTheme.ts','buildVisualizerTheme').statement;
virtual.set('reference:preference-theme',`
import {DEFAULT_THEME} from '${slash(historyFile('src/services/baseThemes.ts'))}';
import {DEFAULT_SONNET_TUNING} from '${slash(historyFile('src/types.ts'))}';
export {resolveThemeFontStack} from 'reference:fonts';
${buildVisualizerTheme}
function build(mode,preferences) {
  return Object.freeze(buildVisualizerTheme({appStyle:{},theme:{...DEFAULT_THEME,animationIntensity:preferences?.animationIntensity??DEFAULT_THEME.animationIntensity},
    lyricsFontStyle:DEFAULT_THEME.fontStyle,lyricsFontColor:preferences?.lyricsFontColor??${JSON.stringify(defaultColor)},
    lyricsKaraokeFillColor:preferences?.lyricsKaraokeFillColor??${JSON.stringify(defaultColor)},
    lyricsCustomFontFamily:preferences?.fontFamily||null,sonnetTuning:preferences?.sonnetColors??DEFAULT_SONNET_TUNING,visualizerMode:mode}).visualizerTheme);
}
export const themes=Object.freeze(Object.fromEntries(${JSON.stringify([...Object.keys(modes),'gallery','3d'])}.map(mode=>[mode,build(mode)])));
export function resolvePreferenceTheme(mode,preferences) {return preferences?build(mode,preferences):themes[mode];}
`);
virtual.set('reference:modes',`export const loaders={${Object.entries(modes).map(([name,file])=>`${JSON.stringify(name)}:()=>import(${JSON.stringify(slash(visualizer(file)))})`).join(',')}};`);

adaptations.push('Backported from Folia 0.7.21 (bf7f24a): Tilt segment handler runs in a layout effect; Sonnet display trees are destroyed node by node (Graphics contexts and GPU buffers released); Fume per-glyph measure cache is capped at 2048 entries.',
  'VisualizerShell alias retains original classes, ref, opacity, font stack and geometry; only external background, effects registry and system chrome removed.',
  'LiquidGlass and topVisual aliases only disable external compositing registration; renderer canvases and effects are retained.',
  'Translation hook is a dependency-free waiting-for-music label; no app i18n/provider initialization.',
  'buildTimedWords is extracted as one exact TypeScript AST declaration from parserCore; line-timed tokens are explicitly marked estimated-from-line, not authored word precision.',
  'Original default theme plus stored-default white lyric override; Sonnet defaults remain #ffffff/#38bdf8/#60a5fa with independent persisted colors. The historical global-font override still takes precedence over Sonnet font/glow.',
  'buildVisualizerTheme is extracted as an exact AST declaration; bounded preferences use its original color ordering and the original font stack resolver without importing app state.',
  'No remote font-face rules are loaded: historical local/system font fallback stacks are retained.',
  'The iframe declares color-scheme:dark to preserve transparent compositing with the dark parent document.',
  'Monet AudioOverlay clears and returns when every supplied audio input is zero, suppressing the original synthetic idle spectrum.',
  'Monet absent artwork uses an undefined image src instead of an empty document URL; portrait geometry is unchanged.',
  'Fume paused clock changes redraw once and snap to the existing target camera; playing camera/layout logic is unchanged.',
  'Sonnet initialization failures also emit lume-reference-renderer-error to the iframe host.',
  'Sonnet theme updates reuse Pixi, clear and rebuild the original bounded scene/credits/overlay caches, and release/reload icon textures with stale-load guards; camera, program, ticker and canvas survive.');

const patches={
  // Lume shows no upcoming-line strip under any lyric style; translations stay.
  'src/components/visualizer/VisualizerSubtitleOverlay.tsx':[
    ['        recentCompletedLine,\n        nextLines,\n        hideTranslationSubtitle,',
     '        recentCompletedLine,\n        nextLines: [],\n        hideTranslationSubtitle,'],
  ],
  // Backported from Folia 0.7.21: the insertion effect sets state, which React rejects.
  'src/components/visualizer/tilt/VisualizerTilt.tsx':[
    ["import React, { useMemo, useState, useRef, useInsertionEffect } from 'react';",
     "import React, { useMemo, useState, useRef, useInsertionEffect, useLayoutEffect } from 'react';"],
    ['    }, [activeLine, layout]);\n\n    useInsertionEffect(() => {\n        const handler = (latest: number) => {',
     '    }, [activeLine, layout]);\n\n    useLayoutEffect(() => {\n        const handler = (latest: number) => {'],
  ],
  // Backported from Folia 0.7.21: Pixi 8 Graphics only destroy their own context
  // when destroy() gets no options, so a tree is destroyed node by node.
  'src/components/visualizer/sonnet/sonnetPixiResources.ts':[
    ['// removeChildren() only detaches nodes; explicitly destroy them when rebuilding overlays.',
     `export const destroySonnetDisplayTree = (root: SonnetDisplayNode) => {
    const order: SonnetDisplayNode[] = [];
    const stack = [root];
    while (stack.length > 0) {
        const node = stack.pop()!;
        order.push(node);
        if (node.children?.length) stack.push(...node.children);
    }
    for (let index = order.length - 1; index >= 0; index -= 1) order[index]!.destroy?.();
};

// removeChildren() only detaches nodes; explicitly destroy them when rebuilding overlays.`],
    ['        child.destroy?.({ children: true });','        destroySonnetDisplayTree(child);'],
  ],
  'src/components/visualizer/fume/VisualizerFume.tsx':[
    // Backported from Folia 0.7.21: the per-glyph measure cache was unbounded across songs.
    ['    segmentMeasureCache.set(cacheKey, offsets);',
     `    if (segmentMeasureCache.size >= 2048) segmentMeasureCache.delete(segmentMeasureCache.keys().next().value!);
    segmentMeasureCache.set(cacheKey, offsets);`],
    ['if (entryFocusPoint) {','if (entryFocusPoint && !paused) {'],
    ['const screenScale = cameraRef.current.scale;',`if (paused) {
                Object.assign(cameraRef.current, {x: targetCameraX, y: targetCameraY, scale: targetCameraScale,
                    focusX: targetCameraX, focusY: targetCameraY, focusScale: targetCameraScale,
                    velocityX: 0, velocityY: 0, velocityScale: 0});
            }
            const screenScale = cameraRef.current.scale;`],
    [`        draw();\n        return () => {\n            window.cancelAnimationFrame(frameId);`,
     `        const stopPausedClock = currentTime.on('change', () => { if (paused) draw(); });
        draw();
        return () => {
            stopPausedClock();
            window.cancelAnimationFrame(frameId);`],
  ],
  'src/components/visualizer/monet/VisualizerMonet.tsx':[
    ["src={portraitUrl || ''}",'src={portraitUrl || undefined}',2],
  ],
  'src/components/visualizer/monet/AudioOverlay.tsx':[
    ["const hasRawSpectrum = rawSpectrum.length > MIN_RAW_SPECTRUM_BIN;",`const hasRawSpectrum = rawSpectrum.length > MIN_RAW_SPECTRUM_BIN;
            if (audioPower.get() === 0 && bands.every(value => value === 0) && !rawSpectrum.some(value => value !== 0)) return;`],
  ],
  'src/components/visualizer/sonnet/VisualizerSonnet.tsx':[
    ["console.error('[Sonnet] Pixi runtime initialization failed', error);",`console.error('[Sonnet] Pixi runtime initialization failed', error);
                if (!disposed) window.dispatchEvent(new Event('lume-reference-renderer-error'));`],
    ['    useEffect(() => {\n        const host = hostRef.current;',`    const latestThemeRef = useRef(theme);
    latestThemeRef.current = theme;
    useEffect(() => { runtimeRef.current?.setTheme(theme); }, [theme]);
    useEffect(() => {
        const host = hostRef.current;`],
    ['                    theme,','                    theme: latestThemeRef.current,'],
    ['                runtime.setSongMetadata(latestSongMetadataRef.current);',`                runtime.setTheme(latestThemeRef.current);
                runtime.setSongMetadata(latestSongMetadataRef.current);`],
    ['        theme,\n        props.background?.shellOwnsWallpaper,','        props.background?.shellOwnsWallpaper,'],
  ],
  'src/components/visualizer/sonnet/createSonnetPixiRuntime.ts':[
    ['    destroySonnetContainerChildren,\n    unloadSonnetDisplayTree,\n} from \'./sonnetPixiResources\';',
     '    destroySonnetContainerChildren,\n    destroySonnetDisplayTree,\n    unloadSonnetDisplayTree,\n} from \'./sonnetPixiResources\';'],
    ['        scene.container.destroy({ children: true });','        destroySonnetDisplayTree(scene.container);'],
    ['    private destroyed = false;','    private destroyed = false;\n    private themeRevision = 0;'],
    ['    setSongMetadata(metadata: SonnetSongMetadata) {',`    setTheme(theme: Theme) {
        if (this.destroyed || this.options.theme === theme) return;
        this.options.theme = theme;
        const revision = ++this.themeRevision;
        this.clearScenes();
        this.iconTextures.clear();
        const pool = getSonnetTexturePool(this.pixi);
        this.iconUrls.forEach(url => pool.release(url));
        this.iconUrls.clear();
        this.drawCredits(this.lastWidth, this.lastHeight);
        this.drawOverlay(this.lastWidth, this.lastHeight);
        this.renderOnce();
        void this.preloadIcons().then(() => {
            if (this.destroyed || revision !== this.themeRevision) return;
            this.clearScenes();
            this.renderOnce();
        });
    }

    setSongMetadata(metadata: SonnetSongMetadata) {`],
    ['    private async preloadIcons() {',`    private async preloadIcons() {
        const revision = this.themeRevision, theme = this.options.theme;`],
    ['resolveSonnetIconNames(this.options.theme.lyricsIcons)','resolveSonnetIconNames(theme.lyricsIcons)'],
    ['                this.options.theme.accentColor,\n                this.options.theme.secondaryColor,\n                this.options.theme.primaryColor,',
     '                theme.accentColor,\n                theme.secondaryColor,\n                theme.primaryColor,'],
    ['                this.iconTextures.set(key, await texturePool.acquire(url));',`                const texture = await texturePool.acquire(url);
                if (this.destroyed || revision !== this.themeRevision) { texturePool.release(url); return; }
                this.iconTextures.set(key, texture);`],
  ],
};
const forbidden=/(?:^|\/)(?:App\.[tj]sx?|electron|providers?|accounts?|stores)(?:\/|$)|components\/app\/|visualizer\/(?:registry|settingsPanels|tuningRegistry)|visualizer\/(?:backgrounds|effects)\/|(?:^|\/)node:/i;
const forbiddenSource=name=>forbidden.test(name)||name.startsWith('src/')&&
  !(/^(?:src\/components\/visualizer\/|src\/utils\/|src\/types\.ts$)/.test(name)||name==='src/services/baseThemes.ts');
const graphSource=new Map();
const sourcePlugin={name:'reference-leaf-boundary',setup(build){
  build.onResolve({filter:/.*/},args=>{
    if(virtual.has(args.path))return {path:args.path,namespace:'reference'};
    if(args.path==='reference:fonts')return {path:historyFile('src/utils/fontStacks.ts')};
    if(args.path==='react-i18next')return {path:path.join(output,'src/bridges.js')};
    if(args.path.startsWith('.')){
      const full=slash(path.resolve(args.resolveDir,args.path)).replace(/\.[tj]sx?$/,'');
      if(aliases.has(full))return {path:aliases.get(full)};
    }
    if(args.path.startsWith('node:'))throw new Error(`Node runtime import forbidden: ${args.path}`);
  });
  build.onLoad({filter:/.*/,namespace:'reference'},args=>({contents:virtual.get(args.path),loader:'ts',resolveDir:upstream}));
  build.onLoad({filter:/\.[cm]?[jt]sx?$/},args=>{
    const relative=slash(path.relative(upstream,args.path));
    if(relative.startsWith('../'))return;
    if(relative.startsWith('src/')&&forbiddenSource(relative))throw new Error(`App surface forbidden: ${relative}`);
    let contents=historical(relative);
    if(relative.startsWith('src/'))graphSource.set(relative,contents);
    for(const [before,after,count=1] of patches[relative]??[]){
      contents=contents.replaceAll('\r\n','\n');
      if(contents.split(before).length!==count+1)throw new Error(`Historical patch drift: ${relative}`);
      contents=contents.replaceAll(before,after);
    }
    return {contents,loader:path.extname(args.path).endsWith('tsx')?'tsx':path.extname(args.path)==='.ts'?'ts':'js'};
  });
}};
mkdirSync(output,{recursive:true});
const entryPoints={entry:path.join(output,'src/entry.tsx'),contracts:'reference:defaults','preference-theme':'reference:preference-theme',
  'preference-protocol':path.join(output,'src/protocol.js'),
  ...Object.fromEntries(Object.entries(modes).map(([name,file])=>[`mode-${name}`,visualizer(file)]))};
const result=await esbuild.build({absWorkingDir:upstream,entryPoints,outdir:output,
  bundle:true,splitting:true,format:'esm',platform:'browser',target:['es2022'],jsx:'automatic',
  minify:false,metafile:true,write:false,legalComments:'eof',preserveSymlinks:true,
  chunkNames:'chunks/[name]-[hash]',entryNames:'[name]',
  nodePaths:[path.join(upstream,'node_modules')],
  define:{'process.env.NODE_ENV':'"production"','import.meta.env.DEV':'false'},
  plugins:[sourcePlugin],logLevel:'warning'});

const inputPaths=Object.keys(result.metafile.inputs).map(slash);
const forbiddenImportGraph=inputPaths.filter(forbiddenSource);
if(forbiddenImportGraph.length)throw new Error(`Forbidden import graph: ${forbiddenImportGraph.join(', ')}`);
const modeOwner=name=>name.match(/src\/components\/visualizer\/(classic|cadenza|partita|fume|tilt|monet|sonnet)\//)?.[1];
const crossModeImports=[];
for(const [source,input] of Object.entries(result.metafile.inputs))for(const dependency of input.imports){
  const from=modeOwner(source),to=modeOwner(dependency.path);
  if(from&&to&&from!==to)crossModeImports.push({source,target:dependency.path});
}
if(crossModeImports.length)throw new Error(`Cross-mode implementation import: ${JSON.stringify(crossModeImports)}`);
const generated=[];
function emit(relative,bytes){
  const destination=path.resolve(output,relative);
  if(!destination.startsWith(output+path.sep))throw new Error('Output escapes reference directory');
  mkdirSync(path.dirname(destination),{recursive:true});writeFileSync(destination,bytes);
  generated.push({path:slash(relative),bytes:Buffer.byteLength(bytes),sha256:sha256(bytes)});
}
const previousManifest=path.join(output,'manifest.json');
if(existsSync(previousManifest)){
  // Remove only the exact previous generated chunk paths, within this output directory.
  for(const item of JSON.parse(readFileSync(previousManifest,'utf8')).generated??[]){
    const destination=path.resolve(output,item.path);
    if(item.path.startsWith('chunks/')&&destination.startsWith(path.join(output,'chunks')+path.sep)&&existsSync(destination))unlinkSync(destination);
  }
}
for(const file of result.outputFiles)emit(path.relative(output,file.path),file.contents);

// Rebuild Tailwind utilities from only the actual renderer dependency graph, not an old dist bundle.
const tailwindBase=path.dirname(require.resolve('tailwindcss/package.json'));
const tailwindCss={};
for(const name of ['theme.css','preflight.css'])tailwindCss[name]=historical(slash(path.relative(upstream,path.join(tailwindBase,name))));
const compiler=await compile(`@layer theme, base, components, utilities;\n@layer theme {${tailwindCss['theme.css']}}\n@layer base {${tailwindCss['preflight.css']}}\n@layer utilities {@tailwind utilities;}`);
const scanner=new Scanner({sources:[]});
const candidates=scanner.scanFiles([...graphSource.values(),readFileSync(path.join(output,'src/Shell.tsx'),'utf8')].map(content=>({content,extension:'tsx'})));
const oldCss=postcss.parse(historical('src/index.css'));
const custom=[];
oldCss.walkRules(rule=>{
  if(/^(?:\.hide-scrollbar|\.visualizer-overlay-scrollbar)(?:$|\b|:)/.test(rule.selector))custom.push(rule.toString());
});
emit('reference.css',`${compiler.build(candidates)}\n${custom.join('\n')}\n:root{color-scheme:dark;}\nhtml,body,#reference-stage{width:100%;height:100%;margin:0;overflow:hidden;background:transparent;}\nbody{color:#e4e4e7;font-family:Inter,sans-serif;}\n`);

// Keep the corresponding original renderer sources with their source paths and checksums.
for(const relative of graphSource.keys())emit(`source/upstream/${relative}`,readFileSync(historyFile(relative)));
for(const [name,contents] of virtual)emit(`source/generated/${name.replace('reference:','')}.ts`,contents);
emit('source/build-metafile.json',JSON.stringify(result.metafile,null,2)+'\n');
const sourceLicense=historical('LICENSE');emit('licenses/Folia-AGPL-3.0.txt',sourceLicense);
const provenanceOnly=['src/components/visualizer/VisualizerShell.tsx',
  'src/components/visualizer/lumeTopVisualCompositor.ts',
  'src/components/liquid-glass/LiquidGlassWebGLSceneBridge.ts',parser,'src/stores/useSettingsUiStore.ts',
  'src/components/app/presentation/buildVisualizerTheme.ts'];
for(const file of provenanceOnly)historical(file);
const packages=new Map();
for(const input of inputPaths){
  const match=input.match(/(?:^|\/)node_modules\/((?:@[^/]+\/)?[^/]+)/);
  if(!match||match[1]==='.pnpm')continue;
  const name=match[1],base=historyFile(`node_modules/${name}`),metadataFile=path.join(base,'package.json');
  if(packages.has(name))continue;
  if(!existsSync(metadataFile))throw new Error(`Cannot find dependency metadata: ${name}`);
  const metadata=JSON.parse(historical(`node_modules/${name}/package.json`));
  const licenses=readdirSync(base).filter(file=>/^(?:LICEN[SC]E|COPYING|NOTICE)(?:\.|$)/i.test(file));
  packages.set(name,{version:metadata.version,license:metadata.license,licenses:licenses.map(file=>`${name}/${file}`),licenseMetadata:`${name}/package.json`});
  for(const file of licenses)if(!generated.some(item=>item.path===`licenses/${name}/${file}`))emit(`licenses/${name}/${file}`,historical(`node_modules/${name}/${file}`));
  emit(`licenses/${name}/package.json`,readFileSync(metadataFile));
  if(!licenses.length){
    if(name!=='@pixi/colord')throw new Error(`Review missing license: ${name}`);
    const license=readFileSync(path.join(output,'src/licenses/colord-MIT.txt'));
    emit(`licenses/${name}/LICENSE.md`,license);
    Object.assign(packages.get(name),{licenses:[`${name}/LICENSE.md`],
      licenseSource:'https://raw.githubusercontent.com/omgovich/colord/master/LICENSE.md',licenseSha256:sha256(license)});
  }
}
for(const name of ['tailwindcss','@tailwindcss/oxide','postcss','@babel/parser']){
  const metadata=JSON.parse(historical(`node_modules/${name}/package.json`));
  const licenses=readdirSync(historyFile(`node_modules/${name}`)).filter(file=>/^(?:LICEN[SC]E|COPYING|NOTICE)(?:\.|$)/i.test(file));
  packages.set(name,{version:metadata.version,license:metadata.license,buildOnly:true,
    licenseMetadata:`${name}/package.json`,licenses:licenses.map(file=>`${name}/${file}`)});
  emit(`licenses/${name}/package.json`,historical(`node_modules/${name}/package.json`));
  for(const file of licenses)
    emit(`licenses/${name}/${file}`,historical(`node_modules/${name}/${file}`));
}
const esbuildBase=path.join(upstream,'node_modules/.pnpm',`esbuild@${esbuild.version}`,'node_modules/esbuild');
const esbuildMetadata=JSON.parse(historical(slash(path.relative(upstream,path.join(esbuildBase,'package.json')))));
packages.set('esbuild',{version:esbuild.version,license:esbuildMetadata.license,buildOnly:true,
  licenseMetadata:'esbuild/package.json',licenses:['esbuild/LICENSE.md']});
for(const name of ['LICENSE.md','package.json'])emit(`licenses/esbuild/${name}`,historical(slash(path.relative(upstream,path.join(esbuildBase,name)))));
for(const dependency of packages.values())dependency.compiledBytes=0;
for(const bundle of Object.values(result.metafile.outputs))for(const [input,value] of Object.entries(bundle.inputs)){
  const name=input.match(/(?:^|\/)node_modules\/((?:@[^/]+\/)?[^/]+)/)?.[1];
  if(packages.has(name))packages.get(name).compiledBytes+=value.bytesInOutput;
}
const bundleOutputs=Object.fromEntries(Object.entries(result.metafile.outputs).map(([name,value])=>[
  slash(path.relative(output,path.resolve(upstream,name))),{...value,imports:value.imports.map(item=>({...item,path:slash(path.relative(output,path.resolve(upstream,item.path)))}))}]));
const closure=(entries,includeDynamic)=>{
  const seen=new Set();
  const visit=name=>{if(seen.has(name)||!bundleOutputs[name])return;seen.add(name);
    for(const item of bundleOutputs[name].imports)if(includeDynamic||item.kind!=='dynamic-import')visit(item.path);};
  entries.forEach(visit);return [...seen].sort();
};
const baseFiles=closure(['entry.js'],false);
const modeBundles=Object.fromEntries(Object.keys(modes).map(mode=>{
  const files=[...new Set([...baseFiles,...closure([`mode-${mode}.js`],true)])].sort();
  return [mode,{files,bytes:files.reduce((total,file)=>total+bundleOutputs[file].bytes,0)}];
}));
const adapters=Object.fromEntries(['src/entry.tsx','src/protocol.js','src/Shell.tsx','src/bridges.js','index.html','../../music-preference-values.js'].map(file=>[file,sha256(readFileSync(path.join(output,file)))]));
const manifest={schema:1,builder:'esbuild',builderVersion:esbuild.version,
  reference:'lume1.0.1/third_party/folia-major/upstream',modes:Object.keys(modes),
  entries:Object.fromEntries(Object.entries(modes).map(([mode,file])=>[mode,`src/components/visualizer/${file}`])),
  dependencies:Object.fromEntries([...packages].sort(([a],[b])=>a.localeCompare(b,'en'))),
  sources:Object.fromEntries([...sourceHashes].sort(([a],[b])=>a.localeCompare(b,'en'))),
  adapters,buildScriptSha256:sha256(readFileSync(fileURLToPath(import.meta.url))),adaptations,
  forbiddenImportGraph,crossModeImports,provenanceOnly,importGraph:inputPaths,
  modeBundles,bootstrapBytes:baseFiles.reduce((total,file)=>total+bundleOutputs[file].bytes,0),
  javascriptBytes:generated.filter(file=>file.path.endsWith('.js')).reduce((total,file)=>total+file.bytes,0),
  cssBytes:generated.find(file=>file.path==='reference.css').bytes,
  generated:generated.sort((a,b)=>a.path.localeCompare(b.path,'en'))};
writeFileSync(previousManifest,JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({modes:Object.keys(modes),sources:sourceHashes.size,packages:packages.size,
  javascriptBytes:generated.filter(file=>file.path.endsWith('.js')).reduce((sum,file)=>sum+file.bytes,0),
  cssBytes:generated.find(file=>file.path==='reference.css').bytes,forbiddenImportGraph},null,2));
