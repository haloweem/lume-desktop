import nodeTest from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createMusicRuntime} from '../music-entry.js';
import {initialMusicState, normalizeMusicState} from '../music-runtime-state.js';

const jsdomPath = process.env.LUME_JSDOM_PATH;
const {JSDOM} = jsdomPath ? createRequire(import.meta.url)(jsdomPath) : {};
const test = (name, run) => nodeTest(name, {skip:!jsdomPath}, run);

const track = id => ({id, title:id, artist:'', album:'', source:`online:${id}`, artwork:'', duration:200,
  vipRequired:false, membershipBadge:''});
const snapshot = (session, id) => normalizeMusicState({
  version:1, type:'music-state', status:'playing', queue:[track(id)], current:track(id), currentIndex:0,
  timeline:{position:10, duration:200}, playing:true, repeat:'none', shuffle:false, session, revision:session,
  capabilities:{canToggle:true, canPrevious:true, canNext:true, canSeek:true},
  pageOpen:true, pageVisible:true, account:{signedIn:false}, error:''}, initialMusicState());

// Every new queue bumps the host session. The view must keep sending commands
// for the newest session instead of the one it was mounted with.
test('music commands follow the newest session after a new song starts', () => {
  const dom = new JSDOM('<main id="content"></main>', {url:'https://lume.local/', pretendToBeVisual:true});
  // jsdom has no canvas; backgrounds and visualizers only need a no-op context.
  const inert = new Proxy(function(){}, {get:(target, key) => key === Symbol.toPrimitive ? () => 0 : inert, apply:() => inert, construct:() => inert});
  dom.window.HTMLCanvasElement.prototype.getContext = () => inert;
  const keys = ['window','document','HTMLElement','Node','Event','CustomEvent','KeyboardEvent','PointerEvent','MouseEvent','getComputedStyle','requestAnimationFrame','cancelAnimationFrame'];
  const previous = new Map(keys.map(key => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
  for (const key of keys)
    if (dom.window[key] !== undefined) globalThis[key] = key === 'window' ? dom.window : dom.window[key];
  globalThis.requestAnimationFrame ??= callback => setTimeout(callback, 0);
  globalThis.cancelAnimationFrame ??= handle => clearTimeout(handle);
  const sent = [];
  const accept = () => true;
  const bridge = {
    music:(action, fields) => { sent.push({action, ...fields}); return true; },
    musicBackground:accept, musicDesktop:accept, musicDropFiles:accept, musicFontAssets:accept, musicFonts:accept,
    musicLibrary:accept, musicOnline:accept, musicPreferences:accept, musicProviderAccount:accept,
    subscribe:() => () => {},
  };
  const notices = [];
  const runtime = createMusicRuntime({bridge, content:dom.window.document.getElementById('content'),
    showNotice:text => notices.push(text), translate:value => value});
  try {
    runtime.render(snapshot(1, 'qq:first'));
    runtime.updateMusicState(snapshot(2, 'qq:second'));
    dom.window.document.querySelector('[data-music-open-player]')?.click();
    const toggle = dom.window.document.querySelector('[data-music-action="toggle"]');
    if (!toggle) console.log('actions:', [...dom.window.document.querySelectorAll('[data-music-action],[data-music-open-player]')].map(e => e.dataset.musicAction || 'open-player').join(','));
    assert.ok(toggle, 'the player toggle is rendered');
    toggle.dispatchEvent(new dom.window.MouseEvent('click', {bubbles:true}));
    const command = sent.find(item => ['toggle','pause','play'].includes(item.action));
    if (!command) console.log('sent:', JSON.stringify(sent));
    assert.ok(command, `toggle reaches the host (notices: ${JSON.stringify(notices)})`);
    assert.equal(command.session, 2, 'the command carries the current session');
    assert.equal(notices.includes('音乐操作暂不可用'), false, 'no "unavailable" notice while playback works');
  } finally {
    runtime.dispose?.();
    for (const [key, descriptor] of previous) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else delete globalThis[key];
    }
  }
});
