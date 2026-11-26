'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { PROVIDERS, FOLIA_COMMIT, NODE_VERSION, NODE_SHA256, FORBIDDEN, safeFile, sha256,
    listFiles, describeFiles, sourceInventory, packageInventory } = require('./folia-runtime-inventory.cjs');

function verifyRuntime(runtimeRoot, stage) {
    const manifest = JSON.parse(fs.readFileSync(safeFile(stage, 'runtime-inventory.json'), 'utf8'));
    if (manifest.schemaVersion !== 2 || manifest.foliaCommit !== FOLIA_COMMIT || manifest.nodeVersion !== NODE_VERSION ||
        JSON.stringify(manifest.providers) !== JSON.stringify(PROVIDERS)) throw new Error('Unexpected runtime provenance');
    if (!Array.isArray(manifest.files) || !manifest.files.length || !Array.isArray(manifest.sources))
        throw new Error('Incomplete runtime inventory');
    for (const item of manifest.files) {
        safeFile(stage, item.path);
        if (FORBIDDEN.test(item.path)) throw new Error('Excluded platform in runtime payload');
    }
    const actual = describeFiles(stage, listFiles(stage).filter(file => file !== 'runtime-inventory.json'));
    if (JSON.stringify(actual) !== JSON.stringify(manifest.files)) throw new Error('Runtime payload differs from inventory');
    for (const required of ['main.cjs', 'secure-store.cjs', 'backend/backends.cjs', 'backend/qishui.cjs',
        'provider/environment.cjs', 'provider/facade.cjs', 'dist/providers.cjs', 'licenses/NODE-LICENSE.txt',
        'vendor/folia/LICENSE', 'vendor/SOURCES.md', 'node_modules/@koromix/koffi-win32-x64/package.json']) safeFile(stage, required);
    if (sha256(safeFile(stage, 'LumeMusicProviderHost.exe')) !== NODE_SHA256)
        throw new Error('Runtime Node binary differs from the official pinned binary');
    const packages = packageInventory(stage, actual);
    if (JSON.stringify(packages) !== JSON.stringify(manifest.packages)) throw new Error('Runtime package inventory differs');
    if (packages.some(item => item.name === 'esbuild' || item.name?.startsWith('@esbuild/')))
        throw new Error('Development bundler must not be shipped');
    const currentSources = sourceInventory(runtimeRoot);
    if (JSON.stringify(currentSources) !== JSON.stringify(manifest.sources)) throw new Error('Runtime source inventory is stale');
    return { files: actual.length, packages: packages.length, sourceFiles: currentSources.length,
        bytes: actual.reduce((sum, item) => sum + item.bytes, 0), inventorySha256: sha256(path.join(stage, 'runtime-inventory.json')) };
}

function verifyDescriptor(stage, descriptorPath) {
    const descriptor = JSON.parse(fs.readFileSync(descriptorPath, 'utf8'));
    const inventorySha256 = sha256(safeFile(stage, 'runtime-inventory.json'));
    if (descriptor.schemaVersion !== 1 || descriptor.id !== inventorySha256)
        throw new Error('Runtime descriptor does not match the bundled inventory');
    if (!/^[a-f0-9]{64}$/.test(descriptor.sha256) ||
        descriptor.url !== `https://static.lumedesktop.com/downloads/LumeMusicRuntime-${descriptor.sha256.slice(0, 16)}.zip` ||
        !Number.isSafeInteger(descriptor.size) || descriptor.size < 22 ||
        !Number.isSafeInteger(descriptor.unpackedBytes) || descriptor.unpackedBytes <= 0)
        throw new Error('Invalid runtime download descriptor');
    return { descriptorMatchesInventory: true, inventorySha256 };
}

module.exports = { verifyRuntime, verifyDescriptor };
if (require.main === module) {
    const [runtimeRoot, stage, descriptor] = process.argv.slice(2).map(value => path.resolve(value));
    if (!runtimeRoot || !stage) throw new Error('Expected runtime source and staged runtime directory');
    console.log(JSON.stringify({ ...verifyRuntime(runtimeRoot, stage),
        ...(descriptor ? verifyDescriptor(stage, descriptor) : {}) }));
}
