import test from 'node:test';
import assert from 'node:assert/strict';
import {backgroundFixture} from './music-background-harness.js';
import {mountMusicBackground,validateMusicBackgroundMedia,musicDefaultBackground} from '../music-background-view.js';
import {mountGalleryBackground} from '../music-visualizers/gallery-background.js';

const video=id=>({id,name:`${id}.webm`,kind:'video',mimeType:'video/webm',url:`https://lume-music.local/${id}.webm`});
const image=id=>({id,name:`${id}.webp`,kind:'image',mimeType:'image/webp',url:`https://lume-music.local/${id}.webp`});

test('default home poster accepts only official images, without relaxing selected-media authorization',()=>{
  const url='https://static.lumedesktop.com/lume/wallpapers/official/hero-thumbnails/forest-2.webp';
  const media=musicDefaultBackground(url,'Forest 2');
  assert.deepEqual(media,{id:'default-home-background',name:'Forest 2',kind:'image',mimeType:'image/webp',url});
  assert.equal(validateMusicBackgroundMedia(media),false);
  for(const value of ['https://example.com/lume/one.jpg','file:///C:/one.jpg',
    'https://static.lumedesktop.com/lume/one.mp4','https://static.lumedesktop.com/other/one.jpg',
    'https://static.lumedesktop.com/lume/../one.jpg',
    'https://static.lumedesktop.com/lume/a%2fb.png','https://user@static.lumedesktop.com/lume/a.png'])
    assert.equal(musicDefaultBackground(value),null,value);
});

test('media boundary accepts only native-shaped opaque local descriptors',()=>{
  assert.equal(validateMusicBackgroundMedia(video('fixture')),true);
  assert.equal(validateMusicBackgroundMedia(image('fixture')),true);
  assert.equal(validateMusicBackgroundMedia(null),true);
  for(const value of [{},[],{...video('x'),url:'https://example.com/x.webm'},
    {...video('x'),url:'https://lume-music.local/x.webm?remote=1'},
    {...video('x'),kind:'image'}, {...video('x'),mimeType:'image/webp'},
    {...video('x'),extra:true}])assert.equal(validateMusicBackgroundMedia(value),false);
});

test('compositor preserves cover crop and the original 420ms incoming/outgoing curve',async()=>{
  const f=backgroundFixture(),load=f.compositor.setMedia(video('a'),'#000000');f.ready(0);assert.equal(await load,true);f.tick();
  const first=f.context.draws.at(-1);assert.equal(first[0],f.media[0]);assert.ok(Math.abs(first[1]+44.444)<.1);assert.ok(Math.abs(first[4]-500)<.1);
  const replacement=f.compositor.setMedia(image('b'),'#000000');f.ready(1,'image');assert.equal(await replacement,true);f.tick(210);
  const middle=f.context.draws.at(-1);assert.notEqual(middle[0],f.media[0]);assert.notEqual(middle[0],f.media[1]);
  f.tick(220);assert.equal(f.context.draws.at(-1)[0],f.media[1]);assert.equal(f.frames.size,0);f.compositor.dispose();
});

test('rapid replacement cancels stale loads, preserves the current layer on failure and stops hidden work',async()=>{
  const f=backgroundFixture(),first=f.compositor.setMedia(video('a'),'#000000');f.ready(0);assert.equal(await first,true);f.tick();
  const stale=f.compositor.setMedia(video('b'),'#000000'),latest=f.compositor.setMedia(image('c'),'#000000');
  assert.equal(await stale,false);f.fail(2);assert.equal(await latest,false);f.tick();assert.equal(f.context.draws.at(-1)[0],f.media[0]);
  f.compositor.setVisible(false);assert.equal(f.frames.size,0);assert.equal(f.media[0].paused,true);
  f.compositor.setVisible(true);f.tick();assert.equal(f.media[0].paused,false);f.compositor.dispose();assert.equal(f.frames.size,0);assert.equal(f.media[0].src,'');
});

