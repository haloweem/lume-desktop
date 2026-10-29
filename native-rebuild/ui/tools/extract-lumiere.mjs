// Usage: node tools/extract-lumiere.mjs <upstream checkout> <dependency prefix>
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
const upstream=path.resolve(process.argv[2]),deps=path.resolve(process.argv[3]);
const output=path.resolve(import.meta.dirname,'../music-visualizers/lumiere');
const require=createRequire(path.join(deps,'package.json'));
const {build}=require('esbuild');
fs.mkdirSync(output,{recursive:true});
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const parserPath='src/utils/lyrics/parserCore.ts';
const parser=fs.readFileSync(path.join(upstream,parserPath),'utf8');
const timing=parser.slice(parser.indexOf('const buildTimedWords ='),parser.indexOf('export const isInterludeLine'));
if(!timing.startsWith('const buildTimedWords =')||!timing.includes('return words;'))throw new Error('Review upstream timing');
const overlayPath='src/components/visualizer/VisualizerSubtitleOverlay.tsx';
const overlay=fs.readFileSync(path.join(upstream,overlayPath),'utf8');
const selection=overlay.slice(overlay.indexOf('export const resolveVisualizerSubtitleOverlayContent ='),overlay.indexOf('const VisualizerSubtitleOverlay:'));
if(!selection.includes('shouldRenderOverlay'))throw new Error('Review upstream subtitle selection');
const result=await build({stdin:{contents:`
// Pixi's CSP adapter replaces generated uniform upload functions with static
// implementations. Despite its name, this module removes the eval requirement.
import 'pixi.js/unsafe-eval';
export {LumierePixiRuntime} from './src/components/visualizer/lumiere/createLumierePixiRuntime';
export {compileLumiereProgram} from './src/components/visualizer/lumiere/lumiereProgram';
export {DEFAULT_LUMIERE_TUNING} from './src/types';
export {DEFAULT_THEME} from './src/services/baseThemes';
import {annotateLyricLines} from './src/utils/lyrics/renderHints';
export {getLineRenderEndTime} from './src/utils/lyrics/renderHints';
export {getRecentCompletedLine,getUpcomingLines} from './src/components/visualizer/runtime';
export {getLyricsSignature,decideSongCommit,isInstrumentalConfirmed} from './src/components/visualizer/songHandover';
export {resolveSubtitleFontSizes} from './src/components/visualizer/subtitleFontSizes';
export {resolveThemeTranslationFontStack,resolveThemeFontWeight} from './src/utils/fontStacks';
import {resolveLyricSubtitleTracks,resolveSubtitleContentMode} from './src/utils/lyrics/alternateText';
const hasReadableText=(text)=>!!text&&/[\\p{L}\\p{N}]/u.test(text);
${selection}
${timing}
export function prepareLumiereLines(lines){
  return annotateLyricLines(lines.map(line=>({...line,
    words:line.words?.length?line.words:buildTimedWords(line.fullText,line.startTime,line.endTime),
    isWordByWord:!!line.words?.length})));
}
`,resolveDir:upstream,sourcefile:'lume-lumiere-entry.ts',loader:'ts'},
  nodePaths:[path.join(deps,'node_modules')],bundle:true,format:'esm',platform:'browser',target:'es2022',
  minify:true,metafile:true,legalComments:'linked',outfile:path.join(output,'runtime.js'),
  define:{'process.env.NODE_ENV':'"production"'},logLevel:'warning',
  plugins:[{name:'lume-lumiere-size',setup(builder){
    builder.onLoad({filter:/createLumierePixiRuntime\.ts$/},async args=>{
      const source=fs.readFileSync(args.path,'utf8');
      if(!source.includes('autoDensity: true,')||!source.includes('    renderOnce() {'))
        throw new Error('Review changed upstream Lumiere sizing');
      return {loader:'ts',contents:source.replace('autoDensity: true,','autoDensity: false,')
        .replace('    renderOnce() {',`    // Lume changes WebView zoom even when CSS host dimensions stay constant.
    resize() {
        if (this.resizeToHost(false) && this.options.paused) this.renderOnce();
    }

    renderOnce() {`)};
    });
  }}]});
const sources={},packages=new Set();
for(const name of Object.keys(result.metafile.inputs)){
  if(name==='<stdin>')continue;
  const file=path.resolve(name);
  if(file===path.join(upstream,'lume-lumiere-entry.ts'))continue;
  const relative=path.relative(upstream,file);
  if(!relative.startsWith('..')&&!path.isAbsolute(relative)){
    const dest=path.join(output,'source',relative);
    fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(file,dest);
    sources[relative.replaceAll('\\','/')]=hash(fs.readFileSync(file));
  } else {
    const dep=path.relative(path.join(deps,'node_modules'),file).split(path.sep);
    if(dep[0]!=='..')packages.add(dep[0].startsWith('@')?dep.slice(0,2).join('/'):dep[0]);
  }
}
const licenses=path.join(output,'licenses');fs.mkdirSync(licenses,{recursive:true});
for(const relative of [parserPath,overlayPath]){
  const file=path.join(upstream,relative),dest=path.join(output,'source',relative);
  fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(file,dest);
  sources[relative]=hash(fs.readFileSync(file));
}
fs.copyFileSync(path.join(upstream,'LICENSE'),path.join(licenses,'Folia-AGPL-3.0.txt'));
const dependencies={};
for(const name of [...packages].sort()){
  const root=path.join(deps,'node_modules',name),pkg=JSON.parse(fs.readFileSync(path.join(root,'package.json')));
  const notices=fs.readdirSync(root).filter(x=>/^(license|licence|copying|notice)(\.|$)/i.test(x)&&fs.statSync(path.join(root,x)).isFile());
  if(!notices.length){
    // Some npm distributions omit the license text; retain their exact package
    // metadata and require a checked-in upstream license alongside it.
    const fallback=path.join(licenses,name.replaceAll('/','_')+'-LICENSE.txt');
    if(!fs.existsSync(fallback))throw new Error(`Supply upstream license: ${fallback}`);
  }
  fs.copyFileSync(path.join(root,'package.json'),path.join(licenses,name.replaceAll('/','_')+'-package.json'));
  dependencies[name]={version:pkg.version,license:pkg.license,files:notices};
  for(const file of notices)fs.copyFileSync(path.join(root,file),path.join(licenses,name.replaceAll('/','_')+'-'+file+'.txt'));
}
const generated=Object.fromEntries(fs.readdirSync(output).filter(x=>x.startsWith('runtime.')).map(file=>[file,hash(fs.readFileSync(path.join(output,file)))]));
for(const name of ['package.json','package-lock.json'])fs.copyFileSync(path.join(deps,name),path.join(output,'source',name));
fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify({repository:'https://github.com/chthollyphile/folia-major',
  commit:execFileSync('git',['-C',upstream,'rev-parse','HEAD'],{encoding:'utf8'}).trim(),
  adaptation:'Original Lumiere compiler and Pixi runtime; Lume supplies playback, lyrics and preferences directly. Pixi CSP static adapter; autoDensity disabled to preserve 100% CSS size; public resize hook updates DPR on WebView zoom.',sources,dependencies,generated},null,2)+'\n');
console.log(`Lumiere: ${Object.keys(sources).length} upstream files, ${packages.size} dependencies, ${fs.statSync(path.join(output,'runtime.js')).size} bundled bytes`);
