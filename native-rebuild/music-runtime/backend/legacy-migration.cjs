'use strict';

const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

// Import an old DPAPI-decrypted account once; only a provider's fresh account read confirms it.
const MARKER = 'LUME_LEGACY_MUSIC_MIGRATION_V1';
const QQ_SESSION = 'QQ_API_AUTH_SESSION_V1';
const KUGOU_SESSION = 'KUGOU_API_SESSION_V2';
const sessionKey = provider => `online_provider:${provider}:cookie`;
const text = (value, max = 8192) => typeof value === 'string' && value.length > 0 && value.length <= max && !/[\x00-\x20\x7f;]/.test(value) ? value : '';
const deviceText = value => typeof value === 'string' && value.length > 0 && value.length <= 512 && !/[\x00-\x1f\x7f]/.test(value) ? value : '';
const accountId = value => typeof value === 'number' && !Number.isSafeInteger(value) ? '' : /^\d{1,19}$/.test(String(value ?? '')) ? String(value) : '';
const present = value => value !== undefined && value !== null && value !== '';

function qqSessionFromLegacy(legacy, token, loadQq, now) {
    const old = legacy?.credential;
    if (!old || !accountId(old.uin) || !text(old.musicKey)) return null;
    const root = loadQq ? null : path.dirname(require.resolve('@yakult-green-tea/qq-music-api/package.json'));
    const load = relative => loadQq ? loadQq(relative) : require(path.join(root, 'dist', 'src', 'services', 'auth', relative));
    const { isAndroidDevice } = load('androidDevice.js');
    const { isAuthSession } = load('sessionState.js');
    const { authSessionExpiryAt } = load('credentialExpiry.js');
    const oldDevice = legacy.device || {};
    const device = {};
    for (const key of ['display', 'product', 'device', 'board', 'model', 'fingerprint', 'procVersion', 'imei', 'brand', 'androidId', 'openUdid'])
        device[key] = deviceText(oldDevice[key]);
    device.osRelease = deviceText(oldDevice.osRelease || oldDevice.version?.release);
    device.sdk = Number(oldDevice.sdk ?? oldDevice.version?.sdk);
    for (const [to, from] of [['qimei', 'qimei16'], ['qimei36', 'qimei36'], ['sessionUid', 'sessionUid'], ['sessionSid', 'sessionSid']]) {
        const value = text(oldDevice[from]);
        if (value) device[to] = value;
    }
    if (Number.isFinite(oldDevice.qimeiUpdatedAt) && oldDevice.qimeiUpdatedAt > 0) device.qimeiSavedAt = oldDevice.qimeiUpdatedAt;
    if (!isAndroidDevice(device)) return null;
    const credential = { musicid: accountId(old.uin), str_musicid: accountId(old.uin), musickey: old.musicKey, loginType: Number(old.loginType || 2) };
    for (const [to, from] of [['access_token', 'accessToken'], ['refresh_token', 'refreshToken'], ['refresh_key', 'refreshKey'], ['openid', 'openId'], ['unionid', 'unionId']]) {
        const value = text(old[from]);
        if (value) credential[to] = value;
    }
    for (const key of ['keyExpiresIn', 'musickeyCreateTime', 'needRefreshKeyIn'])
        if (Number.isFinite(old[key]) && old[key] > 0) credential[key] = old[key];
    if (Number.isFinite(old.expiresAt) && old.expiresAt > 0) credential.expired_at = old.expiresAt;
    const session = { token, credential, device, expiresAt: authSessionExpiryAt(credential, now) };
    return isAuthSession(session) ? session : null;
}

function createLegacyMigration({ store, safeStorage, root, authSessionRepository, canMigrateQq, canMigrateKugou, loadQq, now = Date.now }) {
    const storageOrDefault = storage => storage || globalThis.localStorage;
    const marker = () => {
        const current = store.get(MARKER);
        return current?.version === 1 && current.providers && typeof current.providers === 'object'
            ? { version: 1, providers: { ...current.providers } } : { version: 1, providers: {} };
    };
    const stageLegacy = (legacy, { storage } = {}) => {
        const sessions = storageOrDefault(storage);
        if (!sessions || typeof sessions.getItem !== 'function' || typeof sessions.setItem !== 'function')
            throw new Error('Legacy migration requires private provider storage');
        const record = marker();
        const result = {};
        for (const provider of ['netease', 'qq', 'kugou']) {
            if (!legacy?.[provider]) continue;
            if (Object.hasOwn(record.providers, provider)) { result[provider] = 'already-handled'; continue; }
            const existing = sessions.getItem(sessionKey(provider));
            const backendKept = provider === 'qq' ? store.get(QQ_SESSION) : provider === 'kugou' ? store.get(KUGOU_SESSION) : null;
            if (present(existing) || present(backendKept)) {
                record.providers[provider] = result[provider] = 'kept-existing';
                continue;
            }
            const old = legacy[provider];
            let imported = false;
            if (provider === 'netease' && text(old.MUSIC_U)) {
                const cookie = ['MUSIC_U', '__csrf', 'MUSIC_A'].filter(key => text(old[key])).map(key => `${key}=${old[key]}`).join('; ');
                sessions.setItem(sessionKey(provider), cookie);
                imported = true;
            } else if (provider === 'qq') {
                if (!canMigrateQq()) throw new Error('QQ legacy migration must precede backend startup');
                const token = crypto.randomBytes(32).toString('hex');
                const session = qqSessionFromLegacy(old, token, loadQq, now());
                if (session) {
                    const devicePath = path.join(root, 'qq-auth-state', 'qq-device.json');
                    if (!fs.existsSync(devicePath)) fs.writeFileSync(devicePath, JSON.stringify({ version: 1, device: session.device }), { flag: 'wx', mode: 0o600 });
                    authSessionRepository.save([session]);
                    sessions.setItem(sessionKey(provider), `qqmusic_session=${token}`);
                    imported = true;
                }
            } else if (provider === 'kugou' && text(old.token) && accountId(old.userid)) {
                if (!canMigrateKugou()) throw new Error('KuGou legacy migration must precede backend use');
                const cookies = { token: old.token, userid: accountId(old.userid) };
                for (const [key, value] of Object.entries(old))
                    if ((key.startsWith('KUGOU_API_') || key === 'dfid') && text(value, 256)) cookies[key] = value;
                if (!safeStorage.isEncryptionAvailable()) throw new Error('Encrypted account storage is unavailable');
                store.set(KUGOU_SESSION, safeStorage.encryptString(JSON.stringify({ version: 2, cookies })).toString('base64'));
                imported = true;
            }
            record.providers[provider] = result[provider] = imported ? 'pending-verification' : 'invalid-legacy';
        }
        store.set(MARKER, record);
        return result;
    };
    const verifyLegacy = async (staged, { verify } = {}) => {
        if (typeof verify !== 'function') return { ...staged };
        const result = { ...staged };
        for (const [provider, state] of Object.entries(staged)) {
            if (state !== 'pending-verification') continue;
            try {
                const user = await verify(provider);
                result[provider] = user && present(user.id ?? user.userId) ? 'migrated' : 'not-confirmed';
            } catch {
                result[provider] = 'pending-verification';
            }
            const record = marker();
            record.providers[provider] = result[provider];
            store.set(MARKER, record);
        }
        return result;
    };
    return {
        stageLegacy,
        verifyLegacy,
        async migrateLegacy(legacy, options) { return verifyLegacy(stageLegacy(legacy, options), options); },
    };
}

module.exports = { createLegacyMigration, qqSessionFromLegacy, MARKER };
