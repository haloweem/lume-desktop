'use strict';

const path = require('node:path');

// The pinned package owns the car protocol, QR authorization, ticket renewal and account catalogue.
function createQishuiBackend(options) {
    const { store, safeStorage, log = console.info, warn = console.warn } = options;
    const root = typeof options.root === 'string' ? { directory: options.root } : options.root ?? {};
    const vendor = root.vendorRoot ?? path.join(__dirname, '..', 'vendor', 'folia');
    const createBridge = options.createBridge ?? require(path.join(vendor, 'electron', 'qishui', 'qishuiApiBridge.cjs')).createQishuiApiBridge;
    const bridge = createBridge({ store, safeStorage, log, warn, fetch: options.fetch ?? globalThis.fetch });
    let closed = false;
    return {
        qishuiRequest(operation, params = {}) {
            if (closed) return Promise.resolve({ ok: false, error: { code: 'unavailable', message: 'Qishui runtime is stopped' } });
            return bridge.request(operation, params);
        },
        handleAudioRequest() { return Promise.resolve(null); },
        async shutdown() { closed = true; },
    };
}

module.exports = { createQishuiBackend };
