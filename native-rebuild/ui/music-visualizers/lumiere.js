import {LumierePixiRuntime,compileLumiereProgram,DEFAULT_LUMIERE_TUNING,DEFAULT_THEME,prepareLumiereLines} from './lumiere/runtime.js';
import {mountLumiereSubtitles} from './lumiere-subtitles.js';
import {createLumiereSongGate} from './lumiere-song.js';
import {analysisSnapshot,EMPTY_ANALYSIS,applyAnalysis} from './reference/preference-protocol.js';
import {musicPreferenceSnapshot as preferenceSnapshot} from '../music-preference-values.js';
import {registerMusicMaterialSceneSource} from '../music-material/registry.js';

// Only .get/.set are consumed by the original runtime; the native playback
// projection remains the clock owner, with no second audio player or timer.
const value=initial=>({current:initial,get(){return this.current;},set(next){this.current=next;}});
export const lumiereTheme=preferences=>({...DEFAULT_THEME,
  ...(preferences?.fontFamily?{fontFamily:preferences.fontFamily}:{}),
  animationIntensity:preferences?.animationIntensity||DEFAULT_THEME.animationIntensity});
export function mountLumiere(host,initial,{onError}={}){
  const document=host.ownerDocument;
  const container=document.createElement('div');
  Object.assign(container.style,{position:'absolute',inset:'0',pointerEvents:'none',overflow:'hidden'});
  container.dataset.lumiereState='loading';host.append(container);
  const subtitles=mountLumiereSubtitles(host);
  const clock=value(0),power=value(0);
  const bands=Object.fromEntries(['bass','lowMid','mid','vocal','treble'].map(key=>[key,value(0)]));
  bands.spectrum=value(new Uint8Array(128));
  let latest=initial,disposed=false,runtime=null,pending=null,controller=null,releaseScene=null;
  let lines=null,prepared=[],seed=null,duration=null,program=null,preferences,theme,song,appliedSong=null,paused=true,failed=false;
  let subtitleSource=null,subtitleLines=[];
  const gate=createLumiereSongGate(document.defaultView,()=>{if(!disposed)update(latest);});
  const visible=()=>latest.visible!==false&&!document.hidden;
  function release(){
    gate.pause();
    controller?.abort();controller=null;pending=null;
    releaseScene?.();releaseScene=null;runtime?.destroy();runtime=null;appliedSong=null;
    container.replaceChildren();container.dataset.lumiereState='hidden';
    subtitles.hide();
  }
  function project(){
    clock.set(latest.currentTime||0);
    applyAnalysis(latest.playing&&visible()?analysisSnapshot(undefined,latest.analysis):EMPTY_ANALYSIS,power,bands);
    const nextPreferences=preferenceSnapshot(preferences,latest.preferences);
    let changed=false;
    if(!theme||nextPreferences?.fontFamily!==preferences?.fontFamily||
       nextPreferences?.animationIntensity!==preferences?.animationIntensity){
      theme=lumiereTheme(nextPreferences);
      changed=true;
    }
    preferences=nextPreferences;
    const key=latest.current?.id||latest.current?.source||'lumiere';
    const committed=gate.resolve(key,latest.lines||[],latest.currentTime||0);
    if(!program||lines!==committed.lines||seed!==committed.seed||duration!==latest.duration){
      lines=committed.lines;seed=committed.seed;duration=latest.duration;
      prepared=prepareLumiereLines(lines||[]);
      program=compileLumiereProgram(prepared,seed,{},
        {duration,seamless:DEFAULT_LUMIERE_TUNING.seamlessTransitions});changed=true;
    }
    if(changed)song={seed,program,theme};
    if(subtitleSource!==latest.lines){
      subtitleSource=latest.lines;
      subtitleLines=subtitleSource===lines?prepared:prepareLumiereLines(subtitleSource||[]);
    }
    subtitles.update(subtitleLines,latest.currentTime||0,theme);
  }
  function update(next){
    if(disposed)return;
    latest=next;
    if(!visible()){release();return;}
    if(failed)return;
    try{
      project();
      if(runtime){
        const nextPaused=!latest.playing;
        if(paused!==nextPaused){paused=nextPaused;runtime.setPaused(paused);}
        runtime.setSongMetadata({title:latest.current?.title,artist:latest.current?.artist,album:latest.current?.album});
        if(appliedSong!==song){appliedSong=song;void runtime.swapSong(song,controller?.signal).catch(fail);}
        if(paused)runtime.renderOnce();
        return;
      }
      if(pending)return;
      const token=new AbortController();controller=token;
      paused=!latest.playing;const startingSong=song;
      pending=LumierePixiRuntime.create({host:container,song:startingSong,tuning:DEFAULT_LUMIERE_TUNING,
        currentTime:clock,audioPower:power,audioBands:bands,staticMode:false,showText:true,paused,
        metadata:{title:latest.current?.title,artist:latest.current?.artist,album:latest.current?.album},signal:token.signal})
        .then(owner=>{
          if(disposed||token.signal.aborted||controller!==token){owner.destroy();return;}
          runtime=owner;appliedSong=startingSong;
          const canvas=container.querySelector('canvas');
          canvas.dataset.lumenicLiquidGlassSceneLyrics='';
          releaseScene=registerMusicMaterialSceneSource(canvas,{refresh:()=>runtime?.renderOnce()});
          container.dataset.lumiereState='ready';update(latest);
        }).catch(error=>{if(!token.signal.aborted&&!disposed)fail(error);})
        .finally(()=>{if(controller===token)pending=null;});
    }catch(error){fail(error);}
  }
  function fail(error){
    if(disposed||failed)return;failed=true;release();container.dataset.lumiereState='error';
    container.textContent='绘光暂不可用';onError?.(error);
  }
  const visibilityChanged=()=>update(latest);
  const resize=()=>{if(!disposed&&runtime&&visible()){runtime.resize();subtitles.position();}};
  document.defaultView.addEventListener('resize',resize);
  document.addEventListener('visibilitychange',visibilityChanged);update(initial);
  return {update,dispose(){if(disposed)return;disposed=true;release();gate.dispose();subtitles.dispose();container.remove();
    document.removeEventListener('visibilitychange',visibilityChanged);
    document.defaultView.removeEventListener('resize',resize);latest=null;lines=null;subtitleSource=null;
    prepared=[];subtitleLines=[];program=null;song=null;}};
}
