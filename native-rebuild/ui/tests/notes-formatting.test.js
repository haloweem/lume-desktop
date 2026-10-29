import test from "node:test";
import assert from "node:assert/strict";
import { renderMarkdown } from "../notes-markdown.js";
import { formatMarkdown } from "../notes-formatting.js";

function apply(source, start, end, kind) {
  assert.equal(typeof formatMarkdown, "function", "the formatting operation must be available");
  const edit = formatMarkdown(source, start, end, kind);
  const value = source.slice(0, edit.start) + edit.text + source.slice(edit.end);
  assert.ok(edit.start >= 0 && edit.end >= edit.start && edit.end <= source.length);
  assert.ok(edit.selectionStart >= 0 && edit.selectionEnd >= edit.selectionStart && edit.selectionEnd <= value.length);
  return { ...edit, value, selected: value.slice(edit.selectionStart, edit.selectionEnd) };
}
const repeat = (result, kind) => apply(result.value, result.selectionStart, result.selectionEnd, kind);

for (const [kind, marker] of [["bold", "**"], ["italic", "*"]]) {
  test(`${kind} formats the selected text and a second click removes the same style`, () => {
    const first = apply("前正文后", 1, 3, kind);
    assert.equal(first.value, `前${marker}正文${marker}后`);
    assert.equal(first.selected, "正文");
    const second = repeat(first, kind);
    assert.equal(second.value, "前正文后");
    assert.equal(second.selected, "正文");
  });

  test(`${kind} removes selected delimiters as well as delimiters outside the selection`, () => {
    assert.equal(apply(`${marker}正文${marker}`, 0, marker.length * 2 + 2, kind).value, "正文");
  });

  test(`${kind} formats each nonempty selected line and leaves blank lines intact`, () => {
    const source = "第一行\n\n 第二行 \n第三行";
    const first = apply(source, 0, source.length, kind);
    assert.equal(first.value, `${marker}第一行${marker}\n\n ${marker}第二行${marker} \n${marker}第三行${marker}`);
    const tag = kind === "bold" ? "strong" : "em";
    assert.equal((renderMarkdown(first.value).match(new RegExp(`<${tag}>`, "g")) || []).length, 3);
    assert.equal(repeat(first, kind).value, source);
  });
}

test("bold and italic toggle independently on the same selected text", () => {
  const bold = apply("正文", 0, 2, "bold");
  const both = repeat(bold, "italic");
  assert.equal(both.value, "***正文***");
  assert.equal(both.selected, "正文");
  assert.equal(repeat(both, "bold").value, "*正文*");
  assert.equal(repeat(both, "italic").value, "**正文**");
  assert.equal(apply("***正文***", 0, 8, "bold").value, "*正文*");
  assert.equal(apply("***正文***", 0, 8, "italic").value, "**正文**");
});

test("repeated italic clicks never turn the text into bold", () => {
  let state = apply("正文", 0, 2, "italic");
  for (let i = 0; i < 5; i++) {
    state = repeat(state, "italic");
    assert.equal(state.value, i % 2 ? "*正文*" : "正文");
  }
});

test("a mixed multiline selection receives the requested style consistently", () => {
  const source = "**第一行**\n第二行";
  assert.equal(apply(source, 0, source.length, "bold").value, "**第一行**\n**第二行**");
});

for (const [kind, prefix] of [["heading", "## "], ["quote", "> "], ["bullet", "- "], ["number", "1. "]]) {
  test(`${kind} applies to full lines and toggles without duplicating prefixes`, () => {
    const first = apply("第一行\n第二行", 1, 6, kind);
    assert.equal(first.value, `${prefix}第一行\n${kind === "number" ? "2. " : prefix}第二行`);
    assert.equal(repeat(first, kind).value, "第一行\n第二行");
  });

  test(`${kind} leaves the next line alone when the selection ends at its start`, () => {
    assert.equal(apply("第一行\n第二行", 0, 4, kind).value, `${prefix}第一行\n第二行`);
  });
}

test("line styles replace an existing style instead of stacking markers", () => {
  assert.equal(apply("- 第一行\n- 第二行", 0, 11, "number").value, "1. 第一行\n2. 第二行");
  assert.equal(apply("## 标题", 0, 5, "quote").value, "> 标题");
  assert.equal(apply("> 引用", 0, 4, "heading").value, "## 引用");
  assert.equal(apply("1. 一\n\n2. 二", 0, 10, "bullet").value, "- 一\n\n- 二");
  assert.equal(apply("一\n\n二", 0, 4, "number").value, "1. 一\n\n2. 二");
});

