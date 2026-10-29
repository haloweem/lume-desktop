// Notes window page: sidebar list, Markdown editor and preview, following the
// layout of Floral Notepaper (花笺, github.com/Achilng/floral-notepaper, MIT).
// Written for Lume without its code: the native host owns the file, this page
// only edits. The manager uses only the command/message validators below.
import { t } from "./localization.js";
import { formatMarkdown } from "./notes-formatting.js";

const MAX_NOTES = 500;
const MAX_BODY_BYTES = 1000000;
const MAX_TITLE_BYTES = 600;
const SAVE_DELAY = 700;
const idPattern = /^[a-z0-9-]{1,64}$/;
const encoder = new TextEncoder();
const bytes = (text) => encoder.encode(text).length;
const own = (value, key) => Object.prototype.hasOwnProperty.call(value, key);
const exact = (value, keys) =>
  !!value && typeof value === "object" && !Array.isArray(value) &&
  Object.keys(value).length === keys.length && keys.every((key) => own(value, key));
const stamp = (value) => Number.isSafeInteger(value) && value >= 0 && value <= 253402300799999;
const text = (value, max) => typeof value === "string" && value.length <= max;

export function validNotesCommand(action, fields = {}) {
  if (["snapshot", "create", "createDesktop", "openWindow", "closeReady"].includes(action)) return exact(fields, []);
  if (action === "open" || action === "delete") return exact(fields, ["id"]) && idPattern.test(fields.id);
  if (action === "pin") return exact(fields, ["id", "value"]) && idPattern.test(fields.id) && typeof fields.value === "boolean";
  if (action === "setEnabled" || action === "setActive") return exact(fields, ["value"]) && typeof fields.value === "boolean";
  if (action === "save")
    return exact(fields, ["id", "title", "part", "index", "count", ...(own(fields, "baseUpdatedAt") ? ["baseUpdatedAt"] : [])]) &&
      (!own(fields, "baseUpdatedAt") || stamp(fields.baseUpdatedAt)) && idPattern.test(fields.id) &&
      text(fields.title, MAX_TITLE_BYTES) && !/[\r\n]/.test(fields.title) && text(fields.part, 4000) &&
      Number.isInteger(fields.count) && fields.count >= 1 && fields.count <= 2000 &&
      Number.isInteger(fields.index) && fields.index >= 0 && fields.index < fields.count;
  return false;
}
const validSummary = (note) =>
  exact(note, ["id", "title", "preview", "createdAt", "updatedAt", "chars", "bytes"]) &&
  idPattern.test(note.id) && text(note.title, MAX_TITLE_BYTES) && text(note.preview, 400) &&
  stamp(note.createdAt) && stamp(note.updatedAt) &&
  Number.isSafeInteger(note.chars) && note.chars >= 0 && Number.isSafeInteger(note.bytes) &&
  note.bytes >= 0 && note.bytes <= MAX_BODY_BYTES;
export function validNotesMessage(m) {
  if (m?.type === "notes-state")
    return exact(m, ["version", "type", "enabled", "running", "windowOpen", "locked", "material", "loadStatus", "error",
      "pinnedId", "notes", "result", "savedId", "saved", ...(own(m, "desktopIds") ? ["desktopIds"] : [])]) &&
      (!own(m, "desktopIds") || (Array.isArray(m.desktopIds) && m.desktopIds.length <= MAX_NOTES &&
        new Set(m.desktopIds).size === m.desktopIds.length && m.desktopIds.every(id => idPattern.test(id)))) &&
      ["enabled", "running", "windowOpen", "locked", "saved"].every((key) => typeof m[key] === "boolean") &&
      ["liquid-glass", "frosted-glass"].includes(m.material) &&
      ["not-restored", "fresh", "loaded", "failed"].includes(m.loadStatus) &&
      ["", "load", "save", "runtime", "limit", "invalid", "not-found", "conflict"].includes(m.error) &&
      (m.pinnedId === "" || idPattern.test(m.pinnedId)) && (m.savedId === "" || idPattern.test(m.savedId)) &&
      ["snapshot", "applied", "saved"].includes(m.result) &&
      Array.isArray(m.notes) && m.notes.length <= MAX_NOTES && m.notes.every(validSummary);
  if (m?.type === "notes-note" || m?.type === "notes-update")
    return exact(m, ["version", "type", "id", "title", "body", "createdAt", "updatedAt"]) &&
      idPattern.test(m.id) && text(m.title, MAX_TITLE_BYTES) && text(m.body, MAX_BODY_BYTES) &&
      stamp(m.createdAt) && stamp(m.updatedAt);
  if (m?.type === "notes-close") return exact(m, ["version", "type"]);
  if (m?.type === "notes-chunk")
    return exact(m, ["version", "type", "id", "index"]) && idPattern.test(m.id) &&
      Number.isInteger(m.index) && m.index >= 0 && m.index < 2000;
  return false;
}

