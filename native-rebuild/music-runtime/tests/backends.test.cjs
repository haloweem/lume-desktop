'use strict';

const assert = require('node:assert/strict');
const { once, EventEmitter } = require('node:events');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const { createBackends } = require('../backend/backends.cjs');
const { TOKEN_HEADER, installPrivateListeners, createPrivateFetch } = require('../backend/private-listener.cjs');
const { withRequest, guardStorage } = require('../provider/request-context.cjs');
const { createStore, createSafeStorage } = require('../secure-store.cjs');
const { createQqAuthSessionRepository } = require('../vendor/folia/electron/qq/qqAuthSessionRepository.cjs');
const { createSessionRepository: createQishuiSessionRepository } = require('../vendor/folia/electron/qishui/session.cjs');
const { createKugouApiBridge } = require('../vendor/folia/electron/kugou/kugouApiBridge.cjs');

// Real loopback requests exercise the authentication boundary without contacting any music service.
test('provider listener binds IPv4 loopback and rejects requests before upstream handlers', async () => {
    const token = 'a'.repeat(64);
    const servers = new Set();
    const guard = installPrivateListeners({ token, servers });
    let requests = 0;
    let upstreamSawToken = false;
    const server = http.createServer((request, response) => {
        requests += 1;
        upstreamSawToken ||= Object.hasOwn(request.headers, TOKEN_HEADER);
        response.end('{"ok":true}');
    });
    try { server.listen(0, '0.0.0.0'); }
    finally { guard.restore(); }
    await once(server, 'listening');
    const address = server.address();
    assert.equal(address.address, '127.0.0.1');
    assert.equal(servers.has(server), true);
    const url = `http://127.0.0.1:${address.port}/login/status`;
    try {
        assert.equal((await fetch(url)).status, 403);
        assert.equal((await fetch(url, { headers: { [TOKEN_HEADER]: 'wrong' } })).status, 403);
        assert.equal(requests, 0, 'unauthorized traffic never reaches any provider route');
        const privateFetch = createPrivateFetch({ token, ports: () => new Set([address.port]), rawFetch: fetch });
        assert.equal((await privateFetch(url)).status, 200);
        assert.equal(requests, 1);
        assert.equal(upstreamSawToken, false, 'runtime token is stripped before provider routing');
    } finally {
        server.closeAllConnections();
        await new Promise(resolve => server.close(resolve));
    }
});

test('private fetch injects no token into unregistered origins and refuses redirect forwarding', async () => {
    const calls = [];
    const privateFetch = createPrivateFetch({
        token: 'b'.repeat(64), ports: () => new Set([1234]),
        rawFetch: async request => { calls.push(request); return new Response('{}'); },
    });
    await privateFetch('http://127.0.0.1:1234/login/status');
    await privateFetch('https://example.invalid/catalog', { headers: { [TOKEN_HEADER]: 'caller-value' } });
    await privateFetch('http://127.0.0.1:5678/login/status');
    await privateFetch('http://localhost:1234/login/status');
    assert.equal(calls[0].headers.get(TOKEN_HEADER), 'b'.repeat(64));
    assert.equal(calls[0].redirect, 'error');
    for (const request of calls.slice(1)) assert.equal(request.headers.get(TOKEN_HEADER), null);
});

