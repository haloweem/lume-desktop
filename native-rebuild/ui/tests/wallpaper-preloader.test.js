import test from 'node:test';
import assert from 'node:assert/strict';
import {createWallpaperPreloader} from '../wallpaper-preloader.js';
import {wallpaperHomePreloadTargets} from '../wallpapers.js';

const targets=n=>Array.from({length:n},(_,i)=>({source:`https://example.test/${i}.jpg`,url:`https://example.test/${i}.jpg`}));
function fixture(options={}){
  const images=[],success=[],failure=[];
  const preloader=createWallpaperPreloader({...options,createImage:()=>{
    const image={removeAttribute(name){if(name==='src'){this.cancelled=true;this.src='';}}};images.push(image);return image;
  },onSuccess:(...args)=>success.push(args),onFailure:(...args)=>failure.push(args)});
  return {images,success,failure,preloader};
}

test('cover preloading deduplicates requests, limits concurrency and releases completed images',()=>{
  const f=fixture(),list=targets(10);f.preloader.warm(list);f.preloader.warm(list);
  assert.equal(f.images.length,2);
  assert.equal(f.images[0].fetchPriority,'low');assert.equal(f.images[0].decoding,'async');
  for(let i=0;i<10;i++){
    assert.ok(f.images.length<=i+2,'at most two images are in flight');
    const image=f.images[i];image.onload();
    assert.equal(image.onload,null);assert.equal(image.onerror,null);
  }
  assert.equal(f.success.length,10);assert.equal(f.images.length,10);
  f.preloader.warm(list);assert.equal(f.images.length,10,'ready covers are not requested again');f.preloader.dispose();
});

test('hiding cancels queued and active images and ignores already queued load callbacks',()=>{
  const f=fixture(),list=targets(6);f.preloader.warm(list);
  const lateLoad=f.images[0].onload,lateError=f.images[1].onerror;
  f.preloader.setVisible(false);
  assert.ok(f.images.every(image=>image.cancelled&&image.onload===null&&image.onerror===null));
  lateLoad();lateError();assert.deepEqual(f.success,[]);assert.deepEqual(f.failure,[]);
  f.preloader.warm(list);assert.equal(f.images.length,2,'hidden pages start no requests');
  f.preloader.setVisible(true);f.preloader.warm(list);assert.equal(f.images.length,4,'reentry starts exactly two requests');
  f.images[2].onload();assert.equal(f.images.length,5,'cancelled callbacks did not corrupt concurrency');f.preloader.dispose();
});

test('failed covers are retried on reentry while successfully warmed covers are reused',()=>{
  const f=fixture(),list=targets(2);f.preloader.warm(list);
  f.images[0].onload();f.images[1].onerror();assert.equal(f.failure.length,1);
  f.preloader.warm(list);assert.equal(f.images.length,2,'failure does not cause a hot retry loop');
  f.preloader.setVisible(false);f.preloader.setVisible(true);f.preloader.warm(list);
  assert.equal(f.images.length,3);assert.equal(f.images[2].src,list[1].url);
  f.images[2].onload();assert.equal(f.success.length,2);f.preloader.dispose();
});

test('preload queue has a fixed capacity and only completed entries can be evicted',()=>{
  const f=fixture({limit:4});f.preloader.warm(targets(9));
  for(let i=0;i<4;i++)f.images[i].onload();
  assert.equal(f.images.length,4,'excess pending work is not queued');
  f.preloader.warm(targets(9).slice(4,8));
  for(let i=4;i<8;i++)f.images[i].onload();
  assert.equal(f.images.length,8);f.preloader.dispose();
});

test('home preheats only ten cover images in carousel order and never wallpaper videos',()=>{
  const state={slide:3,wallpaper:{catalog:targets(20).map((entry,i)=>({id:`cover-${i}`,kind:'video',title:`Cover ${i}`,
    thumbnail:entry.url.replace('example.test','static.lumedesktop.com'),url:`https://static.lumedesktop.com/${i}.mp4`}))}};
  const list=wallpaperHomePreloadTargets(state);
  assert.equal(list.length,10);assert.match(list[0].url,/\/3.jpg$/);assert.match(list.at(-1).url,/\/2.jpg$/);
  assert.ok(list.every(entry=>entry.kind==='hero'&&!entry.url.endsWith('.mp4')));
});
