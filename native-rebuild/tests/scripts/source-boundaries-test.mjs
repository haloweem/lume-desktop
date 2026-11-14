// Static architecture check for owned native sources. Run from any directory:
// node native-rebuild/tests/scripts/source-boundaries-test.mjs
// Conditional includes are checked conservatively; generated, embedded-data and
// third-party files are outside this check. This is not a full C++ preprocessor.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const layers = ['apps', 'core', 'platform'];
const excludedDirectories = new Set(['vendor', 'upstream', 'generated', 'build', 'out', '.cache', '.vs']);
const sourceExtension = /\.(?:h|hh|hpp|cpp|cc|cxx)$/i;
const excludedData = /(?:^|\/)(?:icon_art_data|.*_bytecode|.*_embedded_data)\.(?:h|hpp)$/i;
const relative = file => path.relative(root, file).split(path.sep).join('/');
const key = file => path.resolve(file).toLowerCase();
const files = [];

function collect(directory) {
  for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      if (!excludedDirectories.has(entry.name.toLowerCase())) collect(file);
    } else if (sourceExtension.test(entry.name) && !excludedData.test(relative(file))) {
      files.push(file);
    }
  }
}

// Preserve lines and ordinary quoted include operands while removing comments
// and raw string bodies, so a sample #include in source text is not a dependency.
const mask = text => text.replace(/[^\r\n]/g, ' ');
function directives(text) {
  let result = '', index = 0;
  while (index < text.length) {
    if (text.startsWith('//', index)) {
      const end = text.indexOf('\n', index);
      const next = end < 0 ? text.length : end;
      result += mask(text.slice(index, next)); index = next;
    } else if (text.startsWith('/*', index)) {
      const end = text.indexOf('*/', index + 2);
      const next = end < 0 ? text.length : end + 2;
      result += mask(text.slice(index, next)); index = next;
    } else {
      const raw = /^(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(/.exec(text.slice(index, index + 24));
      if (raw) {
        const end = text.indexOf(`)${raw[1]}"`, index + raw[0].length);
        const next = end < 0 ? text.length : end + raw[1].length + 2;
        result += mask(text.slice(index, next)); index = next;
      } else if (text[index] === '"' || text[index] === "'") {
        const quote = text[index];
        result += text[index++];
        while (index < text.length) {
          const character = text[index++]; result += character;
          if (character === '\\' && index < text.length) result += text[index++];
          else if (character === quote) break;
        }
      } else result += text[index++];
    }
  }
  return result;
}

for (const layer of layers) collect(path.join(root, layer));
files.sort();
const known = new Map(files.map(file => [key(file), file]));
const graph = new Map(files.map(file => [file, new Set()]));
const violations = [], unresolvedInternal = [], windowsInCore = new Set();
const crossLayerEdges = {};
let includeCount = 0;

for (const file of files) {
  const from = relative(file), fromLayer = from.split('/')[0];
  const lines = directives(fs.readFileSync(file, 'utf8')).split(/\r?\n/);
  for (let index = 0; index < lines.length; index++) {
    const match = /^\s*#\s*include\s*["<]([^">]+)[">]/.exec(lines[index]);
    if (!match) continue;
    const operand = match[1].replace(/\\/g, '/');
    if (fromLayer === 'core' && /^(?:windows|winsock2|shellapi|shlobj|dwmapi|psapi)\.h$/i.test(operand)) {
      windowsInCore.add(from);
    }
    const resolved = [path.resolve(path.dirname(file), operand), path.resolve(root, operand)]
      .map(candidate => known.get(key(candidate))).find(Boolean);
    if (!resolved) {
      if (/^(?:apps|core|platform)\//.test(operand) && !excludedData.test(operand)) {
        // Data .inc files are not translation units or headers in this graph.
        if (sourceExtension.test(operand)) unresolvedInternal.push({file: from, line: index + 1, include: operand});
      }
      continue;
    }
    graph.get(file).add(resolved); includeCount++;
    const to = relative(resolved), toLayer = to.split('/')[0];
    if (fromLayer !== toLayer) {
      const edge = `${fromLayer} -> ${toLayer}`;
      crossLayerEdges[edge] = (crossLayerEdges[edge] ?? 0) + 1;
    }
    if ((fromLayer === 'core' && toLayer !== 'core') || (fromLayer === 'platform' && toLayer === 'apps')) {
      violations.push({file: from, line: index + 1, include: to});
    }
  }
}

let nextIndex = 0;
const indices = new Map(), low = new Map(), stack = [], active = new Set(), cycles = [];
function visit(file) {
  indices.set(file, nextIndex); low.set(file, nextIndex++); stack.push(file); active.add(file);
  for (const dependency of graph.get(file)) {
    if (!indices.has(dependency)) {
      visit(dependency); low.set(file, Math.min(low.get(file), low.get(dependency)));
    } else if (active.has(dependency)) low.set(file, Math.min(low.get(file), indices.get(dependency)));
  }
  if (low.get(file) !== indices.get(file)) return;
  const component = [];
  let member;
  do { member = stack.pop(); active.delete(member); component.push(member); } while (member !== file);
  if (component.length > 1 || graph.get(file).has(file)) cycles.push(component.map(relative).sort());
}
for (const file of files) if (!indices.has(file)) visit(file);

const failed = violations.length > 0 || cycles.length > 0 || unresolvedInternal.length > 0;
console.log(JSON.stringify({
  result: failed ? 'failed' : 'passed',
  scope: 'Owned apps/core/platform native sources; conservative static includes only',
  files: files.length, includes: includeCount, crossLayerEdges,
  violations, cycles, unresolvedInternal,
  existingWindowsHeadersInCore: [...windowsInCore].sort(),
}, null, 2));
if (failed) process.exitCode = 1;
