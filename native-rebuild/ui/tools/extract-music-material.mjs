// Build-time extraction only. Historical sources and optical constants remain unchanged.
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync,existsSync,readdirSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const args=process.argv.slice(2),check=args.includes('--check');
const ui=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const output=path.join(ui,'music-material/reference');
const upstream=args.find(value=>!value.startsWith('--'));
const root=upstream?path.resolve(upstream):path.join(output,'source/upstream');
const require=createRequire(path.join(root,'package.json'));
function dependency(name) {
  try{return require(name);}catch(error){
    const store=path.join(root,'node_modules/.pnpm');
    if(!existsSync(store))throw error;
    const entry=readdirSync(store).sort().find(value=>value.startsWith(`${name}@`));
    if(!entry)throw error;
    return require(path.join(store,entry,'node_modules',name));
  }
}
const esbuild=process.env.LUME_ESBUILD_PATH
  ? createRequire(import.meta.url)(process.env.LUME_ESBUILD_PATH):dependency('esbuild');
const prefix='src/components/liquid-glass/';
const files=['LiquidGlassRenderer.ts','liquidGlassShaders.ts','liquidGlassPreset.ts',
  'liquidGlassModel.ts','LiquidGlassSceneComposition.ts','sceneOverflow.ts','LiquidGlassSceneCapture.ts'];
const sources=new Map(files.map(name=>[prefix+name,readFileSync(path.join(root,prefix,name))]));
sources.set('src/components/FloatingPlayerControls.tsx',readFileSync(path.join(root,'src/components/FloatingPlayerControls.tsx')));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const replace=(text,before,after)=>{
  if(text.split(before).length!==2)throw new Error(`Historical extraction drift: ${before}`);
  return text.replace(before,after);
};
const modules=new Map(files.map(name=>[name,sources.get(prefix+name).toString('utf8').replaceAll('\r\n','\n')]));
let overflow=modules.get('sceneOverflow.ts');
for(const signature of [
  'export const getSceneOverflowBounds = (element: HTMLElement) => {',
  'export const getSceneLeafClip = (element: HTMLElement, bounds: { left: number; top: number; right: number; bottom: number }) => {',
])overflow=replace(overflow,signature,`${signature}\n    const document = element.ownerDocument;\n    const window = document.defaultView!;`);
overflow=replace(overflow,'const clip = { ...bounds };',
  'const clip = { left: bounds.left, top: bounds.top, right: bounds.right, bottom: bounds.bottom };');
modules.set('sceneOverflow.ts',overflow);
let composition=modules.get('LiquidGlassSceneComposition.ts');
composition=composition.replaceAll('window.getComputedStyle(', 'styleOf(');
composition=replace(composition,"import { getSceneLeafClip } from './sceneOverflow';",
  "import { getSceneLeafClip } from './sceneOverflow';\nconst styleOf = (element: HTMLElement) => element.ownerDocument.defaultView!.getComputedStyle(element);");
modules.set('LiquidGlassSceneComposition.ts',composition);
// Extract the historical text painter as declarations, without its global scene registry.
// Babel is already used by extract-music-modes; no runtime parser is shipped.
const parser=dependency('@babel/parser');
const capture=modules.get('LiquidGlassSceneCapture.ts');
const ast=parser.parse(capture,{sourceType:'module',plugins:['typescript']});
const declarations=['resolveTextAlign','drawText'].map(name=>{
  const node=ast.program.body.find(node=>node.type==='VariableDeclaration'&&node.declarations.some(item=>item.id.name===name));
  if(!node)throw new Error(`Missing text painter: ${name}`);
  return capture.slice(node.start,node.end);
}).join('\n');
modules.set('text.ts',`import {getSceneLeafClip} from './sceneOverflow';\n${declarations.replaceAll('window.getComputedStyle(element)','element.ownerDocument.defaultView!.getComputedStyle(element)')}\nexport {drawText};`);
modules.set('entry.ts',`export {LiquidGlassRenderer} from './LiquidGlassRenderer';
export {LIQUID_GLASS_STUDIO_PRESET} from './liquidGlassPreset';
export * from './liquidGlassModel';
export {LiquidGlassSceneComposition} from './LiquidGlassSceneComposition';
export {getSceneLeafClip} from './sceneOverflow';
export {drawText} from './text';`);
const result=await esbuild.build({
  entryPoints:['entry.ts'],bundle:true,write:false,format:'esm',platform:'browser',target:'es2022',
  minify:false,charset:'ascii',legalComments:'eof',define:{'import.meta.env.DEV':'false'},
  plugins:[{name:'historical-material',setup(build){
    build.onResolve({filter:/.*/},args=>{
      const name=args.path.replace(/^\.\//,'');
      const key=name.endsWith('.ts')?name:`${name}.ts`;
      if(!modules.has(key))throw new Error(`Unexpected runtime import: ${args.path}`);
      return {path:key,namespace:'material'};
    });
    build.onLoad({filter:/.*/,namespace:'material'},args=>({contents:modules.get(args.path),loader:'ts'}));
  }}],
});
const outputs=new Map([['material.js',Buffer.from(result.outputFiles[0].contents)]]);
outputs.set('.gitattributes',Buffer.from('# Preserve exact source and generated artifact hashes.\n* -text\n'));
for(const [name,bytes] of sources)outputs.set(`source/upstream/${name}`,bytes);
outputs.set('licenses/Folia-AGPL-3.0.txt',readFileSync(upstream?path.join(root,'LICENSE'):path.join(output,'licenses/Folia-AGPL-3.0.txt')));
outputs.set('source/entry.ts',Buffer.from(modules.get('entry.ts')));
const manifest={schema:1,builder:'esbuild',builderVersion:esbuild.version,
  reference:'lume1.0.1/third_party/folia-major/upstream',
  sources:Object.fromEntries([...sources].map(([name,bytes])=>[name,sha(bytes)])),
  adaptations:['Renderer, shaders, model and preset are unmodified; import.meta.env.DEV is false.',
    'Composition and overflow lookup use each element ownerDocument for same-origin iframe realms.',
    'Overflow bounds copy coordinates explicitly because native DOMRect properties are not enumerable.',
    'Historical text painter extracted by AST; global scene registry and React lifecycle are not imported.'],
  generated:Object.fromEntries([...outputs].map(([name,bytes])=>[name,sha(bytes)]))};
outputs.set('manifest.json',Buffer.from(JSON.stringify(manifest,null,2)+'\n'));
for(const [name,bytes] of outputs){
  const file=path.join(output,name);
  if(check){
    if(!existsSync(file)||!readFileSync(file).equals(bytes))throw new Error(`Extraction differs: ${name}`);
  }else{mkdirSync(path.dirname(file),{recursive:true});writeFileSync(file,bytes);}
}
console.log(`${check?'Verified':'Extracted'} music material: ${outputs.size} files, esbuild ${esbuild.version}`);
