import {MUSIC_ICONS} from './music-icons.js';

// Caller supplies owned playlists and revalidates account/track ownership in onSelect.
// onSelect(id) may return false (rejected) or a Promise; only accepted results close.
// Close on native hide, navigation, account change and dispose via close(false).
export function openMusicPlaylistSelection({anchor,songName='',playlists=[],onSelect,onCreate,onClose}={}) {
  if(!anchor?.isConnected)return {close(){}};
  const document=anchor.ownerDocument,window=document.defaultView;
  const abort=new window.AbortController(),{signal}=abort;
  let closed=false,pending=false;
  const menu=document.createElement('div');
  menu.className='MusicPlaylistSelection';
  menu.dataset.playlistSelectionDropdown='true';
  menu.setAttribute('role','menu');
  menu.setAttribute('aria-label','添加到歌单');
  menu.innerHTML=`<header><div><h2>添加到歌单</h2><p data-playlist-song></p></div>
    <button type="button" data-playlist-close aria-label="关闭添加到歌单菜单" title="关闭添加到歌单菜单">${MUSIC_ICONS.close}</button></header>
    <div class="MusicPlaylistSelectionList"></div><p data-playlist-error role="alert" hidden></p>`;
  const song=menu.querySelector('[data-playlist-song]');
  song.textContent=songName;song.hidden=!songName;song.title=songName;
  const list=menu.querySelector('.MusicPlaylistSelectionList');
  const error=menu.querySelector('[data-playlist-error]');
  const originalExpanded=anchor.getAttribute('aria-expanded');
  anchor.setAttribute('aria-expanded','true');

  function close(restoreFocus=true) {
    if(closed)return;
    closed=true;abort.abort();menu.remove();
    if(originalExpanded===null)anchor.removeAttribute('aria-expanded');
    else anchor.setAttribute('aria-expanded',originalExpanded);
    if(restoreFocus && anchor.isConnected)anchor.focus({preventScroll:true});
    onClose?.();
  }
  function position() {
    if(closed)return;
    if(!anchor.isConnected){close(false);return;}
    const rect=anchor.getBoundingClientRect();
    const width=menu.getBoundingClientRect().width,height=menu.getBoundingClientRect().height;
    const left=Math.min(Math.max(8,rect.right+16-width),Math.max(8,window.innerWidth-width-8));
    const below=rect.bottom+8;
    menu.style.left=`${left}px`;
    const top=below+height<=window.innerHeight-8?below:rect.top-height-8;
    menu.style.top=`${Math.max(8,Math.min(top,window.innerHeight-height-8))}px`;
  }
  if(onCreate) {
    const create=document.createElement('button');
    create.type='button';create.dataset.playlistCreate='';create.setAttribute('role','menuitem');
    create.innerHTML=`${MUSIC_ICONS.plus}<span>新建歌单</span>`;
    create.addEventListener('click',()=>{if(!closed&&!pending){close();onCreate();}},{signal});
    list.append(create);
  }
  for(const playlist of playlists) {
    const button=document.createElement('button');
    button.type='button';button.setAttribute('role','menuitem');
    button.dataset.playlistId=String(playlist.id);
    button.innerHTML=`<span><span data-playlist-name></span><small></small></span>${MUSIC_ICONS.plus}`;
    const name=button.querySelector('[data-playlist-name]'),description=button.querySelector('small');
    name.textContent=playlist.name;name.title=playlist.name;
    description.textContent=playlist.description||'';description.hidden=!playlist.description;
    button.addEventListener('click',async()=>{
      if(closed||pending)return;
      pending=true;error.hidden=true;menu.setAttribute('aria-busy','true');
      for(const item of list.querySelectorAll('button'))item.disabled=true;
      try {
        if(typeof onSelect!=='function' || await onSelect(playlist.id)===false)throw new Error('Rejected');
        if(!closed)close();
      } catch {
        if(!closed){error.textContent='添加失败，请重试。';error.hidden=false;}
      } finally {
        if(!closed) {
          pending=false;menu.setAttribute('aria-busy','false');
          for(const item of list.querySelectorAll('button'))item.disabled=false;
          position();
        }
      }
    },{signal});
    list.append(button);
  }
  if(!playlists.length) {
    const empty=document.createElement('p');empty.className='MusicPlaylistSelectionEmpty';
    empty.textContent='暂无可添加的歌单';list.append(empty);
  }
  menu.querySelector('[data-playlist-close]').addEventListener('click',()=>close(),{signal});
  document.addEventListener('pointerdown',event=>{
    if(!menu.contains(event.target)&&!anchor.contains(event.target))close(false);
  },{signal,capture:true});
  document.addEventListener('keydown',event=>{
    if(event.key==='Escape') {
      event.preventDefault();event.stopImmediatePropagation();close();return;
    }
    if(!menu.contains(event.target))return;
    if(event.key==='Tab'){close();return;}
    if(!['ArrowDown','ArrowUp','Home','End'].includes(event.key))return;
    event.preventDefault();event.stopPropagation();
    const items=[...list.querySelectorAll('button:not(:disabled)')];
    const index=items.indexOf(document.activeElement);
    items[event.key==='Home'?0:event.key==='End'?items.length-1:
      (index+(event.key==='ArrowDown'?1:-1)+items.length)%items.length]?.focus();
  },{signal,capture:true});
  document.addEventListener('visibilitychange',()=>{if(document.hidden)close(false);},{signal});
  window.addEventListener('pagehide',()=>close(false),{signal});
  window.addEventListener('resize',position,{signal});
  window.addEventListener('scroll',position,{signal,capture:true});
  document.body.append(menu);position();
  (list.querySelector('button')||menu.querySelector('[data-playlist-close]')).focus({preventScroll:true});
  return {close};
}
