// Development-only, read-only PE audit. Never loads or executes the taskbar core.
// Format: https://learn.microsoft.com/windows/win32/debug/pe-format
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';

const root = path.resolve(process.argv[2] ??
  path.join(path.dirname(fileURLToPath(import.meta.url)), '2026.1'));
const systemDlls = new Set([
  'advapi32.dll', 'bcrypt.dll', 'comctl32.dll', 'comdlg32.dll', 'crypt32.dll',
  'dwmapi.dll', 'gdi32.dll', 'kernel32.dll', 'ntdll.dll', 'ole32.dll',
  'oleaut32.dll', 'propsys.dll', 'rpcrt4.dll', 'secur32.dll', 'shell32.dll',
  'shlwapi.dll', 'user32.dll', 'uxtheme.dll', 'version.dll', 'winmm.dll',
  'ws2_32.dll',
]);
const isSystem = name => systemDlls.has(name) ||
  /^(api|ext)-ms-win-[a-z0-9-]+\.dll$/.test(name);

function pe(file) {
  const data = fs.readFileSync(file);
  const u16 = offset => data.readUInt16LE(offset);
  const u32 = offset => data.readUInt32LE(offset);
  const start = u32(0x3c);
  if (u16(0) !== 0x5a4d || data.toString('ascii', start, start + 4) !== 'PE\0\0')
    throw new Error(`Invalid PE image: ${file}`);
  const optional = start + 24;
  const magic = u16(optional);
  if (magic !== 0x20b && magic !== 0x10b) throw new Error(`Invalid optional header: ${file}`);
  const wide = magic === 0x20b;
  const imageBase = wide ? data.readBigUInt64LE(optional + 24) : BigInt(u32(optional + 28));
  const directoryBase = optional + (wide ? 112 : 96);
  const directoryCount = u32(optional + (wide ? 108 : 92));
  const sectionBase = optional + u16(start + 20);
  const headersSize = u32(optional + 60);
  const sections = Array.from({length: u16(start + 6)}, (_, index) => {
    const offset = sectionBase + index * 40;
    return {address: u32(offset + 12), size: Math.max(u32(offset + 8), u32(offset + 16)),
      rawSize: u32(offset + 16), raw: u32(offset + 20)};
  });
  const offset = rva => {
    if (rva < headersSize && rva < data.length) return rva;
    const section = sections.find(item => rva >= item.address && rva < item.address + item.size);
    if (!section || rva - section.address >= section.rawSize) throw new Error(`Unmapped RVA in ${file}`);
    return section.raw + rva - section.address;
  };
  const text = rva => {
    const begin = offset(rva), end = data.indexOf(0, begin);
    if (end < begin || end - begin > 32768) throw new Error(`Unterminated PE string in ${file}`);
    return data.toString('ascii', begin, end);
  };
  const directory = index => index < directoryCount ?
    {rva: u32(directoryBase + index * 8), size: u32(directoryBase + index * 8 + 4)} :
    {rva: 0, size: 0};
  const thunkSymbols = (rva, absolute = false) => {
    const symbols = [];
    if (!rva) return symbols;
    const begin = offset(rva), stride = wide ? 8 : 4;
    const ordinal = wide ? 1n << 63n : 1n << 31n;
    for (let index = 0; ; ++index) {
      const value = wide ? data.readBigUInt64LE(begin + index * stride) :
        BigInt(u32(begin + index * stride));
      if (!value) break;
      symbols.push((value & ordinal) ? `#${value & 0xffffn}` :
        text(Number(absolute ? value - imageBase : value) + 2));
    }
    return symbols;
  };
  const imports = [];
  for (const delayed of [false, true]) {
    const table = directory(delayed ? 13 : 1);
    if (!table.rva) continue;
    const stride = delayed ? 32 : 20, begin = offset(table.rva);
    for (let current = begin; current < begin + table.size; current += stride) {
      const nameValue = u32(current + (delayed ? 4 : 12));
      if (!nameValue) break;
      const absolute = delayed && !(u32(current) & 1);
      const toRva = value => Number(absolute ? BigInt(value) - imageBase : BigInt(value));
      const thunk = delayed ? u32(current + 16) : (u32(current) || u32(current + 16));
      imports.push({name: text(toRva(nameValue)).toLowerCase(), delayed,
        symbols: thunkSymbols(toRva(thunk), absolute)});
    }
  }
  const exports = new Map();
  const table = directory(0);
  if (table.rva) {
    const begin = offset(table.rva), base = u32(begin + 16), count = u32(begin + 20);
    const functions = offset(u32(begin + 28));
    const value = index => {
      const rva = u32(functions + index * 4);
      return !rva ? null : rva >= table.rva && rva < table.rva + table.size ? text(rva) : '';
    };
    for (let index = 0; index < count; ++index) {
      const target = value(index);
      if (target !== null) exports.set(`#${base + index}`, target);
    }
    const named = u32(begin + 24);
    if (named) {
      const names = offset(u32(begin + 32)), ordinals = offset(u32(begin + 36));
      for (let index = 0; index < named; ++index) {
        const target = value(u16(ordinals + index * 2));
        if (target !== null) exports.set(text(u32(names + index * 4)), target);
      }
    }
  }
  return {machine: u16(start + 4), imports, exports,
    sha256: createHash('sha256').update(data).digest('hex').toUpperCase()};
}

