import {MUSIC_ICONS} from './music-icons.js';
import {validMusicBackgroundCatalogMessage} from './music-background.js';

// Original Lucide v1.37.0 paths used by GlobalSettingsTab/LumeWallpaperGallery.
// ISC license: assets/music/lucide-LICENSE.txt; no React runtime.
const icon=body=>`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${body}</svg>`;
const image=icon('<rect width="18" height="18" x="3" y="3" rx="2" ry="2"/><circle cx="9" cy="9" r="2"/><path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"/>');
const film=icon('<rect width="18" height="18" x="3" y="3" rx="2"/><path d="M7 3v18M3 7.5h4M3 12h18M3 16.5h4M17 3v18M17 7.5h4M17 16.5h4"/>');
const check=icon('<path d="M20 6 9 17l-5-5"/>');
export const MUSIC_BACKGROUND_SOURCE_ICONS=Object.freeze({
  folder:icon('<path d="m6 14 1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2"/>'),
  local:icon('<path d="m22 11-1.296-1.296a2.4 2.4 0 0 0-3.408 0L11 16M4 8a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2"/><circle cx="13" cy="7" r="1" fill="currentColor"/><rect x="8" y="2" width="14" height="14" rx="2"/>'),
});
const TEXT={
  title:'\u9009\u62e9 Lume \u58c1\u7eb8',refresh:'\u5237\u65b0\u58c1\u7eb8\u5217\u8868',close:'\u5173\u95ed\u58c1\u7eb8\u9009\u62e9',
  loading:'\u6b63\u5728\u8bfb\u53d6\u58c1\u7eb8\u2026',applying:'\u6b63\u5728\u5e94\u7528\u58c1\u7eb8',
  empty:'\u8fd8\u6ca1\u6709\u672c\u5730\u58c1\u7eb8',loadError:'\u6682\u65f6\u65e0\u6cd5\u8bfb\u53d6 Lume \u58c1\u7eb8\uff0c\u8bf7\u91cd\u8bd5\u3002',
  selectError:'\u8fd9\u5f20\u58c1\u7eb8\u6682\u65f6\u65e0\u6cd5\u4f7f\u7528\uff0c\u53ef\u80fd\u5df2\u88ab\u79fb\u9664\u3002\u8bf7\u5237\u65b0\u5217\u8868\u540e\u91cd\u8bd5\u3002',
  more:'\u52a0\u8f7d\u66f4\u591a',use:'\u4f7f\u7528\u58c1\u7eb8\uff1a',video:'\u52a8\u6001',image:'\u9759\u6001',
};

