// Toolbar operations return one textarea replacement plus its resulting
// absolute selection. The caller owns focus, undo, rendering and persistence.
const result = (start, end, text, selectionStart = start, selectionEnd = start + text.length) =>
  ({ start, end, text, selectionStart, selectionEnd });

function combine(source, edits, start, end) {
  if (!edits.length) return result(start, end, source.slice(start, end), start, end);
  let text = "", position = edits[0].start, selectionStart, selectionEnd;
  for (const edit of edits) {
    text += source.slice(position, edit.start);
    selectionStart ??= edits[0].start + text.length + edit.innerStart;
    selectionEnd = edits[0].start + text.length + edit.innerEnd;
    text += edit.text;
    position = edit.end;
  }
  return result(edits[0].start, position, text, selectionStart, selectionEnd);
}

function selectedLines(source, start, end) {
  const lines = [];
  let position = start;
  for (const line of source.slice(start, end).split("\n")) {
    const leading = line.length - line.trimStart().length;
    const trailing = line.length - line.trimEnd().length;
    if (line.trim()) lines.push({ start: position + leading, end: position + line.length - trailing });
    position += line.length + 1;
  }
  return lines;
}

function emphasis(source, start, end, kind, placeholders) {
  const bit = kind === "bold" ? 2 : 1;
  if (start === end) {
    const body = placeholders[kind] ?? (kind === "bold" ? "粗体" : "斜体"), marker = "*".repeat(bit);
    return result(start, end, marker + body + marker, start + bit, start + bit + body.length);
  }
  const spans = selectedLines(source, start, end).map((span) => {
    let a = span.start, b = span.end;
    const selected = source.slice(a, b);
    const opening = selected.match(/^\*+/)?.[0].length || 0;
    const closing = selected.match(/\*+$/)?.[0].length || 0;
    if (opening <= 3 && closing <= 3 && selected.length > opening + closing) {
      a += opening;
      b -= closing;
    }
    let left = a, right = b;
    while (left > 0 && source[left - 1] === "*") left--;
    while (right < source.length && source[right] === "*") right++;
    const count = a - left === right - b && a - left <= 3 ? a - left : 0;
    if (!count) { a = span.start; b = span.end; }
    return { start: a - count, end: b + count, body: source.slice(a, b), count };
  });
  const remove = spans.length > 0 && spans.every((span) => span.count & bit);
  return combine(source, spans.map((span) => {
    const count = remove ? span.count & ~bit : span.count | bit;
    const marker = "*".repeat(count);
    return { ...span, text: marker + span.body + marker, innerStart: count, innerEnd: count + span.body.length };
  }), start, end);
}

function lineRange(source, start, end) {
  const first = start === 0 ? 0 : source.lastIndexOf("\n", start - 1) + 1;
  // A selection ending just after a newline does not include the next line.
  const last = end > start && source[end - 1] === "\n" ? end - 1 : end;
  const newline = source.indexOf("\n", last);
  return { start: first, end: newline < 0 ? source.length : newline };
}

