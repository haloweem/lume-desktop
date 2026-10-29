// Producer-only boundary: no shaders, renderer, DOM globals or playback imports.
const providers=new WeakMap();

export function registerMusicMaterialSceneSource(canvas,{refresh}={}) {
  if(!canvas?.getContext||typeof refresh!=='function')throw new TypeError('Canvas and synchronous refresh required');
  const entry={refresh};
  providers.set(canvas,entry);
  return ()=>{if(providers.get(canvas)===entry)providers.delete(canvas);};
}

// A canvas in a lyric stage iframe lives in another realm and cannot reach this
// registry; it carries its synchronous redraw as lumeMaterialSceneRefresh instead.
export const musicMaterialSceneSource=canvas=>providers.get(canvas)??
  (typeof canvas?.lumeMaterialSceneRefresh==='function'?{refresh:canvas.lumeMaterialSceneRefresh}:undefined);
