import {WallpaperCompositor} from './music-background/reference/background.js';
import {validMusicBackgroundLoopbackUrl,validMusicBackgroundThumbnailUrl} from './music-background.js';

const DEFAULT_MEDIA=Object.freeze({id:'default-forest',name:'forest.webp',kind:'image',mimeType:'image/webp',url:new URL('./assets/music/forest.webp',import.meta.url).href});
const FORMATS=Object.freeze({jpg:['image','image/jpeg'],jpeg:['image','image/jpeg'],png:['image','image/png'],webp:['image','image/webp'],gif:['image','image/gif'],mp4:['video','video/mp4'],webm:['video','video/webm']});
const exact=(value,keys)=>value!==null&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).length===keys.length&&keys.every(key=>Object.hasOwn(value,key));
const utf8=value=>{let bytes=0;for(let i=0;i<value.length;i++){const code=value.charCodeAt(i);if(code>=0xd800&&code<=0xdbff){const low=value.charCodeAt(++i);if(low<0xdc00||low>0xdfff)return -1;bytes+=4;continue;}if(code>=0xdc00&&code<=0xdfff)return -1;bytes+=code<=0x7f?1:code<=0x7ff?2:3;}return bytes;};

// Optional image default for embedded callers; the product uses the bundled forest.
export function musicDefaultBackground(url,name='') {
  if(typeof url!=='string'||!url.startsWith('https://')||!validMusicBackgroundThumbnailUrl(url))return null;
  const extension=new URL(url).pathname.split('.').at(-1)?.toLowerCase();
  const format=FORMATS[extension];
  if(!format||format[0]!=='image')return null;
  return {id:'default-home-background',name:typeof name==='string'?name.slice(0,128):'',
    kind:'image',mimeType:format[1],url};
}

export function validateMusicBackgroundMedia(media) {
  if(media===null)return true;
  if(!exact(media,['id','name','kind','mimeType','url'])||typeof media.id!=='string'||!/^[a-z0-9-]{1,80}$/.test(media.id)||
    typeof media.name!=='string'||!media.name||utf8(media.name)<0||utf8(media.name)>512||/[\u0000-\u001f\u007f-\u009f]/.test(media.name)||typeof media.url!=='string')return false;
  return Object.entries(FORMATS).some(([extension,[kind,mimeType]])=>media.kind===kind&&media.mimeType===mimeType&&
    (media.url===`https://lume-music.local/${media.id}.${extension}` ||
      validMusicBackgroundLoopbackUrl(media.url)&&media.url.endsWith(`.${extension}`)));
}

const sameMedia=(a,b)=>a?.id===b?.id&&a?.kind===b?.kind&&a?.url===b?.url&&a?.mimeType===b?.mimeType;
const DEFAULT_TIMEOUT=15000;

