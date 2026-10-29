import test from "node:test";
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { homedir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ui = fileURLToPath(new URL("..", import.meta.url));
const enabled = process.env.LUME_UI_BROWSER_TESTS === "1";
test("rich notes edit styles, preserve Markdown, isolate history and reject rich paste", { skip: !enabled, timeout: 60000 }, async () => {
  const require = createRequire(import.meta.url);
  const { chromium } = require(process.env.LUME_PLAYWRIGHT_DIR || path.join(homedir(), ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright"));
  const server = createServer(async (request, response) => {
    const pathname = new URL(request.url, "http://127.0.0.1").pathname;
    if (pathname === "/") return response.end('<!doctype html><meta charset="utf-8"><div id="editor"></div>');
    const file = path.resolve(ui, "." + pathname);
    if (!file.startsWith(path.resolve(ui) + path.sep)) return response.writeHead(403).end();
    try {
      const content = await readFile(file);
      response.writeHead(200, { "Content-Type": file.endsWith(".js") || file.endsWith(".mjs") ? "text/javascript" : "text/plain" }).end(content);
    } catch { response.writeHead(404).end(); }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  let browser;
  try {
    browser = await chromium.launch({ headless: true, channel: "msedge" });
    const page = await browser.newPage();
    const errors = [], external = [];
    page.on("pageerror", (error) => errors.push(error.message));
    page.on("request", (request) => { if (!request.url().startsWith("http://127.0.0.1:")) external.push(request.url()); });
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    const available = await page.evaluate(async () => {
      try {
        const { createNotesRichEditor } = await import("/notes-rich-editor.js");
        window.changes = [];
        window.formulas = [];
        window.selections = [];
        window.rich = createNotesRichEditor(document.querySelector("#editor"), {
          onChange: (value) => window.changes.push(value),
          onFormula: (...args) => window.formulas.push(args),
          onSelection: (value) => window.selections.push(value),
        });
        return true;
      } catch (error) { return error.message; }
    });
    assert.equal(available, true, "the rich editor adapter should initialize");
    await page.evaluate(() => { rich.setMarkdown("正文"); rich.setSelection({ from: 1, to: 3 }); rich.format("bold"); });
    assert.equal(await page.locator(".tiptap strong").innerText(), "正文");
    assert.equal(await page.evaluate(() => rich.getMarkdown()), "**正文**");
    assert.equal(await page.evaluate(() => changes.length), 1, "loading must not emit an edit");
    await page.evaluate(() => rich.format("italic"));
    assert.equal(await page.locator(".tiptap strong em, .tiptap em strong").innerText(), "正文");
    await page.evaluate(() => rich.undo());
    assert.equal(await page.locator(".tiptap em").count(), 0);
    await page.evaluate(() => rich.redo());
    assert.equal(await page.locator(".tiptap em").count(), 1);
    await page.evaluate(() => { rich.setMarkdown("另一个便签"); rich.undo(); });
    assert.equal(await page.locator(".tiptap").innerText(), "另一个便签");
    const roundtrip = await page.evaluate(() => {
      const value = "第一行\n第二行\n\n- [x] 完成\n- [ ] 待办\n\n|甲|乙|\n|---|---|\n|一|二|\n\n$x^2$\n\n$$\n\\sum_{i=1}^{n}i\n$$";
      rich.setMarkdown(value);
      const result = rich.getMarkdown();
      rich.setMarkdown(result);
      return result;
    });
    assert.match(roundtrip, /第一行 {2}\n第二行/);
    assert.match(roundtrip, /\[x\] 完成/);
    assert.equal(await page.locator(".tiptap table").count(), 1);
    assert.equal(await page.locator(".tiptap input[type=checkbox]").count(), 2);
    assert.equal(await page.locator(".tiptap .katex").count(), 2);
    await page.locator('.tiptap [data-type="inline-math"]').click();
    assert.equal(await page.evaluate(() => formulas[0][1]), "x^2");
    await page.evaluate(() => rich.format("math", "y^2"));
    assert.match(await page.evaluate(() => rich.getMarkdown()), /\$y\^2\$/);
    for (const [kind, selector] of [["heading", "h2"], ["bullet", "ul"], ["number", "ol"], ["quote", "blockquote"], ["code", "code"]]) {
      await page.evaluate((format) => { rich.setMarkdown("文字"); rich.setSelection({ from: 1, to: 3 }); rich.format(format); }, kind);
      assert.equal(await page.locator(`.tiptap ${selector}`).count(), 1, kind);
      await page.evaluate((format) => rich.format(format), kind);
      assert.equal(await page.locator(`.tiptap ${selector}`).count(), 0, `toggle ${kind}`);
    }
    await page.evaluate(() => { rich.setMarkdown("第一行\n第二行"); rich.setSelection({ from: 1, to: 8 }); rich.format("code"); });
    assert.match(await page.evaluate(() => rich.getMarkdown()), /```\n第一行\n第二行\n```/);
    await page.evaluate(() => { rich.setMarkdown("保留正文"); rich.setSelection({ from: 1, to: 5 }); rich.format("rule"); });
    assert.match(await page.locator(".tiptap").innerText(), /保留正文/);
    assert.equal(await page.locator(".tiptap hr").count(), 1);
    await page.evaluate(() => { rich.setMarkdown(""); rich.focus(); });
    await page.locator(".tiptap").pressSequentially("**literal**");
    assert.match(await page.evaluate(() => rich.getMarkdown()), /literal/);
    await page.evaluate(() => { rich.setMarkdown(""); rich.focus(); });
    await page.locator(".tiptap").pressSequentially("正文");
    await page.locator(".tiptap").press("Shift+Enter");
    await page.locator(".tiptap").pressSequentially("下一行");
    assert.match(await page.evaluate(() => rich.getMarkdown()), /正文 {2}\n下一行/);
    await page.evaluate(() => {
      rich.setMarkdown("<img src=https://example.invalid/request><script>window.bad=1</script>\n\n![photo](https://example.invalid/image)\n\n[site](https://example.invalid)");
    });
    assert.equal(await page.locator(".tiptap img[src], .tiptap script").count(), 0, await page.locator(".tiptap").innerHTML());
    assert.equal(await page.evaluate(() => window.bad), undefined);
    assert.match(await page.evaluate(() => rich.getMarkdown()), /!\[photo\]\(https:\/\/example.invalid\/image\)/);
    const url = page.url();
    await page.locator('.tiptap a').click();
    assert.equal(page.url(), url);
    const mathLimits = await page.evaluate(() => {
      rich.setMarkdown("$\\gdef\\notesLeak{42}$\n\n$\\notesLeak$");
      const errors = document.querySelectorAll(".tiptap .MdMathError").length;
      const before = rich.getMarkdown();
      const accepted = rich.format("math", "x".repeat(8193));
      return { errors, accepted, changed: before !== rich.getMarkdown() };
    });
    assert.equal(mathLimits.errors, 1, "one expression must not define macros for later expressions");
    assert.equal(mathLimits.accepted, false);
    assert.equal(mathLimits.changed, false);
    const literals = await page.evaluate(() => {
      rich.setMarkdown("a < b > c & d\n\n<b>bold</b> **strong**");
      const before = document.querySelector(".tiptap").textContent;
      const paragraphs = document.querySelectorAll(".tiptap p").length;
      rich.setMarkdown(rich.getMarkdown());
      return { before, after: document.querySelector(".tiptap").textContent, paragraphs };
    });
    assert.equal(literals.paragraphs, 2);
    assert.equal(literals.before, "a < b > c & d<b>bold</b> strong");
    assert.equal(literals.after, literals.before);
    const caretState = await page.evaluate(() => {
      rich.setMarkdown(""); rich.focus(); rich.format("bold");
      return selections.at(-1).active.bold;
    });
    assert.equal(caretState, true, "typing marks must update the toolbar even without a document change");
    await page.evaluate(() => {
      rich.setMarkdown(""); rich.focus();
      const clipboardData = new DataTransfer();
      clipboardData.setData("text/plain", "<b>纯文本</b>\n第二行");
      clipboardData.setData("text/html", '<img src="https://example.invalid/paste"><b>rich</b>');
      document.querySelector(".tiptap").dispatchEvent(new ClipboardEvent("paste", { clipboardData, bubbles: true, cancelable: true }));
    });
    assert.match(await page.locator(".tiptap").innerText(), /<b>纯文本<\/b>/);
    assert.equal(await page.locator(".tiptap b, .tiptap strong, .tiptap img").count(), 0);
    assert.deepEqual(external, []);
    assert.deepEqual(errors, []);
    await page.evaluate(() => rich.destroy());
    assert.equal(await page.locator(".tiptap").count(), 0);
  } finally {
    await browser?.close();
    await new Promise((resolve) => server.close(resolve));
  }
});