test("line formatting with an empty selection keeps the caret at its text position", () => {
  const empty = apply("", 0, 0, "heading");
  assert.equal(empty.value, "## ");
  assert.equal(empty.selectionStart, 3);
  assert.equal(empty.selectionEnd, 3);
  const middle = apply("正文", 1, 1, "bullet");
  assert.equal(middle.value, "- 正文");
  assert.equal(middle.selectionStart, 3);
  assert.equal(middle.selectionEnd, 3);
  const toggled = repeat(middle, "bullet");
  assert.equal(toggled.value, "正文");
  assert.equal(toggled.selectionStart, 1);
  assert.equal(toggled.selectionEnd, 1);
});

test("code blocks have their own lines and preserve text before and after a selection", () => {
  const result = apply("前正文一\n正文二后", 1, 8, "code");
  assert.equal(result.value, "前\n\n```\n正文一\n正文二\n```\n\n后");
  assert.equal(result.selected, "正文一\n正文二");
});

test("inline code chooses a delimiter longer than any literal backtick run", () => {
  const result = apply("a`b", 0, 3, "code");
  assert.equal(result.value, "``a`b``");
  assert.equal(result.selected, "a`b");
  const edge = apply("`x`", 0, 3, "code");
  assert.equal(edge.value, "`` `x` ``");
  assert.equal(edge.selected, "`x`");
});

test("code fences remain intact when the selected code contains backticks", () => {
  const source = "first\n```\nlast";
  const result = apply(source, 0, source.length, "code");
  assert.equal(result.value, "````\nfirst\n```\nlast\n````");
  assert.equal(result.selected, source);
});

test("code toggles its adjacent inline delimiters, including padding around literal backticks", () => {
  for (const source of ["正文", "a`b", "`x`", " a "]) {
    const first = apply(source, 0, source.length, "code");
    const second = repeat(first, "code");
    assert.equal(second.value, source);
    assert.equal(second.selected, source);
  }
});

test("code toggles adjacent block fences without consuming surrounding paragraphs", () => {
  const source = "a\n```\nb";
  const first = apply(source, 0, source.length, "code");
  assert.equal(repeat(first, "code").value, source);
  assert.equal(apply("前\n\n```\na\nb\n```\n\n后", 7, 10, "code").value, "前\n\na\nb\n\n后");
});

test("horizontal rules are inserted after selected lines without deleting their text", () => {
  assert.equal(apply("第一行\n第二行\n第三行", 1, 5, "rule").value, "第一行\n第二行\n\n---\n\n第三行");
  assert.equal(apply("第一行\n第二行", 0, 4, "rule").value, "第一行\n\n---\n\n第二行");
  assert.equal(apply("保留正文", 0, 4, "rule").value, "保留正文\n\n---\n\n");
});

test("math delimiters remain balanced for inline and multiline selections", () => {
  const inline = apply("x^2", 0, 3, "math");
  assert.equal(inline.value, "$x^2$");
  assert.equal(inline.selected, "x^2");
  assert.equal(apply("x^2\n\ny^2", 0, 8, "math").value, "$x^2$\n\n$y^2$");
  const block = apply("前x^2后", 1, 4, "block");
  assert.equal(block.value, "前\n\n$$\nx^2\n$$\n\n后");
  assert.equal(block.selected, "x^2");
});

test("math toggles adjacent delimiters instead of accumulating dollar signs", () => {
  const first = apply("x^2", 0, 3, "math");
  assert.equal(repeat(first, "math").value, "x^2");
  const multiline = apply("x^2\ny^2", 0, 7, "math");
  assert.equal(repeat(multiline, "math").value, "x^2\ny^2");
  assert.equal(apply("$x^2$", 0, 5, "math").value, "x^2");
});

test("empty selections insert editable placeholders inside the requested delimiters", () => {
  for (const kind of ["bold", "italic", "code", "math", "block"]) {
    const result = apply("前后", 1, 1, kind);
    assert.ok(result.selected.length > 0, kind);
    assert.ok(result.value.startsWith("前") && result.value.endsWith("后"), kind);
    assert.doesNotMatch(result.selected, /[\n*`$]/, kind);
  }
});

test("callers can supply localized placeholders without changing selection semantics", () => {
  const placeholders = { bold: "bold", italic: "italic", code: "code", block: "sum" };
  for (const [kind, placeholder] of Object.entries(placeholders)) {
    const edit = formatMarkdown("", 0, 0, kind, placeholders);
    assert.equal(edit.text.slice(edit.selectionStart, edit.selectionEnd), placeholder);
  }
});
