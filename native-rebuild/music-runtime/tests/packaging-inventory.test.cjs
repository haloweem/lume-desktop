'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { PROVIDERS, FOLIA_COMMIT, NODE_VERSION, describeFiles, sourceInventory, sha256 } = require('../../tools/folia-runtime-inventory.cjs');
const { verifyRuntime, verifyDescriptor } = require('../../tools/verify-folia-runtime.cjs');

function fixture(t) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'lume-runtime-inventory-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    fs.writeFileSync(path.join(root, 'main.cjs'), '// immutable fixture\n');
    const manifest = { schemaVersion: 2, providers: PROVIDERS, foliaCommit: FOLIA_COMMIT,
        nodeVersion: NODE_VERSION, files: describeFiles(root, ['main.cjs']), packages: [], sources: [] };
    const write = () => fs.writeFileSync(path.join(root, 'runtime-inventory.json'), JSON.stringify(manifest));
    write();
    return { root, manifest, write };
}

test('reused runtime rejects changed bytes and unlisted extra files before execution', t => {
    const { root } = fixture(t);
    fs.writeFileSync(path.join(root, 'main.cjs'), '// changed fixture\n');
    assert.throws(() => verifyRuntime(root, root), /Runtime payload differs/);
    fs.writeFileSync(path.join(root, 'main.cjs'), '// immutable fixture\n');
    fs.writeFileSync(path.join(root, 'injected.cjs'), '// extra\n');
    assert.throws(() => verifyRuntime(root, root), /Runtime payload differs/);
});

test('an installer cannot reuse a download descriptor after its bundled runtime changes', t => {
    const { root, manifest, write } = fixture(t);
    const descriptorPath = path.join(root, 'music-runtime.json');
    const descriptor = { schemaVersion: 1, id: sha256(path.join(root, 'runtime-inventory.json')),
        sha256: 'a'.repeat(64), url: `https://static.lumedesktop.com/downloads/LumeMusicRuntime-${'a'.repeat(16)}.zip`,
        size: 100, unpackedBytes: 1000 };
    const save = () => fs.writeFileSync(descriptorPath, JSON.stringify(descriptor));
    save();
    assert.equal(verifyDescriptor(root, descriptorPath).descriptorMatchesInventory, true);
    fs.writeFileSync(path.join(root, 'main.cjs'), '// updated provider entry\n');
    manifest.files = describeFiles(root, ['main.cjs']); write();
    assert.throws(() => verifyDescriptor(root, descriptorPath), /descriptor does not match/);
    descriptor.id = sha256(path.join(root, 'runtime-inventory.json')); save();
    assert.equal(verifyDescriptor(root, descriptorPath).descriptorMatchesInventory, true);
});

test('runtime inventory cannot escape its directory or contain duplicate paths', t => {
    const { root, manifest, write } = fixture(t);
    manifest.files.push({ ...manifest.files[0] }); write();
    assert.throws(() => verifyRuntime(root, root), /Runtime payload differs/);
    manifest.files[1].path = '../outside.cjs'; write();
    assert.throws(() => verifyRuntime(root, root), /Unsafe inventory path/);
});

test('four-platform payload rejects an Apple Music companion', t => {
    const { root, manifest, write } = fixture(t);
    assert.deepEqual(PROVIDERS, ['netease', 'qq', 'kugou', 'qishui']);
    fs.mkdirSync(path.join(root, 'apple-music'));
    fs.writeFileSync(path.join(root, 'apple-music', 'companion.exe'), 'excluded fixture');
    manifest.files.push(...describeFiles(root, ['apple-music/companion.exe'])); write();
    assert.throws(() => verifyRuntime(root, root), /Excluded platform in runtime payload/);
});

test('changed provider bundle input requires a fresh bundle before staging', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'lume-runtime-source-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    for (const directory of ['provider', 'backend', 'dist']) fs.mkdirSync(path.join(root, directory));
    const input = path.join(root, 'provider', 'entry.ts');
    fs.writeFileSync(input, 'export const providers = [];\n');
    const receipt = { schemaVersion: 1, inputs: [{ path: 'provider/entry.ts', sha256: sha256(input) }] };
    fs.writeFileSync(path.join(root, 'dist', 'providers-inputs.json'), JSON.stringify(receipt));
    fs.writeFileSync(input, 'export const providers = [1];\n');
    assert.throws(() => sourceInventory(root), /Provider bundle is stale: provider\/entry.ts/);
});
