import {mountSnowCanvas} from './reference/snow.js';
import {mountEffectCanvas} from './canvas-host.js';

export function mountSnow(host, state, options) {
  return mountEffectCanvas(host, 'snow', canvas => mountSnowCanvas(canvas, options), options);
}
