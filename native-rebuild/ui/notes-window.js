// Entry of the standalone notes window (notes.html). The native window owns
// persistence; page editing and component-window controls have separate messages.
import { mountNotesPage, validNotesCommand, validNotesMessage } from "./notes.js";
import { renderMarkdown } from "./notes-markdown.js";
import { createNotesRichEditor } from "./notes-rich-editor.js";
import { t } from "./localization.js";

const webview = globalThis.chrome?.webview;
const send = (action, fields = {}) => {
  if (!validNotesCommand(action, fields) || typeof webview?.postMessage !== "function") return false;
  try {
    webview.postMessage({ version: 1, scope: "notes", action, ...fields });
    return true;
  } catch {
    return false;
  }
};
// A separate host-only status gates transparent CSS on the actual native
// material. Notes persistence messages remain on the validated notes protocol.
document.documentElement.dataset.notesMaterial = "fallback";
const parameters = new URLSearchParams(location.hash.slice(1));
const stickyId = /^[a-z0-9-]{1,64}$/.test(parameters.get("sticky") || "") ? parameters.get("sticky") : "";
const page = mountNotesPage(document.querySelector("#notes"), { send, renderMarkdown, createRichEditor: createNotesRichEditor,
  stickyId, startEditing: parameters.get("edit") === "1" });
const root = document.querySelector("[data-notes-root]");
if (!stickyId) {
  const close = document.createElement("button");
  close.type = "button";
  close.className = "NotesWindowClose";
  close.dataset.notesWindowClose = "";
  close.title = close.ariaLabel = t("关闭便签", "Close notes");
  close.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m7 7 10 10M17 7 7 17"/></svg>';
  root.append(close);
  close.addEventListener("click", () => windowAction("close"));
}
const resizeEdges = ["n", "ne", "e", "se", "s", "sw", "w", "nw"];
for (const edge of resizeEdges) {
  const handle = document.createElement("div");
  handle.className = "NotesResize";
  handle.dataset.notesResize = edge;
  handle.ariaHidden = "true";
  root.append(handle);
}
const windowAction = (action, edge) => {
  if (typeof webview?.postMessage !== "function") return false;
  const fields = action === "resize" && resizeEdges.includes(edge) ? { edge } : {};
  if (!["drag", "close", "menu"].includes(action) && !Object.hasOwn(fields, "edge")) return false;
  try {
    webview.postMessage({ version: 1, type: "notes-window", action, ...fields });
    return true;
  } catch { return false; }
};
document.addEventListener("contextmenu", (event) => {
  event.preventDefault();
  if (stickyId) windowAction("menu");
});
root.addEventListener("pointerdown", (event) => {
  if (event.button !== 0 || !event.isPrimary) return;
  const edge = event.target.closest("[data-notes-resize]")?.dataset.notesResize;
  const toolbarBlank = event.target.matches(".NotesToolbar, .NotesToolbar > .NotesSpacer, .NotesToolbar > .NotesDivider, .NotesStickyHeader, .NotesStickyHeader > span");
  if ((edge && windowAction("resize", edge)) || (toolbarBlank && windowAction("drag"))) event.preventDefault();
});
webview?.addEventListener("message", (event) => {
  const message = event.data;
  if (message?.version === 1 && message.type === "notes-material" &&
      Object.keys(message).length === 3 && ["ready", "starting", "fallback"].includes(message.status)) {
    document.documentElement.dataset.notesMaterial = message.status === "ready" ? "ready" : "fallback";
    return;
  }
  if (message?.version === 1 && validNotesMessage(message)) page.receive(message);
});
