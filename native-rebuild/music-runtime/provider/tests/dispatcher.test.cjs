'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { EventEmitter } = require('node:events');
const context = require('../request-context.cjs');

function harness(facade, startBackend) {
    const input = new EventEmitter(), replies = [], saved = new Map();
    let guarded;
    const modules = {
        'node:readline': { createInterface: () => input }, 'node:path': path,
        './secure-store.cjs': { createSafeStorage: () => ({}), createStore: () => ({
            get: (key, fallback) => saved.get(key) ?? fallback, set: (key, value) => saved.set(key, value), delete: key => saved.delete(key),
        }) },
        './backend/backends.cjs': { createBackends: async options => {
            guarded = options.store; await startBackend?.(); return { fetch: async () => ({}) };
        } },
        './backend/qishui.cjs': { createQishuiBackend: () => ({}) },
        './provider/environment.cjs': { installProviderEnvironment: () => ({ dispose() {} }) },
        './provider/request-context.cjs': context,
        './dist/providers.cjs': { providers: {} },
        './provider/facade.cjs': { createFacade: () => facade },
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../../main.cjs'), 'utf8'), {
        require: name => { assert.ok(modules[name], name); return modules[name]; },
        process: { stdout: { write: line => replies.push(JSON.parse(line)) }, stdin: {}, on() {}, exit() {} },
        console: {}, Buffer, setTimeout: () => ({ unref() {} }),
    }, { filename: 'main.cjs' });
    return { send: message => input.emit('line', JSON.stringify(message)), replies, saved, store: () => guarded };
}
async function until(predicate) {
    for (let count = 0; count < 50; count++) {
        if (predicate()) return;
        await new Promise(resolve => setImmediate(resolve));
    }
    assert.fail('dispatcher did not settle');
}

test('native cancel-request cancels the matching facade attempt and blocks its late credential write', async () => {
    let release; const cancellations = [];
    const host = harness({
        auth: async () => { await new Promise(resolve => { release = resolve; }); host.store().set('session', 'late'); return { status: 'signed-in' }; },
        cancel: async (...args) => cancellations.push(args),
    });
    host.send({ id: 1, action: 'init', params: { root: path.resolve('fixture-data') } });
    await until(() => host.replies.length === 1);
    assert.deepEqual([...host.replies[0].result.providers].sort(), ['kugou', 'netease', 'qishui', 'qq']);
    host.send({ id: 2, action: 'auth', provider: 'qq', params: { operation: 'poll', attempt: 8 } });
    await until(() => release);
    host.send({ id: 0, action: 'cancel-request', target: 2 });
    await until(() => cancellations.length === 1); release();
    await new Promise(resolve => setImmediate(resolve));
    assert.deepEqual(cancellations, [['qq', 8]]);
    assert.equal(host.saved.has('session'), false);
    assert.equal(host.replies.some(reply => reply.id === 2), false);
});

test('a cancelled request waiting for initialization never opens a login flow', async () => {
    let release; let started = 0;
    const host = harness({ auth: async () => { started++; }, cancel: async () => {} },
        () => new Promise(resolve => { release = resolve; }));
    host.send({ id: 1, action: 'init', params: { root: path.resolve('fixture-data') } });
    await until(() => release);
    host.send({ id: 2, action: 'auth', provider: 'qishui', params: { operation: 'begin', attempt: 1 } });
    host.send({ id: 0, action: 'cancel-request', target: 2 }); release();
    await until(() => host.replies.length === 1);
    assert.equal(started, 0);
    assert.equal(host.replies[0].id, 1);
});
