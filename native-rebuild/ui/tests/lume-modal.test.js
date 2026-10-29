import test from "node:test";
import assert from "node:assert/strict";
import {existsSync} from "node:fs";
import {spawnSync} from "node:child_process";
import {homedir} from "node:os";
import {fileURLToPath} from "node:url";
import path from "node:path";

const moduleUrl = new URL("../lume-modal.js", import.meta.url);

test("Lume modal controller exposes quota and update dialogs without MessageBox", async () => {
  assert.equal(existsSync(moduleUrl), true, "需要独立 lume-modal.js");
  const source = await import(moduleUrl);
  assert.equal(typeof source.createLumeModalController, "function");
  assert.doesNotMatch(await import("node:fs").then(({readFileSync}) =>
    readFileSync(fileURLToPath(moduleUrl), "utf8")), /\bMessageBox\b/);
});

test("real Edge quota/update lifecycle, production bridge and glass layout", {
  skip: process.env.LUME_UI_BROWSER_TESTS !== "1",
}, () => {
  const playwright = process.env.LUME_PLAYWRIGHT_DIR ||
    path.join(homedir(), ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright");
  const result = spawnSync(process.execPath, [
    fileURLToPath(new URL("./lume-modal-browser.mjs", import.meta.url)), playwright,
    process.env.LUME_MODAL_OUTPUT || path.join(homedir(), ".codex/visualizations/2026/10/07/01a118ac-621f-7840-a947-af6b243f6623/lume-dialogs"),
  ], {encoding: "utf8", timeout: 120000});
  assert.equal(result.status, 0, result.stdout + result.stderr);
  console.log(result.stdout.trim());
});