function fakeDependencies() {
    const observed = { qqStarts: 0, qqStops: 0, neteaseStarts: 0, requests: [], workers: [] };
    const modules = {
        'electron/qq/qqAuthSessionRepository.cjs': {
            createQqAuthSessionRepository(options) { observed.repository = options; return { kind: 'fake-encrypted' }; },
        },
        'electron/qq/qqApiStartup.cjs': {
            async startQqApi(options) {
                observed.startOptions = options;
                options.loadModule('fake-package');
                return { port: options.port, close: async () => {} };
            },
        },
        'electron/qq/qqBackend.cjs': {
            createQqBackend(options) {
                observed.qqOptions = options;
                let state = { status: 'starting', port: null };
                return {
                    async start() {
                        observed.qqStarts += 1;
                        await options.startServer({ port: 1234, stateFilePath: options.getStateFilePath(), authSessionRepository: options.authSessionRepository });
                        state = { status: 'running', port: 1234 };
                        options.broadcast('qq-api-status-changed', state);
                    },
                    async stop() { observed.qqStops += 1; },
                    getStatus: () => state,
                    getPort: () => state.port,
                    getDiagnostics: async () => ({ backend: state }),
                };
            },
        },
        'electron/netease/neteaseBackend.cjs': {
            createNeteaseBackend(options) {
                let state = { status: 'starting', port: null };
                let worker;
                return {
                    async start() {
                        observed.neteaseStarts += 1;
                        worker ||= options.createWorker();
                        worker.on('error', () => { throw new Error('Electron fatal worker handler must not run'); });
                        worker.on('exit', () => { state = { status: 'error', port: null }; });
                        state = { status: 'running', port: 2345 };
                        return state;
                    },
                    getStatus: () => state,
                    getPort: () => state.port,
                    getDiagnostics: async () => ({ backend: state }),
                };
            },
        },
        'electron/kugou/kugouApiBridge.cjs': {
            createKugouApiBridge(options) {
                observed.kugouOptions = options;
                return {
                    getStatus: () => ({ available: true }),
                    async request(operation, params) { observed.requests.push({ operation, params }); return { ok: true }; },
                };
            },
        },
    };
    const dependencies = {
        loadVendor(relative) { assert.ok(modules[relative], relative); return modules[relative]; },
        loadPackage(target) { assert.equal(target, 'fake-package'); return {}; },
        makeWorker(file, options) {
            const worker = new EventEmitter();
            worker.stdout = new EventEmitter();
            worker.stderr = new EventEmitter();
            worker.postMessage = () => {};
            worker.terminate = async () => { worker.terminated = true; worker.emit('exit', 0); };
            observed.workers.push({ worker, file, options });
            return worker;
        },
        rawFetch: async request => { observed.requests.push(request); return new Response('{}'); },
    };
    return { observed, dependencies };
}

