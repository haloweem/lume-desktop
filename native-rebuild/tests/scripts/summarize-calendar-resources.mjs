// Summarize explicit native probe observations without imposing arbitrary budgets.
// Usage: node summarize-calendar-resources.mjs <samples.jsonl> <summary.json>
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const [input, output] = process.argv.slice(2);
assert(input && output, 'Pass samples and output paths');
const rows = fs.readFileSync(input, 'utf8').trim().split(/\r?\n/).map(line => JSON.parse(line));
const byPhase = phase => rows.filter(row => row.phase === phase);
for (const row of rows) {
  for (const key of ['pid', 'tickMs', 'privateBytes', 'workingSetBytes', 'handles', 'gdi', 'user', 'cpu100ns', 'children']) {
    assert(Number.isSafeInteger(row[key]) && row[key] >= 0, `Invalid ${key} sample`);
  }
  assert.equal(row.pid, rows[0].pid, 'One probe process only');
  assert.equal(row.children, 0, 'Native calendar must not start child processes');
}
assert.equal(byPhase('baseline').length, 1);
assert.equal(byPhase('destroyed').length, 1);
for (const phase of ['spacious', 'split', 'picker', 'closed']) {
  assert.deepEqual(byPhase(phase).map(row => row.round), Array.from({length: 10}, (_, i) => i + 1), `${phase}: ten complete rounds required`);
}
const idle = byPhase('idle');
assert.deepEqual(idle.map(row => row.round), Array.from({length: 11}, (_, i) => i), 'Ten idle intervals required');
assert.equal(rows.length, 53, 'Unexpected sample matrix');
const firstIdle = idle[0], lastIdle = idle.at(-1);
const elapsedMs = lastIdle.tickMs - firstIdle.tickMs;
const cpuSeconds = (lastIdle.cpu100ns - firstIdle.cpu100ns) / 1e7;
assert(elapsedMs >= 10000 && cpuSeconds >= 0, 'Complete monotonic idle interval required');
const toMiB = value => Number((value / 1048576).toFixed(3));
const range = (data, key) => ({min: Math.min(...data.map(row => row[key])), max: Math.max(...data.map(row => row[key]))});
const describe = data => ({
  count: data.length,
  privateCommitMiB: Object.fromEntries(Object.entries(range(data, 'privateBytes')).map(([key, value]) => [key, toMiB(value)])),
  totalWorkingSetMiB: Object.fromEntries(Object.entries(range(data, 'workingSetBytes')).map(([key, value]) => [key, toMiB(value)])),
  handles: range(data, 'handles'), gdi: range(data, 'gdi'), user: range(data, 'user'),
});
const closed = byPhase('closed');
const closedDelta = {};
for (const key of ['privateBytes', 'workingSetBytes', 'handles', 'gdi', 'user']) closedDelta[key] = closed.at(-1)[key] - closed[0][key];
const summary = {
  source: path.resolve(input), pid: rows[0].pid, sampleMatrixComplete: true,
  nativeChildProcesses: 0,
  phases: Object.fromEntries(['baseline', 'spacious', 'split', 'picker', 'closed', 'idle', 'destroyed'].map(phase => [phase, describe(byPhase(phase))])),
  closedRound1To10Delta: closedDelta,
  closedWarmRounds3To10: describe(closed.slice(2)),
  idle: {elapsedMs, cpuSeconds, cpuPercentOneCore: cpuSeconds * 100000 / elapsedMs,
    cpuPercentMachine: cpuSeconds * 100000 / elapsedMs / os.cpus().length,
    logicalProcessors: os.cpus().length},
  limitations: ['Private bytes are process commit, not private working set.',
    'Working set includes shared resident pages.',
    'CPU includes the development probe and one process snapshot per second.',
    'Ten rounds are a bounded observation, not proof of absence of all leaks.',
    'Resource acceptance requires reviewing the trend and product scope; no memory trimming is performed.'],
};
fs.mkdirSync(path.dirname(path.resolve(output)), {recursive: true});
fs.writeFileSync(output, JSON.stringify(summary, null, 2));
console.log(JSON.stringify(summary, null, 2));
