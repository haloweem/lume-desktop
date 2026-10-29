// Reproduce the browser-only bundle from locked official npm package archives.
// node ui/tools/vendor-notes-editor.mjs [--workdir existing-build-directory]
import { createHash } from "node:crypto";
import { copyFile, mkdir, mkdtemp, readFile, readdir, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const target = fileURLToPath(new URL("../vendor/notes-editor/", import.meta.url));
const provided = process.argv.indexOf("--workdir");
const work = provided >= 0 ? resolve(process.argv[provided + 1]) : await mkdtemp(join(tmpdir(), "lume-notes-editor-"));
await mkdir(work, { recursive: true });
for (const name of ["package.json", "package-lock.json"])
  await copyFile(join(target, name), join(work, name));
const npmArgs = ["ci", "--ignore-scripts", "--no-audit", "--no-fund"];
const install = spawnSync(process.platform === "win32" ? "cmd.exe" : "npm",
  process.platform === "win32" ? ["/d", "/s", "/c", "npm.cmd ci --ignore-scripts --no-audit --no-fund"] : npmArgs,
  { cwd: work, stdio: "inherit", windowsHide: true });
if (install.status !== 0) throw new Error("Pinned npm ci failed");
const require = createRequire(join(work, "package.json"));
const { build } = require("esbuild");
const built = await build({ entryPoints: [fileURLToPath(new URL("notes-editor-entry.mjs", import.meta.url))],
  nodePaths: [join(work, "node_modules")], bundle: true, format: "esm", target: "es2022", minify: true,
  legalComments: "eof", write: false, metafile: true,
  plugins: [{ name: "existing-katex", setup(build) {
    build.onResolve({ filter: /^katex$/ }, () => ({ path: "../notes-math/katex.mjs", external: true }));
  } }],
});
await writeFile(join(target, "editor.mjs"), built.outputFiles[0].contents);
const lock = JSON.parse(await readFile(join(target, "package-lock.json"), "utf8"));
const packages = [];
await mkdir(join(target, "licenses"), { recursive: true });
for (const [location, info] of Object.entries(lock.packages)) {
  if (!location || location.includes("@esbuild/")) continue;
  const directory = join(work, location);
  let names;
  try { names = await readdir(directory); } catch { continue; }
  const packageInfo = JSON.parse(await readFile(join(directory, "package.json"), "utf8"));
  const licenses = names.filter((name) => /^(LICENSE|LICENCE|COPYING)(\.|$)/i.test(name));
  if (!licenses.length) throw new Error(`No license found for ${packageInfo.name}`);
  const files = [];
  for (const license of licenses) {
    const name = `${packageInfo.name.replace(/[@/]/g, "_")}-${license}.txt`;
    await copyFile(join(directory, license), join(target, "licenses", name));
    files.push(`licenses/${name}`);
  }
  packages.push({ name: packageInfo.name, version: packageInfo.version, license: packageInfo.license,
    source: info.resolved, integrity: info.integrity, licenses: files });
}
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
await writeFile(join(target, "manifest.json"), `${JSON.stringify({ name: "Lume notes editor", tiptap: "3.31.4", esbuild: "0.25.12",
  external: "../notes-math/katex.mjs", sha256: sha256(built.outputFiles[0].contents),
  packages: packages.sort((a, b) => a.name.localeCompare(b.name, "en")) }, null, 2)}\n`);
console.log(`Vendored Tiptap 3.31.4 with ${packages.length} dependency license records.`);
