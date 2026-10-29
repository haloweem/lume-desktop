import test from "node:test";
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { homedir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ui = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const require = createRequire(import.meta.url);
const runtime = path.join(homedir(), ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules");
const enabled = process.env.LUME_UI_BROWSER_TESTS === "1";
const sample = (png, x, y) => {
  const offset = (y * png.width + x) * 4;
  return [...png.data.subarray(offset, offset + 3)];
};
const distance = (a, b) => a.reduce((total, value, index) => total + Math.abs(value - b[index]), 0);

test("便签组件透明回执、拖动缩放与关闭保存握手", { skip: !enabled, timeout: 60000 }, async () => {
  const { chromium } = require(process.env.LUME_PLAYWRIGHT_DIR || path.join(runtime, "playwright"));
  const { PNG } = require(path.join(runtime, "pngjs"));
  const server = createServer(async (request, response) => {
    const pathname = new URL(request.url, "http://127.0.0.1").pathname;
    if (pathname === "/") {
      response.writeHead(200, { "Content-Type": "text/html" }).end(
        '<style>html,body{margin:0;width:100%;height:100%;background:#c62828}iframe{width:100%;height:100%;border:0}</style><iframe src="/notes.html"></iframe>');
      return;
    }
    if (pathname === "/favicon.ico") { response.writeHead(204).end(); return; }
    const file = path.resolve(ui, "." + pathname);
    if (!file.startsWith(ui + path.sep)) { response.writeHead(403).end(); return; }
    try {
      const content = await readFile(file);
      const mime = { ".css": "text/css", ".js": "text/javascript", ".mjs": "text/javascript", ".woff2": "font/woff2" };
      response.writeHead(200, { "Content-Type": mime[path.extname(file)] || "text/html" }).end(content);
    } catch { response.writeHead(404).end(); }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  let browser;
  try {
    browser = await chromium.launch({ headless: true, channel: "msedge" });
    const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
    await page.addInitScript(() => {
      const listeners = new Set();
      window.nativeCommands = [];
      window.chrome = { webview: {
        addEventListener(type, listener) { if (type === "message") listeners.add(listener); },
        postMessage(command) { window.nativeCommands.push(command); },
      } };
      window.emitNative = (data) => { for (const listener of listeners) listener({ data }); };
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    const frame = page.frameLocator("iframe");
    await frame.locator("[data-notes-root]").waitFor();
    const color = async () => sample(PNG.sync.read(await page.screenshot()), 950, 160);
    const setDesktop = (color) => page.evaluate((value) => { document.body.style.background = value; }, color);
    const sendStatus = (status) => frame.locator("body").evaluate((_, value) =>
      window.emitNative({ version: 1, type: "notes-material", status: value }), status);

    const initial = await color();
    await setDesktop("#2e7d32");
    assert.ok(distance(initial, await color()) < 8, "材质未就绪时桌面颜色不应穿过便签");
    await sendStatus("starting");
    assert.ok(distance(initial, await color()) < 8, "启动中的材质仍使用实色底");

    await sendStatus("ready");
    assert.equal(await frame.locator("html").getAttribute("data-notes-material"), "ready");
    const readyGreen = await color();
    await setDesktop("#c62828");
    assert.ok(distance(readyGreen, await color()) > 90, "材质就绪后桌面颜色应穿过半透明便签层");

    await sendStatus("fallback");
    const fallbackRed = await color();
    await setDesktop("#2e7d32");
    assert.ok(distance(fallbackRed, await color()) < 8, "材质失效后实色底应重新遮住桌面");
    assert.ok(distance(sample(PNG.sync.read(await page.screenshot()), 0, 0), [46, 125, 50]) < 8,
      "圆角外的 WebView 前景必须透明");

    const note = { id: "n-1", title: "磨砂玻璃便签", body: "磨砂玻璃下仍要看清标题和正文。\n编辑内容在材质切换时保持不变。",
      createdAt: 1000, updatedAt: 2000 };
    const summary = { id: note.id, title: note.title, preview: note.body, createdAt: note.createdAt,
      updatedAt: note.updatedAt, chars: note.body.replace(/\s+/gu, "").length, bytes: Buffer.byteLength(note.body) };
    await frame.locator("body").evaluate((_, fixture) => {
      window.emitNative({ version: 1, type: "notes-state", enabled: false, running: false, windowOpen: true,
        locked: false, material: "frosted-glass", loadStatus: "loaded", error: "", pinnedId: "",
        notes: [fixture.summary], result: "snapshot", savedId: "", saved: false });
      window.emitNative({ version: 1, type: "notes-note", ...fixture.note });
    }, { note, summary });
    await frame.locator("[data-notes-title]").waitFor();
    assert.equal(await frame.locator("[data-notes-title]").inputValue(), note.title);
    await setDesktop("linear-gradient(120deg,#3267a6,#942737 48%,#9f885c)");
    await sendStatus("ready");
    assert.equal(await frame.locator("[data-notes-editor]").inputValue(), note.body);
    if (process.env.LUME_NOTES_VISUAL_OUTPUT) {
      const output = path.resolve(process.env.LUME_NOTES_VISUAL_OUTPUT);
      await mkdir(output, { recursive: true });
      await frame.locator("[data-notes-root]").hover();
      await writeFile(path.join(output, "ready-simulated-desktop.png"), await page.screenshot());
      await setDesktop("linear-gradient(120deg,#e3e8ef,#d0e2d8 48%,#fff2c9)");
      await writeFile(path.join(output, "ready-simulated-bright-desktop.png"), await page.screenshot());
      await page.setViewportSize({ width: 720, height: 480 });
      await writeFile(path.join(output, "ready-simulated-minimum.png"), await page.screenshot());
      await page.setViewportSize({ width: 1100, height: 700 });
    }
    await sendStatus("fallback");
    assert.equal(await frame.locator("[data-notes-editor]").inputValue(), note.body);

    const commands = () => frame.locator("body").evaluate(() => window.nativeCommands);
    const clearCommands = () => frame.locator("body").evaluate(() => { window.nativeCommands.length = 0; });
    await clearCommands();
    await frame.locator(".NotesToolbar > .NotesSpacer").click();
    assert.deepEqual(await commands(), [{ version: 1, type: "notes-window", action: "drag" }]);
    await clearCommands();
    await frame.locator("[data-notes-mode='split']").click();
    for (const selector of ["[data-notes-title]", "[data-notes-editor]", "[data-notes-search]", "[data-notes-mode='split']"]) {
      await frame.locator(selector).click();
    }
    assert.deepEqual(await commands(), [], "编辑、搜索及工具栏按钮不可拖动窗口");
    for (const edge of ["n", "ne", "e", "se", "s", "sw", "w", "nw"]) {
      await frame.locator(`[data-notes-resize='${edge}']`).click();
    }
    assert.deepEqual((await commands()).map((command) => command.edge), ["n", "ne", "e", "se", "s", "sw", "w", "nw"]);
    assert.ok((await commands()).every((command) => command.type === "notes-window" && command.action === "resize"));
    const edgeCenters = { n: [550, 3], ne: [1092, 8], e: [1097, 350], se: [1092, 692], s: [550, 697],
      sw: [8, 692], w: [3, 350], nw: [8, 8] };
    for (const [edge, [x, y]] of Object.entries(edgeCenters)) {
      const hit = await page.evaluate(({ x, y }) => {
        const frame = document.querySelector("iframe");
        const rect = frame.getBoundingClientRect();
        const doc = frame.contentDocument;
        return doc.elementFromPoint(x - rect.left, y - rect.top)?.dataset.notesResize || "";
      }, { x, y });
      assert.equal(hit, edge, `真实边缘命中 ${edge}`);
    }

    await clearCommands();
    const changedBody = "关闭前必须保存这段修改。";
    await frame.locator("[data-notes-editor]").fill(changedBody);
    await frame.locator("[data-notes-window-close]").click();
    assert.deepEqual((await commands()).filter((command) => command.type === "notes-window"),
      [{ version: 1, type: "notes-window", action: "close" }],
      "组件关闭按钮只请求关闭，不能自行发送 closeReady");
    assert.equal((await commands()).some((command) => command.action === "closeReady"), false);
    await frame.locator("body").evaluate(() => window.emitNative({ version: 1, type: "notes-close" }));
    assert.equal((await commands()).filter((command) => command.action === "save").at(-1).part, changedBody);
    assert.equal((await commands()).some((command) => command.action === "closeReady"), false,
      "保存未确认前不得关闭窗口");
    await frame.locator("body").evaluate((_, fixture) => {
      window.emitNative({ version: 1, type: "notes-state", enabled: false, running: false, windowOpen: true,
        locked: false, material: "frosted-glass", loadStatus: "loaded", error: "", pinnedId: "",
        notes: [fixture], result: "saved", savedId: fixture.id, saved: true });
    }, { ...summary, preview: changedBody, updatedAt: 3000, bytes: Buffer.byteLength(changedBody), chars: changedBody.length });
    assert.equal((await commands()).at(-1).action, "closeReady", "保存回执后才允许原生关闭窗口");
    await page.close();
  } finally {
    await browser?.close();
    await new Promise((resolve) => server.close(resolve));
  }
});