test('view exposes the ready canvas scene and releases it on dispose',async()=>{
  const f=backgroundFixture(),oldDocument=globalThis.document,oldResize=globalThis.ResizeObserver;
  globalThis.document={hidden:false,createElement:kind=>f.media.push(new (class extends EventTarget {constructor(){super();this.kind=kind;this.paused=true;this.videoWidth=640;this.videoHeight=360;this.readyState=0;this.play=async()=>{this.paused=false;};this.pause=()=>{this.paused=true;};this.load=()=>{};this.removeAttribute=()=>{};}})())};
  globalThis.ResizeObserver=class {observe(){} disconnect(){}};
  try {
    const ready=[];const mounted=mountMusicBackground(f.canvas,{media:null,onReady:media=>ready.push(media)});
    assert.equal(mounted.getScene(),null);assert.equal(await mounted.update({media:image('fixture'),visible:true}),false);
    mounted.dispose();assert.equal(mounted.getScene(),null);assert.equal(ready.length,0);
  } finally {globalThis.document=oldDocument;globalThis.ResizeObserver=oldResize;}
});

test('shared scenes expose the compositor source without duplicating video decoders',async()=>{
  const f=backgroundFixture(),oldDocument=globalThis.document,oldResize=globalThis.ResizeObserver;
  let now=0;
  const sources=[];
  const window={
    performance:{now:()=>now},
    requestAnimationFrame:()=>1,
    cancelAnimationFrame(){},
    setTimeout:callback=>{callback();return 1;},
    clearTimeout(){},
    ResizeObserver:class {observe(){} disconnect(){}},
  };
  const createCanvas=()=>Object.assign(new EventTarget(),{
    width:800,height:500,
    getContext:()=>({drawImage(){},fillRect(){},clearRect(){},globalAlpha:1,fillStyle:''}),
  });
  const document={
    hidden:false,defaultView:window,
    createElement:kind=>{
      if(kind==='canvas')return createCanvas();
      const source=new (class extends EventTarget {
        constructor(){
          super();this.kind=kind;this.src='';this.paused=true;this.readyState=0;
          this.videoWidth=640;this.videoHeight=360;this.naturalWidth=640;this.naturalHeight=360;
          this.play=async()=>{this.paused=false;};this.pause=()=>{this.paused=true;};
          this.load=()=>{};this.removeAttribute=name=>{if(name==='src')this.src='';};
        }
      })();
      sources.push(source);return source;
    },
    addEventListener(){},removeEventListener(){},
  };
  Object.defineProperty(f.canvas,'ownerDocument',{value:document,configurable:true});
  try {
    const mounted=mountMusicBackground(f.canvas,{media:image('image-a')});
    sources[0].readyState=2;sources[0].dispatchEvent(new Event('load'));
    assert.equal(await mounted.ready,true);
    const first=mounted.getScene();
    assert.equal(first.canvas,f.canvas);
    assert.equal(first.media.id,'image-a');
    assert.equal(first.mediaSource,sources[0]);
    assert.deepEqual(Object.keys(first).sort(),['canvas','media','mediaSource']);

    const videoMedia=video('video-a');
    const videoUpdate=mounted.update({media:videoMedia});
    sources[1].readyState=2;sources[1].dispatchEvent(new Event('loadeddata'));
    assert.equal(await videoUpdate,true);
    const videoScene=mounted.getScene();
    assert.equal(videoScene.canvas,f.canvas);
    assert.equal(videoScene.mediaSource,sources[1]);
    assert.equal(sources.filter(source=>source.kind==='video').length,1);

    await mounted.update({visible:false});
    await mounted.update({visible:true});
    assert.equal(mounted.getScene().mediaSource,sources[1]);
    assert.equal(sources.filter(source=>source.kind==='video').length,1);

    const imageUpdate=mounted.update({media:image('image-b')});
    sources[2].readyState=2;sources[2].dispatchEvent(new Event('load'));
    assert.equal(await imageUpdate,true);
    assert.equal(mounted.getScene().media.id,'image-b');
    assert.equal(sources.filter(source=>source.kind==='video').length,1);
    const firstPoster=musicDefaultBackground('https://static.lumedesktop.com/lume/forest.webp','Forest');
    await mounted.update({defaultMedia:firstPoster});
    assert.equal(mounted.getScene().media.id,'image-b','default updates never replace the user selection');
    const reset=mounted.update({media:null});
    sources[3].dispatchEvent(new Event('load'));
    assert.equal(await reset,true);
    assert.equal(mounted.getScene().media.url,firstPoster.url);
    const unavailable=musicDefaultBackground('https://static.lumedesktop.com/lume/missing.webp','Missing');
    const fallback=mounted.update({defaultMedia:unavailable});
    sources[4].dispatchEvent(new Event('error'));
    await Promise.resolve();await Promise.resolve();
    sources[5].dispatchEvent(new Event('load'));
    assert.equal(await fallback,true);
    assert.equal(mounted.getScene().media.id,'default-forest','offline catalog image falls back to the bundled image');
    await mounted.update({media:null,defaultMedia:unavailable});
    assert.equal(sources.length,6,'playback updates do not retry a failed poster every frame');
    const stale=mounted.update({media:image('late-image')});
    const retained=mounted.getScene();
    assert.equal(await mounted.update({media:null}),true);
    assert.equal(await stale,false,'returning to the current background cancels the pending replacement');
    sources[6].dispatchEvent(new Event('load'));
    await Promise.resolve();
    assert.equal(mounted.getScene().mediaSource,retained.mediaSource);
    assert.equal(sources[6].src,'','cancelled background releases its image source');
    mounted.dispose();mounted.dispose();
    assert.equal(sources.filter(source=>source.kind==='video').length,1);
    assert.equal(mounted.getScene(),null);
  } finally {globalThis.document=oldDocument;globalThis.ResizeObserver=oldResize;}
});