function lineStyle(source, start, end, kind) {
  const range = lineRange(source, start, end);
  const lines = source.slice(range.start, range.end).split("\n");
  const prefixPattern = /^(#{1,6}[ \t]+|>[ \t]?|[-+*][ \t]+|\d+[.)][ \t]+)/;
  const styleOf = (prefix) => !prefix ? "" : prefix.startsWith("#") ? "heading" :
    prefix.startsWith(">") ? "quote" : /^\d/.test(prefix) ? "number" : "bullet";
  const entries = lines.map((line) => {
    const prefix = line.match(prefixPattern)?.[0] || "";
    return { line, body: line.slice(prefix.length), kind: styleOf(prefix) };
  });
  const nonempty = entries.filter((entry) => entry.line.trim());
  const remove = nonempty.length > 0 && nonempty.every((entry) => entry.kind === kind);
  let index = 0;
  const text = entries.map((entry) => {
    if (!entry.line.trim() && entries.length > 1) return entry.line;
    if (remove) return entry.body;
    index++;
    const prefix = kind === "heading" ? "## " : kind === "quote" ? "> " : kind === "bullet" ? "- " : `${index}. `;
    return prefix + entry.body;
  }).join("\n");
  if (start === end) {
    const oldPrefix = entries[0].line.length - entries[0].body.length;
    const newPrefix = text.length - entries[0].body.length;
    const caret = range.start + newPrefix + Math.max(0, start - range.start - oldPrefix);
    return result(range.start, range.end, text, caret, caret);
  }
  return result(range.start, range.end, text, range.start, range.start + text.length);
}

const newlinePadding = (text, before) => {
  if (!text) return "";
  const count = (text.match(before ? /\n*$/ : /^\n*/)?.[0] || "").length;
  return "\n".repeat(Math.max(0, 2 - count));
};

function block(source, start, end, body, delimiter) {
  const left = newlinePadding(source.slice(0, start), true) + delimiter + "\n";
  const right = (body.endsWith("\n") ? "" : "\n") + delimiter + newlinePadding(source.slice(end), false);
  return result(start, end, left + body + right, start + left.length, start + left.length + body.length);
}

function code(source, start, end, placeholders) {
  const body = source.slice(start, end) || (placeholders.code ?? "代码");
  if (start !== end) {
    const opening = source.slice(0, start).match(/(^|\n)(`{3,})\n$/);
    const closing = source.slice(end).match(/^(\n?)(`{3,})(?=\n|$)/);
    if (opening && closing && opening[2] === closing[2]) {
      const a = start - opening[2].length - 1, b = end + closing[0].length;
      return result(a, b, body, a, a + body.length);
    }
    for (const pad of [0, 1]) {
      if (pad && (source[start - 1] !== " " || source[end] !== " ")) continue;
      const left = source.slice(0, start - pad).match(/`+$/)?.[0];
      const right = source.slice(end + pad).match(/^`+/)?.[0];
      if (left && left === right) {
        const a = start - pad - left.length, b = end + pad + right.length;
        return result(a, b, body, a, a + body.length);
      }
    }
  }
  const runs = body.match(/`+/g) || [];
  const longest = runs.reduce((length, run) => Math.max(length, run.length), 0);
  if (body.includes("\n")) return block(source, start, end, body, "`".repeat(Math.max(3, longest + 1)));
  const marker = "`".repeat(longest + 1);
  const pad = /^`|`$/.test(body) || (/^ .* $/.test(body) && body.trim()) ? " " : "";
  const left = marker + pad;
  return result(start, end, left + body + pad + marker, start + left.length, start + left.length + body.length);
}

function math(source, start, end) {
  if (start === end) return result(start, end, "$x^2$", start + 1, start + 4);
  const spans = selectedLines(source, start, end).map((span) => {
    let a = span.start, b = span.end;
    if (source[a] === "$") a++;
    if (source[b - 1] === "$") b--;
    const wrapped = b > a && source[a - 1] === "$" && source[b] === "$" &&
      source[a - 2] !== "$" && source[b + 1] !== "$";
    if (!wrapped) { a = span.start; b = span.end; }
    return { start: a - Number(wrapped), end: b + Number(wrapped), body: source.slice(a, b), wrapped };
  });
  const remove = spans.length > 0 && spans.every((span) => span.wrapped);
  return combine(source, spans.map((span) => {
    const marker = remove ? "" : "$";
    return { ...span, text: marker + span.body + marker, innerStart: marker.length, innerEnd: marker.length + span.body.length };
  }), start, end);
}

export function formatMarkdown(source, start, end, kind, placeholders = {}) {
  source = String(source);
  start = Math.max(0, Math.min(source.length, start));
  end = Math.max(start, Math.min(source.length, end));
  if (kind === "bold" || kind === "italic") return emphasis(source, start, end, kind, placeholders);
  if (["heading", "quote", "bullet", "number"].includes(kind)) return lineStyle(source, start, end, kind);
  if (kind === "code") return code(source, start, end, placeholders);
  if (kind === "math") return math(source, start, end);
  if (kind === "block") return block(source, start, end, source.slice(start, end) || (placeholders.block ?? "x^2"), "$$");
  if (kind === "rule") {
    const at = lineRange(source, start, end).end;
    const text = newlinePadding(source.slice(0, at), true) + "---" + (newlinePadding(source.slice(at), false) || "\n\n");
    return result(at, at, text, at + text.length, at + text.length);
  }
  return result(start, end, source.slice(start, end), start, end);
}
