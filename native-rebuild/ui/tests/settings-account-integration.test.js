import test from "node:test";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

test("real app settings/account wiring preserves navigation and isolates forms", {
  skip: !process.env.LUME_JSDOM_PATH,
}, () => {
  const result = spawnSync(process.execPath, [
    fileURLToPath(new URL("./settings-account-integration.mjs", import.meta.url)),
    process.env.LUME_JSDOM_PATH,
  ], { encoding: "utf8", timeout: 30000 });
  assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
});
