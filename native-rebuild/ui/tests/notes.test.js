import test from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { createBridge, validHostMessage } from "../bridge.js";
import { characterCount, mountNotesPage, notesPageMarkup, splitBody, validNotesCommand, validNotesMessage } from "../notes.js";
import { renderMarkdown } from "../notes-markdown.js";

const jsdomPath = process.env.LUME_JSDOM_PATH;
const { JSDOM } = jsdomPath ? createRequire(import.meta.url)(jsdomPath) : {};
const summary = (note) => ({ id: note.id, title: note.title, preview: note.body.replace(/\s+/g, " ").slice(0, 80),
  createdAt: note.createdAt, updatedAt: note.updatedAt, chars: characterCount(note.body), bytes: Buffer.byteLength(note.body) });
const stateMessage = (notes, fields = {}) => ({ version: 1, type: "notes-state", enabled: false, running: false, windowOpen: true, locked: false,
  material: "frosted-glass", loadStatus: "loaded", error: "", pinnedId: "", notes: notes.map(summary),
  result: "snapshot", savedId: "", saved: false, ...fields });

test("notes starts with visual editing and keeps source and preview accessible", () => {
  const html = notesPageMarkup();
  assert.match(html, /data-mode="edit"/);
  assert.match(html, /data-notes-mode="source"[^>]*>源码<\/button>/);
  assert.match(html, /data-notes-rich/);
  assert.match(html, /Markdown 源码/);
  assert.match(html, /效果预览/);
});

test("notes commands and host messages are strictly validated", () => {
  assert.equal(validNotesCommand("snapshot", {}), true);
  assert.equal(validNotesCommand("open", { id: "n-1" }), true);
  assert.equal(validNotesCommand("open", { id: "../x" }), false);
  assert.equal(validNotesCommand("save", { id: "n-1", title: "t", part: "body", index: 0, count: 1 }), true);
  assert.equal(validNotesCommand("save", { id: "n-1", title: "a\nb", part: "", index: 0, count: 1 }), false);
  assert.equal(validNotesCommand("save", { id: "n-1", title: "t", part: "", index: 1, count: 1 }), false);
  assert.equal(validNotesCommand("pin", { id: "n-1", value: true }), true);
  assert.equal(validNotesCommand("remove", {}), false);
  assert.equal(validNotesCommand("openWindow", {}), true);
  assert.equal(validNotesCommand("setActive", { value: false }), true);
  assert.equal(validNotesCommand("closeReady", {}), true);
  const note = { id: "n-1", title: "标题", body: "正文", createdAt: 1, updatedAt: 2 };
  assert.equal(validHostMessage(stateMessage([note])), true);
  assert.equal(validHostMessage({ ...stateMessage([note]), extra: 1 }), false);
  assert.equal(validNotesMessage({ version: 1, type: "notes-note", ...note }), true);
  assert.equal(validNotesMessage({ version: 1, type: "notes-close" }), true);
  assert.equal(validHostMessage({ version: 1, type: "notes-chunk", id: "n-1", index: 3 }), false, "the manager only receives the summary");
  const posted = [], bridge = createBridge({ postMessage: (value) => posted.push(value) });
  assert.equal(bridge.notes("create"), true);
  assert.deepEqual(posted[0], { version: 1, scope: "notes", action: "create" });
});

