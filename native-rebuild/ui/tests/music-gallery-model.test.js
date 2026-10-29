import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync} from 'node:fs';

const url = new URL('../music-visualizers/gallery-model.js', import.meta.url);
async function model() {
  assert.ok(existsSync(url), 'gallery-model.js must implement the Monet model adapter');
  return import(url);
}
const lines = (count = 2000) => Array.from({length:count}, (_, i) => ({
  startTime:i * 5 + 2, endTime:i * 5 + 5, fullText:`Line ${i}`, words:[],
}));

test('focus follows Pendolo: sung line, half past it in a gap, before the intro and after the outro', async () => {
  const {galleryAnchor,galleryStatus} = await model(), data = lines();
  assert.equal(galleryAnchor(data,1000,5003),1000);
  assert.equal(galleryAnchor(data,1000,5006.5),1000.5,'the gap after line 1000 rests between it and the next');
  assert.equal(galleryAnchor(data,-1,0),-1);
  assert.equal(galleryAnchor(data,1999,20000),2000);
  assert.equal(galleryAnchor([], -1, 0),0);
  assert.equal(galleryStatus(1000,1000),'active');
  assert.equal(galleryStatus(1000,1000.5),'passed');
  assert.equal(galleryStatus(1001,1000.5),'waiting');
});

test('scroll uses accumulation, direction reset, one step per event and clamped anchors', async () => {
  const {createGalleryScroll, GALLERY_SCROLL_RESET_MS} = await model();
  assert.equal(GALLERY_SCROLL_RESET_MS,1800);
  const scroll=createGalleryScroll();
  assert.equal(scroll.move('wheel',71,10,100),null);
  assert.equal(scroll.move('wheel',1,10,100),11);
  assert.equal(scroll.move('wheel',2000,10,100),12);
  scroll.reset();
  assert.equal(scroll.move('wheel',60,10,100),null);
  assert.equal(scroll.move('wheel',-71,10,100),null);
  assert.equal(scroll.move('wheel',-1,10,100),9);
  scroll.reset();
  assert.equal(scroll.move('touch',51,10,100),null);
  assert.equal(scroll.move('touch',1,10,100),11);
  scroll.reset();
  assert.equal(scroll.move('touch',-52,0,100),0);
  assert.equal(scroll.move('touch',52,0,1),0);
});

test('rail positions stack measured rows without overlap and interpolate the focus', async () => {
  const {galleryPositions,galleryToneAt,GALLERY_LINE_GAP} = await model();
  const heights=[40,90,40,40],p=galleryPositions(1,-1,4,4,i=>heights[i],50);
  assert.equal(p.top(1),0);assert.equal(p.top(2),90+GALLERY_LINE_GAP);assert.equal(p.top(0),-(40+GALLERY_LINE_GAP));
  for(let i=0;i<3;i++)assert.ok(p.top(i+1)>=p.top(i)+heights[i]);
  assert.equal(p.at(1),p.center(1));
  assert.equal(p.at(1.5),(p.center(1)+p.center(2))/2);
  assert.equal(p.top(-1),p.top(0)-50-GALLERY_LINE_GAP,'virtual rows before the first line keep a steady step');
  const focus=galleryToneAt(0,.72,false),half=galleryToneAt(.5,.72,false),far=galleryToneAt(3,.72,false);
  assert.deepEqual(focus,{opacity:1,scale:1,blurPx:0});
  assert.ok(half.opacity<1&&half.opacity>far.opacity&&half.scale>far.scale&&half.blurPx<far.blurPx);
  assert.ok(galleryToneAt(1,.72,true).opacity<galleryToneAt(1,.72,false).opacity,'sung lines recede faster');
});

