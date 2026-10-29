import nodeTest from 'node:test';
import assert from 'node:assert/strict';
import {existsSync,readFileSync} from 'node:fs';
import {createRequire} from 'node:module';

const jsdomPath=process.env.LUME_JSDOM_PATH;
const {JSDOM}=jsdomPath?createRequire(import.meta.url)(jsdomPath):{};
const test=(name,fn)=>nodeTest(name,{skip:!jsdomPath},fn);
const rendererUrl=new URL('../music-visualizers/gallery.js',import.meta.url);
const data=Array.from({length:2000},(_,i)=>({startTime:i*5,endTime:i*5+4,fullText:`Lyric ${i}`,words:[]}));
const snapshot=fields=>({lines:data,activeLineIndex:1000,currentTime:5001,playing:true,visible:true,current:{title:'Song',artist:'Artist',artwork:''},...fields});

async function setup(t, fields={}, {noCanvas=false}={}) {
  assert.ok(existsSync(rendererUrl),'gallery.js must implement mountGallery');
  const {mountGallery}=await import(rendererUrl);
  const dom=new JSDOM('<main id="host"></main>',{url:'https://lume.local/music.html',pretendToBeVisual:true});
  const {window}=dom, document=window.document,host=document.querySelector('main');
  Object.defineProperty(document,'fonts',{value:new window.EventTarget()});
  let now=0,id=0,hidden=false,measurements=0;
  const raf=new Map(),timers=new Map(),observers=[],listeners=new Set();
  Object.defineProperty(window.performance,'now',{value:()=>now});
  Object.defineProperty(document,'hidden',{get:()=>hidden});
  window.requestAnimationFrame=fn=>{raf.set(++id,fn);return id;};
  window.cancelAnimationFrame=key=>raf.delete(key);
  window.setTimeout=(fn,delay)=>{timers.set(++id,{fn,at:now+delay});return id;};
  window.clearTimeout=key=>timers.delete(key);
  window.matchMedia=()=>({matches:false,addEventListener(){},removeEventListener(){}});
  window.ResizeObserver=class {constructor(fn){this.fn=fn;observers.push(this);} observe(target){this.target=target;this.connected=true;} disconnect(){this.connected=false;}};
  window.HTMLCanvasElement.prototype.getContext=function(){return noCanvas?null:{font:'',measureText:text=>{measurements++;return {width:Array.from(text).length*14,actualBoundingBoxAscent:22,actualBoundingBoxDescent:5};}};};
  for(const target of [document,window]) {
    const add=target.addEventListener.bind(target),remove=target.removeEventListener.bind(target);
    target.addEventListener=(type,fn,opts)=>{if(['visibilitychange','resize'].includes(type))listeners.add(fn);add(type,fn,opts);};
    target.removeEventListener=(type,fn,opts)=>{listeners.delete(fn);remove(type,fn,opts);};
  }
  const oldDocument=globalThis.document;
  globalThis.document=document;
  const controller=mountGallery(host,snapshot(fields));
  const step=ms=>{now+=ms;const callbacks=[...raf.values()];raf.clear();callbacks.forEach(fn=>fn(now));for(const [key,timer] of [...timers])if(timer.at<=now){timers.delete(key);timer.fn();}};
  const fire=(type,fields={})=>{const event=new window.Event(type,{cancelable:true,bubbles:true});Object.assign(event,fields);host.querySelector('[data-monet-lyrics-viewport]').dispatchEvent(event);return event;};
  const visibility=value=>{hidden=value;document.dispatchEvent(new window.Event('visibilitychange'));};
  t.after(()=>{controller.dispose();dom.window.close();globalThis.document=oldDocument;});
  return {window,document,host,controller,raf,timers,observers,listeners,step,fire,visibility,
    advanceWithoutCallbacks:ms=>{now+=ms;},get measurements(){return measurements;}};
}

test('2000 lyrics mount only a small window; time-only updates retain nodes and measurement',async t=>{
  const f=await setup(t), rows=()=>[...f.host.querySelectorAll('[data-gallery-lyric-line]')];
  assert.equal(rows().length,13);
  assert.equal(f.host.querySelector('[data-gallery-line-status="active"]').dataset.galleryLyricLine,'1000');
  const row=f.host.querySelector('[data-gallery-line-status="active"]'),count=f.measurements;
  f.controller.update(snapshot({currentTime:5001.5}));
  assert.equal(f.host.querySelector('[data-gallery-line-status="active"]'),row);assert.equal(f.measurements,count);
  for(let i=1010;i<1200;i+=10)f.controller.update(snapshot({activeLineIndex:i,currentTime:i*5+1}));
  assert.ok(rows().length<=18,'entering plus retiring windows are bounded');
  f.step(2000);assert.equal(rows().length,13);
});

