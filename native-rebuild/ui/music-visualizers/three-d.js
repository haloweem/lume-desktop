import {Mineradio3DStageRuntime} from './vendor/mineradio-stage.js';
import {localArtwork,preferenceSnapshot} from './reference/preference-protocol.js';
import {resolvePreferenceTheme,resolveThemeFontStack} from './reference/preference-theme.js';
import {registerMusicMaterialSceneSource} from '../music-material/registry.js';

// Owns one local Three stage. Playback and lyric parsing stay with the caller.
const defaultTheme = resolvePreferenceTheme('3d');
const emptyLines = Object.freeze([]);

/**
 * Mount a transparent lyric canvas in a sized host.
 * Times are seconds. Replace lines when lyric text or timings change.
 * Current artwork colors the optional star river in the shared scene.
 * @returns {{update(state: object): void, dispose(): void}}
 */
export function mountThreeD(host, state) {
  const document = host.ownerDocument;
  let latest = state;
  let preferences,theme=defaultTheme;
  const toStageState = value => {
    const nextPreferences=preferenceSnapshot(preferences,value.preferences);
    if(nextPreferences!==preferences) {
      const nextTheme=resolvePreferenceTheme('3d',nextPreferences);
      // The original 3D canvas accepts CSS strings directly; resolve quoting before passing a family.
      theme=Object.freeze({...nextTheme,fontFamily:nextPreferences.fontFamily?resolveThemeFontStack(nextTheme):undefined});
      preferences=nextPreferences;
    }
    return {
      lines: value.lines ?? emptyLines,
      activeLineIndex: value.activeLineIndex ?? -1,
      currentTime: value.currentTime ?? 0,
      paused: !value.playing,
      visible: value.visible !== false && !document.hidden,
      theme,
      showTranslation: true,
      visualizerEffectMode: preferences?.effectMode === 'star-river' ? 'star-river' : 'none',
      coverUrl: localArtwork(value.current?.artwork,document.baseURI),
    };
  };
  let runtime = new Mineradio3DStageRuntime(host, toStageState(state));
  const releaseScene=registerMusicMaterialSceneSource(runtime.getCanvas(),{
    refresh:()=>runtime?.refreshSceneFrame(),
  });
  const update = next => {
    if (!runtime) return;
    const stageState = toStageState(next);
    latest = next;
    runtime.setVisible(stageState.visible);
    if (stageState.visible) runtime.update(stageState);
  };
  const visibilityChanged = () => update(latest);
  document.addEventListener('visibilitychange', visibilityChanged);
  return {
    update,
    dispose() {
      if (!runtime) return;
      document.removeEventListener('visibilitychange', visibilityChanged);
      releaseScene();
      runtime.destroy();
      runtime = null;
      latest = null;
      preferences=undefined;theme=defaultTheme;
    },
  };
}