const escapeHtml = (value) => String(value).replace(/[&<>"']/g, (c) =>
  ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
const pad = (n) => String(n).padStart(2, "0");
const day = (ms) => { const d = new Date(ms); return `${pad(d.getMonth() + 1)}-${pad(d.getDate())}`; };
const clock = (ms) => { const d = new Date(ms); return `${pad(d.getHours())}:${pad(d.getMinutes())}`; };
// Same rule as the host: non-whitespace code points.
export const characterCount = (value) => Array.from(value.replace(/\s+/gu, "")).length;

// Splits body into parts that each post as a message below the host's 8 KB
// limit, never between the halves of a surrogate pair.
export function splitBody(body, title, id) {
  const parts = [];
  let at = 0;
  do {
    let size = Math.min(3000, body.length - at);
    for (;;) {
      if (size > 0 && at + size < body.length && /[\uD800-\uDBFF]/.test(body[at + size - 1])) size--;
      const part = body.slice(at, at + size);
      const length = JSON.stringify({ version: 1, scope: "notes", action: "save", id, title, part, index: 1999, count: 2000 }).length;
      if (length <= 7600 || size <= 1) { parts.push(part); at += size; break; }
      size = Math.floor(size / 2);
    }
  } while (at < body.length);
  return parts;
}

const icons = {
  sidebar: '<rect x="3.5" y="4.5" width="17" height="15" rx="2"/><path d="M9 4.5v15"/>',
  pin: '<path d="M9 4h6l-1 6 3 3v1H7v-1l3-3z"/><path d="M12 14v6"/>',
  undo: '<path d="M9 14 4 9l5-5"/><path d="M4 9h10a6 6 0 0 1 0 12h-3"/>',
  redo: '<path d="m15 14 5-5-5-5"/><path d="M20 9H10a6 6 0 0 0 0 12h3"/>',
  trash: '<path d="M4 7h16"/><path d="M9 7V4h6v3"/><path d="M6 7l1 13h10l1-13"/>',
  search: '<circle cx="11" cy="11" r="6.5"/><path d="m20 20-4.2-4.2"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  import: '<path d="M12 4v11"/><path d="m7 11 5 5 5-5"/><path d="M5 20h14"/>',
};
const icon = (name) => `<svg viewBox="0 0 24 24" aria-hidden="true">${icons[name]}</svg>`;
const formats = [
  ["bold", "B", "加粗"], ["italic", "I", "斜体"], ["heading", "H", "标题"], ["rule", "—", "分隔线"],
  ["bullet", "•", "无序列表"], ["number", "1.", "有序列表"], ["code", "<>", "代码"], ["quote", "❝", "引用"],
  ["math", "Σ", "行内公式"], ["block", "∫", "公式块"],
];

export function notesPageMarkup() {
  return `<div class="NotesRoot" data-notes-root data-sidebar="open" data-mode="edit">
  <aside class="NotesSidebar" aria-label="${t("笔记列表", "Notes")}">
    <label class="NotesSearch">${icon("search")}<input type="search" data-notes-search maxlength="200" placeholder="${t("搜索笔记…", "Search notes…")}" aria-label="${t("搜索笔记", "Search notes")}"></label>
    <button type="button" class="NotesSideButton" data-notes-action="create">${icon("plus")}<span>${t("新建笔记", "New note")}</span></button>
    <button type="button" class="NotesSideButton" data-notes-action="import">${icon("import")}<span>${t("导入 Markdown", "Import Markdown")}</span></button>
    <input type="file" data-notes-file accept=".md,.markdown,.txt,text/markdown,text/plain" multiple hidden>
    <div class="NotesCount"><span data-notes-count></span><button type="button" data-notes-action="create" aria-label="${t("新建笔记", "New note")}" title="${t("新建笔记", "New note")}">${icon("plus")}</button></div>
    <div class="NotesList" data-notes-list role="listbox" aria-label="${t("笔记", "Notes")}"></div>
  </aside>
  <section class="NotesMain">
    <header class="NotesToolbar">
      <button type="button" class="NotesTool" data-notes-action="sidebar" title="${t("显示或隐藏列表", "Toggle list")}" aria-label="${t("显示或隐藏列表", "Toggle list")}">${icon("sidebar")}</button>
      <span class="NotesDivider"></span>
      <button type="button" class="NotesTool" data-notes-action="pin" data-notes-needs-note aria-pressed="false">${icon("pin")}</button>
      <button type="button" class="NotesTool" data-notes-action="undo" data-notes-needs-note title="${t("撤销", "Undo")}" aria-label="${t("撤销", "Undo")}">${icon("undo")}</button>
      <button type="button" class="NotesTool" data-notes-action="redo" data-notes-needs-note title="${t("重做", "Redo")}" aria-label="${t("重做", "Redo")}">${icon("redo")}</button>
      <button type="button" class="NotesTool NotesTextTool" data-notes-action="save" data-notes-needs-note title="Ctrl+S">${t("保存", "Save")}</button>
      <span class="NotesDeleteWrap">
        <button type="button" class="NotesTool" data-notes-action="delete" data-notes-needs-note title="${t("删除", "Delete")}" aria-label="${t("删除", "Delete")}">${icon("trash")}</button>
        <span class="NotesConfirm" data-notes-confirm role="dialog" hidden><span>${t("删除这篇笔记？", "Delete this note?")}</span>
          <button type="button" data-notes-action="confirm-delete" class="is-danger">${t("删除", "Delete")}</button>
          <button type="button" data-notes-action="cancel-delete">${t("取消", "Cancel")}</button></span>
      </span>
      <span class="NotesSpacer"></span>
      <div class="NotesModes" role="group" aria-label="${t("视图", "View")}">
        <button type="button" data-notes-mode="edit" title="${t("直接编辑排版内容", "Edit formatted text")}">${t("编辑", "Edit")}</button>
        <button type="button" data-notes-mode="source" title="${t("编辑 Markdown 源码", "Edit Markdown source")}">${t("源码", "Source")}</button>
        <button type="button" data-notes-mode="split" title="${t("源码和实时预览", "Source and live preview")}">${t("分栏", "Split")}</button>
        <button type="button" data-notes-mode="preview">${t("预览", "Preview")}</button>
      </div>
    </header>
    <div class="NotesDocument" data-notes-document hidden>
      <div class="NotesHead">
        <input class="NotesTitle" data-notes-title maxlength="200" placeholder="${t("无标题笔记", "Untitled note")}" aria-label="${t("标题", "Title")}">
        <div class="NotesMeta"><span data-notes-created></span><span class="NotesDot">·</span><span data-notes-chars></span><span class="NotesDot">·</span><span data-notes-saved></span></div>
      </div>
      <div class="NotesFormat" role="toolbar" aria-label="${t("格式", "Format")}">${formats.map(([key, label, name]) =>
        `<button type="button" data-notes-format="${key}" title="${name}" aria-label="${name}">${escapeHtml(label)}</button>`).join("")}</div>
      <div class="NotesPaneLabels" aria-hidden="true"><span>${t("Markdown 源码", "Markdown source")}</span><span>${t("效果预览", "Live preview")}</span></div>
      <form class="NotesFormula" data-notes-formula hidden aria-label="${t("编辑公式", "Edit formula")}">
        <label>${t("公式", "Formula")}<input data-notes-formula-input maxlength="8192" spellcheck="false" autocomplete="off" aria-label="${t("公式内容", "Formula source")}" placeholder="x^2"></label>
        <button type="submit" data-notes-formula-apply>${t("确定", "Apply")}</button>
        <button type="button" data-notes-formula-cancel>${t("取消", "Cancel")}</button>
      </form>
      <div class="NotesPanes">
        <div class="NotesRich MarkdownBody" data-notes-rich aria-label="${t("正文", "Body")}"></div>
        <textarea class="NotesEditor" data-notes-editor spellcheck="false" aria-label="${t("正文", "Body")}" placeholder="${t("开始书写…支持 Markdown", "Start writing… Markdown supported")}"></textarea>
        <div class="NotesPreview MarkdownBody" data-notes-preview aria-label="${t("预览", "Preview")}"></div>
      </div>
    </div>
    <div class="NotesBlank" data-notes-blank>
      <p data-notes-blank-text></p>
      <button type="button" data-notes-action="create">${t("新建笔记", "New note")}</button>
    </div>
    <footer class="NotesStatus"><span data-notes-line>Ln 1</span><span class="NotesBar">|</span><span>Markdown + LaTeX</span>
      <span class="NotesSpacer"></span><span data-notes-notice class="NotesNotice"></span><span>UTF-8</span><span class="NotesBar">|</span><span data-notes-size>0 B</span></footer>
  </section>
</div>`;
}

const storage = {
  get(key) { try { return globalThis.localStorage?.getItem(`lume.notes.${key}`) || ""; } catch { return ""; } },
  set(key, value) { try { globalThis.localStorage?.setItem(`lume.notes.${key}`, value); } catch { /* optional */ } },
};

export function mountNotesPage(container, { send, timers = globalThis, renderMarkdown, createRichEditor, stickyId = "", startEditing = false } = {}) {
  container.innerHTML = notesPageMarkup();
  const root = container.querySelector("[data-notes-root]");
  const $ = (selector) => root.querySelector(selector);
  const titleInput = $("[data-notes-title]"), editor = $("[data-notes-editor]"), preview = $("[data-notes-preview]");
  const list = $("[data-notes-list]"), search = $("[data-notes-search]"), file = $("[data-notes-file]");
  let state = null, current = null, dirty = false, saving = null, saveTimer = 0, previewTimer = 0;
  let pendingOpen = "", creating = 0, imports = [], afterSave = [], disposed = false, retiring = false, notice = "";
  let rich = null, richBody = null, formulaKind = "";
  let conflict = false;
  const conflictPanel = container.ownerDocument.createElement("div");
  conflictPanel.className = "NotesConflict";
  conflictPanel.hidden = true;
  conflictPanel.innerHTML = `<span>${t("此笔记在另一窗口有新修改", "This note changed in another window")}</span><button type="button" data-notes-conflict="keep">${t("保存我的修改", "Save my changes")}</button><button type="button" data-notes-conflict="load">${t("载入新内容", "Load changes")}</button>`;
  $(".NotesMain").append(conflictPanel);
  function showConflict() {
    conflict = true; conflictPanel.hidden = false;
    timers.clearTimeout(saveTimer); saveTimer = 0;
    setNotice(t("未覆盖另一窗口的内容，请选择保留哪份修改", "No changes overwritten. Choose which version to keep."));
  }
  const compact = idPattern.test(stickyId);
  let editing = compact && startEditing;
  let done = null;
  if (compact) {
    root.dataset.compact = "true";
    const header = document.createElement("header");
    header.className = "NotesStickyHeader";
    header.innerHTML = `<span>${t("便签", "Note")}</span><button type="button" data-notes-done>${t("完成", "Done")}</button>`;
    $(".NotesMain").prepend(header);
    done = header.querySelector("button");
    done.addEventListener("click", finishEditing);
  }
  const mode = !compact && ["edit", "source", "split", "preview"].includes(storage.get("mode")) ? storage.get("mode") : "edit";
  root.dataset.mode = mode;
  root.dataset.sidebar = storage.get("sidebar") === "closed" ? "closed" : "open";

  const post = (action, fields = {}) => {
    if (disposed) return false;
    const ok = send?.(action, fields) === true;
    if (!ok) setNotice(t("便签服务暂不可用", "Notes service unavailable"));
    return ok;
  };
  function setNotice(value) {
    notice = value;
    $("[data-notes-notice]").textContent = value;
  }
  const summary = (id) => state?.notes.find((note) => note.id === id);
  const onDesktop = (id) => state?.desktopIds ? state.desktopIds.includes(id) : state?.pinnedId === id;
  function renderEditing() {
    if (!compact) return;
    root.dataset.editing = String(editing);
    titleInput.readOnly = !editing;
    editor.readOnly = !editing;
    rich?.setEditable(editing);
    done.hidden = !editing;
  }
  function finishEditing() {
    if (!compact || !editing) return;
    save(() => { editing = false; renderEditing(); });
  }
  const onDoubleClick = (event) => {
    if (!compact || editing || !current || event.target.closest("button,[data-notes-resize]")) return;
    editing = true; renderEditing();
    if (event.target === titleInput) titleInput.focus(); else rich?.focus();
  };

  function renderList() {
    const query = search.value.trim().toLocaleLowerCase();
    const notes = (state?.notes || []).filter((note) =>
      !query || note.title.toLocaleLowerCase().includes(query) || note.preview.toLocaleLowerCase().includes(query));
    $("[data-notes-count]").textContent = query
      ? t(`找到 ${notes.length} 篇`, `${notes.length} found`)
      : t(`${state?.notes.length || 0} 篇笔记`, `${state?.notes.length || 0} notes`);
    list.innerHTML = notes.map((note) => {
      const active = note.id === current?.id;
      const title = active ? titleInput.value : note.title;
      return `<button type="button" class="NotesItem${active ? " is-active" : ""}" role="option" aria-selected="${active}" data-notes-open="${note.id}">
        <span class="NotesItemHead"><strong>${escapeHtml(title || t("无标题笔记", "Untitled note"))}</strong><span>${day(note.updatedAt)}</span></span>
        <span class="NotesItemPreview">${escapeHtml(note.preview || t("空白笔记", "Empty note"))}</span>
        <span class="NotesItemMeta">${clock(note.updatedAt)}<i>·</i>${t(`${note.chars} 字`, `${note.chars} chars`)}${onDesktop(note.id) ? `<i>·</i>${t("桌面", "Desktop")}` : ""}</span></button>`;
    }).join("") || `<p class="NotesListEmpty">${query ? t("没有匹配的笔记", "No matching notes") : t("还没有笔记", "No notes yet")}</p>`;
  }
  function savedText() {
    if (state?.loadStatus === "failed") return t("读取失败，已停止写入", "Read failed, saving disabled");
    if (saving) return t("保存中…", "Saving…");
    if (dirty) return t("未保存", "Unsaved");
    return t("已保存", "Saved");
  }
  function renderMeta() {
    if (!current) return;
    $("[data-notes-created]").textContent = `${day(current.createdAt)} ${clock(current.createdAt)}`;
    $("[data-notes-chars]").textContent = t(`${characterCount(editor.value)} 字`, `${characterCount(editor.value)} chars`);
    const saved = $("[data-notes-saved]");
    saved.textContent = savedText();
    saved.classList.toggle("is-dirty", dirty || saving !== null);
    const size = bytes(editor.value);
    $("[data-notes-size]").textContent = size < 1024 ? `${size} B` : `${(size / 1024).toFixed(1)} KB`;
    renderLine();
  }
  function renderLine() {
    if (root.dataset.mode === "edit" && rich) {
      $("[data-notes-line]").textContent = t("可视编辑", "Visual editing");
      return;
    }
    const line = editor.value.slice(0, editor.selectionStart).split("\n").length;
    $("[data-notes-line]").textContent = `Ln ${line}`;
  }
  function renderPreview() {
    timers.clearTimeout(previewTimer);
    previewTimer = 0;
    if (["split", "preview"].includes(root.dataset.mode))
      preview.innerHTML = renderMarkdown ? renderMarkdown(editor.value) : escapeHtml(editor.value);
  }
  function renderChrome() {
    const has = !!current;
    $("[data-notes-document]").hidden = !has;
    $("[data-notes-blank]").hidden = has;
    $("[data-notes-blank-text]").textContent = state?.loadStatus === "failed"
      ? t("便签数据读取失败。为保护原文件，暂不允许写入。", "Could not read notes. Saving is disabled to protect the file.")
      : state?.notes.length ? t("选择左侧的一篇笔记", "Select a note") : t("还没有笔记，写下第一条吧", "No notes yet. Write your first one.");
    root.querySelectorAll("[data-notes-needs-note]").forEach((button) => { button.disabled = !has; });
    root.querySelectorAll("[data-notes-mode]").forEach((button) =>
      button.setAttribute("aria-pressed", String(button.dataset.notesMode === root.dataset.mode)));
    const pinned = has && onDesktop(current.id);
    const pin = $('[data-notes-action="pin"]');
    pin.setAttribute("aria-pressed", String(pinned));
    pin.title = pinned ? t("取消在桌面显示", "Remove from desktop") : t("在桌面便签中显示", "Show on the desktop note");
    pin.setAttribute("aria-label", pin.title);
    root.querySelectorAll('[data-notes-action="create"],[data-notes-action="import"]').forEach((button) => {
      button.disabled = state?.loadStatus === "failed" || (state?.notes.length || 0) >= MAX_NOTES;
    });
    renderFormat();
  }
  function renderFormat(selection = rich?.getSelection()) {
    const active = root.dataset.mode === "edit" ? selection?.active || {} : {};
    const pressed = { bold: active.bold, italic: active.italic, heading: active.heading,
      bullet: active.bulletList, number: active.orderedList, quote: active.blockquote,
      code: active.code || active.codeBlock };
    for (const [kind, value] of Object.entries(pressed))
      $(`[data-notes-format="${kind}"]`).setAttribute("aria-pressed", String(!!value));
  }
  function load(note) {
    conflict = false; conflictPanel.hidden = true;
    current = { id: note.id, createdAt: note.createdAt, updatedAt: note.updatedAt };
    titleInput.value = note.title;
    editor.value = note.body;
    closeFormula();
    if (rich) { rich.setMarkdown(note.body); richBody = note.body; }
    dirty = false;
    if (!compact) storage.set("last", note.id);
    renderChrome();
    renderMeta();
    renderPreview();
    renderList();
    renderEditing();
  }

  function schedule() {
    if (conflict) return;
    timers.clearTimeout(saveTimer);
    saveTimer = timers.setTimeout(() => { saveTimer = 0; save(); }, SAVE_DELAY);
  }
  function edited() {
    if (!current) return;
    dirty = true;
    schedule();
    renderMeta();
    if (["split", "preview"].includes(root.dataset.mode) && !previewTimer)
      previewTimer = timers.setTimeout(renderPreview, 160);
  }
  function save(then) {
    if (conflict) { showConflict(); return; }
    if (then) afterSave.push(then);
    timers.clearTimeout(saveTimer);
    saveTimer = 0;
    if (!current || saving) return;
    if (!dirty) { flush(); return; }
    const title = titleInput.value.replace(/[\r\n]+/g, " ").trim();
    const body = editor.value;
    if (bytes(body) > MAX_BODY_BYTES || bytes(title) > MAX_TITLE_BYTES) {
      setNotice(t("笔记过长，无法保存（正文上限约 1 MB）", "Note is too long to save (about 1 MB limit)"));
      afterSave = [];
      return;
    }
    const parts = splitBody(body, title, current.id);
    saving = { id: current.id, title, body, parts, index: 0, baseUpdatedAt: current.updatedAt };
    renderMeta();
    if (!sendPart()) { saving = null; afterSave = []; renderMeta(); }
  }
  function sendPart() {
    const s = saving;
    return post("save", { id: s.id, title: s.title, part: s.parts[s.index], index: s.index, count: s.parts.length, baseUpdatedAt: s.baseUpdatedAt });
  }
  function flush() {
    const callbacks = afterSave;
    afterSave = [];
    callbacks.forEach((callback) => callback());
  }
  function saved(finished) {
    if (current?.id === finished.id) {
      dirty = titleInput.value.replace(/[\r\n]+/g, " ").trim() !== finished.title || editor.value !== finished.body;
      setNotice("");
    }
    // A receipt only commits the text captured by that transaction. Closing,
    // switching notes and disposing must also wait for any newer edits.
    if (dirty) {
      if (afterSave.length) save();
      else schedule();
    } else flush();
  }

  function open(id) {
    if (id === current?.id || pendingOpen === id) return;
    const go = () => { pendingOpen = id; post("open", { id }); };
    if (dirty || saving) save(go); else go();
  }
  function create() {
    if (state?.loadStatus === "failed") return;
    if (compact) { save(() => post("createDesktop")); return; }
    const go = () => { creating++; post("create"); };
    if (dirty || saving) save(go); else go();
  }
  function nextImport() {
    if (!imports.length || creating) return;
    create();
  }

  function receive(message) {
    if (disposed) return;
    if (message.type === "notes-update") {
      if (retiring || message.id !== current?.id) return;
      if (message.title === titleInput.value && message.body === editor.value) {
        current.updatedAt = message.updatedAt;
        return;
      }
      if (dirty || saving) {
        showConflict();
        return;
      }
      const scroll = $("[data-notes-rich]").scrollTop;
      load(message);
      $("[data-notes-rich]").scrollTop = scroll;
      return;
    }
    // The window is closing: finish any pending save, then let it close.
    if (message.type === "notes-close") {
      if ((dirty || saving) && current) save(() => post("closeReady"));
      else post("closeReady");
      return;
    }
    if (message.type === "notes-chunk") {
      if (saving && saving.id === message.id && saving.index === message.index && saving.index + 1 < saving.parts.length) {
        saving.index++;
        if (!sendPart()) { saving = null; afterSave = []; renderMeta(); }
      }
      return;
    }
    if (message.type === "notes-note") {
      if (retiring) return;
      if (creating) {
        creating--;
        const item = imports.shift();
        load(message);
        if (item) {
          titleInput.value = item.title;
          editor.value = item.body;
          syncRich();
          edited();
          save(nextImport);
        } else titleInput.focus();
        return;
      }
      if (message.id === pendingOpen) {
        pendingOpen = "";
        load(message);
        if (compact && editing) titleInput.focus();
      }
      return;
    }
    if (message.type !== "notes-state") return;
    state = message;
    if (message.error === "not-found") pendingOpen = "";
    if (retiring) {
      if (saving && message.savedId === saving.id && (message.result === "saved" || !message.saved)) {
        const finished = saving;
        saving = null;
        if (message.saved) {
          if (current?.id === finished.id) current.updatedAt = summary(current.id)?.updatedAt ?? current.updatedAt;
          saved(finished);
        }
        else {
          afterSave = [];
          disposed = true;
          retiring = false;
          timers.clearTimeout(saveTimer);
        }
      }
      return;
    }
    if (saving && message.result === "saved" && message.savedId === saving.id) {
      const finished = saving;
      saving = null;
      if (message.saved) {
        if (current?.id === finished.id) current.updatedAt = summary(current.id)?.updatedAt ?? current.updatedAt;
        saved(finished);
      }
      else {
        setNotice(message.error === "limit" ? t("笔记过长，未保存", "Note too long, not saved")
          : message.error === "load" ? t("便签数据读取失败，已停止写入", "Notes unreadable; saving disabled")
          : t("保存失败，内容仍保留在编辑器中", "Save failed; your text is kept in the editor"));
        afterSave = [];
        if (message.error === "conflict") showConflict();
      }
    } else if (saving && message.result === "applied" && message.savedId === saving.id && !message.saved) {
      saving = null;
      afterSave = [];
      setNotice(t("保存失败，内容仍保留在编辑器中", "Save failed; your text is kept in the editor"));
    }
    if (message.result === "applied" && creating && !message.saved && !message.savedId) {
      creating = 0;
      imports = [];
      if (message.error === "limit") setNotice(t(`最多 ${MAX_NOTES} 篇笔记`, `At most ${MAX_NOTES} notes`));
    }
    if (current && !summary(current.id) && !saving) {
      current = null;
      dirty = false;
    }
    if (!current && !pendingOpen && !creating && state.notes.length) {
      const last = compact ? stickyId : storage.get("last");
      if (!compact || summary(stickyId)) open(summary(last) ? last : state.notes[0].id);
    }
    renderChrome();
    renderMeta();
    renderList();
  }

  function format(kind) {
    if (!current) return;
    if (root.dataset.mode === "edit" && rich) {
      if (kind === "math" || kind === "block") showFormula(kind);
      else rich.format(kind);
      return;
    }
    const change = formatMarkdown(editor.value, editor.selectionStart, editor.selectionEnd, kind, {
      bold: t("粗体", "bold"), italic: t("斜体", "italic"), code: "code", block: "\\sum_{i=1}^{n} i",
    });
    editor.focus();
    editor.setSelectionRange(change.start, change.end);
    // A native insertText transaction keeps formatting in the textarea undo stack.
    if (!editor.ownerDocument.execCommand?.("insertText", false, change.text))
      editor.setRangeText(change.text, change.start, change.end, "end");
    editor.setSelectionRange(change.selectionStart, change.selectionEnd);
    edited();
    // The tool's rendered result must be visible even when invoked from source view.
    if (root.dataset.mode === "source") setMode("split");
    else renderPreview();
  }
  function syncRich() {
    if (rich && richBody !== editor.value) {
      rich.setMarkdown(editor.value);
      richBody = editor.value;
    }
  }
  function closeFormula() {
    $("[data-notes-formula]").hidden = true;
    formulaKind = "";
  }
  function showFormula(kind, latex) {
    formulaKind = kind;
    $("[data-notes-formula]").hidden = false;
    const input = $("[data-notes-formula-input]");
    input.value = latex ?? (kind === "block" ? "\\sum_{i=1}^{n} i" : "x^2");
    input.focus();
    input.select();
  }
  const onFormulaSubmit = (event) => {
    event.preventDefault();
    const latex = $("[data-notes-formula-input]").value.trim();
    if (rich && formulaKind && latex) {
      rich.format(formulaKind, latex);
      closeFormula();
      rich.focus();
    }
  };
  function setMode(next) {
    closeFormula();
    if (next === "edit") { if (rich) syncRich(); else next = "source"; }
    root.dataset.mode = next;
    storage.set("mode", next);
    renderChrome();
    renderPreview();
    renderLine();
  }
  function confirm(show) {
    $("[data-notes-confirm]").hidden = !show;
    if (show) $('[data-notes-action="confirm-delete"]').focus();
  }
  async function importFiles(files) {
    const items = [];
    for (const item of Array.from(files || []).slice(0, 20)) {
      if (item.size > MAX_BODY_BYTES) { setNotice(t(`${item.name} 超过 1 MB，已跳过`, `${item.name} is over 1 MB; skipped`)); continue; }
      const body = await item.text();
      const heading = body.match(/^\s*#\s+(.+)$/m);
      const title = (heading ? heading[1] : item.name.replace(/\.(md|markdown|txt)$/i, "")).trim().slice(0, 200);
      items.push({ title, body });
    }
    if (disposed || !items.length) return;
    imports.push(...items);
    nextImport();
  }

  const onClick = (event) => {
    const button = event.target.closest("button");
    if (!button || button.disabled || !root.contains(button)) return;
    if (button.dataset.notesConflict && current) {
      conflict = false; conflictPanel.hidden = true; setNotice("");
      if (button.dataset.notesConflict === "keep") {
        current.updatedAt = summary(current.id)?.updatedAt ?? current.updatedAt;
        save();
      } else {
        timers.clearTimeout(saveTimer); dirty = false; saving = null; afterSave = [];
        pendingOpen = current.id; post("open", { id: current.id });
      }
      return;
    }
    if (button.hasAttribute("data-notes-formula-cancel")) { closeFormula(); rich?.focus(); return; }
    if (button.dataset.notesOpen) { open(button.dataset.notesOpen); return; }
    if (button.dataset.notesMode) { setMode(button.dataset.notesMode); return; }
    if (button.dataset.notesFormat) { format(button.dataset.notesFormat); return; }
    const action = button.dataset.notesAction;
    if (action === "create") create();
    else if (action === "import") file.click();
    else if (action === "sidebar") {
      root.dataset.sidebar = root.dataset.sidebar === "closed" ? "open" : "closed";
      storage.set("sidebar", root.dataset.sidebar);
    } else if (action === "save") { if (!dirty) dirty = true; save(); }
    else if (action === "undo" || action === "redo") {
      if (root.dataset.mode === "edit" && rich) { rich[action](); return; }
      editor.focus();
      const before = editor.value;
      editor.ownerDocument.execCommand?.(action);
      if (editor.value !== before) edited();
    } else if (action === "pin" && current) save(() => post("pin", { id: current.id, value: !onDesktop(current.id) }));
    else if (action === "delete") confirm($("[data-notes-confirm]").hidden);
    else if (action === "cancel-delete") confirm(false);
    else if (action === "confirm-delete" && current) {
      confirm(false);
      const id = current.id;
      timers.clearTimeout(saveTimer);
      saving = null; dirty = false; afterSave = []; current = null;
      post("delete", { id });
      renderChrome();
    }
  };
  const onKey = (event) => {
    if (compact && editing && !event.isComposing && (event.key === "Escape" || ((event.ctrlKey || event.metaKey) && event.key === "Enter"))) {
      event.preventDefault(); event.stopPropagation(); finishEditing(); return;
    }
    if (event.key === "Escape" && !$("[data-notes-formula]").hidden) { closeFormula(); rich?.focus(); event.preventDefault(); return; }
    if (event.key === "Escape" && !$("[data-notes-confirm]").hidden) { confirm(false); event.preventDefault(); return; }
    if (!(event.ctrlKey || event.metaKey) || event.altKey) return;
    const key = event.key.toLowerCase();
    if (key === "s") { event.preventDefault(); if (current) { dirty = true; save(); } }
    else if (key === "n" && !event.shiftKey) { event.preventDefault(); create(); }
    else if (event.target === editor && (key === "b" || key === "i")) { event.preventDefault(); format(key === "b" ? "bold" : "italic"); }
  };
  const onTitleKey = (event) => { if (event.key === "Enter") { event.preventDefault(); root.dataset.mode === "edit" && rich ? rich.focus() : editor.focus(); } };
  const onFormatPointerDown = (event) => {
    if (event.button === 0 && event.target.closest('[data-notes-format],[data-notes-action="undo"],[data-notes-action="redo"]')) event.preventDefault();
  };
  root.addEventListener("click", onClick);
  root.addEventListener("pointerdown", onFormatPointerDown);
  root.addEventListener("keydown", onKey, compact);
  root.addEventListener("dblclick", onDoubleClick);
  titleInput.addEventListener("input", () => { edited(); renderList(); });
  titleInput.addEventListener("keydown", onTitleKey);
  editor.addEventListener("input", edited);
  editor.addEventListener("keyup", renderLine);
  editor.addEventListener("click", renderLine);
  editor.addEventListener("blur", () => { if (dirty) save(); });
  search.addEventListener("input", renderList);
  file.addEventListener("change", () => { importFiles(file.files); file.value = ""; });
  $("[data-notes-formula]").addEventListener("submit", onFormulaSubmit);

  if (createRichEditor) {
    rich = createRichEditor($("[data-notes-rich]"), {
      placeholder: t("开始书写…", "Start writing…"),
      onChange(value) {
        if (!current || disposed || retiring) return;
        editor.value = value;
        richBody = value;
        edited();
      },
      onFormula: (kind, latex) => { if (!compact || editing) showFormula(kind, latex); },
      onSelection: renderFormat,
    });
  } else if (root.dataset.mode === "edit") root.dataset.mode = "source";

  renderChrome();
  renderList();
  renderEditing();
  post("snapshot");
  return {
    root,
    receive,
    // Leaving the page still finishes a pending save: keep forwarding messages
    // while retiring is true. The host keeps the last committed text on failure.
    dispose() {
      if (disposed || retiring) return;
      retiring = true;
      timers.clearTimeout(previewTimer);
      root.removeEventListener("click", onClick);
      root.removeEventListener("pointerdown", onFormatPointerDown);
      root.removeEventListener("keydown", onKey, compact);
      root.removeEventListener("dblclick", onDoubleClick);
      $("[data-notes-formula]").removeEventListener("submit", onFormulaSubmit);
      rich?.destroy();
      const finish = () => { disposed = true; retiring = false; timers.clearTimeout(saveTimer); };
      if ((dirty || saving) && current) save(finish); else finish();
    },
    get retiring() { return retiring; },
    get notice() { return notice; },
  };
}