test('preferences recolor in place; font changes invalidate measurements and reset keeps the stage',async t=>{
  const f=await setup(t,{playing:false});
  const defaults={version:1,mode:'gallery',fontFamily:'',animationIntensity:'normal',lyricsFontColor:'#ffffff',lyricsKaraokeFillColor:'#ffffff'};
  const root=f.host.firstElementChild,active=f.host.querySelector('[data-gallery-line-status="active"]');
  const fill=active.querySelector('.MusicGalleryFillText'),count=f.measurements;
  // This legacy jsdom drops gradients; capture the paint input and verify CSS in Edge separately.
  let gradient='';Object.defineProperty(fill.style,'backgroundImage',{get:()=>gradient,set:value=>{gradient=value;}});
  const colors={...defaults,lyricsFontColor:'#12ab34',lyricsKaraokeFillColor:'#ab1234'};
  f.controller.update(snapshot({playing:false,preferences:colors}));
  assert.equal(f.host.firstElementChild,root);assert.equal(f.measurements,count);
  assert.equal(active.querySelector('.MusicGalleryFillText'),fill);
  assert.match(gradient,/171,18,52/);
  assert.match(active.querySelector('.MusicGalleryWord > span').style.color,/18, 171, 52/);
  const preferences={...colors,fontFamily:'Courier New'};
  f.controller.update(snapshot({playing:false,preferences}));
  assert.ok(f.measurements>count);
  assert.ok(root.style.fontFamily.startsWith('"Courier New", '));
  assert.notEqual(active.querySelector('.MusicGalleryFillText'),fill);
  const measured=f.measurements,newFill=active.querySelector('.MusicGalleryFillText');
  f.controller.update(snapshot({playing:false,preferences:{...preferences}}));
  f.controller.update(snapshot({playing:false}));
  assert.equal(f.measurements,measured);assert.equal(active.querySelector('.MusicGalleryFillText'),newFill);
  assert.throws(()=>f.controller.update(snapshot({preferences:{...preferences,unknown:1}})),TypeError);
  f.controller.update(snapshot({playing:false,preferences:defaults}));
  assert.ok(f.measurements>measured);assert.equal(f.host.firstElementChild,root);
  assert.ok(root.style.fontFamily.startsWith('"Inter", '));
  assert.match(active.querySelector('.MusicGalleryWord > span').style.color,/255, 255, 255/);
});

test('invalid initial preferences allocate no Gallery DOM or subscriptions',async t=>{
  const f=await setup(t),{mountGallery}=await import(rendererUrl),host=f.document.createElement('main');
  const listeners=f.listeners.size,observers=f.observers.length;
  assert.throws(()=>mountGallery(host,snapshot({preferences:{version:1}})),TypeError);
  assert.equal(host.children.length,0);assert.equal(f.listeners.size,listeners);assert.equal(f.observers.length,observers);
});

test('wheel and touch browse independently of playback then restore the newest active line',async t=>{
  const f=await setup(t),rail=f.host.querySelector('[data-monet-lyrics-viewport]');
  assert.equal(f.fire('wheel',{deltaY:71,deltaMode:0}).defaultPrevented,true);
  assert.equal(rail.dataset.galleryScrollAnchor,undefined);
  f.fire('wheel',{deltaY:1,deltaMode:0});assert.equal(rail.dataset.galleryScrollAnchor,'1001');
  f.controller.update(snapshot({activeLineIndex:1002,currentTime:5011}));
  assert.equal(rail.dataset.galleryScrollAnchor,'1001');
  f.step(1799);assert.equal(rail.dataset.galleryScrollAnchor,'1001');
  f.step(1);assert.equal(rail.dataset.galleryScrollAnchor,undefined);
  assert.equal(f.host.querySelector('[data-gallery-line-status="active"]').dataset.galleryLyricLine,'1002');
  f.fire('touchstart',{touches:[{clientY:200}]});
  f.fire('touchmove',{touches:[{clientY:149}]});assert.equal(rail.dataset.galleryScrollAnchor,'1002');
  f.fire('touchmove',{touches:[{clientY:148}]});assert.equal(rail.dataset.galleryScrollAnchor,'1003');
  f.fire('touchend',{touches:[]});
});

