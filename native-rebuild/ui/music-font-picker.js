import {MUSIC_ICONS} from './music-icons.js';
import {DEFAULT_MUSIC_PREFERENCES,validMusicPreferences} from './music-preference-values.js';
import {isMusicImportedFontFamily,validMusicFontLabel} from './music-font-assets.js';

export function musicFontFamilies(fonts) {
  const families=new Map();
  for(const font of fonts) {
    const family=typeof font?.family==='string'?font.family.trim():'';
    if(!family || !validMusicPreferences({...DEFAULT_MUSIC_PREFERENCES,fontFamily:family}))continue;
    const key=family.toLocaleLowerCase();
    if(!families.has(key))families.set(key,family);
  }
  return [...families.values()].sort((a,b)=>a.localeCompare(b));
}

// queryFonts: ({signal:AbortSignal}) => Promise<Array<{family:string}>>; called only on open.
// Omit it to use queryLocalFonts(). Adapters return family names only, never font blobs.
// Rejections show an error; closing and reopening retries. Nonempty results are cached per view.
// Close/hide/dispose abort the signal and discard late results, even if the adapter ignores abort.
// onUpload({signal}) resolves a decoded imported family, or null on native picker cancellation.
// Parent owns candidate import + ensureFont; success uses the existing delegated family click.
// Commit before closing in that click handler so the renderer acquires its own font lease.
// getFontLabel(family) returns loader.labelFor(family). queryFonts may merge listAssets()
// with system families; refresh that merged list on each open when this hook is provided.
export function createMusicFontPicker(root,{queryFonts,onUpload,getFontLabel}={}) {
  const document=root.ownerDocument;
  let dialog=null,opener=null,requestController=null,families=[];
  const labelFor=family=>{
    const label=getFontLabel?.(family);
    return validMusicFontLabel(label)?label:family;
  };

  function close(restoreFocus=true) {
    if(!dialog)return;
    const previous=dialog,trigger=opener;
    dialog=null;opener=null;
    requestController?.abort();requestController=null;
    if(previous.open && typeof previous.close==='function')previous.close();
    previous.remove();
    trigger?.setAttribute('aria-expanded','false');
    if(restoreFocus && trigger?.isConnected)trigger.focus({preventScroll:true});
  }

  function search(query='') {
    if(!dialog || dialog.getAttribute('aria-busy')==='true')return;
    const list=dialog.querySelector('[data-music-font-list]');
    list.replaceChildren();
    const needle=query.trim().toLocaleLowerCase();
    const matches=families.map(family=>({family,label:labelFor(family),imported:isMusicImportedFontFamily(family)}))
      .filter(({family,label})=>label.toLocaleLowerCase().includes(needle)||family.toLocaleLowerCase().includes(needle))
      .sort((a,b)=>Number(b.imported)-Number(a.imported)||a.label.localeCompare(b.label));
    const hasImports=matches.some(font=>font.imported);
    let group;
    for(const {family,label,imported} of matches) {
      if(hasImports && group!==imported) {
        const heading=document.createElement('h3');
        heading.textContent=imported?'已导入字体':'系统字体';
        list.append(heading);group=imported;
      }
      const button=document.createElement('button');
      button.type='button';
      button.dataset.musicFontFamily=family;
      button.textContent=label;
      button.style.fontFamily=JSON.stringify(family);
      button.setAttribute('aria-pressed',String(family===dialog.dataset.currentFont));
      list.append(button);
    }
    const current=dialog.dataset.currentFont;
    dialog.querySelector('[data-music-font-current]').textContent=`当前：${current?labelFor(current):'默认字体'}`;
    const status=dialog.querySelector('[data-music-font-status]');
    if(status.getAttribute('role')!=='alert')
      status.textContent=list.childElementCount?'':families.length?'没有找到匹配字体。':'没有读取到可用字体。';
  }

  async function open(trigger,currentFamily='') {
    if(trigger.disabled)return;
    if(dialog) {
      const same=opener===trigger;
      close(false);
      if(same)return;
    }
    opener=trigger;
    const opened=document.createElement('div');
    dialog=opened;
    requestController=new document.defaultView.AbortController();
    const {signal}=requestController;
    document.addEventListener('pointerdown',event=>{
      if(!opened.contains(event.target) && !trigger.contains(event.target))close(false);
    },{signal});
    if(trigger.dataset.musicFontTrigger==='quick') {
      opened.className='MusicVisualizerMenu MusicFontQuickMenu';
      opened.dataset.musicFontPicker='';
      opened.setAttribute('role','listbox');
      opened.setAttribute('aria-label','常用字体');
      for(const option of root.querySelector('#music-font').options) {
        const button=document.createElement('button');
        button.type='button';button.dataset.musicFontFamily=option.value;
        button.textContent=option.textContent;button.setAttribute('role','option');
        button.setAttribute('aria-selected',String(option.value===currentFamily));
        opened.append(button);
      }
      trigger.parentElement.append(opened);
      trigger.setAttribute('aria-expanded','true');
      opened.addEventListener('keydown',event=>{
        const buttons=[...opened.querySelectorAll('button')];
        const index=buttons.indexOf(document.activeElement);
        if(!['ArrowDown','ArrowUp','Home','End'].includes(event.key))return;
        event.preventDefault();
        buttons[event.key==='Home'?0:event.key==='End'?buttons.length-1:
          (index+(event.key==='ArrowDown'?1:-1)+buttons.length)%buttons.length]?.focus();
      });
      opened.querySelector('[aria-selected="true"]')?.focus({preventScroll:true});
      return;
    }
    opened.className='MusicFontPicker';
    opened.dataset.musicFontPicker='';
    opened.dataset.currentFont=currentFamily;
    opened.setAttribute('aria-label','选择字体');
    opened.setAttribute('role','dialog');
    opened.setAttribute('aria-busy','true');
    // Historical GlobalSettingsTab font picker spacing/type and index.css music-frosted-surface.
    opened.innerHTML=`<style>
      .MusicFontPicker { position: relative; z-index: 20; width: 100%; min-width: 0; padding: 12px; border: 1px solid rgba(255,255,255,.1); border-radius: 12px; background: rgba(0,0,0,.2); color: rgba(255,255,255,.92); backdrop-filter: blur(24px); -webkit-backdrop-filter: blur(24px); box-shadow: 0 20px 25px -5px rgb(0 0 0 / .1), 0 8px 10px -6px rgb(0 0 0 / .1); font: 10px/1.5 ui-sans-serif, system-ui, sans-serif; }
      .MusicFontPicker header { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 12px; }
      .MusicFontPicker header > div { min-width: 0; }
      .MusicFontPicker h2 { font-size: 12px; line-height: 16px; font-weight: 600; }
      .MusicFontPicker .MusicIcon { width: 14px; height: 14px; }
      .MusicFontPicker button { padding: 8px 10px; border: 0; border-radius: 8px; background: transparent; color: inherit; }
      .MusicFontPicker button:hover, .MusicFontPicker button[aria-pressed="true"] { background: rgba(255,255,255,.1); }
      .MusicFontPicker [data-music-font-close] { flex-shrink: 0; padding: 4px; border-radius: 6px; color: rgba(255,255,255,.6); }
      .MusicFontPicker [data-music-font-close]:hover { color: #fff; }
      .MusicFontPicker [data-music-font-current] { margin-top: 4px; color: rgba(255,255,255,.45); font-size: 9px; overflow-wrap: anywhere; }
      .MusicFontPicker label { display: flex; align-items: center; gap: 8px; padding: 8px 10px; margin-bottom: 12px; border: 1px solid rgba(255,255,255,.1); border-radius: 8px; background: rgba(255,255,255,.05); color: rgba(255,255,255,.45); }
      .MusicFontPicker input { flex: 1; width: 100%; min-width: 0; padding: 0; border: 0; background: transparent; color: #fff; font-size: 10px; }
      .MusicFontPicker input::placeholder { color: rgba(255,255,255,.35); }
      .MusicFontPicker [data-music-font-status] { padding: 12px 0; color: rgba(255,255,255,.45); }
      .MusicFontPicker [data-music-font-status]:empty { display: none; }
      .MusicFontPicker[aria-busy="true"] [data-music-font-status] { padding: 20px 0; text-align: center; color: rgba(255,255,255,.55); }
      .MusicFontPicker [role="alert"] { color: #fecaca; }
      .MusicFontPicker [data-music-font-list] { max-height: 160px; overflow-y: auto; padding-right: 4px; }
      .MusicFontPicker [data-music-font-list] h3 { margin: 8px 0 4px; padding: 0 10px; font-size: 9px; font-weight: 500; color: rgba(255,255,255,.45); }
      .MusicFontPicker [data-music-font-family] { display: block; width: 100%; min-height: 31px; font-size: 10px; text-align: left; overflow-wrap: anywhere; }
      .MusicFontPicker .MusicFontPickerActions { display: flex; gap: 8px; margin-top: 12px; }
      .MusicFontPicker [data-music-font-upload] { display: flex; flex: 1; align-items: center; justify-content: center; gap: 8px; min-width: 0; padding: 8px 12px; background: #fff; color: #09090b; font-weight: 600; }
      .MusicFontPicker [data-music-font-upload]:disabled { opacity: .5; cursor: default; }
      .MusicFontPicker [data-music-font-upload-error] { color: #fecaca; padding-top: 8px; }
      .MusicFontPicker [data-music-font-reset] { width: auto; flex-shrink: 0; border: 1px solid rgba(255,255,255,.1); padding: 8px 12px; font-size: 10px; text-align: center; color: rgba(255,255,255,.75); }
    </style>
    <header><div><h2>选择字体</h2><p data-music-font-current></p></div><button type="button" data-music-font-close aria-label="关闭字体选择" title="关闭字体选择">${MUSIC_ICONS.close}</button></header>
    <label>${MUSIC_ICONS.search}<input type="search" data-music-font-search aria-label="搜索字体" placeholder="搜索字体" autocomplete="off" autofocus></label>
    <p data-music-font-status role="status" aria-live="polite">正在读取字体</p>
    <div data-music-font-list></div>
    ${onUpload||currentFamily?`<div class="MusicFontPickerActions">
      ${onUpload?`<button type="button" data-music-font-upload>${MUSIC_ICONS.upload}<span>上传字体</span></button>`:''}
      ${currentFamily?'<button type="button" data-music-font-family="" data-music-font-reset>恢复默认</button>':''}
    </div>`:''}${onUpload?'<p data-music-font-upload-error role="alert" hidden></p>':''}`;
    opened.querySelector('[data-music-font-current]').textContent=`当前：${currentFamily?labelFor(currentFamily):'默认字体'}`;
    (trigger.closest('.MusicFontRow')||trigger).after(opened);
    trigger.setAttribute('aria-expanded','true');
    opened.addEventListener('cancel',event=>{event.preventDefault();close();});
    opened.addEventListener('click',async event=>{
      const button=event.target.closest?.('[data-music-font-upload]');
      if(!button || button.disabled)return;
      const error=opened.querySelector('[data-music-font-upload-error]');
      error.hidden=true;button.disabled=true;
      button.querySelector('span').textContent='上传中';
      try {
        const family=await onUpload({signal});
        if(signal.aborted || dialog!==opened || !opened.isConnected || family===null)return;
        if(!isMusicImportedFontFamily(family) ||
          !validMusicPreferences({...DEFAULT_MUSIC_PREFERENCES,fontFamily:family}))
          throw new Error('Invalid imported family');
        const selection=document.createElement('button');
        selection.type='button';selection.hidden=true;selection.dataset.musicFontFamily=family;
        opened.append(selection);selection.click();selection.remove();
      } catch {
        if(signal.aborted || dialog!==opened || !opened.isConnected)return;
        error.textContent='上传字体失败，请检查字体文件。';error.hidden=false;
      } finally {
        button.disabled=false;button.querySelector('span').textContent='上传字体';
      }
    });
    opened.scrollIntoView({block:'nearest'});
    opened.querySelector('input').focus({preventScroll:true});
    try {
      if(!families.length || getFontLabel) {
        families=[];
        const window=document.defaultView;
        if(!queryFonts && typeof window.queryLocalFonts!=='function') {
          opened.querySelector('[data-music-font-status]').setAttribute('role','alert');
          opened.querySelector('[data-music-font-status]').textContent='当前环境无法读取本机字体。';
          return;
        }
        const fonts=await (queryFonts?queryFonts({signal}):window.queryLocalFonts());
        if(dialog!==opened || !opened.isConnected)return;
        families=musicFontFamilies(fonts);
      }
      opened.setAttribute('aria-busy','false');
      search(opened.querySelector('input').value);
    } catch {
      if(dialog!==opened || !opened.isConnected)return;
      const status=opened.querySelector('[data-music-font-status]');
      status.setAttribute('role','alert');
      status.textContent='读取本机字体失败，请检查权限。';
    } finally {
      opened.setAttribute('aria-busy','false');
    }
  }

  return {open,close,search,contains:target=>!!dialog?.contains(target)};
}
