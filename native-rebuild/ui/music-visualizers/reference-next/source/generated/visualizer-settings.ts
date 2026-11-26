import {DEFAULT_SMART_BEAT_SETTINGS} from 'H:/refactor/workspace/lume1.0.7/build-artifacts/folia-lyrics-snapshot-0721/src/types.ts';
// Lume has no Folia visualizer settings store; renderers see the default settings.
const state = {smartBeat: DEFAULT_SMART_BEAT_SETTINGS, lyricRenderQuality:{scale:1,antialias:'auto'}};
export const useVisualizerSettingsStore = selector => selector ? selector(state) : state;
useVisualizerSettingsStore.getState = () => state;
useVisualizerSettingsStore.subscribe = () => () => {};