test('visibility freezes every owned animation/timer and resumes; disposal releases resources',async t=>{
  const f=await setup(t);
  f.fire('wheel',{deltaY:72,deltaMode:0});
  assert.ok(f.raf.size); assert.ok(f.timers.size);
  f.visibility(true);
  assert.equal(f.raf.size,0);assert.equal(f.timers.size,0);
  assert.equal(f.host.querySelector('[data-gallery-disc]').style.animationPlayState,'paused');
  f.controller.update(snapshot({currentTime:5020,activeLineIndex:1004}));
  assert.equal(f.raf.size,0);
  // The focus does not move while hidden; it resumes already settled.
  f.step(10000);f.visibility(false);assert.equal(f.raf.size,0);
  f.controller.update(snapshot({visible:false}));assert.equal(f.raf.size,0);assert.equal(f.timers.size,0);
  const rail=f.host.querySelector('[data-monet-lyrics-viewport]');
  f.controller.dispose();f.controller.dispose();
  assert.equal(f.host.children.length,0);assert.equal(f.raf.size,0);assert.equal(f.timers.size,0);
  assert.ok(f.observers.every(o=>!o.connected));assert.equal(f.listeners.size,0);
  rail.dispatchEvent(new f.window.Event('wheel',{bubbles:true,cancelable:true}));
  f.controller.update(snapshot());assert.equal(f.host.children.length,0);assert.equal(f.raf.size,0);
});

test('empty lyrics are explicit; metadata is text; default artwork is the independent music image',async t=>{
  const f=await setup(t,{lines:[],activeLineIndex:-1,current:{title:'<img onerror=1>',artist:'A',artwork:''}});
  assert.match(f.host.querySelector('[data-gallery-empty]').textContent,/暂无歌词/);
  assert.equal(f.host.querySelectorAll('[data-gallery-lyric-line]').length,0);
  assert.equal(f.host.querySelector('[data-gallery-song-info] h2').textContent,'<img onerror=1>');
  assert.match(f.host.querySelector('[data-gallery-artwork] img').getAttribute('src'),/assets\/music\/forest\.webp$/);
  f.controller.update(snapshot({playing:false,current:{title:'Other',artist:'',artwork:'https://approved.example/cover.jpg'}}));
  assert.equal(f.host.querySelector('[data-gallery-disc]').style.animationPlayState,'paused');
});

test('equivalent cloned snapshots retain the current rows and manual scroll anchor',async t=>{
  const f=await setup(t);
  f.fire('wheel',{deltaY:72,deltaMode:0});
  const row=f.host.querySelector('[data-gallery-line-status="active"]'),count=f.measurements;
  f.controller.update(structuredClone(snapshot({currentTime:5002})));
  assert.equal(f.host.querySelector('[data-gallery-line-status="active"]'),row);
  assert.equal(f.host.querySelector('[data-monet-lyrics-viewport]').dataset.galleryScrollAnchor,'1001');
  assert.equal(f.measurements,count);
});

test('late font loading invalidates both row measurement and karaoke offsets',async t=>{
  const f=await setup(t,{playing:false});
  // A width change also invalidates precomputed glyph offsets even at the same font size.
  const rail=f.host.querySelector('[data-monet-lyrics-viewport]');
  const observer=f.observers.find(item=>item.target===rail);
  assert.ok(observer,'lyric rail owns its resize observer separately from the background');
  Object.defineProperty(rail,'clientWidth',{configurable:true,value:220});
  observer.fn();
  const count=f.measurements;
  Object.defineProperty(rail,'clientWidth',{configurable:true,value:300});
  observer.fn();
  assert.ok(f.measurements>count);
  const active=f.host.querySelector('[data-gallery-line-status="active"]');
  const oldFill=active.querySelector('.MusicGalleryFill'),beforeFont=f.measurements;
  f.document.fonts.dispatchEvent(new f.window.Event('loadingdone'));
  assert.notEqual(active.querySelector('.MusicGalleryFill'),oldFill);
  assert.ok(f.measurements>beforeFont);
});

test('paused playback settles, same-time updates never invent lyric progress, and hidden mount is inert',async t=>{
  const f=await setup(t,{playing:false});
  assert.equal(f.raf.size,0);
  const fill=f.host.querySelector('[data-gallery-line-status="active"] .MusicGalleryFill'),mask=fill.style.maskImage;
  f.step(5000);assert.equal(fill.style.maskImage,mask);
  f.controller.update(snapshot({visible:false}));
  f.step(5000);assert.equal(f.raf.size,0);
});

