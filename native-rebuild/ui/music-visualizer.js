import {createMusicFontGate} from './music-font-assets.js';

// Owns only the active visualizer and its playback clock, never native resources.
export const MUSIC_VISUALIZERS = Object.freeze([
  Object.freeze({id:'partita',label:'\u4e91\u9636'}),
  Object.freeze({id:'fume',label:'\u6d6e\u540d'}),
  Object.freeze({id:'sonnet',label:'\u5546\u7c41'}),
  Object.freeze({id:'gallery',label:'\u753b\u5883'}),
  Object.freeze({id:'3d',label:'3D'}),
  Object.freeze({id:'lumiere',label:'绘光'}),
  Object.freeze({id:'cadenza',label:'心象'}),
  Object.freeze({id:'tilt',label:'倾诉'}),
  Object.freeze({id:'cappella',label:'群唱'}),
  Object.freeze({id:'diorama',label:'镜台'}),
  Object.freeze({id:'pendolo',label:'时计'}),
  Object.freeze({id:'classic',label:'流光'}),
  Object.freeze({id:'tempera',label:'凝彩'}),
]);
export const DEFAULT_MUSIC_VISUALIZER = 'partita';
export const resolveMusicVisualizer = mode => MUSIC_VISUALIZERS.some(item=>item.id===mode) ? mode : DEFAULT_MUSIC_VISUALIZER;
const galleryStyles=new WeakMap();
const loadVisualizer = async (mode,host,options) => {
  if(mode==='lumiere'){
    const {mountLumiere}=await import('./music-visualizers/lumiere.js');
    return (host,state)=>mountLumiere(host,state,options);
  }
  if(mode==='gallery') {
    const document=host.ownerDocument;
    if(!galleryStyles.has(document)) {
      galleryStyles.set(document,new Promise((resolve,reject)=>{
        const link=document.createElement('link');link.rel='stylesheet';
        link.href=new URL('./music-visualizers/gallery.css',import.meta.url).href;
        link.onload=()=>resolve();link.onerror=()=>{galleryStyles.delete(document);link.remove();reject(new Error('Gallery stylesheet unavailable'));};
        document.head.append(link);
      }));
    }
    await galleryStyles.get(document);
    return (await import('./music-visualizers/gallery.js')).mountGallery;
  }
  if(mode==='3d') return (await import('./music-visualizers/three-d.js')).mountThreeD;
  const {mountReference}=await import('./music-visualizers/reference-host.js');
  return (host,state)=>mountReference(host,state,mode,options);
};

export function activeLyricIndex(lines, time) {
  let start=0, end=lines.length;
  while (start<end) {
    const middle=(start+end)>>>1;
    if (lines[middle].startTime<=time) start=middle+1;
    else end=middle;
  }
  return start-1;
}

export function mountMusicVisualizer(host, {load=loadVisualizer,onError,ensureFont} = {}) {
  const document=host.ownerDocument, window=document.defaultView;
  let state=null, mode='', runtime=null, generation=0, pending=null, disposed=false, frame=null;
  let receivedAt=window.performance.now();
  const fonts=createMusicFontGate(document,{ensureFont,onReady:()=>refresh(),onError});
  const visible=()=>!!state?.visible && !document.hidden;
  const stop=()=>{if(frame!==null)window.cancelAnimationFrame(frame);frame=null;};
  const projected=()=>{
    const elapsed=state.playing && visible() ? Math.max(0,(window.performance.now()-receivedAt)/1000) : 0;
    // Never free-run beyond one missed native update; seek/track snapshots reset this clock.
    const time=Math.min(state.duration || 86400,state.currentTime+Math.min(elapsed,1));
    return {...state,preferences:fonts.preferences(state.preferences,{visible:visible()}),
      currentTime:time,activeLineIndex:activeLyricIndex(state.lines,time),visible:visible()};
  };
  function draw() {
    frame=null;
    if(disposed || !runtime || !state) return;
    runtime.update(projected());
    if(state.playing && visible()) frame=window.requestAnimationFrame(draw);
  }
  function refresh() {stop();draw();}
  function clear() {
    stop();runtime?.dispose();runtime=null;pending=null;
    host.replaceChildren();
  }
  function visibilityChanged() {
    if(!state)return;
    fonts.preferences(state.preferences,{visible:visible()});
    if(!runtime && visible()) void api.update(state,mode,{syncClock:false});
    else refresh();
  }
  document.addEventListener('visibilitychange',visibilityChanged);
  const api = {
    async update(next,nextMode=mode,{syncClock=true}={}) {
      if(disposed) return;
      if(!state || syncClock)receivedAt=window.performance.now();
      state=next;
      const selected=resolveMusicVisualizer(nextMode);
      const needsRuntime=!!state.current && (selected==='gallery' || selected==='lumiere' || state.lines.length>0 ||
        selected==='3d' && state.preferences?.effectMode==='star-river');
      if(selected!==mode || !needsRuntime) {generation++;clear();mode=selected;}
      host.dataset.visualizerMode=mode;
      if(!needsRuntime) {
        fonts.preferences(state.preferences,{visible:false});
        host.textContent=state.current?'\u6682\u65e0\u6b4c\u8bcd':'\u6682\u65e0\u64ad\u653e';
        host.dataset.visualizerState='empty';return;
      }
      fonts.preferences(state.preferences,{visible:visible()});
      if(runtime) {refresh();return;}
      if(pending || !visible()) return pending;
      const ticket=++generation;
      host.dataset.visualizerState='loading';
      pending=(async()=>{
        try {
          const mount=await load(mode,host,{ensureFont,onError});
          if(disposed || ticket!==generation || !visible()) return;
          host.replaceChildren();
          runtime=mount(host,projected());
          host.dataset.visualizerState='ready';refresh();
        } catch(error) {
          if(disposed || ticket!==generation) return;
          clear();host.dataset.visualizerState='error';
          host.textContent='\u6b4c\u8bcd\u663e\u793a\u6682\u4e0d\u53ef\u7528';onError?.(error);
        } finally {if(ticket===generation)pending=null;}
      })();
      return pending;
    },
    dispose() {
      if(disposed)return;
      disposed=true;generation++;clear();state=null;
      fonts.dispose();
      document.removeEventListener('visibilitychange',visibilityChanged);
    },
  };
  return api;
}
