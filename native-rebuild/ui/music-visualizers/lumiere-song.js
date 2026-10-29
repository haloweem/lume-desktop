import {getLyricsSignature,decideSongCommit,isInstrumentalConfirmed} from './lumiere/runtime.js';

// Folia's useVisualizerSongCommit decision, driven by Lume's playback snapshots
// and one bounded deadline instead of a second per-frame React/RAF clock.
export function createLumiereSongGate(window,onReady){
  let committed=null,watch=null,timer=null,cachedLines=null,cachedSignature='',disposed=false;
  const cancel=()=>{if(timer!==null)window.clearTimeout(timer);timer=null;watch=null;};
  return {
    resolve(seed,lines,time){
      if(disposed)return committed;
      if(!committed)committed={seed,lines,isInstrumental:false,signature:getLyricsSignature(lines)};
      if(cachedLines!==lines){cachedLines=lines;cachedSignature=getLyricsSignature(lines);}
      const decision=decideSongCommit({seed,committedSeed:committed.seed,lyricsSignature:cachedSignature,
        committedSignature:committed.signature,isCommittedInstrumental:committed.isInstrumental});
      if(decision.action==='commit'){
        cancel();committed={seed,lines,isInstrumental:false,signature:cachedSignature};
      }else if(decision.action==='idle')cancel();
      else{
        if(!watch||watch.seed!==seed||watch.signature!==cachedSignature){
          cancel();watch={seed,signature:cachedSignature,started:window.performance.now(),reset:false};
          timer=window.setTimeout(()=>{
            timer=null;if(disposed||!watch)return;
            committed={seed:watch.seed,lines:[],isInstrumental:true,signature:''};watch=null;onReady();
          },3000);
        }
        if(time<1)watch.reset=true;
        if(isInstrumentalConfirmed({sawPlaybackReset:watch.reset,playbackTime:time,
          elapsedMs:window.performance.now()-watch.started},{instrumentalCommitSeconds:2,readyGraceMs:3000})){
          cancel();committed={seed,lines:[],isInstrumental:true,signature:''};
        }
      }
      return committed;
    },
    pause:cancel,
    dispose(){disposed=true;cancel();committed=null;cachedLines=null;}
  };
}