test('controller seconds are authoritative and settled playing rows do not own another clock',async t=>{
  const f=await setup(t);
  const fill=f.host.querySelector('[data-gallery-line-status="active"] .MusicGalleryFill'),mask=fill.style.maskImage;
  f.step(500);assert.equal(fill.style.maskImage,mask);
  assert.equal(f.raf.size,0);
  f.controller.update(snapshot({currentTime:5002}));
  assert.notEqual(fill.style.maskImage,mask);
});

test('missing canvas context falls back to readable bounded lyric layout',async t=>{
  const f=await setup(t,{playing:false},{noCanvas:true});
  assert.equal(f.host.querySelectorAll('[data-gallery-lyric-line]').length,13);
  const active=f.host.querySelector('[data-gallery-line-status="active"]');
  assert.ok(Number.parseFloat(active.style.height)>0);
  assert.ok(active.textContent.includes('Lyric 1000'));
});

test('a reset becoming due exactly as the page hides still runs on resume',async t=>{
  const f=await setup(t);
  f.fire('wheel',{deltaY:72,deltaMode:0});
  f.advanceWithoutCallbacks(1800);f.visibility(true);
  assert.equal(f.timers.size,0);
  f.visibility(false);f.step(0);
  assert.equal(f.host.querySelector('[data-monet-lyrics-viewport]').dataset.galleryScrollAnchor,undefined);
});

test('a gap between lines rests halfway past the last sung line without dimming it into the next',async t=>{
  const f=await setup(t,{playing:false,currentTime:5004.5});
  const status=index=>f.host.querySelector(`[data-gallery-lyric-line="${index}"]`).dataset.galleryLineStatus;
  assert.equal(status(1000),'passed');assert.equal(status(1001),'waiting');
  assert.equal(f.host.querySelector('[data-gallery-line-status="active"]'),null);
  const passed=f.host.querySelector('[data-gallery-lyric-line="1000"]'),next=f.host.querySelector('[data-gallery-lyric-line="1001"]');
  // Both neighbours sit half a line from the focus: neither is dimmed to a far line.
  for(const row of [passed,next])assert.ok(Number(row.style.opacity)>.7&&Number(row.style.opacity)<1);
  f.controller.update(snapshot({playing:false,activeLineIndex:1001,currentTime:5005}));
  assert.equal(f.host.querySelector('[data-gallery-lyric-line="1001"]'),next,'a status change keeps the row');
  assert.equal(next.dataset.galleryLineStatus,'active');
});

test('tapping a line asks the page to seek there and long lines are never clipped',async t=>{
  const long=data.map((line,i)=>i===1001?{...line,fullText:'很长的一句歌词'.repeat(20)}:line);
  const f=await setup(t,{lines:long,playing:false});
  const seeks=[];f.host.addEventListener('lume-lyric-seek',event=>seeks.push(event.detail.time));
  const row=f.host.querySelector('[data-gallery-lyric-line="1001"]');
  row.dispatchEvent(new f.window.MouseEvent('click',{bubbles:true}));
  assert.deepEqual(seeks,[5005]);
  const text=row.querySelector('.MusicGalleryText');
  assert.equal(text.style.maskImage||'','');
  assert.ok(Number.parseFloat(row.style.height)>3*Number.parseFloat(text.style.lineHeight),'every wrapped row has room');
});

nodeTest('stylesheet keeps the historical continuous Gallery typography scale',async()=>{
  const {galleryFonts}=await import(new URL('../music-visualizers/gallery-model.js',import.meta.url));
  const url=new URL('../music-visualizers/gallery.css',import.meta.url);
  assert.ok(existsSync(url),'gallery.css must provide the Gallery stage');
  const css=readFileSync(url,'utf8');
  assert.match(css,/bottom:\s*176px/);assert.match(css,/24s/);
  assert.deepEqual(galleryFonts(1200),{lyric:25.8,inactive:18.6,translation:12.48});
  assert.match(css,/font-size:\s*clamp\(2rem,\s*5\.8vw,\s*5\.8rem\)/);
  assert.match(css,/font-size:\s*clamp\(1rem,\s*2\.1vw,\s*1\.8rem\)/);
  assert.match(css,/88%/);assert.match(css,/11%/);
});
