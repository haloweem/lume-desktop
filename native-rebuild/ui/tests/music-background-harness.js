import {WallpaperCompositor} from '../music-background/reference/background.js';

class FakeMedia extends EventTarget {
  constructor(kind) { super(); this.kind=kind; this.src=''; this.crossOrigin=''; this.muted=false; this.loop=false; this.playsInline=false; this.preload='';
    this.videoWidth=640;this.videoHeight=360;this.naturalWidth=640;this.naturalHeight=360;this.readyState=0;this.paused=true;this.playCalls=0;this.pauseCalls=0; }
  play=async()=>{this.paused=false;this.playCalls++;};
  pause=()=>{this.paused=true;this.pauseCalls++;};
  load=()=>{};
  removeAttribute=name=>{if(name==='src')this.src='';};
}

export function backgroundFixture() {
  const media=[],frames=new Map(),events=[];let now=0,nextFrame=0;
  const makeContext=()=>({globalAlpha:1,fillStyle:'',draws:[],fills:[],clearRect(){},fillRect(...args){this.fills.push(args);},drawImage(...args){this.draws.push(args);},});
  const context=makeContext();
  const canvas=Object.assign(new EventTarget(),{width:800,height:500,dataset:{},getContext:()=>context});
  const createCanvas=()=>{
    const scratchContext=makeContext();
    return Object.assign(new EventTarget(),{width:800,height:500,getContext:()=>scratchContext});
  };
  const compositor=new WallpaperCompositor(canvas,{createMedia:kind=>{const value=new FakeMedia(kind);media.push(value);return value;},createCanvas,
    requestFrame:callback=>{frames.set(++nextFrame,callback);return nextFrame;},cancelFrame:id=>frames.delete(id),now:()=>now});
  return {canvas,context,media,frames,events,compositor,tick(ms=16){now+=ms;const pending=[...frames.values()];frames.clear();pending.forEach(callback=>callback(now));},
    ready(index,kind=media[index]?.kind){media[index].readyState=2;media[index].dispatchEvent(new Event(kind==='video'?'loadeddata':'load'));},
    fail(index){media[index].dispatchEvent(new Event('error'));}};
}

export {FakeMedia};
