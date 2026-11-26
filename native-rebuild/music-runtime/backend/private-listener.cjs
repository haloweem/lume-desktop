'use strict';

const crypto = require('node:crypto');
const http = require('node:http');

// The imported provider servers are reachable only by this host's authenticated fetch boundary.
const TOKEN_HEADER = 'x-lume-music-runtime';
const GUARDED = Symbol('lumePrivateListener');

function sameToken(received, token) {
    if (typeof received !== 'string') return false;
    const actual = Buffer.from(received);
    const expected = Buffer.from(token);
    return actual.length === expected.length && crypto.timingSafeEqual(actual, expected);
}

function guardServer(server, token, servers) {
    if (server[GUARDED]) return;
    server[GUARDED] = true;
    servers?.add(server);
    const emit = server.emit;
    server.emit = function privateRequest(event, ...args) {
        if (['request', 'checkContinue', 'checkExpectation', 'upgrade', 'connect'].includes(event)) {
            const [request, response] = args;
            if (!sameToken(request?.headers?.[TOKEN_HEADER], token)) {
                if (event === 'upgrade' || event === 'connect') response?.destroy();
                else {
                    response.writeHead(403, { 'content-type': 'application/json', 'connection': 'close' });
                    response.end('{"error":"runtime-access-denied"}');
                }
                return true;
            }
            // It is a local transport secret, never an upstream provider header.
            delete request.headers[TOKEN_HEADER];
        }
        return emit.call(this, event, ...args);
    };
}

function loopbackArguments(args) {
    const values = [...args];
    if (typeof values[0] === 'object' && values[0] !== null) {
        const options = values[0];
        if (!Object.hasOwn(options, 'port') || options.path || options.handle || options.fd !== undefined)
            throw new Error('Provider listeners must use a private TCP port');
        values[0] = { ...options, host: '127.0.0.1', exclusive: true };
        return values;
    }
    if (!Number.isInteger(Number(values[0])) || Number(values[0]) < 0 || Number(values[0]) > 65535)
        throw new Error('Provider listeners must use a private TCP port');
    values[0] = Number(values[0]);
    if (typeof values[1] === 'string') values[1] = '127.0.0.1';
    else values.splice(1, 0, '127.0.0.1');
    return values;
}

// QQ listens synchronously on import; NetEase installs this inside its dedicated worker.
function installPrivateListeners({ token, servers = new Set() }) {
    if (typeof token !== 'string' || token.length < 32) throw new Error('Missing runtime access token');
    const previous = http.Server.prototype.listen;
    function listen(...args) {
        guardServer(this, token, servers);
        return previous.apply(this, loopbackArguments(args));
    }
    http.Server.prototype.listen = listen;
    return {
        servers,
        restore() {
            if (http.Server.prototype.listen === listen) http.Server.prototype.listen = previous;
        },
    };
}

function createPrivateFetch({ token, ports, rawFetch, isClosed = () => false }) {
    return async function privateFetch(input, init) {
        if (isClosed()) throw new Error('Music backend is closed');
        const request = new Request(input, init);
        const url = new URL(request.url);
        const headers = new Headers(request.headers);
        headers.delete(TOKEN_HEADER);
        if (url.protocol === 'http:' && url.hostname === '127.0.0.1' && ports().has(Number(url.port))) {
            headers.set(TOKEN_HEADER, token);
            // An API redirect must not forward the private transport token to another origin.
            return rawFetch(new Request(request, { headers, redirect: 'error' }));
        }
        return rawFetch(new Request(request, { headers }));
    };
}

module.exports = { TOKEN_HEADER, installPrivateListeners, createPrivateFetch };
