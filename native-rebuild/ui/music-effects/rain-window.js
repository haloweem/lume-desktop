import createRainWindowEffect, {RAIN_WINDOW_SETTINGS} from './reference/rain-window.js';
import {mountEffectCanvas} from './canvas-host.js';

function loadImage(document, name, signal) {
  return new Promise((resolve, reject) => {
    const image = document.createElement('img');
    const finish = error => {
      signal.removeEventListener('abort', abort);
      image.onload = image.onerror = null;
      if (error) { image.removeAttribute('src'); reject(error); }
      else resolve(image);
    };
    const abort = () => finish(signal.reason || new Error('Rain assets aborted'));
    if (signal.aborted) { abort(); return; }
    signal.addEventListener('abort', abort, {once: true});
    image.onload = () => finish();
    image.onerror = () => finish(new Error(`Rain Window asset unavailable: ${name}`));
    image.src = new URL(`./reference/assets/${name}`, import.meta.url).href;
  });
}

export async function loadRainWindow(document, signal) {
  const [dropColor, dropAlpha] = await Promise.all([
    loadImage(document, 'rain-window-drop-color.png', signal),
    loadImage(document, 'rain-window-drop-alpha.png', signal),
  ]);
  signal.throwIfAborted();
  return (host, state, options) => mountEffectCanvas(host, 'rain-window', canvas => {
    const release = createRainWindowEffect(canvas, {
      background: state.scene.canvas, dropColor, dropAlpha, onError: options?.onError,
    }, RAIN_WINDOW_SETTINGS);
    if (!release) throw new Error('Rain Window could not initialize');
    return {update() {}, dispose: release};
  }, options);
}
