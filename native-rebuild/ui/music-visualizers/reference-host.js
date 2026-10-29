import {analysisSnapshot,EMPTY_ANALYSIS} from './reference/preference-protocol.js';
// The generated reference bundle carries a snapshot of the preference rules from
// its build, whose style list predates the newer renderers: validate with the live rules.
import {musicPreferenceSnapshot as preferenceSnapshot} from '../music-preference-values.js';
import {createMusicFontGate} from '../music-font-assets.js';

// Each historical renderer gets an unloadable realm; native playback stays outside it.
const MODES=new Set(['classic','cadenza','partita','fume','tilt','monet','sonnet']);
// Styles from the newer Folia release are a separate bundle with the same stage protocol.
const NEXT_MODES=new Set(['cappella','diorama','pendolo','tempera']);
export function mountReference(host,initial,mode,{ensureFont,onError}={}) {
  if(!MODES.has(mode)&&!NEXT_MODES.has(mode))throw new Error('Unknown lyric renderer');
  const document=host.ownerDocument,window=document.defaultView;
  const frame=document.createElement('iframe');
  const source=new URL(NEXT_MODES.has(mode)?'./reference-next/index.html':'./reference/index.html',import.meta.url);
  source.hash=mode;
  frame.src=source.href;frame.title='\u6b4c\u8bcd';frame.className='MusicReferenceStage';
  frame.setAttribute('sandbox','allow-scripts allow-same-origin');
  let state=initial,ready=false,disposed=false;
  let lastLines=null;
  let lastTrack=null;
  let lastAnalysis,blockedAnalysis;
  let preferences=preferenceSnapshot(undefined,initial.preferences),lastPreferences;
  let fonts=null,fontDocument=null;
  // Tempera only shows steadily in the app while its canvas is redrawn and read
  // once a frame from this document, which the player bar's liquid glass does
  // while it is on screen; without it (immersive mode) the wallpaper flashes
  // through. The same one-frame redraw and a 1-pixel read keep it steady; the
  // canvas skips the redraw when the glass already asked for one that frame.
  // It runs only while the stage is shown and playing, so a hidden or paused page owns no frames.
  let keepAlive=null,steady=null;
  if(mode==='tempera'){
    const sink=document.createElement('canvas');sink.width=1;sink.height=1;
    const context=sink.getContext('2d');
    const active=()=>!disposed&&ready&&state?.visible!==false&&state?.playing===true&&!document.hidden;
    const tick=()=>{
      keepAlive=null;
      if(!active())return;
      keepAlive=window.requestAnimationFrame(tick);
      const canvas=frame.contentDocument?.querySelector('canvas[data-lumenic-liquid-glass-scene-effect]');
      if(!canvas?.lumeMaterialSceneRefresh||!canvas.width)return;
      try{canvas.lumeMaterialSceneRefresh();context?.drawImage(canvas,0,0,1,1);}catch{}
    };
    steady=()=>{
      if(active()&&keepAlive===null)keepAlive=window.requestAnimationFrame(tick);
      else if(!active()&&keepAlive!==null){window.cancelAnimationFrame(keepAlive);keepAlive=null;}
    };
  }
  const publish=()=>{
    if(disposed)return;
    steady?.();
    const lines=state.lines;
    const current=state.current;
    const visible=state.visible!==false&&!document.hidden;
    const sameTrack=lastTrack && current && ['id','source','title','artist','album','artwork'].every(key=>lastTrack[key]===current[key]);
    const changedTrack=lastTrack!==null&&['id','source'].some(key=>lastTrack?.[key]!==current?.[key]);
    if(!visible||!state.playing||!current||changedTrack)blockedAnalysis=state.analysis;
    if(!ready)return;
    const resolved=preferenceSnapshot(lastPreferences,fonts.preferences(preferences,{visible}));
    const analysis=analysisSnapshot(lastAnalysis,
      visible&&state.playing&&current&&!changedTrack&&state.analysis!==blockedAnalysis?state.analysis:null);
    // Transfer the bounded lyric payload once per track, never once per frame.
    frame.contentWindow.postMessage({type:'lume-lyric-stage',mode,
      state:{currentTime:state.currentTime,duration:state.duration,activeLineIndex:state.activeLineIndex,
        playing:state.playing,visible,
        analysis:lastAnalysis===analysis?undefined:analysis,
        lines:lastLines===lines?undefined:lines,current:sameTrack?undefined:current,
        preferences:lastPreferences===resolved?undefined:resolved}},source.origin);
    lastLines=lines;lastTrack=current;lastPreferences=resolved;lastAnalysis=analysis;
  };
  const receive=event=>{
    if(disposed||event.source!==frame.contentWindow||event.origin!==source.origin)return;
    if(event.data?.type==='lume-lyric-stage-ready'){
      const nextDocument=frame.contentDocument;
      if(!fonts || nextDocument!==fontDocument) {
        fonts?.dispose();fontDocument=nextDocument;
        fonts=createMusicFontGate(fontDocument,{ensureFont,onReady:publish,onError});
      }
      ready=true;lastLines=null;lastTrack=null;lastPreferences=undefined;lastAnalysis=undefined;publish();
    }
    else if(event.data?.type==='lume-lyric-stage-error')host.dataset.visualizerState='error';
    else if(event.data?.type==='lume-lyric-stage-key') {
      const data=event.data;
      if(data.mode!==mode || !['keydown','keyup'].includes(data.eventType) ||
        ![' ','Spacebar','ArrowLeft','ArrowRight','Escape','k','K'].includes(data.key) ||
        !['repeat','ctrlKey','metaKey','altKey','shiftKey'].every(key=>typeof data[key]==='boolean'))return;
      // Forward input to the owning controller; the renderer has no playback port.
      host.dispatchEvent(new window.KeyboardEvent(data.eventType,{bubbles:true,cancelable:true,
        key:data.key,repeat:data.repeat,ctrlKey:data.ctrlKey,metaKey:data.metaKey,altKey:data.altKey,shiftKey:data.shiftKey}));
    }
  };
  document.addEventListener?.('visibilitychange',publish);
  window.addEventListener('message',receive);host.replaceChildren(frame);
  publish();
  return {
    update(next){
      if(disposed)return;
      preferences=preferenceSnapshot(preferences,next.preferences);
      // Readiness must not erase a track boundary observed while the iframe loads.
      if(!ready&&state.current&&['id','source'].some(key=>state.current[key]!==next.current?.[key]))
        blockedAnalysis=next.analysis;
      state=next;publish();
    },
    dispose(){
      if(disposed)return;disposed=true;ready=false;if(keepAlive!==null)window.cancelAnimationFrame(keepAlive);keepAlive=null;state=null;lastLines=null;lastTrack=null;preferences=lastPreferences=undefined;
      lastAnalysis=EMPTY_ANALYSIS;blockedAnalysis=undefined;
      fonts?.dispose();fonts=null;fontDocument=null;
      document.removeEventListener?.('visibilitychange',publish);
      window.removeEventListener('message',receive);frame.remove();
    },
  };
}
