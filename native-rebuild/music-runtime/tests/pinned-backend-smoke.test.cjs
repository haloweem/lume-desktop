'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { createBackends } = require('../backend/backends.cjs');

// The real pinned QQ package serves only local capabilities here; no QR or account request is made.
test('pinned QQ 3.3.6 runs behind the private listener and exposes its declared login channels', { timeout: 12000 }, async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'lume-pinned-qq-'));
    const values = new Map();
    const store = { get: key => values.get(key), set: (key, value) => values.set(key, value), delete: key => values.delete(key) };
    const safeStorage = { isEncryptionAvailable: () => true, encryptString: value => Buffer.from(value), decryptString: value => value.toString() };
    const methods = ['log', 'info', 'warn', 'error', 'debug'];
    const original = Object.fromEntries(methods.map(method => [method, console[method]]));
    methods.forEach(method => { console[method] = () => {}; });
    let api;
    try {
        api = await createBackends({ root, store, safeStorage });
        const port = await api.getQqPort();
        assert.equal(Number.isInteger(port), true);
        assert.equal((await fetch(`http://127.0.0.1:${port}/login/channels`)).status, 403);
        const answer = await api.fetch(`http://127.0.0.1:${port}/login/channels`);
        assert.equal(answer.status, 200);
        const channels = (await answer.json()).data.channels;
        assert.ok(channels.includes('qqmusic'));
        assert.ok(channels.includes('wechat'));
        assert.equal((await api.getQqApiStatus()).status, 'running');
        const diagnostic = await api.getLoginDiagnostics('qq');
        assert.equal(diagnostic.version, '3.3.6');
    } finally {
        await api?.shutdown();
        methods.forEach(method => { console[method] = original[method]; });
        fs.rmSync(root, { recursive: true, force: true });
    }
});
