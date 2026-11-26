import path from 'node:path';
import fs from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { runInNewContext } from 'node:vm';
import { fileURLToPath } from 'node:url';
import { build, transform } from 'esbuild';

// Host substitutions replace browser storage/react bindings, never the provider protocol code.
const directory = path.dirname(fileURLToPath(import.meta.url));
const runtime = path.dirname(directory);
const substitutions = new Map([
    ['/src/i18n/config', 'i18n.ts'],
    ['/src/services/db', 'cache.cjs'],
    ['/src/utils/lyrics/workerClient', 'lyrics-worker.ts'],
]);
const hostPlugin = {
    name: 'lume-provider-host',
    setup(builder) {
        // Provider labels need no React/i18next runtime or unrelated application text.
        builder.onLoad({ filter: /[\\/]i18n[\\/]locales[\\/](?:zh-CN|en)\.ts$/ }, async args => {
            const source = await fs.readFile(args.path, 'utf8');
            const compiled = await transform(source, { loader: 'ts', format: 'cjs', target: 'node24' });
            const data = { module: { exports: {} } };
            runInNewContext(compiled.code, data, { timeout: 1000, filename: args.path });
            const all = data.module.exports.default;
            const groups = ['home', 'qishui', 'listeningRank', 'navidrome', 'personalFmMode', 'commandPalette'];
            const labels = Object.fromEntries(groups.filter(key => all[key]).map(key => [key, all[key]]));
            labels.ui = { cloudDrive: all.ui?.cloudDrive };
            return { contents: `export default ${JSON.stringify(labels)}`, loader: 'js' };
        });
        builder.onResolve({ filter: /^zustand$/ }, () => ({ path: path.join(directory, 'state.cjs') }));
        builder.onResolve({ filter: /^\./ }, args => {
            const resolved = path.resolve(args.resolveDir, args.path).replaceAll('\\', '/').replace(/\.[cm]?[jt]sx?$/, '');
            for (const [suffix, replacement] of substitutions) {
                if (resolved.endsWith(suffix)) return { path: path.join(directory, replacement) };
            }
            return undefined;
        });
    },
};

const result = await build({
    entryPoints: [path.join(directory, 'entry.ts')],
    outfile: path.join(runtime, 'dist', 'providers.cjs'),
    bundle: true,
    platform: 'node',
    format: 'cjs',
    target: 'node24',
    sourcemap: false,
    metafile: true,
    logLevel: 'warning',
    define: { 'import.meta': '{"env":{"MODE":"production"}}' },
    external: ['@applemusic-like-lyrics/ttml', '@xmldom/xmldom', 'blueimp-md5', 'opencc-js', 'wanakana'],
    plugins: [hostPlugin],
});
const inputFiles = [...new Set([...Object.keys(result.metafile.inputs).filter(input => !input.startsWith('<')).map(input => path.resolve(input)),
    path.join(directory, 'build.mjs'), path.join(directory, 'build.cjs')])];
const inputs = [];
for (const file of inputFiles) {
    const relative = path.relative(runtime, file).replaceAll('\\', '/');
    if (relative.startsWith('../') || relative.includes('node_modules/')) continue;
    inputs.push({ path: relative, sha256: createHash('sha256').update(await fs.readFile(file)).digest('hex') });
}
inputs.sort((left, right) => left.path.localeCompare(right.path));
await fs.writeFile(path.join(runtime, 'dist', 'providers-inputs.json'), JSON.stringify({ schemaVersion: 1, inputs }, null, 2) + '\n');
console.log(JSON.stringify({ bundle: 'dist/providers.cjs', inputs: Object.keys(result.metafile.inputs).length,
    bytes: Object.values(result.metafile.outputs).reduce((sum, output) => sum + output.bytes, 0) }));
