// The notes Markdown subset. User HTML is always escaped; links and images are
// inert. Math uses the pinned local KaTeX runtime, never a network renderer.
import katex from "./vendor/notes-math/katex.mjs";

const escapeHtml = (value) => String(value).replace(/[&<>"']/g, (character) =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[character]);
const MAX_FORMULA_LENGTH = 8192;
const namedEntities = { amp: "&", lt: "<", gt: ">", quot: "\"", apos: "'" };
const escapablePunctuation = /^[!"#$%&'()*+,\-.\/:;<=>?@[\\\]^_`{|}~]$/;

// Marked/Tiptap may serialize an ordinary ampersand as &amp;. Decode one HTML
// entity layer before escaping output again; a second layer stays literal.
function decodeEntities(value) {
  return value.replace(/&(?:#x([0-9a-f]+)|#([0-9]+)|([a-z][a-z0-9]+));/gi, (full, hex, decimal, name) => {
    if (name) return namedEntities[name.toLowerCase()] ?? full;
    const codePoint = Number.parseInt(hex || decimal, hex ? 16 : 10);
    if (!Number.isSafeInteger(codePoint) || codePoint <= 0 || codePoint > 0x10ffff ||
      (codePoint >= 0xd800 && codePoint <= 0xdfff)) return "\ufffd";
    return String.fromCodePoint(codePoint);
  });
}

function math(source, displayMode) {
  let rendered;
  try {
    if (source.length > MAX_FORMULA_LENGTH) throw new Error("Formula exceeds the 8192-character limit");
    rendered = katex.renderToString(source, {
      displayMode, output: "htmlAndMathml", throwOnError: false, trust: false,
      strict: "ignore", maxExpand: 1000, maxSize: 10, macros: {}, errorColor: "#ff9b85",
    });
  } catch (error) {
    rendered = `<span class="katex-error" title="${escapeHtml(error.message)}">${escapeHtml(source)}</span>`;
  }
  return displayMode
    ? `<div class="MdMath MdMathBlock">${rendered}</div>`
    : `<span class="MdMath">${rendered}</span>`;
}

// Pair delimiter runs before emitting HTML so nested formatting cannot create
// crossing tags. Unpaired markers stay visible as literal Markdown.
function emphasis(source) {
  const parts = [];
  const openers = [];
  let at = 0;
  for (const match of source.matchAll(/\*+|_{2,}|~{2,}|={2,}/g)) {
    parts.push(source.slice(at, match.index));
    const marker = match[0][0];
    let remaining = match[0].length;
    const before = source[match.index - 1] || "";
    const after = source[match.index + remaining] || "";
    if (before && !/\s/.test(before)) {
      let openerIndex = openers.length - 1;
      while (openerIndex >= 0 && remaining > 0) {
        const opener = openers[openerIndex];
        if (opener.marker !== marker || opener.length > remaining) { openerIndex--; continue; }
        parts[opener.part] = `<${opener.tag}>`;
        parts.push(`</${opener.tag}>`);
        remaining -= opener.length;
        // Discard intervening unmatched delimiters; their text remains literal.
        openers.splice(openerIndex);
        openerIndex = openers.length - 1;
      }
    }
    if (remaining && after && !/\s/.test(after)) {
      const widths = marker === "*"
        ? [...(remaining % 2 ? [1] : []), ...Array(Math.floor(remaining / 2)).fill(2)]
        : Array(Math.floor(remaining / 2)).fill(2);
      for (const length of widths) {
        const tag = marker === "*" && length === 1 ? "em" : marker === "~" ? "del" : marker === "=" ? "mark" : "strong";
        openers.push({ marker, length, tag, part: parts.length });
        parts.push(marker.repeat(length));
        remaining -= length;
      }
    }
    if (remaining) parts.push(marker.repeat(remaining));
    at = match.index + match[0].length;
  }
  parts.push(source.slice(at));
  return parts.join("");
}

// Protect complete code/math/link fragments before applying text emphasis.
// NUL is reserved for private tokens and cannot be supplied by note contents.
function inline(input) {
  const source = input.replace(/\0/g, "\ufffd");
  const fragments = [];
  const protect = (html) => `\0${fragments.push(html) - 1}\0`;
  let text = "";
  for (let at = 0; at < source.length;) {
    const rest = source.slice(at);
    if (source[at] === "\\" && escapablePunctuation.test(source[at + 1] || "")) {
      text += protect(escapeHtml(source[at + 1]));
      at += 2;
      continue;
    }
    const entity = source[at] === "&" && rest.match(/^&(?:#x[0-9a-f]+|#[0-9]+|[a-z][a-z0-9]+);/i);
    if (entity) {
      text += protect(escapeHtml(decodeEntities(entity[0])));
      at += entity[0].length;
      continue;
    }
    if (source[at] === "`") {
      const delimiter = rest.match(/^`+/)[0];
      const runs = /`+/g;
      runs.lastIndex = at + delimiter.length;
      let closing;
      while ((closing = runs.exec(source)) && closing[0].length !== delimiter.length) { /* find an equal run */ }
      if (closing) {
        let body = source.slice(at + delimiter.length, closing.index);
        if (body.startsWith(" ") && body.endsWith(" ") && /[^ ]/.test(body)) body = body.slice(1, -1);
        text += protect(`<code>${escapeHtml(body)}</code>`);
        at = closing.index + delimiter.length;
        continue;
      }
      text += delimiter;
      at += delimiter.length;
      continue;
    }
    if (source[at] === "$" && source[at + 1] !== "$" && source[at - 1] !== "$") {
      let stop = at + 1;
      for (; stop < source.length; stop++) {
        if (source[stop] === "\\") { stop++; continue; }
        if (source[stop] === "$") break;
      }
      if (stop < source.length && stop > at + 1) {
        text += protect(math(source.slice(at + 1, stop), false));
        at = stop + 1;
        continue;
      }
    }
    const image = rest.match(/^!\[([^\]]*)\]\([^)\s]+\)/);
    if (image) {
      text += protect(`<span class="MdImage">${escapeHtml(image[1])}</span>`);
      at += image[0].length;
      continue;
    }
    const link = rest.match(/^\[([^\]]+)\]\(((?:https?:|mailto:)[^)\s]+)\)/);
    if (link) {
      const url = escapeHtml(link[2]);
      text += protect(`<a class="MdLink" title="${url}" data-href="${url}">${escapeHtml(link[1])}</a>`);
      at += link[0].length;
      continue;
    }
    text += source[at++];
  }
  const html = emphasis(escapeHtml(text));
  return html.replace(/\0(\d+)\0/g, (_, index) => fragments[Number(index)]);
}

const tableRow = (line) => line.trim().replace(/^\||\|$/g, "").split("|").map((cell) => cell.trim());
const openingFence = (line) => line.match(/^\s*(`{3,}|~{3,})\s*([\w-]*)\s*$/);
const rule = (line) => /^\s*([-*_])(\s*\1){2,}\s*$/.test(line);

export function renderMarkdown(source) {
  const lines = String(source).replace(/\r\n?/g, "\n").split("\n");
  const out = [];
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    if (!line.trim()) { i++; continue; }
    const fence = openingFence(line);
    if (fence) {
      const body = [];
      const closingFence = new RegExp(`^\\s*${fence[1][0]}{${fence[1].length},}\\s*$`);
      for (i++; i < lines.length && !closingFence.test(lines[i]); i++) body.push(lines[i]);
      if (i < lines.length) i++;
      out.push(`<pre><code>${escapeHtml(body.join("\n"))}</code></pre>`);
      continue;
    }
    if (line.trim() === "$$") {
      const body = [];
      for (i++; i < lines.length && lines[i].trim() !== "$$"; i++) body.push(lines[i]);
      if (i < lines.length) i++;
      out.push(math(body.join("\n"), true));
      continue;
    }
    const heading = line.match(/^(#{1,6})\s+(.*)$/);
    if (heading) {
      const level = heading[1].length;
      out.push(`<h${level}>${inline(heading[2].replace(/\s+#+\s*$/, ""))}</h${level}>`);
      i++;
      continue;
    }
    if (rule(line)) { out.push("<hr>"); i++; continue; }
    if (/^\s*>/.test(line)) {
      const body = [];
      for (; i < lines.length && /^\s*>/.test(lines[i]); i++) body.push(lines[i].replace(/^\s*>\s?/, ""));
      out.push(`<blockquote>${renderMarkdown(body.join("\n"))}</blockquote>`);
      continue;
    }
    if (line.includes("|") && i + 1 < lines.length && /^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)*\|?\s*$/.test(lines[i + 1])) {
      const head = tableRow(line);
      const rows = [];
      for (i += 2; i < lines.length && lines[i].includes("|") && lines[i].trim(); i++) rows.push(tableRow(lines[i]));
      out.push(`<table><thead><tr>${head.map((cell) => `<th>${inline(cell)}</th>`).join("")}</tr></thead><tbody>${
        rows.map((row) => `<tr>${head.map((_, column) => `<td>${inline(row[column] || "")}</td>`).join("")}</tr>`).join("")}</tbody></table>`);
      continue;
    }
    const item = line.match(/^\s*([-*+]|\d+[.)])\s+(.*)$/);
    if (item) {
      const ordered = /\d/.test(item[1]);
      const items = [];
      for (; i < lines.length; i++) {
        const next = lines[i].match(/^\s*([-*+]|\d+[.)])\s+(.*)$/);
        if (!next || /\d/.test(next[1]) !== ordered) break;
        const task = next[2].match(/^\[([ xX])\]\s+(.*)$/);
        items.push(task
          ? `<li class="MdTask"><span class="MdCheck${task[1] === " " ? "" : " is-done"}" aria-hidden="true"></span>${inline(task[2])}</li>`
          : `<li>${inline(next[2])}</li>`);
      }
      const start = ordered ? parseInt(item[1], 10) : 1;
      out.push(ordered ? `<ol${start !== 1 ? ` start="${start}"` : ""}>${items.join("")}</ol>` : `<ul>${items.join("")}</ul>`);
      continue;
    }
    const paragraph = [];
    for (; i < lines.length && lines[i].trim() && !/^(#{1,6}\s|\s*>|\s*([-*+]|\d+[.)])\s)/.test(lines[i]) &&
      !openingFence(lines[i]) && !rule(lines[i]) && lines[i].trim() !== "$$"; i++) paragraph.push(inline(lines[i]));
    if (!paragraph.length) { out.push(`<p>${inline(lines[i])}</p>`); i++; continue; }
    out.push(`<p>${paragraph.join("<br>")}</p>`);
  }
  return out.join("");
}
