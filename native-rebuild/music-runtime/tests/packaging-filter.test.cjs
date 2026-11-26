'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { keepDependencyFile } = require('../../tools/stage-folia-runtime.cjs');

test('keeps runtime code, package metadata, native libraries and data in all source/output directories', () => {
    for (const file of ['pkg/src/index.js', 'pkg/dist/full.js', 'pkg/lib/impl.cjs', 'xml2js/lib/xml2js.bc.js',
        'opencc-js/dist/esm/full.js', 'opencc-js/dist/umd/full.js', 'pkg/package.json', 'pkg/src/native.node',
        'pkg/lib/helper.exe', 'pkg/lib/plugin.dll', 'pkg/dist/module.wasm', 'pkg/data/table.json',
        'pkg/data/deviceid.txt', 'pkg/data/world.map', 'pkg/src/runtime.ts', 'pkg/public/icon.png'])
        assert.equal(keepDependencyFile(file), true, file);
});

test('omits source maps, declarations, test/example/benchmark files and development metadata', () => {
    for (const file of ['pkg/dist/index.js.map', 'pkg/dist/style.css.map', 'pkg/index.d.ts', 'pkg/index.d.cts',
        'pkg/index.d.mts.map', 'pkg/test/fixture.js', 'pkg/examples/image.png', 'pkg/benchmark/test.js',
        'pkg/__tests__/test.json', 'pkg/.github/workflows/build.yml', 'pkg/.yarn/plugins/tool.cjs',
        'pkg/tsconfig.build.json', 'pkg/eslint.config.mjs', 'pkg/.npmignore', '.package-lock.json'])
        assert.equal(keepDependencyFile(file), false, file);
});

test('removes only the verified NCM help asset directories', () => {
    for (const file of ['@neteasecloudmusicapienhanced/api/public/docs/aigen.png',
        '@neteasecloudmusicapienhanced/api/public/static/eapi_params.png']) assert.equal(keepDependencyFile(file), false, file);
    for (const file of ['@neteasecloudmusicapienhanced/api/data/deviceid.txt',
        '@neteasecloudmusicapienhanced/api/module/login_qr_create.js',
        '@neteasecloudmusicapienhanced/api/public/qrlogin.html', 'other/public/static/actual-runtime.png'])
        assert.equal(keepDependencyFile(file), true, file);
});

test('keeps every license and notice even inside excluded folders', () => {
    for (const file of ['pkg/LICENSE', 'pkg/NOTICE.md', 'pkg/test/COPYING', 'pkg/example/ATTRIBUTION.txt',
        'pkg/.yarn/LICENSE.txt', 'pkg/test/licenses/third-party.txt',
        '@neteasecloudmusicapienhanced/api/public/docs/NOTICE', 'pkg/COPYRIGHT.txt'])
        assert.equal(keepDependencyFile(file), true, file);
});
