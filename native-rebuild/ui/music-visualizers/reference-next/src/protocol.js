// The newer Folia renderers share the reference stage protocol; only the mode list differs.
export {preferenceSnapshot,EMPTY_ANALYSIS,analysisSnapshot,applyAnalysis,localArtwork,createStateReducer} from '../../reference/src/protocol.js';
export const MODES=Object.freeze(['cappella','diorama','pendolo','tempera']);

export function acceptsMessage(event,parent,origin,mode) {
  return parent!==globalThis && event.source===parent && origin!=='null' && event.origin===origin &&
    event.data?.type==='lume-lyric-stage' && MODES.includes(event.data.mode) &&
    event.data.mode===mode && event.data.state!==null && typeof event.data.state==='object' &&
    !Array.isArray(event.data.state);
}
