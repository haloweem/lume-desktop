import {initialMusicState, normalizeMusicState, musicCommand, musicPage, updateMusicPlayback} from "./music.js";
import {DEFAULT_MUSIC_VISUALIZER, mountMusicVisualizer, resolveMusicVisualizer} from "./music-visualizer.js";
import {musicCollectionKey} from './music-collections.js';
import {DEFAULT_MUSIC_PREFERENCES,SONNET_COLOR_KEYS,sameMusicPreferences} from './music-preference-values.js';
import {DEFAULT_MUSIC_BACKGROUND,closeMusicVisualizerPicker,setMusicVisualizerPickerOpen,setMusicEffectPickerOpen,setMusicBackgroundPickerOpen,positionMusicBackgroundPicker,updateMusicSettings,updateMusicBackground,createMusicFontPicker} from './music-settings.js';
import {MUSIC_PANEL_TABS,updateMusicPlayerPanel} from './music-player-panel.js';
import {mountMusicBackground} from './music-background-view.js';
import {mountMusicEffects} from './music-effects.js';
import {copySongInfo} from './music-cover-actions.js';
import {mountMusicProviderAccountView} from './music-provider-account.js';
import {createMusicWallpaperGallery} from './music-wallpaper-gallery.js';
import {openMusicPlaylistSelection} from './music-playlist-selection.js';
import {musicTrackLiked,canLikeMusicTrack} from './music-online.js';

const NAVS = ["recommendations", "albums", "favorites", "recent", "local", "playlists"];
const SCROLLERS = "[data-music-home-content], [data-music-home-sidebar], [data-music-scroll], .MusicContent, .MusicMain, .MusicPanelBody, .MusicPlayerPanel";
const FOCUS_KEYS = ["id", "data-music-search", "data-music-list-search", "data-music-progress", "data-music-rail-search", "data-music-rail-tab", "data-music-rail-playlist",
  "data-music-action", "data-music-quality-trigger", "data-music-quality-option", "data-music-nav", "data-music-playlist", "data-music-playlist-group", "data-music-online-provider", "data-music-provider-open", "data-music-file-input", "data-music-panel-tab", "data-music-preference", "data-music-setting", "data-music-color-reset", "data-music-sonnet-color", "data-music-sonnet-reset", "data-music-background-action", "data-music-cover-copy", "data-music-font-search", "data-music-font-family", "data-music-font-trigger"];
const UNAVAILABLE = "\u97f3\u4e50\u64cd\u4f5c\u6682\u4e0d\u53ef\u7528";
const IMPORT_FAILED = "\u65e0\u6cd5\u5bfc\u5165\u97f3\u4e50\u6587\u4ef6";
const IMPORT_LIMIT = "\u4e00\u6b21\u6700\u591a\u5bfc\u5165 32 \u4e2a\u97f3\u4e50\u6587\u4ef6";
const loadMaterial=async(surface,options)=>{
  const {mountMusicMaterial}=await import('./music-material.js');
  if(!surface.isConnected || !options.isCurrent())return null;
  return mountMusicMaterial(surface,options);
};

const initialView = () => ({nav:"recommendations", screen:"home", panel:"", query:"", search:"",
  listQuery:"", listSearchOpen:false, playlistGroup:"owned",visualizer:DEFAULT_MUSIC_VISUALIZER,panelTab:'globalSettings'});
function restoreViewState(saved, scrollPositions) {
  const view=initialView();
  if(!saved || typeof saved!=="object")return view;
  if(NAVS.includes(saved.nav))view.nav=saved.nav;
  if(saved.screen==="player")view.screen="player";
  if(saved.screen==='desktop'){view.screen='desktop';view.desktopTab=saved.desktopTab==='details'?'details':'settings';}
  if(["","queue","lyrics"].includes(saved.panel))view.panel=saved.panel;
  if(MUSIC_PANEL_TABS.includes(saved.panelTab))view.panelTab=saved.panelTab;
  for(const key of ["query","search","listQuery"])
    if(typeof saved[key]==="string")view[key]=saved[key].slice(0,512);
  if(typeof saved.playlistId==="string")view.playlistId=saved.playlistId.slice(0,512);
  for(const key of ['railQuery','railPlaylist'])
    if(typeof saved[key]==='string')view[key]=saved[key].slice(0,512);
  if(['queue','playlists'].includes(saved.railTab))view.railTab=saved.railTab;
  if(typeof saved.railPinned==='boolean')view.railPinned=saved.railPinned;
  if(typeof saved.immersive==='boolean')view.immersive=saved.immersive;
  if(typeof saved.listSearchOpen==="boolean")view.listSearchOpen=saved.listSearchOpen;
  if(["owned","subscribed"].includes(saved.playlistGroup))view.playlistGroup=saved.playlistGroup;
  view.visualizer=resolveMusicVisualizer(saved.visualizer);
  if(Array.isArray(saved.scrollPositions))for(const entry of saved.scrollPositions.slice(0,120)) {
    if(Array.isArray(entry) && typeof entry[0]==="string" && entry[0].length<=512 &&
      Number.isFinite(entry[1]) && entry[1]>=0 && Number.isFinite(entry[2]) && entry[2]>=0)
      scrollPositions.set(entry[0],[entry[1],entry[2]]);
  }
  return view;
}
const trackKey = state => JSON.stringify([state.current?.id, state.current?.source]);
const sameTrack = (a, b) => a === b || !!a && !!b &&
  ['id','source','title','artist','album','artwork','duration','vipRequired','membershipBadge'].every(key => a[key] === b[key]);
// Compare bounded fields without copying the queue's cover data into a JSON string on every tick.
const sameStructure = (a, b) => a.session === b.session && sameTrack(a.current,b.current) &&
  a.queue.length === b.queue.length && a.queue.every((track,index) => sameTrack(track,b.queue[index])) &&
  ['canPrevious','canNext','canToggle','canSeek'].every(key => a.capabilities[key] === b.capabilities[key]) &&
  a.error === b.error && a.repeat === b.repeat &&
  a.shuffle === b.shuffle &&
  ['requestedQuality','quality','bitrate','switching'].every(key=>a.audio?.[key]===b.audio?.[key]);

function setQualityMenuOpen(surface, open) { setPlayerMenuOpen(surface,open,'quality'); }
function setVolumeMenuOpen(surface, open) { setPlayerMenuOpen(surface,open,'volume'); }
function setPlayerMenuOpen(surface, open, kind) {
  const trigger=surface?.querySelector(`[data-music-${kind}-trigger]`);
  const menu=surface?.querySelector(`[data-music-${kind}-menu]`);
  const dismiss=surface?.querySelector(`[data-music-${kind}-dismiss]`);
  if(!trigger||!menu){dismiss?.remove();return;}
  const shown=!!open&&!trigger.disabled;
  menu.hidden=!shown;
  trigger.setAttribute('aria-expanded',String(shown));
  if(!shown){dismiss?.remove();menu.style.bottom='';menu.style.maxHeight='';menu.style.transform='';return;}
  const bar=menu.closest('.MusicPlayerBar'),picker=menu.closest(`[data-music-${kind}-picker]`);
  const playback=bar?.closest('.MusicPlayback');
  if(bar&&picker){
    const top=bar.getBoundingClientRect().top;
    menu.style.bottom=`${Math.ceil(picker.getBoundingClientRect().bottom-top+8)}px`;
    menu.style.maxHeight=`${Math.max(48,Math.floor(top-16))}px`;
  }
  menu.style.transform='';
  const rect=menu.getBoundingClientRect(),width=surface.ownerDocument.defaultView.innerWidth;
  menu.style.transform=`translateX(${Math.max(12-rect.left,Math.min(0,width-12-rect.right))}px)`;
  if(playback&&!dismiss){
    const cover=surface.ownerDocument.createElement('div');
    cover.setAttribute(`data-music-${kind}-dismiss`,'');cover.className='MusicQualityDismiss';
    cover.setAttribute('aria-hidden','true');
    playback.insertBefore(cover,bar);
  }
}