test('tokens preserve punctuation, spaces, emoji graphemes and seconds', async () => {
  const {createGalleryCore, gallerySweep} = await model(), core=createGalleryCore();
  const line={startTime:10,endTime:14,fullText:'Hi, 世界 👩‍💻!',words:[{text:'Hi',startTime:10,endTime:11},{text:'世界',startTime:12,endTime:13},{text:'👩‍💻',startTime:13,endTime:14}]};
  const tokens=core.buildMonetDisplayTokens(line);
  assert.equal(tokens.map(t=>t.text).join(''),line.fullText);
  assert.equal(tokens.find(t=>t.text==='👩‍💻').graphemeTimings.length,1);
  const word=tokens.find(t=>t.text==='世界');
  assert.equal(gallerySweep(word,[0,20,40],12.25,28).fillWidth,10);
  assert.equal(gallerySweep(word,[0,20,40],13,28).fillWidth,40);
  assert.equal(gallerySweep(word,[0,20,40],11,28).fillWidth,0);
  assert.ok(gallerySweep(word,[0,20,40],13,28).sweepEnd>40);
});

test('measured layout limits active to 3 rows and inactive to 2 without translation', async () => {
  const {createGalleryCore} = await model(), core=createGalleryCore();
  globalThis.OffscreenCanvas=class {getContext(){return {font:'',measureText:text=>({width:Array.from(text).length*20,actualBoundingBoxAscent:22,actualBoundingBoxDescent:8})};}};
  try {
    const line={fullText:'长歌词 Mixed text '.repeat(30),translation:'ignored',startTime:0,endTime:10,words:[]};
    const options={line,fontPx:28,translationFontPx:15,fontStack:'sans-serif',maxWidthPx:240,showSubtitleTranslation:false};
    const active=core.measureMonetLineLayout({...options,status:'active'});
    const waiting=core.measureMonetLineLayout({...options,status:'waiting'});
    assert.equal(active.visibleTextLineCount,3);
    assert.equal(waiting.visibleTextLineCount,2);
    assert.equal(active.translationHeightPx,0);
    assert.ok(active.isTextClipped);
    assert.ok(active.lineHeightPx>=32);
    core.clearGalleryMeasurements();
  } finally {delete globalThis.OffscreenCanvas;}
});

test('one spring moves the focus, keeps momentum when retargeted and jumps on long seeks', async () => {
  const {createGalleryFocus} = await model();
  const focus=createGalleryFocus(10);
  focus.to(11,0);
  const middle=focus.sample(120);
  assert.ok(middle.value>10&&middle.value<11&&middle.velocity>0);
  focus.to(10,120);
  assert.ok(focus.sample(121).value>middle.value,'retargeting keeps momentum');
  assert.equal(focus.sample(10000).value,10);assert.equal(focus.moving,false);
  focus.to(500,10000);assert.equal(focus.moving,false);assert.equal(focus.sample(10000).value,500);
});

test('overwide timed tokens split into bounded runs without changing grapheme timing',async()=>{
  const {splitGalleryToken}=await model();
  assert.equal(typeof splitGalleryToken,'function');
  const token={text:'hello世界',timed:true,startTime:10,endTime:17,key:'test',
    graphemeTimings:Array.from('hello世界',(char,i)=>({char,startTime:10+i,endTime:11+i}))};
  const chunks=splitGalleryToken(token,[0,10,20,30,40,50,70,90],32);
  assert.equal(chunks.map(chunk=>chunk.token.text).join(''),token.text);
  assert.ok(chunks.every(chunk=>chunk.offsets.at(-1)<=32));
  assert.deepEqual(chunks.flatMap(chunk=>chunk.token.graphemeTimings),token.graphemeTimings);
  assert.equal(chunks.at(-1).token.endTime,17);
});

test('separate measurement factories release their own canvas and cache',async()=>{
  const {createGalleryCore}=await model();
  const make=()=>{let reads=0;const document={createElement:()=>({getContext:()=>({font:'',measureText:text=>{reads++;return {width:text.length*10,actualBoundingBoxAscent:20,actualBoundingBoxDescent:3};}})})};return {core:createGalleryCore(document),get reads(){return reads;}};};
  const a=make(),b=make();
  a.core.measureMonetGraphemeOffsets('cache',28,'600 28px sans-serif');
  const count=a.reads;a.core.measureMonetGraphemeOffsets('cache',28,'600 28px sans-serif');
  assert.equal(a.reads,count);assert.equal(b.reads,0);
  a.core.clearGalleryMeasurements();a.core.measureMonetGraphemeOffsets('cache',28,'600 28px sans-serif');
  assert.ok(a.reads>count);assert.equal(b.reads,0);
});