test('Gallery consumes the shared canvas and media source without creating video elements',()=>{
  const images=[],canvases=[],draws=[],attributes=new WeakMap();
  const window={devicePixelRatio:1};
  const document={defaultView:window,createElement:kind=>{
    assert.equal(kind,'canvas');
    const canvas=new EventTarget();
    const values=new Map([['aria-hidden','true']]);
    Object.assign(canvas,{hidden:true,width:0,height:0,
      getBoundingClientRect:()=>({width:120,height:80}),
      getContext:()=>({drawImage:(...args)=>draws.push(args)}),
      setAttribute:(name,value)=>values.set(name,String(value)),
      getAttribute:name=>values.get(name)??null,
      removeAttribute:name=>values.delete(name),
      remove:()=>{canvas.removed=true;}});
    canvases.push(canvas);return canvas;
  }};
  const element=({after=false}={})=>{
    const node=new EventTarget(),values=new Map();
    Object.defineProperty(node,'src',{get:()=>values.get('src')??'',set:value=>values.set('src',String(value))});
    Object.assign(node,{hidden:false,dataset:{},classList:{toggle:(name,value)=>{node.classListState=[name,value];}},
      setAttribute:(name,value)=>values.set(name,String(value)),getAttribute:name=>values.get(name)??null,
      removeAttribute:name=>values.delete(name),getBoundingClientRect:()=>({width:120,height:80})});
    if(after)node.after=child=>{node.afterChild=child;};
    attributes.set(node,values);return node;
  };
  const root=element(),backdrop=element(),artwork=element(),sleeveImage=element({after:true}),discImage=element({after:true});
  root.ownerDocument=document;images.push(sleeveImage,discImage);
  const videoSource=new EventTarget();Object.assign(videoSource,{videoWidth:640,videoHeight:360});
  const scene={canvas:new EventTarget(),media:video('shared-video'),mediaSource:videoSource};
  const mounted=mountGalleryBackground({root,backdrop,artwork,sleeveImage,discImage});
  mounted.update(scene,true);
  assert.equal(artwork.dataset.galleryArtworkSource,'video');
  assert.equal(canvases.every(canvas=>canvas.hidden===false),true);
  assert.equal(draws.length,2);
  assert.equal(draws[0][0],videoSource);
  mounted.update(scene,false);mounted.update(scene,true);
  assert.equal(draws.length,4);
  mounted.update(null,false);mounted.update(scene,true);
  assert.equal(draws.length,6);
  mounted.dispose();mounted.dispose();
  assert.equal(canvases.every(canvas=>canvas.removed===true),true);
  assert.equal(images.every(image=>image.getAttribute('src')===null),true);
});
