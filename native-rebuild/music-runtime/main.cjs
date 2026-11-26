'use strict';
// The native shell owns this process through private inherited pipes and a
// kill-on-close job. Stdout is reserved for correlated, bounded JSON replies.
const readline = require('node:readline');
const path = require('node:path');
const { createSafeStorage, createStore } = require('./secure-store.cjs');
const { createBackends } = require('./backend/backends.cjs');
const { createQishuiBackend } = require('./backend/qishui.cjs');
const { installProviderEnvironment } = require('./provider/environment.cjs');
const { withRequest, guardStorage } = require('./provider/request-context.cjs');

const sendLine = process.stdout.write.bind(process.stdout);
const quiet = () => {};
// Some pinned API packages log during import. Never mix this output into IPC,
// and never serialize their response objects or errors (which may hold tokens).
for (const method of ['log', 'info', 'warn', 'error', 'debug']) console[method] = quiet;
const ids = new Set(['qq', 'netease', 'kugou', 'qishui']);
const errors = new Set(['network', 'timeout', 'service', 'unavailable', 'unsupported', 'invalid-response', 'storage',
  'expired', 'signed-out', 'not-found', 'not-playable', 'preview-only', 'rate-limited', 'access-denied', 'cancelled']);
const inFlight = new Map();
let services, facade, environment, initializing, shuttingDown = false;
const reply = value => {
  const line = JSON.stringify(value);
  if (Buffer.byteLength(line) <= 8 * 1024 * 1024) sendLine(line + '\n');
  else sendLine(JSON.stringify({ id: value.id, ok: false, error: 'invalid-response' }) + '\n');
};
function providerStorage(store) {
  return {
    get length() { return Object.keys(store.get('providerStorage', {})).length; },
    key(index) { return Object.keys(store.get('providerStorage', {}))[index] ?? null; },
    getItem(key) { return store.get('providerStorage', {})[String(key)] ?? null; },
    setItem(key, value) { store.set('providerStorage', { ...store.get('providerStorage', {}), [String(key)]: String(value) }); },
    removeItem(key) { const next = store.get('providerStorage', {}); delete next[String(key)]; store.set('providerStorage', next); },
    clear() { store.delete('providerStorage'); },
  };
}
async function initialize(params) {
  if (!params || typeof params.root !== 'string' || !path.isAbsolute(params.root)) throw new Error('storage');
  const safeStorage = createSafeStorage();
  const store = guardStorage(createStore(params.root, safeStorage));
  const backends = await createBackends({ store, safeStorage, root: params.root, openExternal: async () => false, log: quiet, warn: quiet });
  globalThis.fetch = backends.fetch;
  const options = { store, safeStorage, root: params.root, fetch: backends.fetch, log: quiet, warn: quiet };
  const qishui = createQishuiBackend(options);
  const storage = guardStorage(providerStorage(store));
  backends.stageLegacy?.(params.legacy ?? {}, { storage });
  environment = installProviderEnvironment({ ...backends, ...qishui }, {
    localStorage: storage, language: params.language === 'en' ? 'en' : 'zh-CN',
  });
  const bundle = require('./dist/providers.cjs');
  const providers = bundle.providers ?? bundle;
  const { createFacade } = require('./provider/facade.cjs');
  facade = createFacade({ providers, store });
  services = { backends, qishui };
  return { providers: [...ids], sourceRevision: 'a44758bbc02929225ca7304b5a4c4580d7b4e87b' };
}
async function shutdown() {
  if (shuttingDown) return;
  shuttingDown = true;
  await Promise.allSettled([...inFlight.values()].map(cancelTask));
  try { await facade?.shutdown?.(); } catch {}
  await Promise.allSettled(Object.values(services ?? {}).map(service => service.shutdown?.()));
  environment?.dispose();
  process.exitCode = 0;
  setTimeout(() => process.exit(0), 20).unref();
}
async function cancelTask(task) {
  task.cancelled = true;
  if (task.action === 'auth' && task.attempt !== undefined && ids.has(task.provider))
    await facade?.cancel(task.provider, task.attempt);
}
async function dispatch(message) {
  if (!message || !Number.isSafeInteger(message.id) || message.id < 0 || typeof message.action !== 'string') return;
  if (message.action === 'shutdown') { await shutdown(); return; }
  if (message.action === 'cancel-request') {
    const pending = inFlight.get(message.target);
    if (pending) await cancelTask(pending);
    return;
  }
  if (shuttingDown) return;
  if (message.action === 'auth-cancel') {
    if (ids.has(message.provider)) {
      const attempt = message.params?.attempt;
      for (const pending of inFlight.values())
        if (pending.action === 'auth' && pending.provider === message.provider &&
            (attempt === undefined || String(pending.attempt) === String(attempt))) pending.cancelled = true;
      await facade?.cancel(message.provider, attempt);
    }
    return;
  }
  const task = { cancelled: false, action: message.action, provider: message.provider, attempt: message.params?.attempt };
  inFlight.set(message.id, task);
  try {
    let result;
    if (message.action === 'init') {
      if (!initializing) initializing = initialize(message.params);
      result = await initializing;
    } else {
      if (!initializing || !ids.has(message.provider)) throw new Error('unsupported');
      await initializing;
      if (task.cancelled) return;
      const params = message.params ?? {};
      if (message.action === 'auth') result = await withRequest(task,
        () => facade.auth(message.provider, params, () => task.cancelled));
      else if (message.action === 'online') result = await withRequest(task, () => facade.online(message.provider, params));
      else throw new Error('unsupported');
    }
    if (!task.cancelled) reply({ id: message.id, ok: true, result });
  } catch (error) {
    const code = errors.has(error?.code) ? error.code : errors.has(error?.message) ? error.message : 'service';
    if (!task.cancelled) reply({ id: message.id, ok: false, error: code });
  } finally { inFlight.delete(message.id); }
}
const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
input.on('line', line => {
  if (Buffer.byteLength(line) > 8 * 1024 * 1024) { void shutdown(); return; }
  try { void dispatch(JSON.parse(line)); } catch {}
});
input.on('close', () => void shutdown());
process.on('uncaughtException', () => { void shutdown(); });
process.on('unhandledRejection', () => { void shutdown(); });
