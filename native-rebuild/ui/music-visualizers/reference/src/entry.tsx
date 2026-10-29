import React,{useEffect,useMemo} from 'react';
import {createRoot} from 'react-dom/client';
import {flushSync} from 'react-dom';
import {motionValue} from 'framer-motion';
import {MODES,acceptsMessage,createStateReducer,localArtwork,EMPTY_ANALYSIS,applyAnalysis} from './protocol.js';
import {prepareLines} from 'reference:defaults';
import {resolvePreferenceTheme} from 'reference:preference-theme';
import {loaders} from 'reference:modes';

const mode=location.hash.slice(1);
const host=document.getElementById('reference-stage')!;
const origin=location.origin;
const clock=motionValue(0),audioPower=motionValue(0);
const audioBands=Object.fromEntries(['bass','lowMid','mid','vocal','treble'].map(key=>[key,motionValue(0)]));
audioBands.spectrum=motionValue(new Uint8Array(128));
const reducer=createStateReducer(prepareLines);
const background={shellOwnsWallpaper:true};
let root:any=null,Renderer:any=null,loading:Promise<void>|null=null,disposed=false,failed=false;
let renders=0;
let lastAnalysis=EMPTY_ANALYSIS;

function updateAudio() {
  const state=reducer.state;
  const analysis=!disposed&&!failed&&state.playing&&state.visible&&!document.hidden?state.analysis:EMPTY_ANALYSIS;
  if(analysis===lastAnalysis)return;
  applyAnalysis(analysis,audioPower,audioBands);
  lastAnalysis=analysis;
}

function reportError(error:unknown) {
  if(disposed||failed)return;
  failed=true;host.dataset.referenceState='error';
  updateAudio();
  console.error('[Lume reference]',error);
  if(parent!==window)parent.postMessage({type:'lume-lyric-stage-error'},origin);
}
class Boundary extends React.Component<any,{failed:boolean}> {
  state={failed:false};
  static getDerivedStateFromError(){return {failed:true};}
  componentDidCatch(error:unknown){reportError(error);}
  render(){return this.state.failed?null:this.props.children;}
}

function Stage({state}:any) {
  const theme=useMemo(()=>resolvePreferenceTheme(mode,state.preferences),[state.preferences]);
  useEffect(()=>{
    // Historical Word components subscribe after mounting. dirty publishes even an unchanged time,
    // so mounting in the middle of a paused line does not leave them in their invisible waiting pose.
    clock.dirty();
  },[state.lines,state.activeLineIndex,state.current,state.visible,theme]);
  const current=state.current;
  return <Renderer currentTime={clock} currentLineIndex={state.activeLineIndex} lines={state.lines}
    theme={theme} audioPower={audioPower} audioBands={audioBands}
    songTitle={current?.title} songArtist={current?.artist} songAlbum={current?.album}
    coverUrl={localArtwork(current?.artwork,new URL('../../music.html',location.href).href)}
    seed={current?.id||current?.source||current?.title||`geometry-${mode}`}
    paused={!state.playing} staticMode={false} isPreviewMode={false} background={background}
    showText={true} lyricsFontScale={1} subtitleFontScale={1} subtitleContentMode="translation" />;
}

function render() {
  if(disposed||failed||!Renderer)return;
  const state=reducer.state,visible=state.visible&&!document.hidden;
  host.dataset.referenceMode=mode;
  host.dataset.referenceState=visible?'ready':'hidden';
  host.dataset.referenceRenders=String(++renders);
  host.dataset.referenceLine=String(state.activeLineIndex);
  flushSync(()=>root.render(visible?<Boundary><Stage key={state.current?.id||state.current?.source||''} state={state}/></Boundary>:null));
}

async function load() {
  const module=await loaders[mode]();
  if(disposed)return;
  Renderer=module.default;
  root=createRoot(host,{onUncaughtError:reportError});
  render();
}
function receive(event:MessageEvent) {
  if(disposed||failed||!acceptsMessage(event,parent,origin,mode))return;
  try {
    const delta=event.data.state;
    const {state,structural}=reducer.update(document.hidden?{...delta,analysis:null}:delta);
    updateAudio();
    clock.set(state.currentTime);
    if(!loading)loading=load().catch(reportError);
    else if(structural)render();
  }catch(error){reportError(error);}
}
function visibilityChanged() {
  if(disposed)return;
  reducer.update({analysis:null});
  updateAudio();
  render();
}
function forwardKey(event:KeyboardEvent) {
  if(disposed||event.defaultPrevented||event.isComposing||parent===window||origin==='null'||!MODES.includes(mode))return;
  const target=event.composedPath().find(item=>item instanceof Element) as Element|undefined;
  if(document.designMode==='on'||target?.closest('input,textarea,select')||
    target instanceof HTMLElement&&target.isContentEditable)return;
  const {key,repeat,ctrlKey,metaKey,altKey,shiftKey}=event;
  const space=key===' '||key==='Spacebar';
  if(space&&target?.closest('button,a[href],[role="button"],[role="link"]'))return;
  if(!space&&!['ArrowLeft','ArrowRight','Escape'].includes(key)&&!((ctrlKey||metaKey)&&key.toLowerCase()==='k'))return;
  if(event.type==='keydown')event.preventDefault();
  parent.postMessage({type:'lume-lyric-stage-key',mode,eventType:event.type,key,repeat,ctrlKey,metaKey,altKey,shiftKey},origin);
}
function dispose() {
  if(disposed)return;
  disposed=true;
  updateAudio();
  window.removeEventListener('message',receive);
  window.removeEventListener('keydown',forwardKey);
  window.removeEventListener('keyup',forwardKey);
  document.removeEventListener('visibilitychange',visibilityChanged);
  root?.unmount();
  clock.destroy();audioPower.destroy();Object.values(audioBands).forEach(value=>value.destroy());
}
window.addEventListener('message',receive);
window.addEventListener('keydown',forwardKey);
window.addEventListener('keyup',forwardKey);
window.addEventListener('pagehide',dispose,{once:true});
window.addEventListener('error',event=>reportError(event.error??event.message));
window.addEventListener('unhandledrejection',event=>reportError(event.reason));
window.addEventListener('lume-reference-renderer-error',()=>reportError('Renderer initialization failed'));
document.addEventListener('visibilitychange',visibilityChanged);
if(MODES.includes(mode)&&parent!==window&&origin!=='null')parent.postMessage({type:'lume-lyric-stage-ready'},origin);
else reportError('Reference stage requires a supported hash and a same-origin parent');