// Owns only the mounted music subtree. The caller retains native lifecycle,
// import transport and account ownership; renderer functions own markup.
export function mountMusicView(root, {snapshot = initialMusicState(), viewState, send, onImport, onNotice,
  library = {}, createVisualizer = mountMusicVisualizer, preferences, onPreferencePreview, onPreferenceCommit,
  background=DEFAULT_MUSIC_BACKGROUND,onBackgroundAction,createBackground=mountMusicBackground,
  createEffects=mountMusicEffects,providerAccount,queryFonts,createMaterial=loadMaterial,
  ensureFont,onFontUpload,getFontLabel,queryBackgrounds,onBackgroundSelect,online,localLibrary,desktopControls,onDesktopBack,
  onExitFullscreen,onImmersiveChange,defaultBackground} = {}) {
  let state = normalizeMusicState(snapshot), view;
  let disposed = false, surface = null;
  let seek = null, cancelledSeek = false, keySeek = null, pressed = null;
  let visualizer = null;
  let accountView = null;
  let playlistDialog=null;
  let playlistSelection=null;
  let effects = null;
  let material=null,materialSurface=null,materialTicket=0,materialPending=false,materialFailed=false;
  let copyOperation = null;
  let backgroundRenderer=null,backgroundCanvas=null;
  let nativeVisible = true;
  let immersiveArmed=false,immersiveRevealed=false,reportedImmersive=false;
  // Everything that brings the hidden controls up in immersive mode, and keeps them up.
  const IMMERSIVE_REVEAL='[data-music-immersive-zone], .MusicPlayerBar, [data-music-settings-toggle], .MusicPlayerPanel';
  let composing = false, pendingRender = false;
  const scrollPositions = new Map();
  view=restoreViewState(viewState,scrollPositions);
  let settings=preferences || {preferences:DEFAULT_MUSIC_PREFERENCES,loaded:false,pending:false};
  if(settings.loaded)view.visualizer=resolveMusicVisualizer(settings.preferences.mode);
  else if(view.visualizer!==DEFAULT_MUSIC_VISUALIZER)
    settings={...settings,preferences:{...settings.preferences,mode:view.visualizer}};
  const document = root.ownerDocument;
  const pageLibrary=()=>localLibrary?{...library,localLimit:localLibrary.state.limit}:library;
  const pageOptions=()=>({view,library:pageLibrary(),preferences:settings,background,desktopControls:desktopControls?.state});
  // Optional native adapter: queryFonts({signal}) resolves [{family:string}], like queryLocalFonts().
  const fontPicker=createMusicFontPicker(root,{queryFonts,onUpload:onFontUpload,getFontLabel});
  const wallpaperGallery=createMusicWallpaperGallery({document,query:queryBackgrounds,onSelect:onBackgroundSelect});
  const owned = element => !!element && !!surface && root.contains(surface) &&
    surface.contains(element) && element.closest?.("[data-music-root]") === surface;
  const targetOf = event => event.target?.nodeType === 1 ? event.target : event.target?.parentElement;
  const closeVisualizerPickerOutside = event => {
    const target=targetOf(event);
    if(!target?.closest?.('[data-music-visualizer-picker]'))closeMusicVisualizerPicker(surface);
    if(!target?.closest?.('[data-music-effect-picker]'))setMusicEffectPickerOpen(surface,false);
    if(!target?.closest?.('[data-music-background-picker]'))setMusicBackgroundPickerOpen(surface,false);
    if(!target?.closest?.('[data-music-quality-picker]'))setQualityMenuOpen(surface,false);
    if(!target?.closest?.('[data-music-volume-picker]'))setVolumeMenuOpen(surface,false);
  };
  const positionFloatingMenus=()=>{
    positionMusicBackgroundPicker(surface);
    if(surface?.querySelector('[data-music-quality-menu]:not([hidden])'))setQualityMenuOpen(surface,true);
    if(surface?.querySelector('[data-music-volume-menu]:not([hidden])'))setVolumeMenuOpen(surface,true);
  };
  const notice = message => { if (!disposed) onNotice?.(message); };
  function clearMaterial() {
    ++materialTicket;materialPending=false;materialFailed=false;
    material?.dispose();material=null;materialSurface=null;
  }
  function updateMaterial() {
    const footer=surface?.querySelector('.MusicPlayerBar');
    if(materialSurface!==footer)clearMaterial();
    if(!footer)return;
    materialSurface=footer;
    // Keep the compositor alive while immersive playback is armed. The bar is
    // visually hidden, but retaining its glass canvas lets it reappear on the
    // first frame instead of flashing the CSS fallback while WebGL remounts.
    const visible=state.pageVisible && nativeVisible && !document.hidden;
    if(material){material.update({visible});return;}
    if(!visible || materialPending || materialFailed ||
      createMaterial===loadMaterial && !document.defaultView.ResizeObserver)return;
    const ticket=++materialTicket;
    materialPending=true;
    const isCurrent=()=>!disposed && ticket===materialTicket &&
      footer===materialSurface && state.pageVisible && nativeVisible && !document.hidden;
    Promise.resolve().then(()=>createMaterial(footer,{
      root:surface,getScene:()=>backgroundRenderer?.getScene?.(),visible,isCurrent,
    })).then(owner=>{
      if(!isCurrent()){owner?.dispose();return;}
      material=owner;
    }).catch(()=>{
      if(ticket===materialTicket)materialFailed=true;
      // The original CSS fallback remains visible when WebGL is unavailable.
    })
      .finally(()=>{if(ticket===materialTicket)materialPending=false;});
  }
  const identity = element => ({session:element?.closest("[data-music-session]")
    ? Number(element.closest("[data-music-session]").dataset.musicSession) : state.session,
    track:trackKey(state)});
  const isCurrent = operation => operation && operation.session === state.session && operation.track === trackKey(state);
  const canSeek = () => !!state.current && state.capabilities.canSeek && state.timeline.duration > 0;
  const coverActive = () => !disposed && !document.hidden && nativeVisible && state.pageVisible &&
    view.screen==='player' && view.panel==='lyrics' && view.panelTab==='cover';

  async function copyCover() {
    if (!coverActive() || copyOperation || !state.current) return;
    const started={...identity(),controller:new AbortController()};
    copyOperation=started;
    try {
      const copied=await copySongInfo(document,state.current,{signal:started.controller.signal});
      if (!started.controller.signal.aborted && isCurrent(started))
        notice(copied?'已复制歌曲信息':'复制歌曲信息失败');
    } finally { if(copyOperation===started)copyOperation=null; }
  }

  function cancelCopy() {
    copyOperation?.controller.abort();
    copyOperation=null;
  }

  function cancelInactiveCopy() {
    if(copyOperation && !coverActive())cancelCopy();
  }

  function documentVisibility() {
    cancelInactiveCopy();
    if(document.hidden){closePlaylistDialog();setMusicBackgroundPickerOpen(surface,false);}
  }

  function command(action, fields = {}, started = identity()) {
    if (disposed || !isCurrent(started) || !Number.isSafeInteger(started.session) || started.session <= 0) return false;
    const parsed = musicCommand(action, {...fields, session:started.session}, state);
    if (!parsed) return false;
    const {version, scope, action:wireAction, ...payload} = parsed;
    try {
      if (send?.(wireAction, {...payload, session:started.session}) === true) return true;
    } catch { /* Transport failure leaves the authoritative snapshot unchanged. */ }
    notice(UNAVAILABLE);
    return false;
  }

  function scrollKey(element) {
    const parts = [];
    for (let node = element; node && node !== surface; node = node.parentElement) {
      parts.unshift(Array.prototype.indexOf.call(node.parentElement.children, node));
    }
    const panel = element.closest("[data-music-panel]");
    return `${view.nav}:${view.playlistId || ""}:${view.screen}:${view.playlistGroup}:${panel?.dataset.musicPanel || ""}:${parts.join(".")}`;
  }

  function saveScroll() {
    if (!surface) return;
    for (const element of [surface, ...surface.querySelectorAll("*")]) {
      if (element.matches(SCROLLERS) || element.scrollTop || element.scrollLeft) {
        scrollPositions.set(scrollKey(element), [element.scrollTop, element.scrollLeft]);
      }
    }
  }

  function saveFocus() {
    const element = document.activeElement;
    if (!owned(element)) return null;
    const key = FOCUS_KEYS.find(attribute => element.hasAttribute(attribute));
    if (!key) return null;
    const attributes = [key, "data-music-index"].filter(attribute => element.hasAttribute(attribute))
      .map(attribute => [attribute, element.getAttribute(attribute)]);
    return {attributes, start:element.selectionStart, end:element.selectionEnd,
      direction:element.selectionDirection};
  }

  function restoreFocus(focus) {
    if (!focus) return false;
    const element = [...surface.querySelectorAll(`[${focus.attributes[0][0]}]`)].find(candidate =>
      focus.attributes.every(([key, value]) => candidate.getAttribute(key) === value));
    if (!element) return false;
    element.focus({preventScroll:true});
    if (typeof focus.start === "number" && typeof element.setSelectionRange === "function") {
      element.setSelectionRange(focus.start, focus.end, focus.direction);
    }
    return document.activeElement === element;
  }

  function playback({syncClock=false}={}) {
    cancelInactiveCopy();
    if (!disposed && surface && root.contains(surface)) {
      backgroundRenderer?.update({media:background.media,defaultMedia:defaultBackground,visible:state.pageVisible && nativeVisible});
      effects?.update({mode:settings.preferences.effectMode||'none',visualizer:view.visualizer,
        visible:state.pageVisible && nativeVisible,scene:backgroundRenderer?.getScene?.(),current:state.current});
      updateMusicPlayback(root, state, {seeking:!!seek});
      visualizer?.update({lines:state.lyrics,score:state.score,current:state.current,currentTime:state.timeline.position,
        duration:state.timeline.duration,playing:state.playing,visible:state.pageVisible && nativeVisible,
        analysis:state.analysis,audioEnergy:state.analysis?.energy||0,
        preferences:settings.preferences,backgroundScene:backgroundRenderer?.getScene?.()},view.visualizer,{syncClock});
      updateMaterial();
    }
  }

  function updatePanel() {
    cancelInactiveCopy();
    if(view.panelTab!=='globalSettings')fontPicker.close(false);
    updateMusicPlayerPanel(surface,{state,view,preferences:settings,background,getFontLabel});
    updateLocalArtwork();
    accountView?.refresh();
  }

  function refreshPanels(change={}) {
    setQualityMenuOpen(surface,false);setVolumeMenuOpen(surface,false);
    const focus=saveFocus();
    const scroll=surface.querySelector('[data-music-rail] .MusicPanelBody')?.scrollTop;
    view={...view,...change};
    const next=document.createElement('div');next.innerHTML=musicPage(state,pageOptions());
    renderPanels(next);
    updateLocalArtwork();
    const toggle=surface.querySelector('[data-music-settings-toggle]');
    if(toggle)toggle.hidden=view.panel==='lyrics'||view.screen==='player'&&!!view.immersive&&!immersiveRevealed;
    if(scroll!==undefined&&surface.querySelector('[data-music-rail] .MusicPanelBody'))
      surface.querySelector('[data-music-rail] .MusicPanelBody').scrollTop=scroll;
    restoreFocus(focus);
    cancelInactiveCopy();
    accountView?.refresh();
  }

  function updateImmersive() {
    if(!surface)return;
    const immersive=view.screen==='player'&&!!view.immersive;
    surface.classList.toggle('is-immersive',immersive);
    surface.classList.toggle('is-immersive-revealed',immersive&&immersiveRevealed);
    const bar=surface.querySelector('.MusicPlayerBar');
    if(bar)bar.inert=immersive&&!immersiveRevealed;
    const button=surface.querySelector('[data-music-action="immersive"]');
    if(button) {
      button.title=immersive?'退出沉浸模式':'沉浸模式';
      button.setAttribute('aria-label',button.title);button.setAttribute('aria-pressed',String(immersive));
    }
    // In immersive mode the settings button comes up with the player bar.
    const toggle=surface.querySelector('[data-music-settings-toggle]');
    if(toggle)toggle.hidden=view.panel==='lyrics'||immersive&&!immersiveRevealed;
    if(immersive!==reportedImmersive){reportedImmersive=immersive;onImmersiveChange?.(immersive);}
    updateMaterial();
  }

  function updateLocalArtwork() {
    if(!surface)return;
    // Library artwork leases are revocable; never store them in the native playback snapshot.
    const covers=new Map(normalizeMusicState({queue:library.localTracks||[]}).queue
      .map(track=>[track.id,track.artwork]));
    const update=(cover,id)=>{
      if(!cover || !/^local-\d+$/.test(id||''))return;
      const source=covers.get(id)||'';
      let image=cover.querySelector('[data-music-image]');
      if(!source){image?.remove();return;}
      if(!image) {
        image=document.createElement('img');
        image.dataset.musicImage='';image.alt='';image.decoding='async';image.draggable=false;
        cover.append(image);
      }
      if(image.getAttribute('src')!==source){image.hidden=false;image.src=source;}
    };
    for(const cover of surface.querySelectorAll('.MusicPlayerArtwork, .MusicPanelCover'))
      update(cover,state.current?.id);
    for(const row of surface.querySelectorAll('[data-music-panel="queue"] [data-music-track-id]'))
      update(row.querySelector('.MusicTrackArtwork'),row.dataset.musicTrackId);
  }

  // Keep both the stage browsing context and active native input nodes connected.
  function renderPanels(nextRoot) {
    setQualityMenuOpen(surface,false);setVolumeMenuOpen(surface,false);
    const current=surface.querySelector('[data-music-panel]'),next=nextRoot.querySelector('[data-music-panel]');
    if(current?.matches('[data-music-player-panel]') && next?.matches('[data-music-player-panel]'))updatePanel();
    else if(current && next)current.replaceWith(next);
    else if(current)current.remove();
    else if(next)surface.append(next);
    const dismiss=surface.querySelector('.MusicPanelDismiss'),nextDismiss=nextRoot.querySelector('.MusicPanelDismiss');
    if(dismiss && !nextDismiss)dismiss.remove();
    else if(!dismiss && nextDismiss)surface.append(nextDismiss);
  }

  function renderPlayerChrome() {
    setQualityMenuOpen(surface,false);setVolumeMenuOpen(surface,false);
    const next = document.createElement("div");
    next.innerHTML = musicPage(state, pageOptions());
    const nextRoot = next.querySelector("[data-music-root]");
    const nextPlayback = nextRoot?.querySelector(".MusicPlayback");
    if (!nextRoot || !nextPlayback || !surface) return false;

    const playbackSection = surface.querySelector(".MusicPlayback");
    const currentFooter = playbackSection?.querySelector(".MusicPlayerBar");
    const nextFooter = nextPlayback.querySelector(".MusicPlayerBar");
    if (!playbackSection || !currentFooter || !nextFooter) return false;
    currentFooter.dataset.musicSession=nextFooter.dataset.musicSession;
    // Replacing the range input mid-drag ends the gesture: the new element
    // never receives change and the bar stays frozen. Patch it in place then.
    const currentProgress=currentFooter.querySelector('[data-music-progress]');
    const nextProgress=nextFooter.querySelector('[data-music-progress]');
    if(seek&&currentProgress&&nextProgress){
      currentProgress.max=nextProgress.max;
      currentProgress.disabled=nextProgress.disabled;
    } else currentFooter.querySelector('.MusicProgressLine')?.replaceWith(nextFooter.querySelector('.MusicProgressLine'));
    currentFooter.querySelector('.MusicPlayerLayout')?.replaceWith(nextFooter.querySelector('.MusicPlayerLayout'));

    const status=surface.querySelector('.MusicPlayerStatus');
    renderPanels(nextRoot);
    surface.dataset.musicSession = String(state.session);
    if (status) status.textContent = nextRoot.querySelector('.MusicPlayerStatus')?.textContent || '';
    return true;
  }

  function render(change = {}, {syncClock=false}={}) {
    if (disposed) return;
    if(playlistSelection)closePlaylistDialog();
    if((change.screen!==undefined && change.screen!==view.screen) ||
      (change.panel!==undefined && change.panel!==view.panel))fontPicker.close(false);
    if (composing) {
      view={...view,...change};pendingRender=true;
      cancelInactiveCopy();
      if(syncClock)playback({syncClock});
      return;
    }
    pendingRender=false;
    saveScroll();
    const hadFocus = owned(document.activeElement);
    const focus = saveFocus();
    const seekingValue = seek?.dirty ? surface?.querySelector("[data-music-progress]")?.value : null;
    view = {...view, ...change};
    cancelInactiveCopy();
    const updatedPlayer = view.screen === 'player' && surface?.dataset.screen === 'player' && renderPlayerChrome();
    const updatedHome = view.screen === 'home' && surface?.dataset.screen === 'home' && renderHomeChrome();
    if (!updatedPlayer && !updatedHome) {
      clearMaterial();
      visualizer?.dispose();visualizer=null;
      effects?.dispose();effects=null;
      backgroundCanvas?.remove();
      root.innerHTML = musicPage(state, pageOptions());
      surface = root.querySelector("[data-music-root]");
      if (!surface) throw new Error("musicPage must render a data-music-root subtree");
      const placeholder=surface.querySelector('[data-music-background]');
      if(backgroundCanvas && placeholder)placeholder.replaceWith(backgroundCanvas);
      else if(placeholder) {
        backgroundCanvas=placeholder;
        backgroundCanvas.dataset.musicBackgroundState='loading';
        backgroundCanvas.dataset.musicBackgroundId=background.media?.id||'default-forest';
        backgroundRenderer=createBackground(backgroundCanvas,{media:background.media,defaultMedia:defaultBackground,visible:state.pageVisible && nativeVisible,
          onReady:media=>{backgroundCanvas.dataset.musicBackgroundState='ready';backgroundCanvas.dataset.musicBackgroundId=media.id;playback();},
          onError:()=>{
            backgroundCanvas.dataset.musicBackgroundState='error';
            backgroundCanvas.dataset.musicBackgroundId=backgroundRenderer?.getScene?.()?.media?.id||
              background.media?.id||'default-forest';
            notice('音乐背景加载失败');
          }});
      }
      const stage = surface.querySelector('[data-music-stage]');
      if (stage) {
        visualizer=createVisualizer(stage,{ensureFont,onError:()=>notice('歌词字体或显示资源加载失败')});
        // A lyric style may ask to seek to a tapped line; playback stays owned here.
        stage.addEventListener('lume-lyric-seek',event=>{
          const time=event.detail?.time;
          if(canSeek()&&Number.isFinite(time)&&time>=0)command("seek",{position:Math.min(time,state.timeline.duration)});
        });
        const effectHost=document.createElement('div');
        effectHost.className='MusicEffects';effectHost.dataset.musicEffects='';
        effectHost.setAttribute('aria-hidden','true');
        stage.before(effectHost);
        effects=createEffects(effectHost,{onError:()=>notice('音乐特效暂不可用')});
      }
    }
    for (const element of [surface, ...surface.querySelectorAll("*")]) {
      const position = scrollPositions.get(scrollKey(element));
      if (position) [element.scrollTop, element.scrollLeft] = position;
    }
    if (!restoreFocus(focus) && hadFocus) {
      surface.tabIndex = -1;
      surface.focus({preventScroll:true});
    }
    if (seekingValue !== null) {
      const progress = surface.querySelector("[data-music-progress]");
      if (progress) progress.value = seekingValue;
    }
    updateMusicSettings(surface,settings,{getFontLabel});
    const settingsToggle=surface.querySelector('[data-music-settings-toggle]');
    if(settingsToggle)settingsToggle.hidden=view.panel==='lyrics';
    updateLocalArtwork();
    playback({syncClock});
    updateImmersive();
    accountView?.refresh();
  }

  function renderHomeChrome() {
    const main=surface.querySelector('.MusicMain');
    if(!main)return false;
    const next=document.createElement('div');next.innerHTML=musicPage(state,pageOptions());
    const nextRoot=next.querySelector('[data-music-root]');
    const search=surface.querySelector('[data-music-search]');
    if(search && search.value!==view.query)search.value=view.query;
    const clear=surface.querySelector('[data-music-action="clear-search"]');
    if(clear)clear.hidden=!view.query;
    const oldFilter=surface.querySelector('[data-music-list-search]');
    const newFilter=nextRoot.querySelector('[data-music-list-search]');
    if(oldFilter && newFilter) {
      if(oldFilter.value!==view.listQuery)oldFilter.value=view.listQuery;
      newFilter.replaceWith(oldFilter);
    }
    renderPanels(nextRoot);
    for(const selector of ['.MusicSidebar','.MusicContent','[data-music-open-player]','.MusicPlayerStatus']) {
      const current=surface.querySelector(selector),replacement=nextRoot.querySelector(selector);
      // Account/QR notifications must retain unchanged dialog opener nodes.
      if(current && replacement){if(!current.isEqualNode(replacement))current.replaceWith(replacement);}
      else if(current)current.remove();
      else if(replacement)surface.append(replacement);
    }
    surface.dataset.musicSession=String(state.session);
    return true;
  }

  function focusAction(action) {
    surface.querySelector(`[data-music-action="${action}"]`)?.focus({preventScroll:true});
  }

  function closePanel() {
    const opener=view.panel;
    refreshPanels({panel:"",railPinned:false});
    if(opener)focusAction(opener);
  }

  function closeListSearch() {
    render({listSearchOpen:false,listQuery:""});
    focusAction('list-search');
  }

  function closePlaylistDialog() {
    const selection=playlistSelection;playlistSelection=null;selection?.close(false);
    playlistDialog?.remove();playlistDialog=null;
  }
  function openPlaylistSelection(anchor,trackId) {
    if(!online||!library.signedIn||library.loading||library.busy||document.hidden||
      !nativeVisible||!state.pageVisible||!anchor.isConnected)return;
    closePlaylistDialog();
    const provider=library.providerId;
    const userId=library.accounts?.find(account=>account.provider===provider)?.userId;
    const tracks=()=>[...(library.tracks||[]),...(library.searchTracks||[]),
      ...(library.favorites||[]),...(library.recent||[])];
    const track=tracks().find(track=>track.id===trackId);
    if(!track||!trackId.startsWith(`${provider}:`))return;
    const selection=openMusicPlaylistSelection({
      anchor,songName:track.title,
      playlists:(library.playlists||[]).filter(item=>item.owned&&item.providerId===provider)
        .map(item=>({id:String(item.id),name:item.title,description:item.description})),
      onSelect:id=>{
        if(disposed||playlistSelection!==selection||document.hidden||!nativeVisible||!state.pageVisible||
          !anchor.isConnected||!library.signedIn||library.loading||library.busy||
          library.providerId!==provider||library.accounts?.find(account=>account.provider===provider)?.userId!==userId||
          !tracks().some(track=>track.id===trackId)||!(library.playlists||[]).some(item=>
            item.owned&&item.providerId===provider&&String(item.id)===id))return false;
        return online.request('addToPlaylist',{id,trackId});
      },
      onCreate:()=>openPlaylistDialog('create-playlist'),
      onClose:()=>{if(playlistSelection===selection)playlistSelection=null;},
    });
    playlistSelection=selection;
  }
  function openPlaylistDialog(action,trackId) {
    if(!online||!library.signedIn||library.loading||document.hidden||!nativeVisible)return;
    closePlaylistDialog();
    const dialog=document.createElement('dialog');
    playlistDialog=dialog;
    dialog.className='MusicCollectionDialog';
    dialog.setAttribute('aria-label',action==='create-playlist'?'新建歌单':'添加到歌单');
    const form=document.createElement('form');form.method='dialog';
    const title=document.createElement('h2');
    title.textContent=action==='create-playlist'?'新建歌单':'添加到歌单';
    const label=document.createElement('label');
    label.textContent=action==='create-playlist'?'歌单名称':'选择歌单';
    const field=document.createElement(action==='create-playlist'?'input':'select');
    field.required=true;
    if(action==='create-playlist') {field.type='text';field.maxLength=120;field.autocomplete='off';}
    else for(const item of (library.playlists||[]).filter(item=>item.owned&&item.providerId===library.providerId)) {
      const option=document.createElement('option');option.value=String(item.id);option.textContent=item.title;field.append(option);
    }
    label.append(field);
    const actions=document.createElement('div');
    const cancel=document.createElement('button');cancel.type='button';cancel.textContent='取消';
    const save=document.createElement('button');save.type='submit';save.textContent=action==='create-playlist'?'创建':'添加';
    save.disabled=action!=='create-playlist'&&!field.options.length;
    actions.append(cancel,save);form.append(title,label,actions);dialog.append(form);document.body.append(dialog);
    const opener=document.activeElement;
    const close=()=>{closePlaylistDialog();if(opener?.isConnected)opener.focus({preventScroll:true});};
    cancel.addEventListener('click',close);
    dialog.addEventListener('cancel',event=>{event.preventDefault();close();});
    form.addEventListener('submit',event=>{
      event.preventDefault();
      if(disposed||playlistDialog!==dialog||document.hidden||!nativeVisible||!library.signedIn||library.loading)return;
      const value=field.value.trim();
      if(!value)return;
      if(action==='add-to-playlist'&&!(library.playlists||[]).some(item=>
        item.owned&&item.providerId===library.providerId&&String(item.id)===value))return;
      if(online.request(action==='create-playlist'?'createPlaylist':'addToPlaylist',
        action==='create-playlist'?{title:value}:{id:value,trackId}))close();
    });
    dialog.showModal();field.focus();
  }

  function updateSearchDraft(target) {
    view={...view,query:target.value.slice(0,512)};
    const clear=surface.querySelector('[data-music-action="clear-search"]');
    if(clear)clear.hidden=!view.query;
  }

  function filterList(target) {
    view={...view,listQuery:target.value.slice(0,512)};
    // Keep the active native editor (including selection and IME) in place.
    const next=document.createElement('div');
    next.innerHTML=musicPage(state,pageOptions());
    for(const selector of ['.MusicContent .MusicTrackList','.MusicContent .MusicSongTab',
      '.MusicContent .MusicToolbar .MusicPrimaryButton']) {
      const current=surface.querySelector(selector),replacement=next.querySelector(selector);
      if(current && replacement)current.replaceWith(replacement);
    }
  }

  function cancelSeek() {
    if (seek) cancelledSeek = true;
    seek = null;
    if (keySeek) keySeek.cancelled = true;
    playback();
  }

  function startSeek(element) {
    if (!canSeek() || element.disabled) return;
    cancelledSeek = false;
    seek = {...identity(element), dirty:false};
  }

  function click(event) {
    const target = targetOf(event);
    if (!owned(target)) return;
    if(view.screen==='desktop') {
      const button=target.closest('[data-action="back"], [data-tab], [data-music-action="desktop-enabled"]');
      if(!button)return;
      event.stopPropagation();
      if(button.disabled)return;
      if(button.dataset.action==='back')onDesktopBack?.();
      else if(button.hasAttribute('data-tab'))render({desktopTab:button.dataset.tab});
      else if(desktopControls&&!desktopControls.state.pending)
        desktopControls.setEnabled(!desktopControls.state.enabled);
      return;
    }
    closeVisualizerPickerOutside(event);
    const button = target.closest("[data-music-action], [data-music-quality-trigger], [data-music-quality-option], [data-music-rail-tab], [data-music-rail-playlist], [data-music-nav], [data-music-playlist], [data-music-playlist-group], [data-music-panel-tab], [data-music-setting], [data-music-color-reset], [data-music-sonnet-reset], [data-music-background-action], [data-music-background-trigger], [data-music-visualizer-trigger], [data-music-visualizer], [data-music-cover-copy], [data-music-effect-trigger], [data-music-effect], [data-music-font-trigger], [data-music-font-close], [data-music-font-family]");
    if (!owned(button) || button.disabled || button.getAttribute("aria-disabled") === "true") return;
    const started = pressed || identity(button);
    pressed = null;
    if(button.matches('[data-music-volume-trigger]')) {
      setVolumeMenuOpen(surface,button.getAttribute('aria-expanded')!=='true');return;
    }
    if(button.matches('[data-music-quality-trigger]')) {
      setQualityMenuOpen(surface,button.getAttribute('aria-expanded')!=='true');return;
    }
    if(button.matches('[data-music-quality-option]')) {
      if(button.disabled){notice(button.title);return;}
      if(button.dataset.musicQualityOption!==state.audio?.requestedQuality ||
        (!state.audio?.switching&&button.dataset.musicQualityOption!==state.audio?.quality))
        command('setQuality',{value:button.dataset.musicQualityOption},started);
      setQualityMenuOpen(surface,false);setVolumeMenuOpen(surface,false);
      surface.querySelector('[data-music-quality-trigger]')?.focus({preventScroll:true});return;
    }
    if(button.dataset.musicRailTab) {
      refreshPanels({railTab:button.dataset.musicRailTab,railSearch:false,railQuery:''});return;
    }
    if(button.dataset.musicRailPlaylist) {
      if(library.busy)return;
      const key=button.dataset.musicRailPlaylist;
      if(view.railPlaylist===key){refreshPanels({railPlaylist:''});return;}
      const collection=library.playlists?.find(item=>musicCollectionKey(item)===key);
      if(collection&&online) {
        const previous=view.railPlaylist;view={...view,railPlaylist:key};
        if(!online.request('playlist',{id:collection.id}))view={...view,railPlaylist:previous};
        refreshPanels();
      }
      return;
    }
    if(button.matches('[data-music-font-trigger]')) {
      if(settings.loaded)void fontPicker.open(button,settings.preferences.fontFamily);
      return;
    }
    if(button.matches('[data-music-font-close]')) {fontPicker.close();return;}
    if(button.matches('[data-music-font-family]')) {
      if(settings.loaded && fontPicker.contains(button)) {
        onPreferenceCommit?.({fontFamily:button.dataset.musicFontFamily});
        fontPicker.close();
      }
      return;
    }
    if(button.matches('[data-music-visualizer-trigger]')) {
      setMusicVisualizerPickerOpen(surface,button.getAttribute('aria-expanded')!=='true');
      return;
    }
    if(button.matches('[data-music-background-trigger]')) {
      setMusicBackgroundPickerOpen(surface,button.getAttribute('aria-expanded')!=='true');
      return;
    }
    if(button.dataset.musicBackgroundAction==='gallery') {
      setMusicBackgroundPickerOpen(surface,false);
      surface.querySelector('[data-music-background-trigger]')?.focus({preventScroll:true});
      wallpaperGallery.open();return;
    }
    if(button.matches('[data-music-cover-copy]')) {
      void copyCover();
      return;
    }
    if(button.matches('[data-music-effect-trigger]')) {
      setMusicEffectPickerOpen(surface,button.getAttribute('aria-expanded')!=='true');
      return;
    }
    if(button.matches('[data-music-effect]')) {
      setMusicEffectPickerOpen(surface,false);
      onPreferenceCommit?.({effectMode:button.dataset.musicEffect});
      return;
    }
    if(button.matches('[data-music-visualizer]')) {
      const mode=resolveMusicVisualizer(button.dataset.musicVisualizer);
      closeMusicVisualizerPicker(surface);
      view={...view,visualizer:mode};
      settings={...settings,preferences:{...settings.preferences,mode}};
      updatePanel();playback();
      onPreferenceCommit?.({mode});
      return;
    }
    if(MUSIC_PANEL_TABS.includes(button.dataset.musicPanelTab)) {
      view={...view,panelTab:button.dataset.musicPanelTab};updatePanel();return;
    }
    if(['choose','clear'].includes(button.dataset.musicBackgroundAction)) {
      setMusicBackgroundPickerOpen(surface,false);
      onBackgroundAction?.(button.dataset.musicBackgroundAction);return;
    }
    if(button.dataset.musicSetting==='intensity') {
      onPreferenceCommit?.({animationIntensity:({calm:'normal',normal:'chaotic',chaotic:'calm'})[settings.preferences.animationIntensity]});return;
    }
    if(['lyricsFontColor','lyricsKaraokeFillColor'].includes(button.dataset.musicColorReset)) {
      onPreferenceCommit?.({[button.dataset.musicColorReset]:'#ffffff'});return;
    }
    if(settings.loaded && SONNET_COLOR_KEYS.includes(button.dataset.musicSonnetReset)) {
      onPreferenceCommit?.({sonnetColors:{[button.dataset.musicSonnetReset]:'#ffffff'}});return;
    }
    if (button.dataset.musicNav !== undefined) {
      const action={recommendations:'home',albums:'explore',favorites:'favorites',recent:'recent'}[button.dataset.musicNav];
      if(action&&library.signedIn&&online&&!online.request(action))return;
      if (NAVS.includes(button.dataset.musicNav)) render({nav:button.dataset.musicNav, screen:"home", panel:"",
        playlistId:"",query:"",search:"",listQuery:"",listSearchOpen:false});
      if(button.dataset.musicNav==='local')localLibrary?.load();
      return;
    }
    if(button.dataset.musicPlaylist !== undefined) {
      const collection=(library.playlists || []).concat(library.recommendations || [])
        .find(item=>musicCollectionKey(item)===button.dataset.musicPlaylist);
      if(collection&&online&&!online.request('playlist',{id:String(collection.id)}))return;
      if(collection)render({nav:collection.type==='album'?'albums':'playlists',playlistId:musicCollectionKey(collection),
        screen:'home',panel:'',query:'',search:'',listQuery:'',listSearchOpen:false});
      return;
    }
    if (button.dataset.musicPlaylistGroup !== undefined) {
      if (["owned", "subscribed"].includes(button.dataset.musicPlaylistGroup))
        render({playlistGroup:button.dataset.musicPlaylistGroup});
      return;
    }
    const action = button.dataset.musicAction;
    if(button.dataset.musicLocked&&['online-play','play-now'].includes(action)){notice(`${button.dataset.musicLocked}才能播放`);return;}
    const rowId=button.closest('[data-music-track-id]')?.dataset.musicTrackId||button.dataset.musicTrack||state.current?.id;
    if(action==='immersive') {
      view={...view,immersive:!view.immersive};
      immersiveArmed=false;immersiveRevealed=false;
      refreshPanels({panel:''});
      updateImmersive();
      if(view.immersive){surface.tabIndex=-1;surface.focus({preventScroll:true});}
    }
    else if(action==='rail-pin')refreshPanels({railPinned:!view.railPinned});
    else if(action==='rail-clear-search')refreshPanels({railQuery:'',railSearch:false});
    else if(action==='rail-refresh') {
      if(!library.busy){view={...view,railPlaylist:''};online?.refresh();}
    }
    else if(action==='online-play') {
      if(rowId&&online?.request('play',{id:rowId}))render({screen:'player'});
    } else if(action==='local-play') {
      if(rowId&&localLibrary?.play(rowId))render({screen:'player'});
    } else if(action==='local-enqueue'&&rowId)localLibrary?.enqueue(rowId,false);
    else if(action==='local-next'&&rowId)localLibrary?.enqueue(rowId,true);
    else if(action==='local-remove'&&rowId)localLibrary?.remove(rowId);
    else if(action==='local-retry')localLibrary?.load();
    else if(action==='local-previous'||action==='local-more') {
      const {offset=0,limit=50}=localLibrary?.state||{};
      localLibrary?.page(Math.max(0,offset+(action==='local-more'?limit:-limit)));
    } else if(action==='online-retry')online?.retry();
    else if(action==='account-home') {
      render({screen:'home',panel:'',nav:'recommendations',playlistId:'',query:'',search:'',listQuery:'',listSearchOpen:false});
    }
    else if(action==='insert-next' && rowId) {
      if(/^(netease|qq|kugou):/.test(rowId)&&!button.closest('[data-music-panel="queue"]'))online?.request('insertNext',{id:rowId});
      else command('insertNext',{id:rowId},started);
    } else if(action==='remove-track'&&rowId)command('removeFromQueue',{id:rowId},started);
    else if(action==='like'&&rowId&&canLikeMusicTrack(library,rowId))online?.request('like',{id:rowId,
      value:!musicTrackLiked(library,rowId)});
    else if(action==='add-to-playlist')openPlaylistSelection(button,rowId);
    else if(action==='create-playlist') {
      openPlaylistDialog(action,rowId);
    } else if (["toggle", "previous", "next"].includes(action)) {
      const capability = {toggle:"canToggle", previous:"canPrevious", next:"canNext"}[action];
      if (state.capabilities[capability] && (action !== "toggle" || state.current)) command(action, {}, started);
    } else if (action === "play-now") {
      const value = button.dataset.musicIndex;
      const index = Number(value);
      if (value?.trim() && Number.isSafeInteger(index) && index >= 0 && index < state.queue.length)
        command("play-now", {index,id:button.closest('[data-music-track-id]')?.dataset.musicTrackId || state.queue[index].id}, started);
    } else if (action === "clear-queue") {
      if (state.queue.length) command("clearQueue", {}, started);
    } else if (action === "play-mode") {
      if (state.shuffle) {
        command("setShuffle", {value:false}, started);
        command("setRepeat", {value:"none"}, started);
      } else if (state.repeat === "one") {
        command("setRepeat", {value:"all"}, started);
        command("setShuffle", {value:true}, started);
      } else command("setRepeat", {value:state.repeat === "all" ? "one" : "all"}, started);
    } else if (action === "open-player") render({screen:"player"});
    else if (action === "back-home") render({screen:"home", panel:""});
    else if (action === "queue" || action === "lyrics") {
      if(view.panel===action)closePanel();
      else {
        refreshPanels({panel:action,railTab:action==='queue'?'queue':view.railTab});
        surface.querySelector('[data-music-panel-tab][aria-pressed="true"], aside [data-music-action="close-panel"]')?.focus({preventScroll:true});
      }
    }
    else if (action === "close-panel") closePanel();
    else if (action === "import-local") surface.querySelector("[data-music-file-input]")?.click();
    else if (action === "clear-search") {
      if(view.search&&library.signedIn&&online&&view.nav!=='local') {
        const collection=(library.playlists||[]).concat(library.recommendations||[])
          .find(item=>musicCollectionKey(item)===view.playlistId);
        const read=collection?'playlist':({albums:'explore',favorites:'favorites',recent:'recent'})[view.nav]||'home';
        if(!online.request(read,collection?{id:String(collection.id)}:{}))return;
      }
      render({query:"", search:""});
      surface.querySelector("[data-music-search]")?.focus({preventScroll:true});
    } else if (action === "list-search") {
      render({listSearchOpen:true});
      surface.querySelector("[data-music-list-search]")?.focus({preventScroll:true});
    } else if (action === "close-list-search") closeListSearch();
  }

  function input(event) {
    const target = targetOf(event);
    if (!owned(target)) return;
    if(target.matches('[data-music-volume]')) {
      const value=surface.querySelector('[data-music-volume-value]');
      if(value)value.textContent=`${Math.round(Number(target.value)*100)}%`;
      return;
    }
    if(target.matches('[data-music-rail-search]')) {
      view={...view,railQuery:target.value,railSearch:false};
      const next=document.createElement('div');next.innerHTML=musicPage(state,pageOptions());
      surface.querySelector('[data-music-rail] .MusicPanelBody')?.replaceWith(next.querySelector('[data-music-rail] .MusicPanelBody'));
      const clear=surface.querySelector('[data-music-action="rail-clear-search"]');if(clear)clear.hidden=!view.railQuery;
      return;
    }
    if(target.matches('[data-music-font-search]')) {fontPicker.search(target.value);return;}
    if(target.matches('[data-music-sonnet-color][type="color"]') && !target.disabled &&
      settings.loaded && SONNET_COLOR_KEYS.includes(target.dataset.musicSonnetColor)) {
      onPreferencePreview?.({sonnetColors:{[target.dataset.musicSonnetColor]:target.value}});
    } else if(target.matches('[data-music-preference][type="color"]') && !target.disabled) {
      onPreferencePreview?.({[target.dataset.musicPreference]:target.value});
    } else if (target.matches("[data-music-search]")) {
      updateSearchDraft(target);
    }
    else if (target.matches("[data-music-list-search]")) {
      if (composing || event.isComposing) view = {...view, listQuery:target.value.slice(0,512)};
      else if(surface.querySelector('.MusicContent'))filterList(target);
      else render({listQuery:target.value.slice(0,512)});
    } else if (target.matches("[data-music-progress]") && !cancelledSeek) {
      if (!seek) startSeek(target);
      if (seek) {seek.dirty = true;playback();}
    }
  }

  function change(event) {
    const target = targetOf(event);
    if (!owned(target)) return;
    if(target.matches('[data-music-volume]')) {
      const value=Number(target.value);
      if(Number.isFinite(value)) {
        command('setVolume',{value:Math.max(0,Math.min(1,value))});
        if(value>0&&state.muted)command('setMuted',{value:false});
      }
    } else if(target.matches('[data-music-sonnet-color]')) {
      if(!target.disabled && settings.loaded && SONNET_COLOR_KEYS.includes(target.dataset.musicSonnetColor))
        onPreferenceCommit?.({sonnetColors:{[target.dataset.musicSonnetColor]:target.value}});
    } else if(target.matches('[data-music-preference]')) {
      if(target.disabled)return;
      if(onPreferenceCommit)onPreferenceCommit({[target.dataset.musicPreference]:target.value});
      else if(target.dataset.musicPreference==='mode') {
        view.visualizer=resolveMusicVisualizer(target.value);
        settings={...settings,preferences:{...settings.preferences,mode:view.visualizer}};
        updatePanel();playback();
      }
    } else if (target.matches("[data-music-file-input]")) {
      const files = Array.from(target.files || []);
      target.value = "";
      if (!files.length) return;
      if (files.length > 32) { notice(IMPORT_LIMIT); return; }
      try {
        if (typeof onImport !== "function" || onImport(files) === false) notice(IMPORT_FAILED);
      } catch { notice(IMPORT_FAILED); }
    } else if (target.matches("[data-music-progress]")) {
      const started = seek || identity(target), position = Number(target.value);
      if (!cancelledSeek && canSeek() && !target.disabled && Number.isFinite(position) && position >= 0)
        command("seek", {position:Math.min(position, state.timeline.duration)}, started);
      seek = null;
      playback();
    }
  }

  function submit(event) {
    const target = targetOf(event);
    if(owned(target)&&target.matches('[data-music-rail-search-form]')) {
      event.preventDefault();
      if(!composing&&view.railQuery?.trim()&&library.signedIn&&!library.busy) {
        view={...view,railSearch:true};
        if(!online?.request('search',{query:view.railQuery.trim()},null,false,true))view={...view,railSearch:false};
        refreshPanels();
      }
      return;
    }
    if (!owned(target) || !target.matches("[data-music-search-form]")) return;
    event.preventDefault();
    if(composing)return;
    const query = target.querySelector("[data-music-search]")?.value || "";
    if(!query.trim())return;
    if(library.signedIn&&online&&!online.request('search',{query:query.trim()},null,false,true))return;
    render({query, search:query.trim(), screen:"home", panel:""});
  }

  function keydown(event) {
    const target = targetOf(event);
    if (!owned(target) || event.defaultPrevented || event.isComposing) return;
    if(view.screen==='desktop')return;
    if(event.key==='Escape'&&(event.repeat||document.querySelector('dialog[open], [data-folia-keyboard-window="true"]')))return;
    if(target.closest('[data-music-background-picker]')) {
      if(event.key==='Escape') {
        event.preventDefault();setMusicBackgroundPickerOpen(surface,false);
        surface.querySelector('[data-music-background-trigger]')?.focus();
      } else if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)) {
        event.preventDefault();setMusicBackgroundPickerOpen(surface,true);
        const items=[...surface.querySelectorAll('[data-music-background-menu] button:not(:disabled):not([hidden])')];
        const index=items.indexOf(target);
        const next=event.key==='Home'?0:event.key==='End'?items.length-1:
          event.key==='ArrowDown'?(index+1)%items.length:(index-1+items.length)%items.length;
        items[next]?.focus();
      }
      return;
    }
    if(fontPicker.contains(target)) {
      if(event.key==='Escape') {event.preventDefault();fontPicker.close();}
      if(event.key==='Tab') {
        const fields=[...target.closest('[data-music-font-picker]').querySelectorAll('button:not(:disabled), input:not(:disabled)')];
        const first=fields[0],last=fields.at(-1);
        if(event.shiftKey && target===first || !event.shiftKey && target===last) {
          event.preventDefault();
          (event.shiftKey?last:first)?.focus({preventScroll:true});
        }
      }
      return;
    }
    const volumePicker=target.closest?.('[data-music-volume-picker]');
    if(volumePicker && event.key==='Escape'){
      event.preventDefault();setVolumeMenuOpen(surface,false);
      volumePicker.querySelector('[data-music-volume-trigger]')?.focus({preventScroll:true});return;
    }
    const picker=target.closest?.('[data-music-visualizer-picker], [data-music-effect-picker], [data-music-quality-picker]');
    if(picker) {
      const kind=picker.hasAttribute('data-music-quality-picker')?'quality':
        picker.hasAttribute('data-music-effect-picker')?'effect':'visualizer';
      const setOpen=kind==='quality'?setQualityMenuOpen:kind==='effect'?setMusicEffectPickerOpen:setMusicVisualizerPickerOpen;
      const trigger=picker.querySelector(`[data-music-${kind}-trigger]`);
      const options=[...picker.querySelectorAll(`[data-music-${kind}${kind==='quality'?'-option':''}]:not(:disabled)`)];
      if(event.key==='Escape') {
        event.preventDefault();setOpen(surface,false);trigger?.focus({preventScroll:true});return;
      }
      if(target===trigger && ['ArrowDown','ArrowUp','Home','End'].includes(event.key)) {
        event.preventDefault();
        setOpen(surface,true);
        const index=event.key==='Home'?0:event.key==='End'?options.length-1:
          event.key==='ArrowDown'?0:options.length-1;
        options[index]?.focus({preventScroll:true});return;
      }
      if(options.includes(target) && ['ArrowDown','ArrowUp','Home','End'].includes(event.key)) {
        event.preventDefault();
        const index=options.indexOf(target),next=event.key==='Home'?0:event.key==='End'?options.length-1:
          (index+(event.key==='ArrowDown'?1:-1)+options.length)%options.length;
        options[next]?.focus({preventScroll:true});return;
      }
    }
    if ((event.ctrlKey || event.metaKey) && !event.altKey && event.key.toLowerCase() === "k") {
      event.preventDefault();
      if (view.screen !== "home") render({screen:"home", panel:""});
      const search = surface.querySelector("[data-music-search]");
      search?.focus({preventScroll:true}); search?.select();
      return;
    }
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    if(target.matches('[data-music-cover-copy]') && ['Enter',' ','Spacebar'].includes(event.key)) {
      event.preventDefault();
      if(!event.repeat)void copyCover();
      return;
    }
    if(target.matches('[data-music-panel-tab]') && ['ArrowLeft','ArrowRight','Home','End'].includes(event.key)) {
      event.preventDefault();
      const index=MUSIC_PANEL_TABS.indexOf(target.dataset.musicPanelTab);
      const next=event.key==='Home'?0:event.key==='End'?MUSIC_PANEL_TABS.length-1:
        (index+(event.key==='ArrowRight'?1:-1)+MUSIC_PANEL_TABS.length)%MUSIC_PANEL_TABS.length;
      view={...view,panelTab:MUSIC_PANEL_TABS[next]};updatePanel();
      surface.querySelector(`[data-music-panel-tab="${view.panelTab}"]`)?.focus({preventScroll:true});return;
    }
    if (event.key === "Escape") {
      event.preventDefault();
      if (seek) cancelSeek();
      else if (onExitFullscreen?.()===true) return;
      else if (view.panel) closePanel();
      else if (view.listSearchOpen) closeListSearch();
      else if (view.screen === "player") render({screen:"home"});
      return;
    }
    const range = target.matches("[data-music-progress]");
    if (!range && target.closest("input, textarea, select, [contenteditable]:not([contenteditable=false]), [role=textbox]")) return;
    if (event.key === " " || event.key === "Spacebar") {
      if (target.closest("button, a[href], [role=button]")) return;
      event.preventDefault();
      if (!event.repeat && state.current && state.capabilities.canToggle) command("toggle", {}, identity(target));
    } else if (["ArrowLeft", "ArrowRight"].includes(event.key) && canSeek()) {
      event.preventDefault();
      if (!keySeek) keySeek = {...identity(target), position:state.timeline.position};
      if (!keySeek.cancelled && isCurrent(keySeek)) {
        keySeek.position = Math.max(0, Math.min(state.timeline.duration, keySeek.position + (event.key === "ArrowRight" ? 5 : -5)));
        command("seek", {position:keySeek.position}, keySeek);
      }
    }
  }

  const handlers = {click, input, change, submit, keydown,
    pointerover(event) {
      if(view.immersive&&immersiveArmed&&targetOf(event)?.closest(IMMERSIVE_REVEAL)) {
        immersiveRevealed=true;updateImmersive();
      }
      if(view.screen==='player'&&view.panel!== 'queue'&&owned(targetOf(event))&&
        targetOf(event).matches('[data-music-rail-hotspot]'))refreshPanels({panel:'queue'});
    },
    pointerout(event) {
      if(view.immersive&&targetOf(event)?.closest(IMMERSIVE_REVEAL)&&
        !event.relatedTarget?.closest?.(IMMERSIVE_REVEAL)) {
        immersiveArmed=true;immersiveRevealed=false;updateImmersive();
      }
      if(view.panel!=='queue'||view.railPinned||view.screen!=='player')return;
      const target=targetOf(event),related=event.relatedTarget;
      if(target?.closest('[data-music-rail], [data-music-rail-hotspot]')&&
        !related?.closest?.('[data-music-rail], [data-music-rail-hotspot]')&&
        !document.activeElement?.matches('[data-music-rail-search]'))
        refreshPanels({panel:''});
    },
    keyup(event) { if (owned(targetOf(event)) && ["ArrowLeft", "ArrowRight"].includes(event.key)) keySeek = null; },
    pointerdown(event) {
      const target = targetOf(event);
      closeVisualizerPickerOutside(event);
      if (!owned(target)) return;
      if (event.button !== undefined && event.button !== 0) return;
      if (target.matches("[data-music-progress]")) startSeek(target);
      else {
        const element = target.closest("[data-music-action]");
        if (owned(element)) pressed = identity(element);
      }
    },
    pointerup(event) {
      const target = targetOf(event);
      if (!owned(target) || !seek) return;
      if (!seek.dirty) { seek = null; playback(); return; }
      // A dragged range normally commits through change. If that never
      // arrives (input re-rendered, focus stolen), commit here once.
      const pending = seek;
      setTimeout(() => {
        if (seek !== pending || disposed) return;
        const input = surface?.querySelector("[data-music-progress]");
        const position = Number(input?.value);
        if (!cancelledSeek && canSeek() && input && !input.disabled && Number.isFinite(position) && position >= 0)
          command("seek", {position:Math.min(position, state.timeline.duration)}, pending);
        seek = null;
        playback();
      }, 0);
    },
    pointercancel(event) { if (owned(targetOf(event))) { pressed = null; cancelSeek(); } },
    focusout(event) {
      const target = targetOf(event);
      if (!owned(target)) return;
      if (target.matches("[data-music-progress]") || !owned(event.relatedTarget)) cancelSeek();
      if (!owned(event.relatedTarget)) pressed = null;
      keySeek = null;
    },
    compositionstart(event) {
      const target=targetOf(event);
      if(owned(target) && target.matches('[data-music-search], [data-music-list-search], [data-music-rail-search]'))composing=true;
    },
    compositionend(event) {
      const target = targetOf(event);
      if (!owned(target)) return;
      composing=false;
      if (target.matches("[data-music-list-search]")) {
        if(surface.querySelector('.MusicContent'))filterList(target);
        else render({listQuery:target.value});
      } else if (target.matches("[data-music-search]")) updateSearchDraft(target);
      if(pendingRender)render();
    },
  };

  const imageError = event => {
    const image = targetOf(event);
    if (owned(image) && image.matches('[data-music-image]')) image.hidden = true;
  };

  render();
  accountView=mountMusicProviderAccountView(root,providerAccount,{onSelect:provider=>online?.select(provider),
    onSync:provider=>{online?.select(provider);online?.refresh();}});
  function canLoadMore(target) {
    return !disposed&&owned(target)&&!document.hidden&&nativeVisible&&state.pageVisible&&
      library.signedIn&&!library.loading&&!library.busy&&!library.error&&library.more&&
      target.matches('.MusicContent, [data-music-rail] .MusicPanelBody')&&
      !(view.screen==='home'&&view.nav==='local'&&!view.search)&&target.clientHeight>0;
  }
  function loadMoreOnScroll(event) {
    const target=event.target;
    if(!canLoadMore(target)||target.scrollHeight<=target.clientHeight)return;
    if(target.scrollHeight-target.scrollTop-target.clientHeight<=120)online?.next();
  }
  // A first page that does not fill a tall window has no scrollbar, so no scroll
  // event would ever request the rest of the list.
  function fillVisibleList() {
    (globalThis.requestAnimationFrame||(callback=>setTimeout(callback,0)))(()=>{
      for(const target of surface?.querySelectorAll('.MusicContent, [data-music-rail] .MusicPanelBody')||[])
        if(canLoadMore(target)&&target.scrollHeight-target.clientHeight<=120){online?.next();return;}
    });
  }

  root.addEventListener('scroll',loadMoreOnScroll,true);
  root.addEventListener('error', imageError, true);
  for (const [name, handler] of Object.entries(handlers)) root.addEventListener(name, handler);
  document.addEventListener('pointerdown',closeVisualizerPickerOutside);
  document.addEventListener('scroll',positionFloatingMenus,true);
  document.defaultView.addEventListener('resize',positionFloatingMenus);
  document.addEventListener('visibilitychange',documentVisibility);
  return {
    updateDefaultBackground(media) {
      if(disposed)return;
      defaultBackground=media;
      backgroundRenderer?.update({defaultMedia:defaultBackground,visible:state.pageVisible&&nativeVisible});
    },
    updateDesktopControls() {
      if(disposed||view.screen!=='desktop')return;
      const next=document.createElement('div');next.innerHTML=musicPage(state,pageOptions());
      const button=surface.querySelector('[data-music-action="desktop-enabled"]');
      const replacement=next.querySelector('[data-music-action="desktop-enabled"]');
      if(button&&replacement) {
        button.className=replacement.className;
        button.disabled=replacement.disabled;
        button.setAttribute('aria-checked',replacement.getAttribute('aria-checked'));
      }
      const status=surface.querySelector('.status'),nextStatus=next.querySelector('.status');
      if(status&&nextStatus) {
        status.textContent=nextStatus.textContent;status.hidden=nextStatus.hidden;
        status.className=nextStatus.className;status.setAttribute('role',nextStatus.getAttribute('role'));
      }
    },
    updateLibrary(next) {
      if(disposed)return;
      const accountKey=value=>`${value.providerId||''}:${value.signedIn?value.accounts?.find(account=>
        account.provider===value.providerId)?.userId||'signed-in':''}`;
      if(accountKey(library)!==accountKey(next)) {
        closePlaylistDialog();
        view={...view,railPlaylist:'',railQuery:'',railSearch:false};
        if(view.nav!=='local')view={...view,nav:'recommendations',playlistId:'',query:'',search:'',listQuery:'',listSearchOpen:false};
      }
      library=next;
      fillVisibleList();
      if(view.screen==='home')render();
      else {
        if(view.panel==='queue'&&(view.railSearch||view.railTab==='playlists'))refreshPanels();
        updateLocalArtwork();
        for(const like of surface.querySelectorAll('.MusicPlayerControls [data-music-action="like"], [data-music-rail] [data-music-action="like"]')) {
          const id=like.closest('[data-music-track-id]')?.dataset.musicTrackId||state.current?.id;
          const liked=musicTrackLiked(library,id);
          like.disabled=!canLikeMusicTrack(library,id);
          like.classList.toggle('is-active',liked);like.setAttribute('aria-pressed',String(liked));
          like.title=liked?'取消喜欢':'喜欢';like.setAttribute('aria-label',like.title);
        }
      }
    },
    refreshFontLabels() {
      if(!disposed)updateMusicSettings(surface,settings,{getFontLabel});
    },
    updatePreferences(next) {
      if(disposed)return;
      const changed=!sameMusicPreferences(settings.preferences,next.preferences);
      settings=next;
      if(next.loaded)view.visualizer=resolveMusicVisualizer(next.preferences.mode);
      updateMusicSettings(surface,next,{getFontLabel});
      if(!next.loaded)fontPicker.close(false);
      if(changed)playback();
    },
    updateBackground(next) {
      if(disposed)return;
      background=next;
      updateMusicBackground(surface,background);
      const canvas=surface.querySelector('[data-music-background]');
      if(canvas){canvas.dataset.musicBackgroundState='loading';canvas.dataset.musicBackgroundId=background.media?.id||'default-forest';}
      backgroundRenderer?.update({media:background.media,defaultMedia:defaultBackground,visible:state.pageVisible && nativeVisible});
    },
    setVisible(visible) {nativeVisible=visible===true;if(!nativeVisible){fontPicker.close(false);closePlaylistDialog();setMusicBackgroundPickerOpen(surface,false);setQualityMenuOpen(surface,false);setVolumeMenuOpen(surface,false);}wallpaperGallery.setVisible(nativeVisible);accountView?.setVisible(nativeVisible);playback();},
    update(next,{syncClock=true}={}) {
      if (disposed) return;
      const previous = state;
      state = normalizeMusicState(next, state);
      if(!state.pageVisible){fontPicker.close(false);closePlaylistDialog();setMusicBackgroundPickerOpen(surface,false);setQualityMenuOpen(surface,false);setVolumeMenuOpen(surface,false);}
      wallpaperGallery.setVisible(state.pageVisible&&nativeVisible);
      if(!state.pageVisible)accountView?.setVisible(false);
      else accountView?.setVisible(nativeVisible);
      if(copyOperation && !isCurrent(copyOperation))cancelCopy();
      if (seek && (!isCurrent(seek) || !canSeek())) { seek = null; cancelledSeek = true; }
      if (keySeek && (!isCurrent(keySeek) || !canSeek())) keySeek.cancelled = true;
      if (!sameStructure(previous, state)) render({}, {syncClock});
      else playback({syncClock});
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      setQualityMenuOpen(surface,false);setVolumeMenuOpen(surface,false);
      closePlaylistDialog();
      fontPicker.close(false);
      wallpaperGallery.dispose();
      accountView?.dispose();accountView=null;
      cancelCopy();
      visualizer?.dispose();visualizer=null;
      effects?.dispose();effects=null;
      clearMaterial();
      backgroundRenderer?.dispose();backgroundRenderer=null;backgroundCanvas=null;
      root.removeEventListener('error', imageError, true);
      root.removeEventListener('scroll',loadMoreOnScroll,true);
      for (const [name, handler] of Object.entries(handlers)) root.removeEventListener(name, handler);
      document.removeEventListener('pointerdown',closeVisualizerPickerOutside);
      document.removeEventListener('scroll',positionFloatingMenus,true);
      document.defaultView.removeEventListener('resize',positionFloatingMenus);
      document.removeEventListener('visibilitychange',documentVisibility);
      scrollPositions.clear();
      seek = null; keySeek = null; pressed = null; surface = null;
    },
    snapshot() { return structuredClone({...state, view}); },
    captureView() {
      if(disposed)return null;
      saveScroll();
      return {...view,scrollPositions:[...scrollPositions].slice(-120).map(([key,[top,left]])=>[key,top,left])};
    },
  };
}
