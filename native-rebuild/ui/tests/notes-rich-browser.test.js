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

test("便签所见即所得编辑、格式工具与保存回归", { skip: !enabled, timeout: 180000 }, async (t) => {
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
    async function fixture(subtest, body = "测试文字", { mode, extraNotes = [], exposePage = false, compact = false } = {}) {
      const context = await browser.newContext({ viewport: compact ? { width: 280, height: 260 } : { width: 1100, height: 720 } });
      const page = await context.newPage();
      page.setDefaultTimeout(4000);
      const errors = [], notes = [makeNote(body), ...extraNotes.map((note) => ({ ...note }))];
      if (exposePage) await page.route("**/notes-window.js", async (route) => {
        const source = await readFile(path.join(ui, "notes-window.js"), "utf8");
        assert.ok(source.includes("const page = mountNotesPage("));
        await route.fulfill({ contentType: "text/javascript", body: source.replace("const page = mountNotesPage(", "const page = window.__notesPageForTest = mountNotesPage(") });
      });
      const host = { notes, commands: [], commits: [], pending: [], hold: false, failNext: false, transactions: new Map() };
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
        if (command.action === "create") {
          const note = { id: `n-${notes.length + 1}`, title: "", body: "", createdAt: 3000, updatedAt: 3000 };
          notes.unshift(note);
          return [{ version: 1, type: "notes-note", ...note }, state({ result: "applied", savedId: note.id, saved: true })];
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
          host.transactions.delete(note.id);
          if (host.failNext) {
            host.failNext = false;
            return [state({ result: "saved", savedId: note.id, saved: false, error: "runtime" })];
          }
          if (command.baseUpdatedAt !== undefined && command.baseUpdatedAt !== note.updatedAt)
            return [state({ result: "saved", savedId: note.id, saved: false, error: "conflict" })];
          Object.assign(note, { title: transaction.title, body: transaction.parts.join(""), updatedAt: note.updatedAt + 1 });
          host.commits.push({ id: note.id, body: note.body, count: transaction.count });
          return [state({ result: "saved", savedId: note.id, saved: true })];
        };
        if (host.hold) return new Promise((resolve) => host.pending.push(() => resolve(commit())));
        return commit();
      });
      await page.addInitScript(({ mode }) => {
        if (mode !== undefined && !localStorage.getItem("lume.notes.mode")) localStorage.setItem("lume.notes.mode", mode);
        const listeners = new Set();
        window.fixtureEmit = (message) => { for (const listener of listeners) listener({ data: message }); };
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
      await page.goto(`${origin}/notes.html${compact ? "#sticky=n-1" : ""}`);
      await page.waitForFunction(() => document.querySelector("[data-notes-title]")?.value === "格式工具验收");
      const editor = page.locator("[data-notes-editor]"), rich = page.locator("[data-notes-rich] .tiptap");
      const select = async () => {
        await rich.focus();
        await rich.press("Control+A");
      };
      const click = (format) => page.locator(`[data-notes-format="${format}"]`).click();
      const waitText = (selector, value) => page.waitForFunction(({ selector, value }) =>
        document.querySelector(selector)?.textContent === value, { selector, value }, { timeout: 4000 });
      const save = async () => {
        const expected = await editor.inputValue();
        await page.locator('[data-notes-action="save"]').click();
        await waitText("[data-notes-saved]", "已保存");
        const activeId = await page.locator('[data-notes-open][aria-selected="true"]').getAttribute("data-notes-open");
        assert.equal(notes.find((note) => note.id === activeId)?.body, expected, "the host committed the full current editor body");
      };
      const source = async () => {
        await page.locator('[data-notes-mode="source"]').click();
        return editor.inputValue();
      };
      const edit = () => page.locator('[data-notes-mode="edit"]').click();
      const emit = (message) => page.evaluate((value) => window.fixtureEmit(value), message);
      return { page, editor, rich, host, select, click, waitText, save, source, edit, emit, notes };
    }

    await t.test("小便签原位双击编辑、滚动和右键菜单", async (subtest) => {
      const f = await fixture(subtest, Array.from({length: 50}, (_, i) => "第 " + i + " 行正文").join("\n\n"), { compact: true, mode: "source" });
      assert.equal(await f.rich.getAttribute("contenteditable"), "false");
      assert.equal(await f.page.locator(".NotesSidebar").isVisible(), false);
      assert.equal(await f.page.locator("[data-notes-window-close]").count(), 0, "compact note closes through the native context menu only");
      assert.equal(await f.page.locator("[data-notes-root]").evaluate((element) => getComputedStyle(element).backgroundColor),
        "rgb(41, 45, 53)", "the note stays opaque until the native material is ready");
      const scroller = await f.page.locator("[data-notes-rich]").evaluate((element) => ({
        width: getComputedStyle(element).scrollbarWidth,
        gutter: getComputedStyle(element).scrollbarGutter,
        webkit: getComputedStyle(element, "::-webkit-scrollbar").display,
      }));
      assert.deepEqual(scroller, { width: "none", gutter: "auto", webkit: "none" }, "wheel scrolling does not draw or reserve a scrollbar");
      await f.emit({ version: 1, type: "notes-material", status: "ready" });
      assert.equal(await f.page.locator("[data-notes-root]").evaluate((element) => getComputedStyle(element).backgroundColor),
        "rgba(0, 0, 0, 0)", "the compact note reveals the same native frosted material as other widgets");
      const before = await f.page.locator("[data-notes-root]").boundingBox();
      await f.rich.hover(); await f.page.mouse.wheel(0, 420);
      await f.page.waitForFunction(() => document.querySelector("[data-notes-rich]").scrollTop > 0);
      await f.rich.locator("p").nth(10).dblclick();
      assert.equal(await f.rich.getAttribute("contenteditable"), "true");
      assert.deepEqual(await f.page.locator("[data-notes-root]").boundingBox(), before);
      await f.rich.focus(); await f.rich.press("Control+A"); await f.page.keyboard.type("edited inside sticky");
      await f.rich.press("Control+Enter");
      await f.page.waitForFunction(() => document.querySelector(".tiptap").getAttribute("contenteditable") === "false");
      assert.equal(f.notes[0].body, "edited inside sticky");
      assert.equal(f.host.commands.some(command => command.action === "openWindow"), false);
      await f.rich.click({button: "right"});
      await f.page.waitForFunction(() => true);
      assert.ok(f.host.commands.some(command => command.type === "notes-window" && command.action === "menu"));
      await f.emit({version: 1, type: "notes-update", ...f.notes[0], body: "**changed from manager**", updatedAt: 5000});
      await f.page.waitForFunction(() => document.querySelector(".tiptap strong")?.textContent === "changed from manager");
    });

    await t.test("默认编辑区直接显示已有格式，正文没有 Markdown 星号", async (subtest) => {
      const f = await fixture(subtest, "**已有粗体** 和 *已有斜体*");
      assert.equal(await f.page.locator("[data-notes-window-close]").count(), 1, "manager keeps its own close control");
      assert.equal(await f.page.locator("[data-notes-root]").getAttribute("data-mode"), "edit");
      assert.equal(await f.rich.isVisible(), true);
      assert.equal(await f.rich.getAttribute("contenteditable"), "true");
      assert.equal(await f.editor.isVisible(), false);
      assert.equal(await f.rich.locator("strong").textContent(), "已有粗体");
      assert.equal(await f.rich.locator("em").textContent(), "已有斜体");
      assert.equal(await f.rich.textContent(), "已有粗体 和 已有斜体");
    });

    await t.test("总页面没有右键菜单，外部更新会直接刷新当前正文", async (subtest) => {
      const f = await fixture(subtest);
      await f.rich.click({button: "right"});
      assert.equal(f.host.commands.some(command => command.type === "notes-window" && command.action === "menu"), false);
      await f.emit({version: 1, type: "notes-update", ...f.notes[0], body: "*小窗口的修改*", updatedAt: 5000});
      await f.page.waitForFunction(() => document.querySelector(".tiptap em")?.textContent === "小窗口的修改");
    });
    await t.test("双窗口并发编辑保留本地草稿并要求选择冲突版本", async (subtest) => {
      const f = await fixture(subtest);
      await f.select(); await f.page.keyboard.type("my local edit");
      f.notes[0].body = "external edit"; f.notes[0].updatedAt++;
      await f.emit({version: 1, type: "notes-update", ...f.notes[0]});
      assert.equal(await f.rich.innerText(), "my local edit");
      assert.equal(await f.page.locator(".NotesConflict").isVisible(), true);
      await f.page.locator('[data-notes-conflict="load"]').click();
      await f.page.waitForFunction(() => document.querySelector(".tiptap")?.textContent === "external edit");
      assert.equal(await f.page.locator(".NotesConflict").isVisible(), false);
    });
    await t.test("相同内容的外部更新推进版本，下一次编辑不会误报冲突", async (subtest) => {
      const f = await fixture(subtest);
      f.notes[0].updatedAt++;
      await f.emit({version: 1, type: "notes-update", ...f.notes[0]});
      await f.select(); await f.page.keyboard.type("next change");
      await f.save();
      assert.equal(f.notes[0].body, "next change");
      assert.equal(await f.page.locator(".NotesConflict").isVisible(), false);
    });

    for (const [kind, tag] of [["bold", "strong"], ["italic", "em"]])
      await t.test(`${kind}：实际选择正文后立即显示格式，再次点击取消`, async (subtest) => {
        const f = await fixture(subtest);
        await f.select(); await f.click(kind);
        await f.rich.locator(tag).waitFor();
        assert.equal(await f.page.locator(`[data-notes-format="${kind}"]`).getAttribute("aria-pressed"), "true");
        assert.equal(await f.rich.locator(tag).textContent(), "测试文字");
        assert.equal(await f.rich.textContent(), "测试文字");
        const style = await f.rich.locator(tag).evaluate((node) => ({ weight: getComputedStyle(node).fontWeight, style: getComputedStyle(node).fontStyle }));
        if (kind === "bold") assert.ok(Number(style.weight) >= 600);
        else assert.equal(style.style, "italic");
        await f.click(kind);
        assert.equal(await f.page.locator(`[data-notes-format="${kind}"]`).getAttribute("aria-pressed"), "false");
        assert.equal(await f.rich.locator(tag).count(), 0);
        assert.equal(await f.rich.textContent(), "测试文字");
        await f.save();
      });

    for (const [kind, selector] of [["heading", "h2"], ["bullet", "ul li"], ["number", "ol li"], ["quote", "blockquote"], ["code", "code"]])
      await t.test(`${kind}：在富文本正文实际生成对应元素`, async (subtest) => {
        const f = await fixture(subtest);
        await f.select(); await f.click(kind);
        await f.rich.locator(selector).waitFor();
        assert.equal((await f.rich.locator(selector).textContent()).trim(), "测试文字");
        assert.equal(await f.rich.textContent(), "测试文字");
        await f.save();
      });

    await t.test("分隔线插在正文后，已有文字保留", async (subtest) => {
      const f = await fixture(subtest);
      await f.rich.focus(); await f.rich.press("Control+End"); await f.click("rule");
      await f.rich.locator("hr").waitFor();
      assert.ok((await f.rich.textContent()).includes("测试文字"));
      assert.ok((await f.source()).includes("测试文字"));
      await f.edit(); await f.save();
    });

    await t.test("跨行代码保留全部文字和换行", async (subtest) => {
      const f = await fixture(subtest, "第一行\n\n第二行");
      await f.select(); await f.click("code");
      await f.rich.locator("pre code").first().waitFor();
      assert.match((await f.rich.locator("pre code").allTextContents()).join("\n"), /第一行\s+第二行/);
      const markdown = await f.source();
      assert.match(markdown, /```[\s\S]*第一行[\s\S]*第二行[\s\S]*```/);
      await f.edit(); await f.save();
    });

    for (const [kind, expression, mathSelector] of [["math", "\\frac{1}{2}+x^2", "math mfrac"], ["block", "\\sum_{i=1}^{n} i", "math mo"]])
      await t.test(`${kind}：公式输入框确认后直接排版公式`, async (subtest) => {
        const f = await fixture(subtest, "公式：");
        await f.rich.focus(); await f.rich.press("Control+End"); await f.click(kind);
        await f.page.locator("[data-notes-formula-input]").fill(expression);
        await f.page.locator("[data-notes-formula-apply]").click();
        await f.rich.locator(".katex").waitFor();
        assert.ok(await f.rich.locator(mathSelector).count());
        if (kind === "math") assert.ok(await f.rich.locator("math msup").count());
        else assert.ok((await f.rich.locator("math mo").allTextContents()).includes("∑"));
        const markdown = await f.source();
        assert.ok(markdown.includes(expression));
        assert.match(markdown, kind === "math" ? /\$\\frac/ : /\$\$/);
        await f.edit(); await f.rich.locator(".katex").waitFor(); await f.save();
      });

    await t.test("富文本加粗斜体快捷键、撤销和重做", async (subtest) => {
      const f = await fixture(subtest);
      await f.select(); await f.rich.press("Control+b");
      await f.rich.locator("strong").waitFor();
      await f.rich.press("Control+i");
      await f.rich.locator("em").waitFor();
      await f.page.locator('[data-notes-action="undo"]').click();
      assert.equal(await f.rich.locator("em").count(), 0);
      assert.equal(await f.rich.locator("strong").count(), 1);
      await f.page.locator('[data-notes-action="redo"]').click();
      assert.equal(await f.rich.locator("em").count(), 1);
      assert.equal(await f.rich.textContent(), "测试文字");
      await f.save();
    });

    await t.test("源码与富文本来回切换保留格式和全部正文", async (subtest) => {
      const f = await fixture(subtest);
      await f.select(); await f.click("bold");
      assert.match(await f.source(), /\*\*测试文字\*\*/);
      await f.editor.fill("## 新标题\n\n**粗体** 与 *斜体*\n\n- 第一项\n- 第二项\n\n最后一段");
      await f.edit();
      assert.equal(await f.rich.locator("h2").textContent(), "新标题");
      assert.equal(await f.rich.locator("strong").textContent(), "粗体");
      assert.equal(await f.rich.locator("em").textContent(), "斜体");
      assert.equal(await f.rich.locator("ul li").count(), 2);
      assert.ok((await f.rich.textContent()).includes("最后一段"));
      const textBefore = await f.rich.textContent();
      await f.source(); await f.edit();
      assert.equal(await f.rich.textContent(), textBefore);
      await f.save();
    });

    await t.test("中文文字输入、回车换行与纯文本粘贴都参与保存", async (subtest) => {
      const f = await fixture(subtest, "");
      await f.rich.focus(); await f.page.keyboard.insertText("中文输入 😀");
      await f.rich.press("Enter"); await f.page.keyboard.insertText("第二行文字");
      await f.rich.press("Enter");
      // A DataTransfer represents a plain-text paste without reading or writing
      // the user's operating-system clipboard. This is not a native IME test.
      await f.rich.evaluate((node) => {
        const clipboardData = new DataTransfer();
        clipboardData.setData("text/plain", "纯文本粘贴\n下一行 <b>保留字面量</b>");
        node.dispatchEvent(new ClipboardEvent("paste", { bubbles: true, cancelable: true, clipboardData }));
      });
      const text = await f.rich.textContent();
      for (const part of ["中文输入 😀", "第二行文字", "纯文本粘贴", "下一行 <b>保留字面量</b>"]) assert.ok(text.includes(part));
      assert.equal(await f.rich.locator("b").count(), 0);
      const markdown = await f.source();
      for (const part of ["中文输入 😀", "第二行文字", "纯文本粘贴"]) assert.ok(markdown.includes(part));
      await f.edit(); assert.equal(await f.rich.textContent(), text); await f.save();
    });

    await t.test("富文本格式保存后，切换笔记和重开页面仍有格式", async (subtest) => {
      const f = await fixture(subtest, "测试文字", { extraNotes: [makeNote("第二篇原文", "n-2")] });
      await f.select(); await f.click("bold"); await f.save();
      const saved = f.host.notes.find((note) => note.id === "n-1").body;
      assert.match(saved, /\*\*测试文字\*\*/);
      await f.page.locator('[data-notes-open="n-2"]').click();
      await f.page.waitForFunction(() => document.querySelector("[data-notes-title]")?.value === "第二篇笔记");
      assert.equal(await f.rich.textContent(), "第二篇原文");
      await f.page.locator('[data-notes-open="n-1"]').click();
      await f.rich.locator("strong").waitFor();
      assert.equal(await f.rich.locator("strong").textContent(), "测试文字");
      await f.page.reload(); await f.rich.locator("strong").waitFor();
      assert.equal(await f.rich.locator("strong").textContent(), "测试文字");
      assert.equal(await f.editor.inputValue(), saved);
    });

    const waitForHost = async (f, predicate, message) => {
      const deadline = Date.now() + 4000;
      while (!predicate() && Date.now() < deadline) await new Promise((resolve) => setTimeout(resolve, 20));
      assert.ok(predicate(), message);
    };
    const enterRichText = async (f, value) => {
      await f.rich.focus();
      await f.rich.press("Control+A");
      await f.page.keyboard.insertText(value);
      await f.page.waitForFunction((expected) => document.querySelector("[data-notes-rich] .tiptap")?.textContent === expected, value);
    };
    const releaseReceipt = async (f, expectAction, failure = false) => {
      assert.equal(f.host.pending.length, 1, "the first save receipt is still held");
      f.host.pending.shift()();
      await waitForHost(f, () => f.host.pending.length === 1 && f.host.commands.some((command) => command.action === "save" && command.part.includes("LATEST EDIT")),
        "the newer edit must start a second save after the old receipt");
      assert.equal(f.host.pending.length, 1, "the second save receipt is held independently");
      const finalAction = (command) => command.action === expectAction && (expectAction !== "open" || command.id === "n-2");
      assert.equal(f.host.commands.some(finalAction), false, "the old receipt must not release the final action");
      if (failure) {
        f.host.failNext = true;
        f.host.pending.shift()();
        await f.page.waitForFunction(() => document.querySelector("[data-notes-notice]")?.textContent.includes("保存失败"));
        assert.equal(f.host.commits.some((commit) => commit.body.includes("LATEST EDIT")), false, "a failed second receipt must not commit latest text");
        assert.equal(f.host.commands.some(finalAction), false, `failed save must not release ${expectAction}`);
        return;
      }
      f.host.pending.shift()();
      await waitForHost(f, () => f.host.commands.some(finalAction),
        `${expectAction} is released only after the newer save receipt`);
      assert.ok(f.host.commits.some((commit) => commit.body.includes("LATEST EDIT")), "latest body is committed before the final action");
    };

    for (const failure of [false, true]) await t.test(`旧保存回执后关闭${failure ? "失败不放行" : "仍保存最新内容再放行"}`, async (subtest) => {
      const f = await fixture(subtest, "原始正文");
      f.host.hold = true;
      await enterRichText(f, "FIRST EDIT");
      await f.page.locator('[data-notes-action="save"]').click();
      await waitForHost(f, () => f.host.pending.length === 1, "first save reaches the held receipt");
      await enterRichText(f, "LATEST EDIT");
      await f.emit({ version: 1, type: "notes-close" });
      assert.equal(f.host.commands.some((command) => command.action === "closeReady"), false, "closeReady cannot precede the latest save");
      await releaseReceipt(f, "closeReady", failure);
      if (!failure) assert.equal(f.notes[0].body, "LATEST EDIT");
    });

    for (const failure of [false, true]) await t.test(`旧保存回执后切换笔记${failure ? "失败不放行" : "先保存最新内容"}`, async (subtest) => {
      const f = await fixture(subtest, "原始正文", { extraNotes: [makeNote("第二篇原文", "n-2")] });
      f.host.hold = true;
      await enterRichText(f, "FIRST EDIT");
      await f.page.locator('[data-notes-action="save"]').click();
      await waitForHost(f, () => f.host.pending.length === 1, "first save reaches the held receipt");
      await enterRichText(f, "LATEST EDIT");
      await f.page.locator('[data-notes-open="n-2"]').click();
      assert.equal(f.host.commands.some((command) => command.action === "open" && command.id === "n-2"), false, "open cannot precede the latest save");
      await releaseReceipt(f, "open", failure);
      if (!failure) {
        await f.page.waitForFunction(() => document.querySelector("[data-notes-title]")?.value === "第二篇笔记");
        assert.equal(f.notes[0].body, "LATEST EDIT");
      } else assert.equal(await f.page.locator("[data-notes-title]").inputValue(), "格式工具验收");
    });

    for (const failure of [false, true]) await t.test(`旧保存回执后销毁页面${failure ? "失败保留旧内容" : "等待最新保存"}`, async (subtest) => {
      const f = await fixture(subtest, "原始正文", { exposePage: true });
      f.host.hold = true;
      await enterRichText(f, "FIRST EDIT");
      await f.page.locator('[data-notes-action="save"]').click();
      await waitForHost(f, () => f.host.pending.length === 1, "first save reaches the held receipt");
      await enterRichText(f, "LATEST EDIT");
      await f.page.evaluate(() => window.__notesPageForTest.dispose());
      assert.equal(await f.page.evaluate(() => window.__notesPageForTest.retiring), true);
      f.host.pending.shift()();
      await waitForHost(f, () => f.host.pending.length === 1 && f.host.commands.some((command) => command.action === "save" && command.part.includes("LATEST EDIT")),
        "disposing waits for a second save of the latest body");
      assert.equal(await f.page.evaluate(() => window.__notesPageForTest.retiring), true, "the old receipt cannot finish retiring");
      assert.equal(f.notes[0].body, "FIRST EDIT");
      f.host.failNext = failure;
      f.host.pending.shift()();
      await f.page.waitForFunction(() => !window.__notesPageForTest.retiring);
      if (!failure) assert.equal(f.notes[0].body, "LATEST EDIT");
      else assert.equal(f.notes[0].body, "FIRST EDIT");
    });

    await t.test("导入已有格式的 Markdown 立即可见并保存原意", async (subtest) => {
      const f = await fixture(subtest);
      const body = "# 导入标题\n\n**导入粗体** 和 *导入斜体*\n\n- 第一项\n- 第二项\n\n> 引用内容\n\n$x^2$";
      await f.page.locator("[data-notes-file]").setInputFiles({ name: "已带格式.md", mimeType: "text/markdown", buffer: Buffer.from(body) });
      await f.page.waitForFunction(() => document.querySelector("[data-notes-title]")?.value === "导入标题");
      await f.rich.locator("h1").waitFor();
      assert.equal(await f.rich.locator("h1").textContent(), "导入标题");
      assert.equal(await f.rich.locator("strong").textContent(), "导入粗体");
      assert.equal(await f.rich.locator("em").textContent(), "导入斜体");
      assert.equal(await f.rich.locator("ul li").count(), 2);
      assert.equal(await f.rich.locator("blockquote").textContent(), "引用内容");
      assert.equal(await f.rich.locator("math msup").count(), 1);
      await f.save();
      assert.ok(f.host.notes.some((note) => note.title === "导入标题" && note.body.includes("导入粗体")));
    });

    if (process.env.LUME_NOTES_VISUAL_OUTPUT) await t.test("保存所见即所得编辑效果截图", async (subtest) => {
      const f = await fixture(subtest, "## 所见即所得\n\n**直接看到粗体**，*直接看到斜体*。\n\n- 列表与正文一起编辑\n- 无需阅读 Markdown 符号\n\n> 引用内容\n\n`const answer = 42;`\n\n---\n\n公式：$\\frac{1}{2}+x^2$\n\n$$\n\\sum_{i=1}^{n} i\n$$");
      await f.rich.locator(".katex").first().waitFor();
      await f.page.evaluate(() => document.fonts.ready);
      const output = path.resolve(process.env.LUME_NOTES_VISUAL_OUTPUT);
      await mkdir(output, { recursive: true });
      await f.page.screenshot({ path: path.join(output, "notes-rich-edit.png") });
    });
  } finally {
    await browser?.close();
    await new Promise((resolve) => server.close(resolve));
  }
});
