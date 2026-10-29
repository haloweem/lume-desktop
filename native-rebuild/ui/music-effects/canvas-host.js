// Only the effect canvas is owned here. Never mutate or remove the caller's scene canvas.
export function mountEffectCanvas(host, mode, start, {onError} = {}) {
  const canvas = host.ownerDocument.createElement('canvas');
  canvas.dataset.visualizerEffect = mode;
  canvas.setAttribute('aria-hidden', 'true');
  Object.assign(canvas.style, {position: 'absolute', inset: '0', width: '100%', height: '100%', pointerEvents: 'none'});
  if (mode === 'snow') canvas.style.mixBlendMode = 'screen';
  let runtime = null, disposed = false;
  const lost = event => {
    event.preventDefault();
    if (!disposed) {
      dispose();
      onError?.(new Error(`${mode} WebGL context lost`));
    }
  };
  canvas.addEventListener('webglcontextlost', lost);
  host.append(canvas);
  function dispose() {
    if (disposed) return;
    disposed = true;
    canvas.removeEventListener('webglcontextlost', lost);
    try { runtime?.dispose(); }
    finally { canvas.remove(); canvas.width = canvas.height = 1; }
  }
  try {
    runtime = start(canvas);
    return {update: state => {if (!disposed) runtime.update(state);}, dispose};
  } catch (error) { dispose(); throw error; }
}
