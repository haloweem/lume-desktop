'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { installProviderEnvironment, memoryStorage } = require('../environment.cjs');
const { create } = require('../state.cjs');
const cache = require('../cache.cjs');
const { createQishuiBackend } = require('../../backend/qishui.cjs');

test('the provider environment calls the private bridge without persisting fallback credentials', () => {
    const storage = memoryStorage();
    const environment = installProviderEnvironment({ qishuiRequest: async () => ({ ok: true, data: 'fixture' }) }, { localStorage: storage });
    assert.equal(globalThis.window, globalThis);
    assert.equal(typeof window.electron.qishuiRequest, 'function');
    localStorage.setItem('online_provider:fixture:session', 'fixture-session');
    assert.equal(storage.getItem('online_provider:fixture:session'), 'fixture-session');
    assert.equal(memoryStorage().getItem('online_provider:fixture:session'), null);
    environment.dispose();
});

test('Folia state functions keep transitions and unsubscribe outside React', () => {
    const changes = [];
    const store = create((set, get) => ({ value: 1, increment: () => set({ value: get().value + 1 }) }));
    const unsubscribe = store.subscribe((state, before) => changes.push([before.value, state.value]));
    store.getState().increment(); unsubscribe(); store.getState().increment();
    assert.deepEqual(changes, [[1, 2]]);
    assert.equal(store.getState().value, 3);
});

test('the provider cache is bounded and does not leak mutable account snapshots', async () => {
    await cache.clearCache();
    const value = { nickname: 'fixture' };
    await cache.saveToCache('account', value);
    value.nickname = 'changed';
    const read = await cache.getFromCache('account'); read.nickname = 'modified';
    assert.equal((await cache.getFromCache('account')).nickname, 'fixture');
    for (let index = 0; index < 300; index++) await cache.saveToCache(`page:${index}`, { index });
    assert.equal((await cache.getCacheKeysByPrefix('page:')).length, 256);
    assert.equal(await cache.getFromCache('account'), null);
    await cache.clearCache();
});

test('Qishui passes the same session repository to login and catalogue and preserves its protocol envelope', async () => {
    const store = { fixture: true };
    const credentialStore = { fixture: true };
    const calls = [];
    const backend = createQishuiBackend({ store, safeStorage: credentialStore, createBridge: options => {
        assert.equal(options.store, store); assert.equal(options.safeStorage, credentialStore);
        return { request: async (operation, params) => { calls.push([operation, params]); return { ok: true, data: { state: 'confirmed' } }; } };
    } });
    assert.deepEqual(await backend.qishuiRequest('login_qr_check', { key: 'fixture' }), { ok: true, data: { state: 'confirmed' } });
    await backend.qishuiRequest('user_playlists', { limit: 10 });
    assert.deepEqual(calls.map(call => call[0]), ['login_qr_check', 'user_playlists']);
    await backend.shutdown();
    assert.equal((await backend.qishuiRequest('login_status')).ok, false);
});
