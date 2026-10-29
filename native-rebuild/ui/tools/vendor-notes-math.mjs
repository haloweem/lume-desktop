// Reproduce the offline KaTeX assets from the pinned official npm release.
// Run with Node: node ui/tools/vendor-notes-math.mjs
import { createHash } from "node:crypto";
import { mkdir, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { gunzipSync } from "node:zlib";

const version = "0.19.0";
const tarball = `https://registry.npmjs.org/katex/-/katex-${version}.tgz`;
const integrity = "sha512-v6Tznz3zJ7u3niRCoDTsumM2+HA2XXcCu+WAacCeHD2z3p9A9Ks987o5FzfTGBN0e8A0vjEgIDvLbICrXpdw/Q==";
const target = fileURLToPath(new URL("../vendor/notes-math/", import.meta.url));
const sha256 = (data) => createHash("sha256").update(data).digest("hex");

const response = await fetch(tarball);
if (!response.ok) throw new Error(`npm archive download failed: ${response.status}`);
const archive = Buffer.from(await response.arrayBuffer());
if (`sha512-${createHash("sha512").update(archive).digest("base64")}` !== integrity)
  throw new Error("Pinned npm archive integrity does not match");

// Read regular entries directly so reproduction needs no npm install or tar tool.
// Only the allowlisted dist files below can be written into the target directory.
const tar = gunzipSync(archive);
const selected = new Map();
for (let offset = 0; offset + 512 <= tar.length;) {
  const header = tar.subarray(offset, offset + 512);
  if (header.every((byte) => byte === 0)) break;
  const name = header.subarray(0, 100).toString("utf8").replace(/\0.*$/, "");
  const size = parseInt(header.subarray(124, 136).toString("ascii").replace(/\0.*$/, "").trim(), 8);
  if (!Number.isSafeInteger(size) || size < 0 || offset + 512 + size > tar.length)
    throw new Error("Invalid npm archive entry");
  const regular = header[156] === 0 || header[156] === 48;
  let relative = "";
  if (name === "package/LICENSE") relative = "LICENSE.txt";
  else if (name === "package/dist/katex.mjs") relative = "katex.mjs";
  else if (name === "package/dist/katex.min.css") relative = "katex.min.css";
  else if (/^package\/dist\/fonts\/[A-Za-z0-9_-]+\.woff2$/.test(name)) relative = name.slice("package/dist/".length);
  if (regular && relative) selected.set(relative, tar.subarray(offset + 512, offset + 512 + size));
  offset += 512 + Math.ceil(size / 512) * 512;
}
if (!["LICENSE.txt", "katex.mjs", "katex.min.css"].every((name) => selected.has(name)))
  throw new Error("Expected KaTeX dist files are missing");

const css = selected.get("katex.min.css").toString("utf8")
  .replace(/,url\([^)]*\.(?:woff|ttf)\)\s*format\([^)]*\)/g, "");
if (/url\([^)]*\.(?:woff|ttf)\)/.test(css)) throw new Error("CSS contains an unvendored font format");
for (const [, font] of css.matchAll(/url\((fonts\/[^)]+\.woff2)\)/g))
  if (!selected.has(font)) throw new Error(`CSS references missing font ${font}`);
selected.set("katex.min.css", Buffer.from(css));

const manifest = { name: "KaTeX", version, license: "MIT", source: tarball, integrity,
  archiveSha256: sha256(archive), cssTransform: "Keep only the original woff2 font sources.", files: {} };
for (const [name, data] of [...selected].sort(([left], [right]) => left.localeCompare(right, "en"))) {
  await mkdir(dirname(join(target, name)), { recursive: true });
  await writeFile(join(target, name), data);
  manifest.files[name] = sha256(data);
}
await writeFile(join(target, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
console.log(`Vendored KaTeX ${version}: ${selected.size} offline assets; pinned npm integrity verified.`);
