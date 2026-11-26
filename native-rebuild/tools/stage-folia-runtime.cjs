'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { PROVIDERS, FOLIA_COMMIT, NODE_VERSION, NODE_SHA256, FORBIDDEN, sha256,
    listFiles, describeFiles, vendorClosure, sourceInventory, packageInventory } = require('./folia-runtime-inventory.cjs');

function keepDependencyFile(relative) {
    const parts = relative.replaceAll('\\', '/').split('/');
    const name = parts.at(-1);
    // Notices in otherwise excluded test/example/tool folders must survive too.
    if (parts.some(part => /licen[cs]e|notice|copying|copyright|attribution/i.test(part))) return true;
    if (parts.slice(0, -1).some(part => /^(?:\.bin|\.cache|\.git|\.github|\.husky|\.vscode|\.yarn|tests?|__tests__|__mocks__|fixtures?|examples?|benchmarks?|bench|coverage)$/i.test(part))) return false;
    if (/\.(?:[cm]?js|css|[cm]?ts)\.map$/i.test(name) || /\.d\.[cm]?ts(?:\.map)?$/i.test(name)) return false;
    if (/^(?:\.package-lock\.json|\.(?:editorconfig|gitignore|gitattributes|npmignore|travis\.yml|taprc|yarnrc(?:\.yml)?)|\.(?:eslintrc|prettierrc|babelrc|nycrc|lintstagedrc|mocharc)(?:\..*)?|(?:eslint|babel|rollup|webpack|jest|vitest)\.config\.[cm]?[jt]s|tsconfig(?:\.[^.]+)*\.json)$/i.test(name)) return false;
    // Pinned NCM 4.41.1 exposes these only as optional HTTP help pages/screenshots.
    // Folia uses API endpoints; keep its module/util/data and other public assets.
    if (/^@neteasecloudmusicapienhanced\/api\/public\/(?:docs|static)\//.test(parts.join('/'))) return false;
    return true;
}

module.exports = { keepDependencyFile };
if (require.main === module) {
// Assemble only the four-provider runtime and the fixed production npm closure.
const [runtimeRoot, destination, dependencyRoot, nodeFile, nodeLicense] = process.argv.slice(2).map(value => value && path.resolve(value));
if ([runtimeRoot, destination, dependencyRoot, nodeFile, nodeLicense].some(value => !value))
    throw new Error('Expected runtime, destination, production dependencies, Node and Node license');
if (fs.existsSync(destination) && fs.readdirSync(destination).length) throw new Error('Runtime stage must be empty');
if (sha256(nodeFile) !== NODE_SHA256) throw new Error('Node executable differs from the pinned official binary');
// Record inputs before copying so concurrent edits fail the final source check.
const sources = sourceInventory(runtimeRoot);
fs.mkdirSync(destination, { recursive: true });
const copy = (from, to) => {
    fs.mkdirSync(path.dirname(to), { recursive: true });
    fs.copyFileSync(from, to);
};
const tree = (from, to, keep = () => true, relativeRoot = from) => {
    for (const entry of fs.readdirSync(from, { withFileTypes: true })) {
        if (entry.isSymbolicLink()) throw new Error(`Unexpected linked dependency: ${entry.name}`);
        const source = path.join(from, entry.name), target = path.join(to, entry.name);
        if (!keep(entry.name, entry.isDirectory(), path.relative(relativeRoot, source))) continue;
        if (entry.isDirectory()) tree(source, target, keep, relativeRoot);
        else copy(source, target);
    }
};
copy(nodeFile, path.join(destination, 'LumeMusicProviderHost.exe'));
copy(nodeLicense, path.join(destination, 'licenses', 'NODE-LICENSE.txt'));
for (const entry of fs.readdirSync(runtimeRoot, { withFileTypes: true }))
    if (entry.isFile() && (entry.name.endsWith('.cjs') || ['package.json', 'package-lock.json'].includes(entry.name)))
        copy(path.join(runtimeRoot, entry.name), path.join(destination, entry.name));
tree(path.join(runtimeRoot, 'backend'), path.join(destination, 'backend'), name => name.endsWith('.cjs') && !/spotify|ytm|bodian|apple/i.test(name));
tree(path.join(runtimeRoot, 'provider'), path.join(destination, 'provider'), name => name.endsWith('.cjs') && name !== 'build.cjs');
copy(path.join(runtimeRoot, 'dist', 'providers.cjs'), path.join(destination, 'dist', 'providers.cjs'));

const vendorRoot = path.join(runtimeRoot, 'vendor', 'folia');
const stageVendor = path.join(destination, 'vendor', 'folia');
const copied = vendorClosure(runtimeRoot);
for (const relative of copied) copy(path.join(vendorRoot, relative), path.join(stageVendor, relative));
copy(path.join(vendorRoot, 'LICENSE'), path.join(stageVendor, 'LICENSE'));
copy(path.join(runtimeRoot, 'vendor', 'SOURCES.md'), path.join(destination, 'vendor', 'SOURCES.md'));
tree(path.join(dependencyRoot, 'node_modules'), path.join(destination, 'node_modules'),
    (_name, directory, relative) => directory || keepDependencyFile(relative));

const files = describeFiles(destination, listFiles(destination));
const packages = packageInventory(destination, files);
const forbidden = files.filter(item => FORBIDDEN.test(item.path));
if (forbidden.length) throw new Error('Excluded platform reached the runtime stage');
if (JSON.stringify(sources) !== JSON.stringify(sourceInventory(runtimeRoot)))
    throw new Error('Runtime sources changed during staging; rebuild from stable inputs');
fs.writeFileSync(path.join(destination, 'runtime-inventory.json'), JSON.stringify({
    schemaVersion: 2, providers: PROVIDERS,
    foliaCommit: FOLIA_COMMIT, nodeVersion: NODE_VERSION,
    files, packages, sources,
}, null, 2));
console.log(JSON.stringify({ files: files.length, bytes: files.reduce((sum, item) => sum + item.bytes, 0), packages: packages.length, foliaFiles: copied.length }));
}
