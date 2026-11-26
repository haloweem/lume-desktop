'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { createLegacyMigration, MARKER } = require('../backend/legacy-migration.cjs');

// Synthetic credentials verify protocol shapes and one-time migration without any account requests.
function context() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'lume-legacy-music-'));
    fs.mkdirSync(path.join(root, 'qq-auth-state'));
    const values = new Map();
    const sessions = new Map();
    const store = { get: key => values.get(key), set: (key, value) => values.set(key, value), delete: key => values.delete(key) };
    const storage = { getItem: key => sessions.get(key) ?? null, setItem: (key, value) => sessions.set(key, value), removeItem: key => sessions.delete(key) };
    const safeStorage = {
        isEncryptionAvailable: () => true,
        encryptString: value => Buffer.from(Buffer.from(value).map(byte => byte ^ 0xa5)),
        decryptString: value => Buffer.from(value.map(byte => byte ^ 0xa5)).toString(),
    };
    let importedQq;
    const authSessionRepository = { save: value => { importedQq = value; store.set('QQ_API_AUTH_SESSION_V1', 'encrypted-placeholder'); } };
    const api = createLegacyMigration({ store, safeStorage, root, authSessionRepository, canMigrateQq: () => true, canMigrateKugou: () => true });
    return { root, store, storage, safeStorage, api, importedQq: () => importedQq };
}

function oldAccounts() {
    return {
        netease: { MUSIC_U: 'synthetic-music-u', __csrf: 'synthetic-csrf', MUSIC_A: 'synthetic-anonymous' },
        qq: {
            credential: { uin: '12345678', musicKey: 'synthetic-musickey', loginType: 2, refreshToken: 'synthetic-refresh', refreshKey: 'synthetic-refresh-key', keyExpiresIn: 259200, musickeyCreateTime: Math.floor(Date.now() / 1000) },
            device: { display: 'QMAPI.123456.001', product: 'iarim', device: 'sagit', board: 'eomam', model: 'MI 6', fingerprint: 'synthetic/fingerprint', procVersion: 'Linux 5.4.0 synthetic device', imei: '123456789012345', brand: 'Xiaomi', androidId: '0123456789abcdef', openUdid: '0123456789abcdef0123456789abcdef', version: { release: '10', sdk: 29 }, qimei16: 'synthetic-qimei', qimei36: 'synthetic-qimei36', qimeiUpdatedAt: Date.now() },
        },
        kugou: { token: 'synthetic-token', userid: '12345', KUGOU_API_GUID: 'A'.repeat(32), KUGOU_API_MID: '123456789', dfid: 'synthetic-dfid' },
    };
}

test('old accounts map to the real pinned QQ session shape and encrypted KuGou V2', async () => {
    const c = context();
    try {
        const staged = c.api.stageLegacy(oldAccounts(), { storage: c.storage });
        assert.deepEqual(staged, { netease: 'pending-verification', qq: 'pending-verification', kugou: 'pending-verification' });
        assert.match(c.storage.getItem('online_provider:netease:cookie'), /^MUSIC_U=synthetic-music-u;/);
        const qq = c.importedQq()[0];
        const packageRoot = path.dirname(require.resolve('@yakult-green-tea/qq-music-api/package.json'));
        const { isAuthSession } = require(path.join(packageRoot, 'dist/src/services/auth/sessionState.js'));
        assert.equal(isAuthSession(qq), true, 'the unmodified pinned library accepts the mapped session');
        assert.equal(qq.device.model, 'MI 6');
        assert.equal(qq.device.osRelease, '10');
        assert.equal(qq.device.sdk, 29);
        assert.equal(qq.credential.musicid, '12345678');
        assert.equal(qq.credential.refresh_key, 'synthetic-refresh-key');
        assert.equal(c.storage.getItem('online_provider:qq:cookie'), `qqmusic_session=${qq.token}`);
        const encrypted = c.store.get('KUGOU_API_SESSION_V2');
        assert.doesNotMatch(encrypted, /synthetic-token/);
        const envelope = JSON.parse(c.safeStorage.decryptString(Buffer.from(encrypted, 'base64')));
        assert.equal(envelope.version, 2);
        assert.equal(envelope.cookies.token, 'synthetic-token');
        const verified = await c.api.verifyLegacy(staged, { verify: async provider => provider === 'kugou' ? null : { id: '12345' } });
        assert.deepEqual(verified, { netease: 'migrated', qq: 'migrated', kugou: 'not-confirmed' });
        assert.doesNotMatch(JSON.stringify(verified), /synthetic|musickey|qqmusic_session/);
    } finally { fs.rmSync(c.root, { recursive: true, force: true }); }
});

test('logout cannot revive legacy accounts and a new Folia account always wins', async () => {
    const c = context();
    try {
        c.storage.setItem('online_provider:netease:cookie', 'new-account-cookie');
        const first = await c.api.migrateLegacy(oldAccounts(), { storage: c.storage, verify: async () => { throw new Error('synthetic offline'); } });
        assert.equal(first.netease, 'kept-existing');
        assert.equal(first.qq, 'pending-verification');
        assert.equal(c.storage.getItem('online_provider:netease:cookie'), 'new-account-cookie');
        c.storage.removeItem('online_provider:qq:cookie');
        c.store.delete('QQ_API_AUTH_SESSION_V1');
        c.storage.removeItem('online_provider:netease:cookie');
        c.store.delete('KUGOU_API_SESSION_V2');
        const second = c.api.stageLegacy(oldAccounts(), { storage: c.storage });
        assert.equal(second.qq, 'already-handled');
        assert.equal(second.kugou, 'already-handled');
        assert.equal(second.netease, 'already-handled');
        assert.equal(c.storage.getItem('online_provider:qq:cookie'), null);
        assert.equal(c.storage.getItem('online_provider:netease:cookie'), null);
        assert.equal(c.store.get('KUGOU_API_SESSION_V2'), undefined);
        assert.equal(c.store.get(MARKER).version, 1);
    } finally { fs.rmSync(c.root, { recursive: true, force: true }); }
});

test('partial or malformed old credentials never become an account', () => {
    const c = context();
    try {
        const staged = c.api.stageLegacy({ netease: { MUSIC_U: 'bad;cookie' }, qq: { credential: { uin: '123', musicKey: 'key' } }, kugou: { token: 'one-half' } }, { storage: c.storage });
        assert.deepEqual(staged, { netease: 'invalid-legacy', qq: 'invalid-legacy', kugou: 'invalid-legacy' });
        assert.equal(c.importedQq(), undefined);
        assert.equal(c.storage.getItem('online_provider:netease:cookie'), null);
        assert.equal(c.store.get('KUGOU_API_SESSION_V2'), undefined);
    } finally { fs.rmSync(c.root, { recursive: true, force: true }); }
});
