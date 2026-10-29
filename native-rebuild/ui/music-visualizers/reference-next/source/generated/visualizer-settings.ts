import {DEFAULT_SMART_BEAT_SETTINGS} from 'H:/refactor/workspace/lume1.0.7/build-artifacts/folia-upstream/src/types.ts';
// Lume has no Folia visualizer settings store; renderers see the default settings.
const state = {smartBeat: DEFAULT_SMART_BEAT_SETTINGS};
export const useVisualizerSettingsStore = selector => selector ? selector(state) : state;
useVisualizerSettingsStore.getState = () => state;
useVisualizerSettingsStore.subscribe = () => () => {};