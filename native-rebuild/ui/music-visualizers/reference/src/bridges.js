// Registration only: renderer-owned canvases, geometry, particles and shaders remain intact.
export const publishLiquidGlassWebGLSceneFrame=()=>{};
export const unregisterLiquidGlassWebGLSceneCanvas=()=>{};
export const getLumeEmbeddedTopInset=()=>0;
export const getLumeFullBleedLayerStyle=()=>({top:0,height:'100%'});
export const registerLumeTopVisualLayer=()=>({requestFrame(){},dispose(){}});
export const captureLumeTopVisualFrame=()=>false;
const translations={'ui.waitingForMusic':'\u7b49\u5f85\u97f3\u4e50'};
const t=key=>translations[key]??key;
export const useTranslation=()=>({t});
