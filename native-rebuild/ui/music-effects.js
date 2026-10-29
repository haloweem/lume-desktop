// Effect ownership only: callers own scene decoding, playback, preferences and layering.
async function loadEffect(mode, host, {signal}) {
  if (mode === 'snow') return (await import('./music-effects/snow.js')).mountSnow;
  if (mode === 'star-river') return (await import('./music-effects/star-river.js')).mountStarRiver;
  const {loadRainWindow} = await import('./music-effects/rain-window.js');
  signal.throwIfAborted();
  return loadRainWindow(host.ownerDocument, signal);
}

export function mountMusicEffects(host, {onError, load = loadEffect} = {}) {
  const document = host.ownerDocument;
  let state = null, runtime = null, pending = null, controller = null;
  let mode = 'none', background = null, generation = 0, disposed = false;
  let failed = false, sceneKey = [];
  host.dataset.effectMode = 'none';
  host.dataset.effectState = 'idle';
  const visible = () => state?.visible === true && !document.hidden;
  const report = error => { try { onError?.(error); } catch { /* Keep the owner usable. */ } };
  function clear() {
    generation++;
    controller?.abort();
    controller = null;
    pending = null;
    const previous = runtime;
    runtime = null;
    try { previous?.dispose(); } catch (error) { report(error); }
    host.replaceChildren();
  }
  function selection() {
    if (!visible()) return 'none';
    if (state.mode === 'star-river') return state.visualizer === '3d' ? 'none' : 'star-river';
    if (state.mode === 'snow') return 'snow';
    if (state.mode === 'rain-window' && state.scene?.canvas?.width > 0 && state.scene.canvas.height > 0) return 'rain-window';
    return 'none';
  }
  const api = {
    async update(next) {
      if (disposed) return;
      state = next;
      const selected = selection();
      const canvas = selected === 'rain-window' ? state.scene.canvas : null;
      const nextSceneKey = [state?.scene?.canvas, state?.scene?.canvas?.width, state?.scene?.canvas?.height,
        state?.scene?.mediaSource, state?.scene?.media?.url, state?.scene?.media?.kind];
      const sceneChanged = nextSceneKey.some((value, index) => value !== sceneKey[index]);
      sceneKey = nextSceneKey;
      if (selected !== mode || canvas !== background || (failed && sceneChanged)) {
        clear();
        failed = false;
        mode = selected;
        background = canvas;
      }
      host.dataset.effectMode = mode;
      if (mode === 'none') { host.dataset.effectState = 'idle'; return; }
      if (failed) return;
      if (runtime) {
        try { runtime.update(state); } catch (error) {
          clear(); failed = true; host.dataset.effectState = 'error'; report(error);
        }
        return;
      }
      if (pending) return pending;
      host.dataset.effectState = 'loading';
      const ticket = ++generation;
      controller = new AbortController();
      const signal = controller.signal;
      const fail = error => {
        if (disposed || ticket !== generation) return;
        clear();
        failed = true;
        host.dataset.effectState = 'error';
        report(error);
      };
      pending = (async () => {
        try {
          const mount = await load(mode, host, {signal});
          if (disposed || ticket !== generation || !visible()) return;
          const created = mount(host, state, {onError: fail});
          if (disposed || ticket !== generation) created?.dispose();
          else {
            runtime = created;
            host.dataset.effectState = 'ready';
          }
        } catch (error) { fail(error); }
        finally { if (ticket === generation) pending = null; }
      })();
      return pending;
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      clear();
      state = null;
      background = null;
      sceneKey = [];
      host.dataset.effectMode = 'none';
      host.dataset.effectState = 'disposed';
      document.removeEventListener('visibilitychange', visibilityChanged);
    },
  };
  function visibilityChanged() { if (state) return api.update(state); }
  document.addEventListener('visibilitychange', visibilityChanged);
  return api;
}