test("markdown preview escapes input and renders the supported subset", () => {
  const html = renderMarkdown('# 标题\n\n<script>alert(1)</script> **粗** *斜* `a<b`\n\n- [x] 完成\n- 列表\n\n1. 一\n2. 二\n\n> 引用\n\n```\n<b>code</b>\n```\n\n$$\nx^2\n$$\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n[链接](javascript:alert(1)) [好](https://example.com)');
  assert.doesNotMatch(html, /<script>|<b>code/);
  assert.match(html, /&lt;script&gt;/);
  assert.match(html, /<h1>标题<\/h1>/);
  assert.match(html, /<strong>粗<\/strong> <em>斜<\/em> <code>a&lt;b<\/code>/);
  assert.match(html, /MdCheck is-done/);
  assert.match(html, /<ol><li>一<\/li><li>二<\/li><\/ol>/);
  assert.match(html, /<blockquote><p>引用<\/p><\/blockquote>/);
  assert.match(html, /<pre><code>&lt;b&gt;code&lt;\/b&gt;<\/code><\/pre>/);
  assert.match(html, /class="katex-display"/);
  assert.match(html, /<msup>/);
  assert.match(html, /<table><thead><tr><th>a<\/th><th>b<\/th>/);
  assert.doesNotMatch(html, / href=|data-href="javascript/);
  assert.match(html, /data-href="https:\/\/example.com"/);
});

test("long bodies split into bounded parts without breaking surrogate pairs", () => {
  const body = "😀".repeat(5000) + "\n\"\\".repeat(3000);
  const parts = splitBody(body, "标题", "n-1");
  assert.equal(parts.join(""), body);
  assert.ok(parts.length > 1);
  for (const part of parts) {
    assert.ok(JSON.stringify({ version: 1, scope: "notes", action: "save", id: "n-1", title: "标题", part, index: 1999, count: 2000 }).length <= 8192);
    assert.doesNotMatch(part, /^[\uDC00-\uDFFF]|[\uD800-\uDBFF]$/);
  }
  assert.deepEqual(splitBody("", "", "n-1"), [""]);
});

test("the page opens, edits, autosaves in parts and creates notes through the host", { skip: !jsdomPath }, async (t) => {
  const dom = new JSDOM("<main></main>", { url: "https://lume.local/", pretendToBeVisual: true });
  t.after(() => dom.window.close());
  globalThis.localStorage = dom.window.localStorage;
  const pending = new Map();
  let nextTimer = 1;
  const timers = { setTimeout: (fn) => { const id = nextTimer++; pending.set(id, fn); return id; }, clearTimeout: (id) => pending.delete(id) };
  const runTimers = () => { for (const [id, fn] of [...pending]) { pending.delete(id); fn(); } };
  const notes = [{ id: "n-1", title: "第一篇", body: "你好", createdAt: 1000, updatedAt: 2000 }];
  const sent = [];
  let page;
  const host = (action, fields) => {
    sent.push({ action, ...fields });
    queueMicrotask(() => {
      if (action === "snapshot") page.receive(stateMessage(notes));
      else if (action === "open") page.receive({ version: 1, type: "notes-note", ...notes.find((n) => n.id === fields.id) });
      else if (action === "save") {
        if (fields.index + 1 < fields.count) { page.receive({ version: 1, type: "notes-chunk", id: fields.id, index: fields.index }); return; }
        const parts = sent.filter((m) => m.action === "save" && m.id === fields.id).slice(-fields.count).map((m) => m.part).join("");
        const note = notes.find((n) => n.id === fields.id);
        Object.assign(note, { title: fields.title, body: parts, updatedAt: note.updatedAt + 1 });
        page.receive(stateMessage(notes, { result: "saved", savedId: note.id, saved: true }));
      } else if (action === "create") {
        const note = { id: `n-${notes.length + 1}`, title: "", body: "", createdAt: 3000, updatedAt: 3000 };
        notes.unshift(note);
        page.receive({ version: 1, type: "notes-note", ...note });
        page.receive(stateMessage(notes, { result: "applied", savedId: note.id, saved: true }));
      }
    });
    return true;
  };
  const root = dom.window.document.querySelector("main");
  globalThis.document = dom.window.document;
  t.after(() => { delete globalThis.document; delete globalThis.localStorage; });
  page = mountNotesPage(root, { send: host, timers, renderMarkdown });
  const flush = () => new Promise((resolve) => setTimeout(resolve, 0));
  await flush(); await flush();
  const find = (selector) => root.querySelector(selector);
  assert.equal(find("[data-notes-title]").value, "第一篇");
  assert.equal(find("[data-notes-editor]").value, "你好");
  assert.match(find("[data-notes-count]").textContent, /1 篇笔记/);
  assert.match(find("[data-notes-chars]").textContent, /2 字/);
  const editor = find("[data-notes-editor]");
  editor.value = "x".repeat(9000);
  editor.dispatchEvent(new dom.window.Event("input", { bubbles: true }));
  assert.equal(find("[data-notes-saved]").textContent, "未保存");
  runTimers();
  for (let i = 0; i < 10; i++) await flush();
  assert.ok(sent.filter((m) => m.action === "save").length >= 3, "a 9000-character body is saved in several parts");
  assert.equal(notes[0].body, "x".repeat(9000));
  assert.equal(find("[data-notes-saved]").textContent, "已保存");
  find('[data-notes-mode="preview"]').click();
  editor.value = "# 标题";
  editor.dispatchEvent(new dom.window.Event("input", { bubbles: true }));
  runTimers();
  for (let i = 0; i < 6; i++) await flush();
  assert.match(find("[data-notes-preview]").innerHTML, /<h1>标题<\/h1>/);
  find('[data-notes-action="create"]').click();
  for (let i = 0; i < 6; i++) await flush();
  assert.equal(find("[data-notes-title]").value, "");
  assert.match(find("[data-notes-count]").textContent, /2 篇笔记/);
  assert.equal(root.querySelectorAll("[data-notes-open]").length, 2);
  // Closing the window first saves pending text, then confirms.
  editor.value = "关闭前的修改";
  editor.dispatchEvent(new dom.window.Event("input", { bubbles: true }));
  page.receive({ version: 1, type: "notes-close" });
  for (let i = 0; i < 6; i++) await flush();
  const actions = sent.map((m) => m.action);
  assert.equal(actions.at(-1), "closeReady");
  assert.equal(actions.at(-2), "save");
  assert.equal(notes[0].body, "关闭前的修改");
  page.dispose();
});
