'use strict';

const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { Worker } = require('node:worker_threads');
const { installPrivateListeners, createPrivateFetch } = require('./private-listener.cjs');
const { createLegacyMigration } = require('./legacy-migration.cjs');
const { withoutRequest } = require('../provider/request-context.cjs');

// Adapt Folia's unmodified provider backends to the private Node host, without Electron windows.
async function createBackends({ store, safeStorage, root, openExternal, log = () => {}, warn = () => {} }, dependencies = {}) {
    if (!store || !safeStorage || typeof root !== 'string' || !path.isAbsolute(root))
        throw new Error('Music backend requires an absolute state root and encrypted storage');
    const vendorRoot = dependencies.vendorRoot || path.resolve(__dirname, '..', 'vendor', 'folia');
    const load = dependencies.loadVendor || (relative => require(path.join(vendorRoot, relative)));
    const rawFetch = dependencies.rawFetch || globalThis.fetch.bind(globalThis);
    const makeWorker = dependencies.makeWorker || ((file, options) => new Worker(file, options));
    const token = (dependencies.randomBytes || crypto.randomBytes)(32).toString('hex');
    const logger = { log, info: log, debug: log, warn, error: warn };
    const servers = new Set();
    const workers = new Set();
    const listeners = new Map();
    const tempRoot = path.join(root, 'netease-cache');
    fs.mkdirSync(tempRoot, { recursive: true });
    fs.mkdirSync(path.join(root, 'qq-auth-state'), { recursive: true });
    let closed = false;
    let shutdownPromise = null;
    let qqStart = null;
    let neteaseStart = null;
    let kugouUsed = false;
    const broadcast = (channel, value) => {
        for (const listener of listeners.get(channel) || []) {
            try { listener(value); } catch { warn('[MusicRuntime] backend status listener failed'); }
        }
    };
    const on = channel => listener => {
        if (typeof listener !== 'function') throw new TypeError('Expected status listener');
        if (!listeners.has(channel)) listeners.set(channel, new Set());
        listeners.get(channel).add(listener);
        return () => listeners.get(channel)?.delete(listener);
    };
    const assertOpen = () => { if (closed) throw new Error('Music backend is closed'); };
    const { createQqAuthSessionRepository } = load('electron/qq/qqAuthSessionRepository.cjs');
    const { createQqBackend } = load('electron/qq/qqBackend.cjs');
    const { startQqApi } = load('electron/qq/qqApiStartup.cjs');
    const { createNeteaseBackend } = load('electron/netease/neteaseBackend.cjs');
    const { createKugouApiBridge } = load('electron/kugou/kugouApiBridge.cjs');
    const authSessionRepository = createQqAuthSessionRepository({ store, safeStorage, logger });
    const qq = createQqBackend({
        broadcast, logger, authSessionRepository,
        getStateFilePath: () => path.join(root, 'qq-auth-state', 'qq-device.json'),
        startServer: options => startQqApi({
            ...options,
            loadModule(target) {
                const guard = installPrivateListeners({ token, servers });
                try { return dependencies.loadPackage ? dependencies.loadPackage(target) : require(target); }
                finally { guard.restore(); }
            },
        }),
    });
    const netease = createNeteaseBackend({
        broadcast, logger,
        onUnhandledRejection: () => warn('[MusicRuntime] NetEase worker rejected a request'),
        createWorker: () => {
            assertOpen();
            const worker = makeWorker(path.join(__dirname, 'netease-worker.cjs'), {
                workerData: { vendorRoot, tempRoot, token },
                stdout: true, stderr: true,
            });
            workers.add(worker);
            worker.once('exit', () => workers.delete(worker));
            // A vendor exception is a backend failure, never an uncaught host exception.
            worker.on('error', () => warn('[MusicRuntime] NetEase worker failed'));
            // Folia relays its own console through messages. Any raw package output stays off JSON stdout.
            worker.stdout?.on('data', () => log('[MusicRuntime] NetEase worker output'));
            worker.stderr?.on('data', () => warn('[MusicRuntime] NetEase worker output'));
            // Folia's Electron host deliberately rethrows a worker error via its
            // default listener. Here the same worker exit rejects pending calls,
            // while the private runtime must stay alive for the other providers.
            const control = {
                on(event, listener) {
                    if (event !== 'error') worker.on(event, listener);
                    return control;
                },
                postMessage: value => worker.postMessage(value),
            };
            return control;
        },
    });
    const kugou = createKugouApiBridge({ store, safeStorage, warn });
    const migration = createLegacyMigration({
        store, safeStorage, root, authSessionRepository,
        canMigrateQq: () => qqStart === null,
        canMigrateKugou: () => !kugouUsed,
        loadQq: dependencies.loadQq,
    });
    const ensureQq = () => {
        assertOpen();
        // The pinned package listens on first require and cannot restart from the module cache.
        // Server callbacks outlive the login which first requested the port.
        // They must not inherit that request's later cancellation write fence.
        if (!qqStart) qqStart = withoutRequest(() => Promise.resolve().then(() => qq.start()));
        return qqStart;
    };
    const ensureNetease = () => {
        assertOpen();
        if (!neteaseStart) neteaseStart = withoutRequest(() => Promise.resolve().then(() => netease.start()).finally(() => { neteaseStart = null; }));
        return neteaseStart;
    };
    const ports = () => new Set([qq.getPort(), netease.getPort()].filter(port => Number.isInteger(port) && port > 0));
    const fetch = createPrivateFetch({ token, ports, rawFetch, isClosed: () => closed });
    const shutdown = () => {
        if (shutdownPromise) return shutdownPromise;
        closed = true;
        listeners.clear();
        shutdownPromise = (async () => {
            // Closing a server with an open keep-alive must not leave the stdio host behind.
            for (const server of servers) server.closeAllConnections?.();
            await Promise.allSettled([
                qqStart?.then(() => qq.stop()),
                ...[...workers].map(worker => worker.terminate()),
            ].filter(Boolean));
            for (const server of servers) {
                server.closeAllConnections?.();
                if (server.listening) await new Promise(resolve => server.close(resolve));
            }
            servers.clear();
        })();
        return shutdownPromise;
    };
    return {
        fetch,
        ...migration,
        async getQqPort() { await ensureQq(); return qq.getPort(); },
        async getQqApiStatus() { assertOpen(); return qq.getStatus(); },
        onQqApiStatusChanged: on('qq-api-status-changed'),
        async getNeteasePort() { await ensureNetease(); return netease.getPort(); },
        async getNeteaseApiStatus() { assertOpen(); return netease.getStatus(); },
        async restartNeteaseApi(reason) { assertOpen(); return withoutRequest(() => netease.start(reason)); },
        onNeteaseApiStatusChanged: on('netease-api-status-changed'),
        async getKugouApiStatus() { assertOpen(); kugouUsed = true; return kugou.getStatus(); },
        async kugouRequest(operation, params) { assertOpen(); kugouUsed = true; return kugou.request(operation, params); },
        async getLoginDiagnostics(providerId) {
            assertOpen();
            const backend = providerId === 'qq' ? qq : providerId === 'netease' ? netease : null;
            return backend ? backend.getDiagnostics() : null;
        },
        async openExternalUrl(url) { assertOpen(); return openExternal?.(url) ?? false; },
        shutdown,
    };
}

module.exports = { createBackends };
