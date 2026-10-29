// The page's send() allowlist (ui/bridge.js) and the host's parser
// (core/ui/page_bridge.cpp) are written separately. This test reads the C++
// table and checks both directions: every message the host accepts can be
// sent by the page, and every message the page sends is accepted by the host.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createBridge} from '../bridge.js';
import {validRadialShortcut} from '../radial.js';

const source = readFileSync(new URL('../../core/ui/page_bridge.cpp', import.meta.url), 'utf8');
const exact = new Set([...source.matchAll(/\{\s*L"(v1\|[^"]*)"\s*,\s*PageCommand::\w+\s*\}/g)].map(m => m[1]));
const numeric = [...source.matchAll(/\{L"(v1\|[^"]*\|)",(\d+),(\d+),PageCommand::\w+\}/g)]
  .map(m => ({prefix: m[1], min: Number(m[2]), max: Number(m[3])}));
const delay = source.match(/delayPrefix = L"(v1\|[^"]*\|)";[\s\S]*?if \(delay < (\d+) \|\| delay > (\d+)\)/);
numeric.push({prefix: delay[1], min: Number(delay[2]), max: Number(delay[3])});
// Drops carry files and go through postMessageWithAdditionalObjects, not send().
const dropMessages = ['v1|wallpaper|dropFiles', 'v1|music|dropFiles'];

const hostAccepts = message => exact.has(message) || numeric.some(({prefix, min, max}) => {
  if (!message.startsWith(prefix)) return false;
  const digits = message.slice(prefix.length);
  if (!/^(0|[1-9]\d{0,3})$/.test(digits)) return false;
  const value = Number(digits);
  return value >= min && value <= max && (!prefix.startsWith('v1|radial|shortcut|') || hostRadialShortcut(value));
});

// The host's radial shortcut check, translated from its C++ body.
const radialBody = source.match(/bool ValidRadialShortcut\(int code\) noexcept \{([\s\S]*?)\n\}/)[1]
  .replace(/const (int|bool) /g, 'const ').replace('code / 256', 'Math.trunc(code / 256)');
const hostRadialShortcut = new Function('code', radialBody);

const pageSends = parts => {
  const sent = [];
  const ok = createBridge({postMessage: value => sent.push(value)}).send(...parts);
  return ok ? sent[0] : null;
};
const toParts = message => message.split('|').slice(1)
  .map(part => part === 'true' ? true : part === 'false' ? false : part);

test('the C++ table was read', () => {
  assert.ok(exact.size > 100, `exact commands: ${exact.size}`);
  assert.equal(numeric.length, 16);
  for (const message of dropMessages) assert.ok(exact.has(message), message);
});

test('every exact host command can be sent by the page', () => {
  for (const message of exact) {
    if (dropMessages.includes(message)) continue;
    assert.equal(pageSends(toParts(message)), message, message);
  }
});

test('numeric ranges agree at and beyond both ends', () => {
  for (const {prefix, min, max} of numeric) {
    // Per-style settings carry an extra word: v1|spectrum|blocks|barWidth|9.
    const words = prefix.split('|').slice(1, -1);
    if (words[0] === 'radial') continue;  // checked value by value below
    for (const value of [min, max]) assert.equal(pageSends([...words, value]), prefix + value, prefix + value);
    for (const value of [min - 1, max + 1]) assert.equal(pageSends([...words, value]), null, prefix + value);
  }
});

test('radial shortcuts agree for every code', () => {
  for (let code = -1; code <= 4096; ++code) {
    assert.equal(validRadialShortcut(code), code >= 1 && code <= 4095 && hostRadialShortcut(code), String(code));
    assert.equal(pageSends(['radial', 'shortcut', code]) !== null, hostAccepts(`v1|radial|shortcut|${code}`), String(code));
  }
});

test('everything the page sends is accepted by the host', () => {
  const words = new Set(), numbers = new Set([0, 1, -1]);
  for (const message of exact) for (const part of message.split('|').slice(1)) words.add(part);
  for (const {prefix, min, max} of numeric) {
    for (const part of prefix.split('|').slice(1, -1)) words.add(part);
    for (const value of [min - 1, min, min + 1, max - 1, max, max + 1]) numbers.add(value);
  }
  // Names the page knows that the host table may not mention.
  const page = readFileSync(new URL('../bridge.js', import.meta.url), 'utf8');
  const send = page.slice(page.indexOf('send(...parts)'), page.indexOf('subscribe(callback)'));
  for (const m of send.matchAll(/['"]([A-Za-z][\w-]*)['"]/g)) words.add(m[1]);
  for (const m of page.matchAll(/^const (?:styles|themes|materials|cities) = \[([^\]]*)\]/gm))
    for (const w of m[1].matchAll(/"([^"]+)"/g)) words.add(w[1]);
  words.add('solid');
  const kinds = [...words], values = [...words, true, false, ...numbers];
  let accepted = 0;
  for (const kind of kinds) {
    const single = pageSends([kind]);
    if (single !== null) { ++accepted; assert.ok(hostAccepts(single), single); }
    for (const action of kinds) {
      const pair = pageSends([kind, action]);
      if (pair !== null) { ++accepted; assert.ok(hostAccepts(pair), pair); }
      for (const value of values) {
        const triple = pageSends([kind, action, value]);
        if (triple !== null) { ++accepted; assert.ok(hostAccepts(triple), triple); }
      }
    }
  }
  assert.ok(accepted > exact.size, `accepted ${accepted}`);
});

// The settings pages carry a third copy of each range: the slider or number
// field bounds, the state validator and the dock spacing list. Each must
// match the host's range for the command it sends.
test('settings page controls use the host ranges', () => {
  const read = name => readFileSync(new URL(`../${name}`, import.meta.url), 'utf8');
  const host = Object.fromEntries(numeric.map(({prefix, min, max}) => [prefix, [min, max]]));
  const found = [];
  const expect = (where, prefix, min, max) => found.push({where, prefix, actual: [Number(min), Number(max)]});
  const dock = read('dock.js'), organizer = read('organizer.js'), minimize = read('minimize.js'),
    icons = read('desktop-icons.js'), app = read('app.js');
  const dockKey = {'dock-icon-scale': 'iconScale', 'dock-bottom-offset': 'bottomOffset', 'dock-icon-spacing': 'iconSpacing',
    'dock-launchpad-scale': 'launchpadScale', 'dock-launchpad-icon-scale': 'launchpadIconScale'};
  for (const m of dock.matchAll(/rangeControl\('([\w-]+)',t\([^)]*\),[^,]+,(\d+),(\d+),/g))
    expect(`dock.js slider ${m[1]}`, `v1|dock|${dockKey[m[1]]}|`, m[2], m[3]);
  for (const m of dock.matchAll(/syncRange\('([\w-]+)',[^,]+,(\d+),(\d+),/g))
    expect(`dock.js sync ${m[1]}`, `v1|dock|${dockKey[m[1]]}|`, m[2], m[3]);
  for (const m of dock.matchAll(/inRange\(message\.(\w+),(\d+),(\d+)\)/g))
    if (host[`v1|dock|${m[1]}|`]) expect(`dock.js state ${m[1]}`, `v1|dock|${m[1]}|`, m[2], m[3]);
  const spacing = dock.match(/dockIconSpacings=Array\.from\(\{length:(\d+)\},\(_,index\)=>(\d+)\+index\)/);
  expect('dock.js spacing list', 'v1|dock|iconSpacing|', spacing[2], Number(spacing[2]) + Number(spacing[1]) - 1);
  for (const m of organizer.matchAll(/ettingsRange\((?:root,)?'organizer-icon-scale',[^]*?\?\?\d+,(\d+),(\d+),/g))
    expect('organizer.js icon scale', 'v1|organizer|iconScale|', m[1], m[2]);
  for (const m of minimize.matchAll(/'(durationMs|unhideDurationMs)',(\d+),(\d+)\)/g))
    expect(`minimize.js ${m[1]}`, `v1|minimize|${m[1]}|`, m[2], m[3]);
  const appMinimize = app.match(/const min=key==='durationMs'\?(\d+):(\d+),max=key==='durationMs'\?(\d+):(\d+);/);
  expect('app.js durationMs', 'v1|minimize|durationMs|', appMinimize[1], appMinimize[3]);
  expect('app.js unhideDurationMs', 'v1|minimize|unhideDurationMs|', appMinimize[2], appMinimize[4]);
  const delay = icons.match(/id="icons-delay" type="number" min="(\d+)" max="(\d+)"/);
  expect('desktop-icons.js delay', 'v1|icons|delay|', delay[1], delay[2]);
  assert.ok(found.length >= 19, `controls found: ${found.length}`);
  for (const {where, prefix, actual} of found) assert.deepEqual(actual, host[prefix], `${where} (${prefix})`);
});