// query({offset,limit,signal}) -> catalog DTO; onSelect(id) -> Promise<boolean>.
// This owner never imports playback, changes page markup, or sends wallpaper commands.
export function createMusicWallpaperGallery({document=globalThis.document,query,onSelect,onClose}={}) {
  const window=document.defaultView;
  let dialog=null,opener=null,abort=null,epoch=0,disposed=false,items=[],total=0,selectedId='';
  let loading=false,pendingId='',error='',nextOffset=0;
  const $=selector=>dialog?.querySelector(selector);
  function close() {
    if(!dialog)return;
    ++epoch;abort?.abort();abort=null;
    window.removeEventListener('keydown',key,true);
    dialog.remove();dialog=null;items=[];pendingId='';loading=false;
    if(opener?.isConnected)opener.focus({preventScroll:true});
    opener=null;onClose?.();
  }
  function key(event) {
    if(!dialog)return;
    if(event.key==='Escape'){event.preventDefault();event.stopImmediatePropagation();close();return;}
    if(event.key!=='Tab')return;
    const controls=[...dialog.querySelectorAll('button:not(:disabled)')].filter(node=>!node.hidden);
    const first=controls[0],last=controls.at(-1),active=document.activeElement;
    if(event.shiftKey && (active===first || active===dialog || !dialog.contains(active))) {
      event.preventDefault();last?.focus();
    } else if(!event.shiftKey && (active===last || active===dialog || !dialog.contains(active))) {
      event.preventDefault();first?.focus();
    }
  }
  function render() {
    if(!dialog)return;
    const focused=document.activeElement?.dataset.wallpaperId;
    $('[data-gallery-refresh]').disabled=loading || !!pendingId;
    $('[data-gallery-error]').hidden=!error;
    $('[data-gallery-error]').textContent=error;
    const content=$('[data-gallery-content]');
    content.replaceChildren();
    if(!items.length) {
      const status=document.createElement('div');
      status.className='MusicWallpaperGalleryEmpty';
      status.setAttribute('role','status');
      status.innerHTML=loading?MUSIC_ICONS.loader:image;
      const label=document.createElement('p');label.textContent=loading?TEXT.loading:error?TEXT.loadError:TEXT.empty;
      status.append(label);content.append(status);
    } else {
      const grid=document.createElement('div');grid.className='MusicWallpaperGalleryGrid';
      for(const item of items) {
        const button=document.createElement('button');
        button.type='button';button.className='MusicWallpaperGalleryItem';
        button.dataset.wallpaperId=item.id;button.disabled=loading || !!pendingId;
        button.setAttribute('aria-label',TEXT.use+item.name);
        button.setAttribute('aria-pressed',String(selectedId===item.id));
        button.innerHTML=`<span class="MusicWallpaperGalleryPreview"><span class="MusicWallpaperGalleryFallback">${image}</span>
          <span class="MusicWallpaperGalleryTag">${item.type==='video'?film:image}<span>${TEXT[item.type]}</span></span>
          ${selectedId===item.id?`<span class="MusicWallpaperGallerySelected">${check}</span>`:''}
          ${pendingId===item.id?`<span class="MusicWallpaperGalleryPending" role="status" aria-label="${TEXT.applying}">${MUSIC_ICONS.loader}</span>`:''}</span>
          <span class="MusicWallpaperGalleryName"></span>`;
        if(item.thumbnailUrl) {
          const thumbnail=document.createElement('img');
          thumbnail.src=item.thumbnailUrl;thumbnail.alt=item.name;thumbnail.loading='lazy';thumbnail.decoding='async';
          thumbnail.addEventListener('error',()=>{thumbnail.hidden=true;},{once:true});
          button.querySelector('.MusicWallpaperGalleryPreview').prepend(thumbnail);
        }
        const label=button.querySelector('.MusicWallpaperGalleryName');label.textContent=item.name;label.title=item.name;
        button.addEventListener('click',()=>void choose(item.id));grid.append(button);
      }
      content.append(grid);
    }
    const more=$('[data-gallery-more]');
    more.hidden=nextOffset>=total;more.disabled=loading || !!pendingId;
    more.textContent=loading?TEXT.loading:TEXT.more;
    if(focused) {
      const previous=[...dialog.querySelectorAll('[data-wallpaper-id]')].find(node=>node.dataset.wallpaperId===focused);
      if(previous && !previous.disabled)previous.focus({preventScroll:true});
      else $('[data-gallery-close]').focus({preventScroll:true});
    }
  }
  async function load(append=false) {
    if(!dialog || pendingId)return;
    abort?.abort();abort=new window.AbortController();
    const current=++epoch,offset=append?nextOffset:0;
    if(!append){items=[];total=0;nextOffset=0;}
    loading=true;error='';render();
    try {
      const result=await query({offset,limit:120,signal:abort.signal});
      if(!dialog || current!==epoch)return;
      if(!validMusicBackgroundCatalogMessage(result) || result.status!=='ready' ||
        result.items.length>Math.max(0,result.total-offset) ||
        !result.items.length && offset<result.total)throw Error('invalid');
      if(append && result.items.some(item=>items.some(previous=>previous.id===item.id)))throw Error('invalid');
      items=append?[...items,...result.items]:result.items;
      total=result.total;selectedId=result.selectedId;nextOffset=offset+result.items.length;
    } catch {
      if(!dialog || current!==epoch)return;
      error=TEXT.loadError;
    } finally {
      if(dialog && current===epoch){loading=false;render();}
    }
  }
  async function choose(id) {
    if(!dialog || loading || pendingId)return;
    const current=epoch;pendingId=id;error='';render();
    try {
      const ok=await onSelect?.(id);
      if(!dialog || current!==epoch)return;
      if(ok===true){close();return;}
      error=TEXT.selectError;
    } catch {if(dialog && current===epoch)error=TEXT.selectError;}
    finally {if(dialog && current===epoch){pendingId='';render();}}
  }
  function open() {
    if(disposed || dialog || document.hidden)return false;
    opener=document.activeElement;
    dialog=document.createElement('dialog');
    dialog.className='MusicWallpaperGallery music-frosted-surface';
    dialog.setAttribute('aria-modal','true');dialog.setAttribute('aria-label',TEXT.title);dialog.tabIndex=-1;
    dialog.dataset.lumenicWallpaperGallery='true';dialog.dataset.foliaKeyboardWindow='true';
    dialog.innerHTML=`<header><h2>${TEXT.title}</h2><div class="MusicWallpaperGalleryTools">
      <button type="button" data-gallery-refresh aria-label="${TEXT.refresh}" title="${TEXT.refresh}">${MUSIC_ICONS.reset}</button>
      <button type="button" data-gallery-close aria-label="${TEXT.close}" title="${TEXT.close}">${MUSIC_ICONS.close}</button>
      </div></header><p data-gallery-error role="alert" hidden></p>
      <div data-gallery-content class="MusicWallpaperGalleryContent"></div>
      <footer><button type="button" data-gallery-more hidden>${TEXT.more}</button></footer>`;
    document.body.append(dialog);
    $('[data-gallery-close]').addEventListener('click',close);
    $('[data-gallery-refresh]').addEventListener('click',()=>void load());
    $('[data-gallery-more]').addEventListener('click',()=>void load(true));
    dialog.addEventListener('cancel',event=>{event.preventDefault();close();});
    dialog.addEventListener('close',close);
    dialog.addEventListener('mousedown',event=>{
      if(event.target!==dialog)return;
      const rect=dialog.getBoundingClientRect();
      if(event.clientX<rect.left || event.clientX>rect.right || event.clientY<rect.top || event.clientY>rect.bottom)close();
    });
    window.addEventListener('keydown',key,true);
    if(typeof dialog.showModal==='function')dialog.showModal();else dialog.setAttribute('open','');
    dialog.focus({preventScroll:true});void load();return true;
  }
  const hidden=()=>{if(document.hidden)close();};
  document.addEventListener('visibilitychange',hidden);window.addEventListener('pagehide',close);
  return {open,close,refresh:()=>load(),setVisible:value=>{if(!value)close();},
    dispose(){if(disposed)return;close();disposed=true;
      document.removeEventListener('visibilitychange',hidden);window.removeEventListener('pagehide',close);}};
}