const failures = [];
const report = {scope: 'static-imports-delay-imports-app-local-exports-and-forwarders', architectures: {}};
for (const [architecture, machine] of [['x64', 0x8664], ['arm64', 0xaa64]]) {
  const directory = path.join(root, architecture);
  const files = fs.readdirSync(directory).filter(name => /\.(exe|dll)$/i.test(name)).sort();
  const images = new Map(files.map(name => [name.toLowerCase(), pe(path.join(directory, name))]));
  for (const [name, image] of images) {
    if (image.machine !== machine)
      failures.push(`${architecture}/${name}: machine 0x${image.machine.toString(16)} is not 0x${machine.toString(16)}`);
  }
  const checkSymbol = (source, name, symbol, chain = new Set()) => {
    if (isSystem(name)) return;
    const key = `${name}!${symbol}`;
    if (chain.has(key)) {
      failures.push(`${architecture}/${source}: circular export forwarder ${key}`);
      return;
    }
    const target = images.get(name);
    if (!target) {
      failures.push(`${architecture}/${source}: missing app-local dependency ${name}`);
      return;
    }
    if (!target.exports.has(symbol)) {
      failures.push(`${architecture}/${source}: unresolved import ${key}`);
      return;
    }
    const forwarder = target.exports.get(symbol);
    if (forwarder) {
      const separator = forwarder.lastIndexOf('.');
      if (separator < 1) throw new Error(`Malformed PE forwarder: ${forwarder}`);
      let module = forwarder.slice(0, separator).toLowerCase();
      if (!module.endsWith('.dll')) module += '.dll';
      checkSymbol(source, module, forwarder.slice(separator + 1), new Set([...chain, key]));
    }
  };
  for (const [name, image] of images) {
    for (const dependency of image.imports) {
      if (!dependency.symbols.length && !isSystem(dependency.name) && !images.has(dependency.name))
        failures.push(`${architecture}/${name}: missing app-local dependency ${dependency.name}`);
      for (const symbol of dependency.symbols) checkSymbol(name, dependency.name, symbol);
    }
  }
  report.architectures[architecture] = Object.fromEntries([...images].map(([name, image]) => [name, {
    machine: `0x${image.machine.toString(16).toUpperCase()}`, sha256: image.sha256,
    imports: image.imports.map(item => ({file: item.name, delayed: item.delayed,
      kind: isSystem(item.name) ? 'windows' : 'app-local', symbols: item.symbols.length})),
  }]));
}
report.failures = [...new Set(failures)];
report.passed = report.failures.length === 0;
console.log(JSON.stringify(report, null, 2));
process.exitCode = report.passed ? 0 : 1;
