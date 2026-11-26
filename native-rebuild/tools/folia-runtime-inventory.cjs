'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const PROVIDERS = ['netease', 'qq', 'kugou', 'qishui'];
const FOLIA_COMMIT = 'a44758bbc02929225ca7304b5a4c4580d7b4e87b';
const NODE_VERSION = '24.15.0';
const NODE_SHA256 = '3331e1ffe19874215472217c5e94f5a0c6d8e18c4ac7111d3937aa0ad5e9b4a5';
const FORBIDDEN = /(?:^|\/)(?:spotify|ytm|bodian|node-librespot|appleMusic|apple-music|apple\.cjs)(?:\/|$)/i;
const slash = value => value.split(path.sep).join('/');
const sha256 = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

function safeFile(root, relative) {
    if (typeof relative !== 'string' || !relative || relative.includes('\\') ||
        relative.split('/').some(part => !part || part === '.' || part === '..') ||
        /^[A-Za-z]:/.test(relative) || path.isAbsolute(relative)) throw new Error('Unsafe inventory path');
    const file = path.join(root, relative);
    let current = root;
    for (const part of relative.split('/')) {
        current = path.join(current, part);
        if (fs.lstatSync(current).isSymbolicLink()) throw new Error(`Linked inventory path: ${relative}`);
    }
    if (!fs.statSync(file).isFile()) throw new Error(`Inventory path is not a file: ${relative}`);
    return file;
}

function listFiles(root) {
    const result = [];
    const visit = folder => {
        for (const entry of fs.readdirSync(folder, { withFileTypes: true })) {
            const full = path.join(folder, entry.name);
            if (entry.isSymbolicLink()) throw new Error(`Unexpected linked runtime file: ${entry.name}`);
            if (entry.isDirectory()) visit(full);
            else if (entry.isFile()) result.push(slash(path.relative(root, full)));
            else throw new Error(`Unexpected runtime file kind: ${entry.name}`);
        }
    };
    visit(root);
    return result.sort();
}

function describeFiles(root, relatives) {
    return [...new Set(relatives)].sort().map(relative => {
        const file = safeFile(root, relative);
        return { path: relative, bytes: fs.statSync(file).size, sha256: sha256(file) };
    });
}

function vendorClosure(runtimeRoot) {
    const vendorRoot = path.join(runtimeRoot, 'vendor', 'folia');
    const pending = ['qq/qqAuthSessionRepository.cjs', 'qq/qqBackend.cjs', 'qq/qqApiStartup.cjs',
        'netease/neteaseBackend.cjs', 'netease/neteaseApiWorker.cjs', 'kugou/kugouApiBridge.cjs',
        'qishui/qishuiApiBridge.cjs'].map(file => `electron/${file}`);
    const copied = new Set();
    while (pending.length) {
        const relative = pending.pop();
        if (copied.has(relative)) continue;
        if (FORBIDDEN.test(relative)) throw new Error('Excluded platform in backend dependency closure');
        const file = safeFile(vendorRoot, relative);
        copied.add(relative);
        if (!/\.(?:cjs|js)$/.test(file)) continue;
        for (const match of fs.readFileSync(file, 'utf8').matchAll(/require\(\s*['"](\.[^'"]+)['"]\s*\)/g)) {
            const base = path.resolve(path.dirname(file), match[1]);
            const dependency = [base, `${base}.cjs`, `${base}.js`, `${base}.json`, path.join(base, 'index.cjs')]
                .find(candidate => fs.existsSync(candidate) && fs.statSync(candidate).isFile());
            if (!dependency) throw new Error(`Unresolved Folia dependency in ${relative}`);
            pending.push(slash(path.relative(vendorRoot, dependency)));
        }
    }
    return [...copied].sort();
}

function sourceInventory(runtimeRoot) {
    const nativeRoot = path.dirname(runtimeRoot);
    const files = ['music-runtime/package.json', 'music-runtime/package-lock.json',
        'music-runtime/dist/providers.cjs', 'music-runtime/dist/providers-inputs.json',
        'music-runtime/vendor/SOURCES.md', 'music-runtime/vendor/folia/LICENSE',
        'tools/build-folia-runtime.ps1', 'tools/stage-folia-runtime.cjs',
        'tools/verify-folia-runtime.cjs', 'tools/folia-runtime-inventory.cjs'];
    for (const folder of ['', 'backend', 'provider']) {
        for (const entry of fs.readdirSync(path.join(runtimeRoot, folder), { withFileTypes: true })) {
            if (entry.isFile() && /\.(?:cjs|mjs|ts)$/.test(entry.name))
                files.push(['music-runtime', folder, entry.name].filter(Boolean).join('/'));
        }
    }
    const bundleInputs = JSON.parse(fs.readFileSync(path.join(runtimeRoot, 'dist', 'providers-inputs.json'), 'utf8'));
    if (bundleInputs.schemaVersion !== 1 || !Array.isArray(bundleInputs.inputs) || !bundleInputs.inputs.length)
        throw new Error('Provider bundle source receipt is missing or invalid');
    for (const item of bundleInputs.inputs) {
        if (sha256(safeFile(runtimeRoot, item.path)) !== item.sha256)
            throw new Error(`Provider bundle is stale: ${item.path}`);
        files.push(`music-runtime/${item.path}`);
    }
    files.push(...vendorClosure(runtimeRoot).map(relative => `music-runtime/vendor/folia/${relative}`));
    return describeFiles(nativeRoot, files);
}

function packageInventory(root, files) {
    return files.filter(item => /(?:^|\/)node_modules\/(?:@[^/]+\/)?[^/]+\/package\.json$/.test(item.path)).map(item => {
        const value = JSON.parse(fs.readFileSync(safeFile(root, item.path), 'utf8'));
        return { path: item.path, name: value.name, version: value.version, license: value.license ?? null };
    });
}

module.exports = { PROVIDERS, FOLIA_COMMIT, NODE_VERSION, NODE_SHA256, FORBIDDEN,
    safeFile, listFiles, describeFiles, sha256, sourceInventory, vendorClosure, packageInventory };
