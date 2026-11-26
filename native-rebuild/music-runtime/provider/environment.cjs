'use strict';

// Install only the browser facilities used by the upstream adapters before requiring their bundle.
function memoryStorage() {
    const values = new Map();
    return { get length() { return values.size; }, key: index => [...values.keys()][index] ?? null,
        getItem: key => values.get(String(key)) ?? null,
        setItem: (key, value) => values.set(String(key), String(value)),
        removeItem: key => values.delete(String(key)), clear: () => values.clear() };
}
function installProviderEnvironment(electron, options = {}) {
    if (!electron || typeof electron !== 'object') throw new TypeError('A private provider bridge is required');
    const events = new EventTarget();
    globalThis.window = globalThis;
    globalThis.electron = { platform: process.platform, ...electron };
    globalThis.localStorage = options.localStorage ?? memoryStorage();
    globalThis.sessionStorage = memoryStorage();
    globalThis.addEventListener = events.addEventListener.bind(events);
    globalThis.removeEventListener = events.removeEventListener.bind(events);
    globalThis.dispatchEvent = events.dispatchEvent.bind(events);
    globalThis.__lumeMusicProviderEnvironment = options;
    return { storage: globalThis.localStorage,
        dispose() { delete globalThis.electron; delete globalThis.__lumeMusicProviderEnvironment; } };
}
module.exports = { installProviderEnvironment, memoryStorage };
