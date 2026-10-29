import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';

const vendor = new URL('../music-visualizers/vendor/', import.meta.url);
const hash = bytes => createHash('sha256').update(bytes).digest('hex');

test('all recorded source, license and generated artifact hashes match', () => {
  const manifest = JSON.parse(readFileSync(new URL('manifest.json', vendor)));
  assert.equal(manifest.esbuild, '0.25.12'); assert.equal(manifest.three, '0.128.0');
  for (const [name, expected] of Object.entries(manifest.outputs)) {
    assert.equal(hash(readFileSync(new URL(name, vendor))), expected, name);
  }
  for (const input of manifest.inputs) {
    assert.equal(hash(readFileSync(new URL(`source/${input.path}`, vendor))), input.sha256, input.path);
  }
});

test('the shipped entry graph uses only static local ESM without React, Electron or Node', () => {
  const queue = [new URL('../music-visualizers/three-d.js', import.meta.url)], visited = new Set();
  while (queue.length) {
    const url = queue.pop(); if (visited.has(url.href)) continue; visited.add(url.href);
    assert.doesNotMatch(url.pathname,/\/(?:src|source|licenses)\//,'runtime dependencies must survive StageUi exclusions');
    const source = readFileSync(url, 'utf8');
    assert.doesNotMatch(source, /\b(?:require\s*\(|import\s*\(|eval\s*\(|from\s*['"](?:react|electron|node:))/);
    for (const match of source.matchAll(/\b(?:from\s*|import\s*)['"]([^'"]+)['"]/g)) {
      assert.ok(match[1].startsWith('./') || match[1]==='../music-material/registry.js', match[1]);
      const dependency=new URL(match[1],url);
      assert.ok(dependency.href.startsWith(new URL('../',import.meta.url).href));
      queue.push(dependency);
    }
  }
  assert.ok(visited.has(new URL('../music-visualizers/reference/preference-theme.js',import.meta.url).href));
  assert.ok(visited.size<12,'preference theme stays a small dependency-free leaf graph');
});
