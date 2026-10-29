import {wallpaperAssetPreferredUrl,rememberWallpaperAssetSuccess} from './wallpapers.js';

// Retry only the visible image, never the whole catalog or the COS origin.
// Timers start on intersection, so lazy offscreen cards cannot time out.
export function mountWallpaperImages(root,{timeoutMs=6000}={}) {
  const view=root.ownerDocument.defaultView, records=new Map();
  let visible=true;
  const selector='img[data-wallpaper-preview]';
  const placeholder=image=>image.closest('.wallpaper-card,.wallpaper-detail-media,.wallpaper-info-thumbnail,.preview')
    ?.querySelector('[data-wallpaper-poster-fallback],[data-wallpaper-info-fallback]');
  function clear(record){if(record.timer){view.clearTimeout(record.timer);record.timer=0;}}
  function arm(image,record){
    clear(record);
    if(!visible||record.done||!record.visible||!image.getAttribute('src'))return;
    // The detail underlay already supplies a quick cover. Keep the larger
    // image in flight so a slow response can still improve detail sharpness.
    if(image.classList.contains('wallpaper-detail-poster'))return;
    record.timer=view.setTimeout(()=>fail(image),timeoutMs);
  }
  function fail(image){
    const record=records.get(image);if(!record||record.done)return;
    clear(record);
    const current=image.getAttribute('src');
    const fallback=wallpaperAssetPreferredUrl(image.dataset.wallpaperFallbackSrc);
    // A separate cover is preferable to retrying an unavailable large image.
    if(fallback&&fallback!==current&&!record.tried.has(fallback)) {
      record.tried.add(fallback);image.src=fallback;record.src=image.getAttribute('src');arm(image,record);return;
    }
    if(!record.retried&&current?.startsWith('https://static.lumedesktop.com/')){
      record.retried=true;
      const url=new URL(current);url.searchParams.set('lume_retry','1');
      image.src=url.href;record.src=image.getAttribute('src');arm(image,record);return;
    }
    record.done=true;image.hidden=true;
    // Abort hung transfers rather than occupying connections indefinitely.
    image.removeAttribute('src');record.src=null;
    const hint=placeholder(image);if(hint)hint.hidden=false;
  }
  function loaded(event){
    const image=event.target,record=records.get(image);
    if(!record||!image.naturalWidth)return;
    clear(record);record.done=true;image.hidden=false;
    rememberWallpaperAssetSuccess(image.dataset.wallpaperAssetSource,image.currentSrc||image.src);
    const hint=placeholder(image);if(hint)hint.hidden=true;
  }
  function errored(event){if(event.target.matches?.(selector))fail(event.target);}
  const intersection=typeof view.IntersectionObserver==='function'?new view.IntersectionObserver(entries=>{
    for(const entry of entries){const record=records.get(entry.target);if(!record)continue;
      record.visible=entry.isIntersecting;if(record.visible)arm(entry.target,record);else clear(record);}
  },{rootMargin:'160px'}):null;
  function scan(){
    for(const [image,record] of records)if(!root.contains(image)){
      clear(record);intersection?.unobserve(image);records.delete(image);
    }
    for(const image of root.querySelectorAll(selector)){
      const src=image.getAttribute('src');let record=records.get(image);
      if(record?.src===src)continue;
      if(record)clear(record);
      record={src,tried:new Set(src?[src]:[]),retried:false,done:!src,visible:!intersection,timer:0};
      records.set(image,record);intersection?.observe(image);
      if(image.complete&&image.naturalWidth)loaded({target:image});else arm(image,record);
    }
  }
  const observer=new view.MutationObserver(scan);
  observer.observe(root,{childList:true,subtree:true,attributes:true,attributeFilter:['src']});
  root.addEventListener('load',loaded,true);root.addEventListener('error',errored,true);scan();
  const dispose=()=>{observer.disconnect();intersection?.disconnect();for(const record of records.values())clear(record);
    records.clear();root.removeEventListener('load',loaded,true);root.removeEventListener('error',errored,true);};
  dispose.setVisible=value=>{visible=value;for(const [image,record] of records)arm(image,record);};
  return dispose;
}