test('backend factory is lazy, keeps upstream repositories and owns shutdown', async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'lume-music-backends-'));
    const store = { get() {}, set() {}, delete() {} };
    const safeStorage = { isEncryptionAvailable: () => true };
    const { observed, dependencies } = fakeDependencies();
    const api = await createBackends({ root, store, safeStorage, openExternal: async () => true }, dependencies);
    try {
        assert.equal(observed.qqStarts, 0);
        assert.equal(observed.neteaseStarts, 0);
        assert.equal(observed.repository.store, store);
        assert.equal(observed.repository.safeStorage, safeStorage);
        assert.equal(observed.kugouOptions.store, store);
        let changes = 0;
        const off = api.onQqApiStatusChanged(() => { changes += 1; });
        assert.deepEqual(await Promise.all([api.getQqPort(), api.getQqPort()]), [1234, 1234]);
        assert.equal(observed.qqStarts, 1, 'concurrent requests cannot re-require the listening QQ package');
        assert.equal(changes, 1);
        off();
        assert.equal(await api.getNeteasePort(), 2345);
        assert.equal(observed.workers.length, 1);
        assert.ok(observed.workers[0].file.endsWith('netease-worker.cjs'));
        assert.equal(observed.workers[0].options.workerData.tempRoot, path.join(root, 'netease-cache'));
        assert.equal(typeof observed.workers[0].options.workerData.token, 'string');
        assert.equal(observed.workers[0].options.workerData.token.length, 64);
        await api.kugouRequest('login_qr_key', {});
        assert.deepEqual(observed.requests[0], { operation: 'login_qr_key', params: {} });
        assert.equal((await api.getLoginDiagnostics('qq')).backend.status, 'running');
        assert.equal(await api.getLoginDiagnostics('other'), null);
        assert.equal(await api.openExternalUrl('https://example.invalid/'), true);
        await api.shutdown();
        await api.shutdown();
        assert.equal(observed.qqStops, 1);
        assert.equal(observed.workers[0].worker.terminated, true);
        await assert.rejects(api.getQqPort(), /closed/);
        await assert.rejects(api.kugouRequest('login_qr_key', {}), /closed/);
    } finally {
        await api.shutdown();
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test('a vendor worker error rejects only its backend rather than throwing through the host', async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'lume-music-worker-'));
    const { observed, dependencies } = fakeDependencies();
    const api = await createBackends({ root, store: {}, safeStorage: {} }, dependencies);
    try {
        await api.getNeteasePort();
        const { worker } = observed.workers[0];
        assert.doesNotThrow(() => worker.emit('error', new Error('synthetic worker failure')));
        worker.emit('exit', 1);
        assert.equal((await api.getNeteaseApiStatus()).status, 'error');
        assert.equal(await api.getQqPort(), 1234);
    } finally {
        await api.shutdown();
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test('cancelled first login cannot disable later backend persistence and four accounts survive a fresh store', async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'lume-music-persistence-'));
    const safeStorage = process.platform === 'win32' ? createSafeStorage() : {
        isEncryptionAvailable: () => true, getSelectedStorageBackend: () => 'fixture',
        encryptString: value => Buffer.from(Buffer.from(value).map(byte => byte ^ 0xa5)),
        decryptString: value => Buffer.from(value.map(byte => byte ^ 0xa5)).toString(),
    };
    const logger = { info() {}, warn() {} };
    const store = guardStorage(createStore(root, safeStorage));
    const { dependencies } = fakeDependencies();
    const originalLoad = dependencies.loadVendor;
    let server, api;
    const session = { token: 'synthetic-qq-session', credential: { musicid: '12345', musickey: 'synthetic-qq-key', loginType: 2 },
        expiresAt: Date.now() + 3600000 };
    dependencies.loadVendor = relative => relative === 'electron/qq/qqAuthSessionRepository.cjs'
        ? { createQqAuthSessionRepository }
        : relative === 'electron/qq/qqApiStartup.cjs' ? {
            async startQqApi(options) {
                server = http.createServer((request, response) => {
                    options.authSessionRepository.save([session]); response.end('saved');
                });
                server.listen(0, '127.0.0.1'); await once(server, 'listening');
                return { port: server.address().port, close: async () => {} };
            },
        } : originalLoad(relative);
    try {
        api = await createBackends({ root, store, safeStorage }, dependencies);
        const opening = { cancelled: false };
        await withRequest(opening, () => api.getQqPort());
        opening.cancelled = true;
        // This later HTTP event is independent of the cancelled bootstrap request.
        await fetch(`http://127.0.0.1:${server.address().port}/synthetic-save`);
        assert.ok(store.get('QQ_API_AUTH_SESSION_V1'), 'a subsequent account must reach encrypted storage');
        store.set('providerStorage', {
            'online_provider:qq:cookie': 'qqmusic_session=synthetic-qq-session',
            'online_provider:netease:cookie': 'MUSIC_U=synthetic-netease-session',
        });
        store.set('KUGOU_API_SESSION_V2', safeStorage.encryptString(JSON.stringify({ version: 2,
            cookies: { token: 'synthetic-kugou-token', userid: '12345', dfid: 'synthetic-device' } })).toString('base64'));
        createQishuiSessionRepository({ store, safeStorage, warn() {} }).set({
            userId: '12345', ticket: 'synthetic-ticket', guardKey: 'synthetic-key', guardCert: 'synthetic-cert',
            guardTsSign: 'synthetic-sign', accessCode: 'synthetic-access', expiresAt: Date.now() + 3600000,
        });
        const cancelledPoll = { cancelled: true };
        withRequest(cancelledPoll, () => store.delete('providerStorage'));
        await api.shutdown(); api = null;
        const restored = createStore(root, safeStorage);
        assert.equal(createQqAuthSessionRepository({ store: restored, safeStorage, logger }).load().length, 1);
        assert.equal(restored.get('providerStorage')['online_provider:qq:cookie'], 'qqmusic_session=synthetic-qq-session');
        assert.equal(restored.get('providerStorage')['online_provider:netease:cookie'], 'MUSIC_U=synthetic-netease-session');
        assert.equal(createKugouApiBridge({ store: restored, safeStorage, warn() {}, apiLoader: () => ({}) }).getStatus().authenticated, true);
        assert.ok(createQishuiSessionRepository({ store: restored, safeStorage, warn() {} }).get());
        assert.equal(fs.readFileSync(path.join(root, 'folia-accounts.dpapi')).includes(Buffer.from('synthetic-')), false);
    } finally {
        await api?.shutdown();
        if (server?.listening) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
        fs.rmSync(root, { recursive: true, force: true });
    }
});
