'use strict';
// The provider process has no plaintext credential files. Both the store and
// Folia's safeStorage boundary are backed by Windows DPAPI for the current user.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function createSafeStorage() {
  if (process.platform !== 'win32') throw new Error('storage');
  const koffi = require('koffi');
  const blob = koffi.struct('LumeMusicDataBlob', { cbData: 'uint32', pbData: 'void *' });
  const crypt = koffi.load('crypt32.dll');
  const kernel = koffi.load('kernel32.dll');
  const protect = crypt.func('CryptProtectData', 'bool', [koffi.pointer(blob), 'str16', koffi.pointer(blob), 'void *', 'void *', 'uint32', koffi.out(koffi.pointer(blob))]);
  const unprotect = crypt.func('CryptUnprotectData', 'bool', [koffi.pointer(blob), 'void *', koffi.pointer(blob), 'void *', 'void *', 'uint32', koffi.out(koffi.pointer(blob))]);
  const free = kernel.func('LocalFree', 'void *', ['void *']);
  const entropy = Buffer.from('Lume.Folia.MusicRuntime.v1');
  function transform(value, decrypt) {
    const bytes = Buffer.from(value);
    if (!bytes.length || bytes.length > 2 * 1024 * 1024) throw new Error('storage');
    const source = { cbData: bytes.length, pbData: bytes };
    const extra = { cbData: entropy.length, pbData: entropy };
    const output = {};
    const ok = decrypt ? unprotect(source, null, extra, null, null, 1, output)
      : protect(source, 'Lume music accounts', extra, null, null, 1, output);
    try {
      if (!ok || !output.pbData || !output.cbData || output.cbData > 2 * 1024 * 1024) throw new Error('storage');
      return Buffer.from(koffi.decode(output.pbData, koffi.array('uint8_t', output.cbData)));
    } finally { if (output.pbData) free(output.pbData); bytes.fill(0); }
  }
  return {
    isEncryptionAvailable: () => true,
    getSelectedStorageBackend: () => 'windows-dpapi',
    encryptString: text => transform(Buffer.from(String(text), 'utf8'), false),
    decryptString: value => transform(value, true).toString('utf8'),
  };
}

function createStore(directory, safeStorage) {
  if (!path.isAbsolute(directory) || directory.startsWith('\\\\') || directory.includes('\0')) throw new Error('storage');
  fs.mkdirSync(directory, { recursive: true });
  for (let part = directory;;) {
    if (fs.lstatSync(part).isSymbolicLink()) throw new Error('storage');
    const parent = path.dirname(part);
    if (parent === part) break;
    part = parent;
  }
  const file = path.join(directory, 'folia-accounts.dpapi');
  let values = {};
  if (fs.existsSync(file)) {
    const info = fs.lstatSync(file);
    if (!info.isFile() || info.isSymbolicLink() || info.nlink !== 1 || info.size > 2 * 1024 * 1024) throw new Error('storage');
    values = JSON.parse(safeStorage.decryptString(fs.readFileSync(file)));
    if (!values || typeof values !== 'object' || Array.isArray(values)) throw new Error('storage');
  }
  function commit(next) {
    const sealed = safeStorage.encryptString(JSON.stringify(next));
    const temporary = `${file}.${crypto.randomBytes(12).toString('hex')}.tmp`;
    try {
      const fd = fs.openSync(temporary, 'wx', 0o600);
      try { fs.writeFileSync(fd, sealed); fs.fsyncSync(fd); } finally { fs.closeSync(fd); }
      fs.renameSync(temporary, file);
      values = next;
    } finally { try { fs.unlinkSync(temporary); } catch {} }
  }
  return {
    get: (key, fallback) => Object.hasOwn(values, key) ? structuredClone(values[key]) : fallback,
    set(key, value) { commit({ ...values, [key]: value }); },
    delete(key) { if (Object.hasOwn(values, key)) { const next = { ...values }; delete next[key]; commit(next); } },
  };
}
module.exports = { createSafeStorage, createStore };
