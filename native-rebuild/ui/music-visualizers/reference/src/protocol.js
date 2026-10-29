import {musicPreferenceSnapshot} from '../../../music-preference-values.js';
export const preferenceSnapshot=musicPreferenceSnapshot;
export const MODES=Object.freeze(['classic','cadenza','partita','fume','tilt','monet','sonnet']);
const text=value=>typeof value==='string'?value:'';
const currentKeys=['id','source','title','artist','album','artwork'];
export const EMPTY_ANALYSIS=Object.freeze({energy:0,bins:Object.freeze(Array(32).fill(0))});
export function analysisSnapshot(previous,value) {
  if(value==null)return EMPTY_ANALYSIS;
  const unit=number=>Number.isFinite(number)&&number>=0&&number<=1;
  if(typeof value!=='object'||Array.isArray(value)||!unit(value.energy)||
    !Array.isArray(value.bins)||value.bins.length!==32||
    Array.from(value.bins).some(bin=>!unit(bin)))throw new TypeError('Invalid audio analysis');
  if(previous?.energy===value.energy&&value.bins.every((bin,index)=>bin===previous.bins[index]))return previous;
  return Object.freeze({energy:value.energy,bins:Object.freeze([...value.bins])});
}

export function applyAnalysis(analysis,audioPower,audioBands) {
  audioPower.set(analysis.energy*255);
  // Native bins have no sample-rate metadata: these are relative spectral
  // ranges, not reconstructed Hz bands or a new audio analyser.
  for(const [key,start,end] of [['bass',0,1],['lowMid',1,2],['mid',2,4],['vocal',4,8],['treble',8,32]]) {
    const values=analysis.bins.slice(start,end);
    audioBands[key].set(values.reduce((sum,value)=>sum+value,0)/values.length*255);
  }
  // Preserve all 32 measurements in the historical 128-byte input shape.
  // Repeating each bin adds no invented frequency detail.
  audioBands.spectrum.set(Uint8Array.from({length:128},(_,index)=>Math.round(analysis.bins[index>>>2]*255)));
}

export function acceptsMessage(event,parent,origin,mode) {
  return parent!==globalThis && event.source===parent && origin!=='null' && event.origin===origin &&
    event.data?.type==='lume-lyric-stage' && MODES.includes(event.data.mode) &&
    event.data.mode===mode && event.data.state!==null && typeof event.data.state==='object' &&
    !Array.isArray(event.data.state);
}

export function localArtwork(value,base) {
  if(typeof value!=='string'||!value||value.length>262144)return undefined;
  if(/^data:image\/(?:png|jpeg|webp|gif|avif);base64,[a-z\d+/=\s]+$/i.test(value))return value;
  try {
    const url=new URL(value,base);
    if(url.username||url.password)return undefined;
    if(url.origin===new URL(base).origin && ['http:','https:','blob:'].includes(url.protocol))return url.href;
    if(/^http:\/\/127\.0\.0\.1:[1-9]\d{0,4}\/[a-f0-9]{64}\/media\.png$/.test(value)&&Number(url.port)<=65535)return url.href;
    if(url.protocol==='https:'&&!url.port&&['p1.music.126.net','p2.music.126.net',
      'p3.music.126.net','p4.music.126.net','y.gtimg.cn','imge.kugou.com','imgessl.kugou.com'].includes(url.hostname))return url.href;
  }catch{}
  return undefined;
}

export function createStateReducer(prepareLines) {
  let linesKey='[]';
  let state={lines:[],current:null,currentTime:0,duration:0,activeLineIndex:-1,playing:false,visible:true,analysis:EMPTY_ANALYSIS};
  return {
    get state(){return state;},
    update(delta) {
      let structural=false;
      const next={...state};
      const preferences=preferenceSnapshot(state.preferences,delta.preferences);
      if(preferences!==state.preferences){next.preferences=preferences;structural=true;}
      let nextLinesKey=linesKey;
      if(delta.lines!==undefined) {
        if(delta.lines!==null&&!Array.isArray(delta.lines))throw new TypeError('Invalid lyric lines');
        const raw=delta.lines??[],key=JSON.stringify(raw);
        if(key!==linesKey){next.lines=prepareLines(raw);nextLinesKey=key;structural=true;}
      }
      if(delta.current!==undefined) {
        if(delta.current!==null&&(typeof delta.current!=='object'||Array.isArray(delta.current)))throw new TypeError('Invalid track');
        const current=delta.current===null?null:Object.fromEntries(currentKeys.map(key=>[key,text(delta.current[key])]));
        if((current===null)!==(state.current===null)||current&&currentKeys.some(key=>current[key]!==state.current?.[key])){
          next.current=current;structural=true;
        }
      }
      for(const key of ['currentTime','duration'])if(delta[key]!==undefined){
        if(!Number.isFinite(delta[key])||delta[key]<0)throw new TypeError(`Invalid ${key}`);
        next[key]=delta[key];
      }
      if(delta.activeLineIndex!==undefined){
        if(!Number.isInteger(delta.activeLineIndex)||delta.activeLineIndex< -1)throw new TypeError('Invalid active line');
        next.activeLineIndex=delta.activeLineIndex<next.lines.length?delta.activeLineIndex:-1;
        structural ||= next.activeLineIndex!==state.activeLineIndex;
      }
      for(const key of ['playing','visible'])if(delta[key]!==undefined){
        if(typeof delta[key]!=='boolean')throw new TypeError(`Invalid ${key}`);
        structural ||= next[key]!==delta[key];next[key]=delta[key];
      }
      const analysis=delta.analysis===undefined?state.analysis:analysisSnapshot(state.analysis,delta.analysis);
      const changedTrack=state.current!==null&&['id','source'].some(key=>state.current?.[key]!==next.current?.[key]);
      next.analysis=!next.current||!next.playing||!next.visible||changedTrack?EMPTY_ANALYSIS:analysis;
      linesKey=nextLinesKey;state=next;
      return {state,structural};
    },
  };
}
