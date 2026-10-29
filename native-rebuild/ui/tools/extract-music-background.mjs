// Build-time only. Extract the historical wallpaper compositor into a browser-only leaf.
import {createRequire} from 'node:module';
import {existsSync,mkdirSync,readFileSync,writeFileSync,readdirSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

if(!process.argv[2])throw new Error('Usage: node tools/extract-music-background.mjs <read-only upstream>');
const upstream=path.resolve(process.argv[2]);
const output=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../music-background/reference');
const require=createRequire(path.join(upstream,'package.json'));
function loadEsbuild() {
  try { return require('esbuild'); } catch (standardError) {
    const pnpmRoot=path.join(upstream,'node_modules','.pnpm');
    if (!existsSync(pnpmRoot)) throw standardError;
    const packageDir=readdirSync(pnpmRoot,{withFileTypes:true})
      .filter(entry=>entry.isDirectory()&&/^esbuild@/.test(entry.name))
      .map(entry=>path.join(pnpmRoot,entry.name,'node_modules','esbuild'))
      .find(candidate=>existsSync(path.join(candidate,'package.json')));
    if (!packageDir) throw standardError;
    const packageJson=JSON.parse(readFileSync(path.join(packageDir,'package.json'),'utf8'));
    return createRequire(path.join(packageDir,'package.json'))(path.join(packageDir,packageJson.main));
  }
}
const esbuild=loadEsbuild();
const slash=value=>value.replaceAll('\\','/');
const sha256=value=>createHash('sha256').update(value).digest('hex');
const historyFile=relative=>path.join(upstream,relative);
const sourceHashes=new Map();
const sources={
  compositor:'src/components/visualizer/backgrounds/wallpaper/WallpaperCompositor.ts',
  animation:'src/components/visualizer/backgrounds/wallpaper/AnimatedWallpaperImage.ts',
  registry:'src/components/liquid-glass/glassMediaSource.ts',
};
const patches={
  [sources.compositor]:[
    [`type Media = { kind: 'video' | 'image'; url: string; mimeType?: string };`,
      `type Media = { kind: 'video' | 'image'; url: string; mimeType?: string; skipAnimation?: boolean };`],
    [`    setMedia(media: Media, color: string): Promise<boolean> {
        if (this.disposed) return Promise.resolve(false);
        if (this.pending) { this.release(this.pending.layer); this.pending.finish(false); this.pending = null; }`,
      `    setMedia(media: Media, color: string): Promise<boolean> {
        if (this.disposed) return Promise.resolve(false);
        this.cancelPending();`],
    [`                    try { layer.animation = await AnimatedWallpaperImage.load(media.url, media.mimeType, controller.signal, this.invalidate) ?? undefined; }`,
      `                    try { if (!media.skipAnimation) layer.animation = await AnimatedWallpaperImage.load(media.url, media.mimeType, controller.signal, this.invalidate) ?? undefined; }`],
    [`    setVisible(visible: boolean) {`,
      `    cancelPending() {
        if (!this.pending) return;
        this.release(this.pending.layer); this.pending.finish(false); this.pending = null;
    }

    setVisible(visible: boolean) {`],
  ],
  [sources.animation]:[
    [`type DecoderClass = { new(options: { data: ArrayBuffer; type: string }): Decoder };`,
      `type DecoderClass = { new(options: { data: ArrayBuffer; type: string }): Decoder };
const MAX_ANIMATED_BYTES = 50 * 1024 * 1024;
const MAX_ANIMATED_PIXELS = 64 * 1024 * 1024;
const MAX_ANIMATED_SIDE = 16384;

async function readBounded(response: Response, signal: AbortSignal) {
    const length = Number(response.headers.get('content-length'));
    if (Number.isFinite(length) && length > MAX_ANIMATED_BYTES) throw new Error('Animated wallpaper is too large');
    if (!response.body) {
        const data = await response.arrayBuffer();
        if (data.byteLength > MAX_ANIMATED_BYTES) throw new Error('Animated wallpaper is too large');
        return data;
    }
    const reader = response.body.getReader(), chunks: Uint8Array[] = [];
    let total = 0;
    try {
        while (true) {
            const part = await reader.read();
            if (part.done) break;
            total += part.value.byteLength;
            if (total > MAX_ANIMATED_BYTES) { await reader.cancel(); throw new Error('Animated wallpaper is too large'); }
            chunks.push(part.value); signal.throwIfAborted();
        }
    } finally { reader.releaseLock(); }
    const data = new Uint8Array(total); let offset = 0;
    for (const chunk of chunks) { data.set(chunk, offset); offset += chunk.byteLength; }
    return data.buffer;
}`],
    [`        const data = await response.arrayBuffer(); signal.throwIfAborted();`,
      `        const data = await readBounded(response, signal); signal.throwIfAborted();`],
    [`                if (this.canvas.width !== image.displayWidth) this.canvas.width = image.displayWidth;`,
      `                const width = image.displayWidth, height = image.displayHeight;
                if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1
                    || width > MAX_ANIMATED_SIDE || height > MAX_ANIMATED_SIDE || width * height > MAX_ANIMATED_PIXELS)
                    throw new Error('Animated wallpaper dimensions are too large');
                if (this.canvas.width !== width) this.canvas.width = width;`],
    [`                if (this.canvas.height !== image.displayHeight) this.canvas.height = image.displayHeight;`,
      `                if (this.canvas.height !== height) this.canvas.height = height;`],
  ],
};

function historical(relative) {
  const bytes=readFileSync(historyFile(relative));
  sourceHashes.set(slash(relative),sha256(bytes));
  let source=bytes.toString('utf8');
  for(const [before,after] of patches[relative]??[]) {
    const count=source.split(before).length-1;
    if(count!==1)throw new Error(`Historical patch drift: ${relative} (${count})`);
    source=source.replace(before,after);
  }
  return source;
}

const entry=`export { WallpaperCompositor } from ${JSON.stringify(historyFile(sources.compositor))};
export { AnimatedWallpaperImage } from ${JSON.stringify(historyFile(sources.animation))};
export { GlassMediaRegistry, isGlassCanvas } from ${JSON.stringify(historyFile(sources.registry))};
`;
const plugin={name:'music-background-read-only-source',setup(build){
  build.onLoad({filter:/\.[cm]?[jt]sx?$/},args=>{
    const relative=slash(path.relative(upstream,args.path));
    if(!relative.startsWith('src/'))return;
    const contents=historical(relative);
    return {contents,loader:path.extname(args.path)==='.ts'?'ts':'js',resolveDir:path.dirname(args.path)};
  });
}};

mkdirSync(output,{recursive:true});mkdirSync(path.join(output,'source/upstream'),{recursive:true});mkdirSync(path.join(output,'licenses'),{recursive:true});
const build=await esbuild.build({stdin:{contents:entry,resolveDir:upstream,sourcefile:'source/entry.ts'},bundle:true,format:'esm',platform:'browser',target:'es2022',write:false,minify:false,legalComments:'eof',plugins:[plugin],logLevel:'warning'});
if(build.outputFiles.length!==1)throw new Error('Expected one background bundle');
const generated=[];
function emit(relative,bytes) {
  const destination=path.resolve(output,relative);
  if(!destination.startsWith(output+path.sep))throw new Error('Output escapes reference directory');
  mkdirSync(path.dirname(destination),{recursive:true});writeFileSync(destination,bytes);
  generated.push({path:slash(relative),bytes:bytes.byteLength,sha256:sha256(bytes)});
}
emit('background.js',build.outputFiles[0].contents);
for(const relative of Object.values(sources))emit(`source/upstream/${relative}`,readFileSync(historyFile(relative)));
const license=readFileSync(historyFile('LICENSE'));emit('licenses/Folia-AGPL-3.0.txt',license);
const sourceEntry=Buffer.from(entry);emit('source/entry.ts',sourceEntry);
const manifest={schema:1,builder:'esbuild',builderVersion:esbuild.version,reference:'lume1.0.1/third_party/folia-major/upstream',
  sources:Object.fromEntries([...sourceHashes].sort(([a],[b])=>a.localeCompare(b,'en'))),
  adaptations:['WallpaperCompositor and AnimatedWallpaperImage are bundled as browser-only ESM with no React, store, native or global registry dependency.',
    'Original 420ms cubic-bezier fade, cover crop, canvas frame event and video-frame scheduling are retained.',
    'Added cancelPending for bounded view timeouts and stale replacement cancellation.',
    'Animated decoding is skipped for the static default image to avoid a second fetch/decode.',
    'Animated fetches are bounded to 50 MiB and decoded frames to 64 Mi pixels with a 16384-pixel side limit.'],
  generated:generated.sort((a,b)=>a.path.localeCompare(b.path,'en'))};
writeFileSync(path.join(output,'manifest.json'),JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify({builder:esbuild.version,generated,sourceCount:sourceHashes.size},null,2));