export function mountMusicBackground(canvas,{media=null,defaultMedia=null,visible=true,onError,onReady}={}) {
  const document=canvas.ownerDocument??globalThis.document,window=document.defaultView??globalThis;
  let createdMediaSource=null;
  const createMedia=kind=>{
    const source=document.createElement(kind==='video'?'video':'img');
    createdMediaSource=source;
    return source;
  };
  const createCanvas=()=>document.createElement('canvas');
  const compositor=new WallpaperCompositor(canvas,{createMedia,createCanvas,requestFrame:callback=>window.requestAnimationFrame(callback),cancelFrame:id=>window.cancelAnimationFrame(id),now:()=>window.performance.now()});
  let requestedVisible=visible===true,disposed=false,currentMedia=null,currentMediaSource=null,pending=null,watchdog=null;
  let fallback=musicDefaultBackground(defaultMedia?.url,defaultMedia?.name)||DEFAULT_MEDIA;
  let selectedMedia=media,failedDefault=false;
  const defaultSource=()=>failedDefault?DEFAULT_MEDIA:fallback;
  const effectiveVisible=()=>requestedVisible&&!document.hidden;
  const now=()=>window.performance.now();
  const clearWatchdog=()=>{if(watchdog!==null){window.clearTimeout(watchdog);watchdog=null;}};
  const reschedule=()=>{
    if(!pending)return;clearWatchdog();
    const timestamp=now();
    if(pending.startedAt!==null){pending.elapsed+=Math.max(0,timestamp-pending.startedAt);pending.startedAt=effectiveVisible()?timestamp:null;}
    if(effectiveVisible())watchdog=window.setTimeout(()=>{if(pending){compositor.cancelPending();}},Math.max(1,DEFAULT_TIMEOUT-pending.elapsed));
  };
  const applyVisibility=()=>{compositor.setVisible(effectiveVisible());reschedule();};
  const onVisibility=()=>{if(!disposed)applyVisibility();};
  document.addEventListener?.('visibilitychange',onVisibility);
  const resize=()=>{
    const rect=canvas.getBoundingClientRect?.();
    if(!rect)return;
    const ratio=window.devicePixelRatio||1;compositor.resize(rect.width*ratio,rect.height*ratio);
  };
  const resizeObserver=window.ResizeObserver?new window.ResizeObserver(resize):null;resizeObserver?.observe(canvas);resize();
  compositor.setVisible(effectiveVisible());

  function start(nextMedia,forceDefault=false) {
    const resolved=nextMedia===null?defaultSource():nextMedia;
    const isDefault=nextMedia===null;
    const token={resolved,forceDefault,elapsed:0,startedAt:effectiveVisible()?now():null};pending=token;reschedule();
    createdMediaSource=null;
    let operation;
    try {operation=compositor.setMedia({...resolved,skipAnimation:isDefault});}
    catch(error){operation=Promise.reject(error);}
    const mediaSource=createdMediaSource;
    return Promise.resolve(operation).then(ok=>{
      if(disposed||pending!==token)return false;
      pending=null;clearWatchdog();
      if(ok){currentMedia=resolved;currentMediaSource=mediaSource;onReady?.(resolved);return true;}
      if(isDefault&&resolved!==DEFAULT_MEDIA){failedDefault=true;return start(null,true);}
      if(!forceDefault&&!isDefault){
        onError?.(new Error('MUSIC_BACKGROUND_LOAD_FAILED'));
        return currentMedia ? false : start(null,true);
      }
      onError?.(new Error('MUSIC_BACKGROUND_DEFAULT_FAILED'));return false;
    }).catch(error=>{
      if(disposed||pending!==token)return false;
      pending=null;clearWatchdog();
      onError?.(error);
      if(isDefault&&resolved!==DEFAULT_MEDIA){failedDefault=true;return start(null,true);}
      return !forceDefault&&!isDefault && !currentMedia ? start(null,true) : false;
    });
  }
  const initial=start(media);
  return {
    update(next={}) {
      if(disposed)return Promise.resolve(false);
      if(Object.hasOwn(next,'visible')){requestedVisible=next.visible===true;applyVisibility();}
      const hasMedia=Object.hasOwn(next,'media'),hasDefault=Object.hasOwn(next,'defaultMedia');
      if(hasMedia&&next.media!==null&&!validateMusicBackgroundMedia(next.media)){const error=new TypeError('Invalid music background media');onError?.(error);return Promise.resolve(false);}
      if(hasDefault){
        const updated=musicDefaultBackground(next.defaultMedia?.url,next.defaultMedia?.name)||DEFAULT_MEDIA;
        if(!sameMedia(updated,fallback)){fallback=updated;failedDefault=false;}
      }
      if(hasMedia)selectedMedia=next.media;
      if(!hasMedia&&(!hasDefault||selectedMedia!==null))return Promise.resolve(true);
      const resolved=selectedMedia===null?defaultSource():selectedMedia;
      if(currentMedia&&sameMedia(currentMedia,resolved)){
        if(pending){pending=null;clearWatchdog();compositor.cancelPending();}
        applyVisibility();return Promise.resolve(true);
      }
      if(pending&&sameMedia(pending.resolved,resolved))return Promise.resolve(true);
      return start(selectedMedia);
    },
    dispose(){if(disposed)return;disposed=true;clearWatchdog();resizeObserver?.disconnect();document.removeEventListener?.('visibilitychange',onVisibility);compositor.dispose();currentMedia=null;currentMediaSource=null;pending=null;},
    getScene(){return currentMedia?{canvas,media:currentMedia,mediaSource:currentMediaSource}:null;},
    ready:initial,
  };
}
