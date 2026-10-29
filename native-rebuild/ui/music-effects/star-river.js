import {mountStarRiverCanvas} from './reference/star-river.js';
import {mountEffectCanvas} from './canvas-host.js';

export function mountStarRiver(host, state, options) {
  return mountEffectCanvas(host, 'star-river', canvas => mountStarRiverCanvas(canvas, state, options), options);
}
