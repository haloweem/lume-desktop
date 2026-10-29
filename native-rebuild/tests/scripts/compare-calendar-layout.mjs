// Compare original browser geometry with the actual native renderer's layout.
// Usage: node compare-calendar-layout.mjs <reference.json> <native.json> <report.json>
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

const [referenceFile, nativeFile, reportFile] = process.argv.slice(2);
assert(referenceFile && nativeFile && reportFile, 'Pass reference, native and report paths');
const reference = JSON.parse(fs.readFileSync(referenceFile, 'utf8'));
const native = JSON.parse(fs.readFileSync(nativeFile, 'utf8'));
assert.equal(native.fixedDate, reference.fixedDate, 'Compare the same fixed date');
const dimensions = [[820, 500], [320, 320], [480, 390], [600, 420], [1440, 720]];
const expected = ['spacious', 'split'].flatMap(theme => dimensions.map(([w, h]) => `${theme}-${w}x${h}`));
const key = item => `${item.theme}-${item.width}x${item.height}`;
const index = (data, label) => {
  assert(Array.isArray(data.results), `${label}: results array missing`);
  const result = new Map(data.results.map(item => [key(item), item]));
  assert.equal(result.size, data.results.length, `${label}: duplicate layout`);
  assert.deepEqual([...result.keys()].sort(), [...expected].sort(), `${label}: required layout matrix`);
  return result;
};
const original = index(reference, 'original');
const current = index(native, 'native');
const selectors = ['#calendar', '.calendar-header', '#previous', '#month', '#next',
  '.weekdays', '#days', '#calendar-summary', '#month-picker'];
const toleranceDip = 0.15; // CSS subpixel rounding and DirectWrite text metrics.
const rows = [], failures = [];
let maxDeltaDip = 0, measurements = 0;
for (const name of expected) {
  const before = original.get(name), after = current.get(name);
  for (const selector of selectors) {
    if (selector === '#calendar-summary' && before.theme === 'spacious') continue;
    const a = selector === '#month-picker' ? before.picker : before.selectors[selector];
    const b = after.selectors[selector];
    assert(a && b, `${name}: missing ${selector}`);
    const delta = {};
    for (const property of ['x', 'y', 'width', 'height']) {
      assert(Number.isFinite(a[property]) && Number.isFinite(b[property]), `${name}: non-finite ${selector}.${property}`);
      const difference = Math.abs(a[property] - b[property]);
      delta[property] = difference;
      maxDeltaDip = Math.max(maxDeltaDip, difference);
      ++measurements;
      if (difference > toleranceDip) failures.push(`${name} ${selector}.${property}: original=${a[property]}, native=${b[property]}`);
    }
    rows.push({layout: name, selector, original: a, native: b, delta});
  }
}
const report = {
  scope: 'DIP rectangles only; not text rasterization, lunar content or glass acceptance',
  fixedDate: reference.fixedDate,
  referenceFile: path.resolve(referenceFile), nativeFile: path.resolve(nativeFile),
  sourceHashes: reference.sourceHashes, toleranceDip, maxDeltaDip, measurements,
  passed: failures.length === 0, failures, rows,
};
fs.mkdirSync(path.dirname(path.resolve(reportFile)), {recursive: true});
fs.writeFileSync(reportFile, JSON.stringify(report, null, 2));
assert.deepEqual(failures, [], 'Native calendar geometry differs from the original browser');
console.log(`CALENDAR_LAYOUT_COMPARISON_PASS: ${expected.length} layouts, ${measurements} measurements, max delta ${maxDeltaDip.toFixed(4)} DIP`);
