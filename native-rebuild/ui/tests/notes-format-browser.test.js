import test from "node:test";
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { mkdir, readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { homedir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ui = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const require = createRequire(import.meta.url);
const runtime = path.join(homedir(), ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules");
const enabled = process.env.LUME_UI_BROWSER_TESTS === "1";
const contentTypes = { ".css": "text/css", ".js": "text/javascript", ".mjs": "text/javascript", ".woff": "font/woff", ".woff2": "font/woff2", ".ttf": "font/ttf" };
const makeNote = (body, id = "n-1") => ({ id, title: id === "n-1" ? "格式工具验收" : "第二篇笔记", body,
  createdAt: 1000, updatedAt: 2000 });

test("便签格式工具、可见预览及真实编辑保存回归", { skip: !enabled, timeout: 180000 }, async (t) => {
  const { chromium } = require(process.env.LUME_PLAYWRIGHT_DIR || path.join(runtime, "playwright"));
  const server = createServer(async (request, response) => {
    const pathname = decodeURIComponent(new URL(request.url, "http://127.0.0.1").pathname);
    if (pathname === "/favicon.ico") { response.writeHead(204).end(); return; }
    const file = path.resolve(ui, "." + pathname);
    if (!file.startsWith(ui + path.sep)) { response.writeHead(403).end(); return; }
    try {
      response.writeHead(200, { "Content-Type": contentTypes[path.extname(file)] || "text/html" }).end(await readFile(file));
    } catch { response.writeHead(404).end(); }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  let browser;
  try {
    browser = await chromium.launch({ headless: true, channel: "msedge" });

    // The fixture implements the native persistence handshake. It only commits
    // a note after every ordered chunk arrives, and can hold the final receipt.
    // Each browser context and all note data are private to one subtest.
    async function fixture(subtest, body = "测试文字", { mode, extraNotes = [] } = {}) {
      const context = await browser.newContext({ viewport: { width: 1100, height: 720 } });
      const page = await context.newPage();
      page.setDefaultTimeout(4000);
      const errors = [], notes = [makeNote(body), ...extraNotes.map((note) => ({ ...note }))];
      const host = { notes, commands: [], commits: [], pending: [], hold: false, transactions: new Map() };
      const summary = (note) => ({ id: note.id, title: note.title, preview: note.body.replace(/\s+/gu, " ").slice(0, 80),
        createdAt: note.createdAt, updatedAt: note.updatedAt, chars: Array.from(note.body.replace(/\s+/gu, "")).length,
        bytes: Buffer.byteLength(note.body) });
      const state = (fields = {}) => ({ version: 1, type: "notes-state", enabled: false, running: false, windowOpen: true,
        locked: false, material: "frosted-glass", loadStatus: "loaded", error: "", pinnedId: "",
        notes: notes.map(summary), result: "snapshot", savedId: "", saved: false, ...fields });
      page.on("pageerror", (error) => errors.push(String(error)));
      await page.exposeFunction("__notesFixtureCommand", async (command) => {
        host.commands.push(command);
        if (command.scope !== "notes") return [];
        if (command.action === "snapshot") return [state()];
        if (command.action === "open") {
          const note = notes.find((note) => note.id === command.id);
          assert.ok(note, "the page opens an existing fixture note");
          return [{ version: 1, type: "notes-note", ...note }];
        }
        if (command.action !== "save") return [];
        const note = notes.find((note) => note.id === command.id);
        assert.ok(note, "save targets an existing fixture note");
        assert.ok(Number.isInteger(command.count) && command.count > 0);
        if (command.index === 0) {
          assert.equal(host.transactions.has(command.id), false, "save transactions cannot overlap for the same note");
          host.transactions.set(command.id, { count: command.count, title: command.title, parts: [] });
        }
        const transaction = host.transactions.get(command.id);
        assert.ok(transaction, "continuation chunk has a transaction");
        assert.equal(command.index, transaction.parts.length, "save chunks arrive in order");
        assert.equal(command.count, transaction.count);
        assert.equal(command.title, transaction.title);
        transaction.parts.push(command.part);
        if (transaction.parts.length < transaction.count)
          return [{ version: 1, type: "notes-chunk", id: command.id, index: command.index }];
        const commit = () => {
          Object.assign(note, { title: transaction.title, body: transaction.parts.join(""), updatedAt: note.updatedAt + 1 });
          host.transactions.delete(note.id);
          host.commits.push({ id: note.id, body: note.body, count: transaction.count });
          return [state({ result: "saved", savedId: note.id, saved: true })];
        };
        if (host.hold) return new Promise((resolve) => host.pending.push(() => resolve(commit())));
        return commit();
      });
      await page.addInitScript(({ mode }) => {
        if (mode !== undefined && !localStorage.getItem("lume.notes.mode")) localStorage.setItem("lume.notes.mode", mode);
        const listeners = new Set();
        window.fixtureHostErrors = [];
        window.chrome = { webview: {
          addEventListener(type, listener) { if (type === "message") listeners.add(listener); },
          postMessage(command) {
            window.__notesFixtureCommand(command).then((messages) => {
              for (const message of messages) for (const listener of listeners) listener({ data: message });
            }).catch((error) => window.fixtureHostErrors.push(String(error)));
          },
        } };
      }, { mode });
      subtest.after(async () => {
        host.hold = false;
        for (const release of host.pending.splice(0)) release();
        const hostErrors = page.isClosed() ? [] : await page.evaluate(() => window.fixtureHostErrors);
        await context.close();
        assert.deepEqual(errors, [], "no browser page errors");
        assert.deepEqual(hostErrors, [], "the persistence protocol fixture accepted every command");
      });
      await page.goto(`${origin}/notes.html`);
      await page.waitForFunction(() => document.querySelector("[data-notes-title]")?.value === "格式工具验收");
      const editor = page.locator("[data-notes-editor]"), preview = page.locator("[data-notes-preview]");
      const select = async (start = 0, end) => {
        await editor.focus();
        await editor.evaluate((node, range) => node.setSelectionRange(range.start, range.end ?? node.value.length), { start, end });
      };
      const click = (format) => page.locator(`[data-notes-format="${format}"]`).click();
      const split = () => page.locator('[data-notes-mode="split"]').click();
      const waitText = (selector, value) => page.waitForFunction(({ selector, value }) =>
        document.querySelector(selector)?.textContent === value, { selector, value }, { timeout: 4000 });
      const save = async () => {
        const expected = await editor.inputValue();
        await page.locator('[data-notes-action="save"]').click();
        await waitText("[data-notes-saved]", "已保存");
        assert.equal(notes[0].body, expected, "the host committed the full current editor body");
      };
      return { page, editor, preview, host, select, click, split, waitText, save };
    }

    await t.test("新用户默认富文本；已有源码模式保留，点击格式即显示预览", async (subtest) => {
      const fresh = await fixture(subtest);
      assert.equal(await fresh.page.locator("[data-notes-root]").getAttribute("data-mode"), "edit");
      assert.equal(await fresh.page.locator("[data-notes-rich] .tiptap").isVisible(), true);
      assert.equal(await fresh.editor.isVisible(), false);
      assert.equal(await fresh.page.locator('[data-notes-mode="source"]').textContent(), "源码");
      const existing = await fixture(subtest, "测试文字", { mode: "source" });
      assert.equal(await existing.page.locator("[data-notes-root]").getAttribute("data-mode"), "source");
      await existing.select(); await existing.click("bold");
      assert.equal(await existing.page.locator("[data-notes-root]").getAttribute("data-mode"), "split");
      await existing.preview.locator("strong").waitFor();
      assert.equal(await existing.preview.locator("strong").textContent(), "测试文字");
      await existing.page.locator('[data-notes-mode="preview"]').click();
      assert.equal(await existing.page.locator(".NotesFormat").isVisible(), false);
      assert.equal(await existing.editor.isVisible(), false);
    });

    const formats = [
      { kind: "bold", body: "测试文字", source: "**测试文字**", selector: "strong", text: "测试文字" },
      { kind: "italic", body: "测试文字", source: "*测试文字*", selector: "em", text: "测试文字" },
      { kind: "heading", body: "测试文字", source: "## 测试文字", selector: "h2", text: "测试文字" },
      { kind: "bullet", body: "第一行\n第二行", source: "- 第一行\n- 第二行", selector: "ul > li", text: "第一行" },
      { kind: "number", body: "第一行\n第二行", source: "1. 第一行\n2. 第二行", selector: "ol > li", text: "第一行" },
      { kind: "code", body: "const x = 1;", source: "`const x = 1;`", selector: "p > code", text: "const x = 1;" },
      { kind: "quote", body: "测试文字", source: "> 测试文字", selector: "blockquote", text: "测试文字" },
    ];
    for (const item of formats) await t.test(`${item.kind}：真实点击插入格式并显示对应排版`, async (subtest) => {
      const f = await fixture(subtest, item.body);
      await f.split(); await f.select(); await f.click(item.kind);
      assert.equal(await f.editor.inputValue(), item.source);
      await f.preview.locator(item.selector).first().waitFor();
      assert.equal((await f.preview.locator(item.selector).first().textContent()).trim(), item.text);
      if (item.kind === "bullet" || item.kind === "number") assert.equal(await f.preview.locator(item.selector).count(), 2);
      if (item.kind === "bold") assert.ok(Number(await f.preview.locator("strong").evaluate((node) => getComputedStyle(node).fontWeight)) >= 600);
      if (item.kind === "italic") assert.equal(await f.preview.locator("em").evaluate((node) => getComputedStyle(node).fontStyle), "italic");
      await f.save();
    });

    for (const kind of ["bold", "italic", "heading", "bullet", "number", "quote"])
      await t.test(`${kind}：再次点击取消已有格式`, async (subtest) => {
        const f = await fixture(subtest);
        await f.split(); await f.select(); await f.click(kind); await f.click(kind);
        assert.equal(await f.editor.inputValue(), "测试文字");
        await f.page.waitForFunction(() => {
          const preview = document.querySelector("[data-notes-preview]");
          return preview?.textContent === "测试文字" && !preview.querySelector("strong, em, h2, ul, ol, blockquote");
        });
      });

    for (const [kind, prefix] of [["heading", "## "], ["bullet", "- "], ["number", "1. "], ["quote", "> "]])
      await t.test(`${kind}：选区结束在下一行开头时仅格式化前一行`, async (subtest) => {
        const f = await fixture(subtest, "第一行\n第二行");
        await f.split(); await f.select(0, "第一行\n".length); await f.click(kind);
        assert.equal(await f.editor.inputValue(), `${prefix}第一行\n第二行`);
      });

    await t.test("跨行加粗覆盖全部选中文字，预览不残留星号", async (subtest) => {
      const f = await fixture(subtest, "第一行\n第二行");
      await f.split(); await f.select(); await f.click("bold");
      await f.preview.locator("strong").first().waitFor();
      const formatted = (await f.preview.locator("strong").allTextContents()).join("").replace(/\s+/gu, "");
      assert.equal(formatted, "第一行第二行");
      assert.doesNotMatch(await f.preview.textContent(), /\*/);
    });

    await t.test("分隔线保留选中正文并生成 hr", async (subtest) => {
      const f = await fixture(subtest, "前文\n保留选中的这一段\n后文");
      await f.split(); await f.select(3, 3 + "保留选中的这一段".length); await f.click("rule");
      const source = await f.editor.inputValue();
      for (const part of ["前文", "保留选中的这一段", "后文"]) assert.ok(source.includes(part));
      await f.preview.locator("hr").waitFor();
      assert.ok((await f.preview.textContent()).includes("保留选中的这一段"));
      await f.save();
    });

    for (const [body, selector] of [["a ` b", "p > code"], ["const x = `value`;\n```\nreturn x;", "pre > code"]])
      await t.test(`${selector}：代码包含反引号时保留完整原文`, async (subtest) => {
        const f = await fixture(subtest, body);
        await f.split(); await f.select(); await f.click("code");
        await f.preview.locator(selector).waitFor();
        assert.equal((await f.preview.locator(selector).textContent()).replace(/\n$/, ""), body);
        await f.save();
      });

    await t.test("行内公式工具排版分数和上标，公式块排版求和", async (subtest) => {
      const inline = await fixture(subtest, "\\frac{1}{2}+x^2");
      await inline.split(); await inline.select(); await inline.click("math");
      assert.equal(await inline.editor.inputValue(), "$\\frac{1}{2}+x^2$");
      await inline.preview.locator(".katex").waitFor();
      assert.equal(await inline.preview.locator("math mfrac").count(), 1);
      assert.equal(await inline.preview.locator("math msup").count(), 1);
      await inline.save();
      const block = await fixture(subtest, "\\sum_{i=1}^{n} i");
      await block.split(); await block.select(); await block.click("block");
      assert.match(await block.editor.inputValue(), /\$\$\s*\\sum_\{i=1\}\^\{n\} i\s*\$\$/);
      await block.preview.locator(".katex-display .katex").waitFor();
      assert.ok((await block.preview.locator("math mo").allTextContents()).includes("∑"));
      await block.save();
    });

    await t.test("工具格式支持撤销和重做并同步预览", async (subtest) => {
      const f = await fixture(subtest);
      await f.split(); await f.select(); await f.click("bold");
      assert.equal(await f.editor.inputValue(), "**测试文字**");
      await f.preview.locator("strong").waitFor();
      await f.page.locator('[data-notes-action="undo"]').click();
      assert.equal(await f.editor.inputValue(), "测试文字");
      await f.page.waitForFunction(() => {
        const preview = document.querySelector("[data-notes-preview]");
        return preview?.textContent === "测试文字" && !preview.querySelector("strong");
      });
      await f.page.locator('[data-notes-action="redo"]').click();
      assert.equal(await f.editor.inputValue(), "**测试文字**");
      await f.preview.locator("strong").waitFor();
      await f.save();
    });

    await t.test("分块保存等真实回执，切换笔记与重开页面保留格式正文", async (subtest) => {
      const f = await fixture(subtest, "初始正文", { extraNotes: [makeNote("第二篇原文", "n-2")] });
      const body = "格式正文 😀\n".repeat(1000);
      await f.split(); await f.editor.fill(body); await f.select(0, 4);
      await f.click("bold");
      const formatted = await f.editor.inputValue();
      assert.ok(formatted.startsWith("**格式正文**"));
      // Clicking a tool blurs the editor and may save the pre-format body.
      // Finish that transaction before holding the final formatted version.
      await f.save();
      const nextBody = formatted + "\n最后的修改";
      f.host.hold = true;
      await f.editor.fill(nextBody);
      await f.page.locator('[data-notes-action="save"]').click();
      await f.waitText("[data-notes-saved]", "保存中…");
      assert.equal(f.host.notes[0].body, formatted, "without the final receipt the host still has the prior commit");
      await f.page.locator('[data-notes-open="n-2"]').click();
      assert.equal(await f.page.locator("[data-notes-title]").inputValue(), "格式工具验收", "switch waits for the save receipt");
      assert.equal(f.host.commands.filter((command) => command.action === "open" && command.id === "n-2").length, 0);
      const deadline = Date.now() + 4000;
      while (!f.host.pending.length && Date.now() < deadline)
        await new Promise((resolve) => setTimeout(resolve, 20));
      assert.equal(f.host.pending.length, 1, "all save chunks have reached the host");
      f.host.hold = false;
      for (const release of f.host.pending.splice(0)) release();
      await f.page.waitForFunction(() => document.querySelector("[data-notes-title]")?.value === "第二篇笔记");
      assert.equal(f.host.notes[0].body, nextBody);
      assert.ok(f.host.commits.at(-1).count > 1, "the real save protocol needed multiple acknowledged chunks");
      await f.page.locator('[data-notes-open="n-1"]').click();
      await f.page.waitForFunction((expected) => document.querySelector("[data-notes-editor]")?.value === expected, nextBody);
      await f.page.reload();
      await f.page.waitForFunction((expected) => document.querySelector("[data-notes-editor]")?.value === expected, nextBody);
      await f.preview.locator("strong").first().waitFor();
      assert.equal(await f.preview.locator("strong").first().textContent(), "格式正文");
    });

    if (process.env.LUME_NOTES_VISUAL_OUTPUT) await t.test("保存格式工具可见效果截图", async (subtest) => {
      const f = await fixture(subtest, "## 格式效果\n\n**加粗正文** 与 *斜体正文*\n\n- 无序列表\n- 第二项\n\n1. 有序列表\n2. 第二项\n\n> 引用内容\n\n`const answer = 42;`\n\n---\n\n行内公式：$\\frac{1}{2}+x^2$\n\n$$\n\\sum_{i=1}^{n} i\n$$");
      await f.split(); await f.preview.locator(".katex-display").waitFor();
      await f.page.evaluate(() => document.fonts.ready);
      const output = path.resolve(process.env.LUME_NOTES_VISUAL_OUTPUT);
      await mkdir(output, { recursive: true });
      await f.page.screenshot({ path: path.join(output, "notes-format-split.png") });
      await f.page.locator('[data-notes-mode="preview"]').click();
      await f.page.screenshot({ path: path.join(output, "notes-format-preview.png") });
    });
  } finally {
    await browser?.close();
    await new Promise((resolve) => server.close(resolve));
  }
});
