import { Editor, EditorState, NodeSelection, StarterKit, Markdown, TaskList, TaskItem, TableKit,
  InlineMath, BlockMath, Marked, Tokenizer, Node } from "./vendor/notes-editor/editor.mjs";
import katex from "./vendor/notes-math/katex.mjs";

// The image syntax remains visible and round-trips, without an img node or a
// network request. Raw HTML is tokenized as ordinary text below.
const InertImage = Node.create({
  name: "inertImage", group: "inline", inline: true, atom: true,
  addAttributes: () => ({ alt: { default: "" }, src: { default: "" }, title: { default: null } }),
  parseHTML: () => [{ tag: "span[data-notes-image]" }],
  renderHTML: ({ node }) => ["span", { "data-notes-image": "", class: "MdImage" }, node.attrs.alt || "图片"],
  markdownTokenName: "image",
  parseMarkdown: (token) => ({ type: "inertImage", attrs: { alt: token.text || "", src: token.href || "", title: token.title || null } }),
  renderMarkdown: (node) => `![${node.attrs.alt}](${node.attrs.src}${node.attrs.title ? ` "${node.attrs.title}"` : ""})`,
});

function mathExtension(base, kind, options, select) {
  return base.extend({
    addNodeView() {
      return ({ node, getPos }) => {
        const dom = document.createElement(kind === "block" ? "div" : "span");
        dom.className = `tiptap-mathematics-render MdMath${kind === "block" ? " MdMathBlock" : ""}`;
        dom.dataset.type = kind === "block" ? "block-math" : "inline-math";
        const render = () => {
          dom.dataset.latex = node.attrs.latex;
          try {
            if (!validFormula(node.attrs.latex)) throw new Error("Formula exceeds limit");
            katex.render(node.attrs.latex, dom, { displayMode: kind === "block", throwOnError: true,
              trust: false, strict: "ignore", maxExpand: 1000, maxSize: 10, macros: {} });
            dom.classList.remove("MdMathError");
          } catch { dom.textContent = node.attrs.latex; dom.classList.add("MdMathError"); }
        };
        const click = (event) => {
          event.preventDefault(); event.stopPropagation();
          const pos = getPos();
          if (pos !== undefined) { select(pos); options.onFormula?.(kind, node.attrs.latex); }
        };
        dom.addEventListener("click", click);
        render();
        return { dom, update(next) {
          if (next.type !== node.type) return false;
          node = next; render(); return true;
        }, destroy: () => dom.removeEventListener("click", click) };
      };
    },
  });
}
const validFormula = (value) => typeof value === "string" && value.length > 0 && value.length <= 8192;

export function createNotesRichEditor(element, options = {}) {
  let suppress = false;
  // Reuse Marked's own tag recognition, converting only recognized HTML to text.
  // This preserves ordinary angle brackets and keeps raw HTML in its paragraph.
  const marked = new Marked({ gfm: true, breaks: true });
  marked.use({ tokenizer: {
    html(src) {
      const token = Tokenizer.prototype.html.call(this, src);
      return token ? { type: "paragraph", raw: token.raw, text: token.raw, tokens: [{ type: "text", raw: token.raw, text: token.raw }] } : undefined;
    },
    tag(src) {
      const token = Tokenizer.prototype.tag.call(this, src);
      return token ? { type: "text", raw: token.raw, text: token.raw } : undefined;
    },
  } });
  const selection = () => {
    const { from, to, empty, node } = editor.state.selection;
    return { from, to, empty, nodeType: node?.type.name || "", latex: node?.attrs.latex || "",
      text: editor.state.doc.textBetween(from, to, "\n", (node) => node.type.name === "hardBreak" ? "\n" : node.attrs.latex || ""),
      active: Object.fromEntries(["bold", "italic", "heading", "bulletList", "orderedList", "code", "codeBlock", "blockquote"].map((name) => [name, editor.isActive(name)])),
      canUndo: editor.can().undo(), canRedo: editor.can().redo() };
  };
  const notifySelection = () => { if (!suppress) options.onSelection?.(selection()); };
  const selectFormula = (pos) => editor.commands.setNodeSelection(pos);
  const editor = new Editor({
    element, injectCSS: false, content: "", contentType: "markdown",
    extensions: [StarterKit.configure({ link: { openOnClick: false, autolink: false, linkOnPaste: false }, trailingNode: false }),
      Markdown.configure({ marked, markedOptions: { gfm: true, breaks: true } }),
      TaskList, TaskItem.configure({ nested: true }), TableKit.configure({ table: { resizable: false } }), InertImage,
      mathExtension(InlineMath, "math", options, selectFormula), mathExtension(BlockMath, "block", options, selectFormula)],
    editorProps: {
      attributes: { class: "tiptap NotesRichContent", role: "textbox", "aria-multiline": "true", "aria-label": options.label || "便签正文",
        "data-placeholder": options.placeholder || "写下你的想法…", spellcheck: "false" },
      handleDOMEvents: { click: (_view, event) => {
        if (event.target.closest?.("a")) { event.preventDefault(); return true; }
        return false;
      } },
      handlePaste: (view, event) => {
        const text = event.clipboardData?.getData("text/plain");
        event.preventDefault();
        if (typeof text !== "string") return true;
        const lines = text.replace(/\r\n?/g, "\n").split("\n");
        if (view.state.selection.$from.parent.type.name === "codeBlock") editor.commands.insertContent({ type: "text", text });
        else editor.commands.insertContent(lines.flatMap((line, index) => [
          ...(index ? [{ type: "hardBreak" }] : []), ...(line ? [{ type: "text", text: line }] : []),
        ]));
        return true;
      },
      handleDrop: (_view, event) => { event.preventDefault(); return true; },
    },
    onUpdate: () => { if (!suppress) options.onChange?.(editor.getMarkdown()); },
    // Stored marks (B/I toggled at an empty caret) change neither the document
    // nor selection, but still need to update the toolbar's active state.
    onTransaction: notifySelection,
  });
  return {
    setEditable: (value) => editor.setEditable(!!value, false),
    setMarkdown(value) {
      suppress = true;
      try {
        editor.commands.setContent(String(value), { contentType: "markdown", emitUpdate: false });
        // Reinitializing plugin state clears history between notes and mode loads.
        editor.view.updateState(EditorState.create({ schema: editor.schema, doc: editor.state.doc, plugins: editor.state.plugins }));
      } finally { suppress = false; }
      notifySelection();
    },
    getMarkdown: () => editor.getMarkdown(),
    getSelection: selection,
    setSelection({ from, to = from, nodeType = "" }) {
      const max = editor.state.doc.content.size;
      from = Math.max(0, Math.min(max, from)); to = Math.max(from, Math.min(max, to));
      if (nodeType && editor.state.doc.nodeAt(from)) return editor.commands.setNodeSelection(from);
      return editor.commands.setTextSelection({ from, to });
    },
    format(kind, latex) {
      const chain = editor.chain().focus();
      if (kind === "bold") return chain.toggleBold().run();
      if (kind === "italic") return chain.toggleItalic().run();
      if (kind === "heading") return chain.toggleHeading({ level: 2 }).run();
      if (kind === "bullet") return chain.toggleBulletList().run();
      if (kind === "number") return chain.toggleOrderedList().run();
      if (kind === "quote") return chain.toggleBlockquote().run();
      if (kind === "code") return selection().text.includes("\n") || editor.isActive("codeBlock") ? chain.toggleCodeBlock().run() : chain.toggleCode().run();
      if (kind === "rule") {
        const at = editor.state.selection.$to.end();
        return chain.setTextSelection(at).setHorizontalRule().run();
      }
      if (kind === "math" || kind === "block") {
        const type = kind === "math" ? "inlineMath" : "blockMath";
        const value = String(latex ?? (selection().text || (kind === "math" ? "x^2" : "\\sum_{i=1}^{n} i"))).trim();
        if (!validFormula(value)) return false;
        if (editor.state.selection instanceof NodeSelection && editor.state.selection.node.type.name === type)
          return chain.updateAttributes(type, { latex: value }).run();
        return chain.insertContent({ type, attrs: { latex: value } }).run();
      }
      return false;
    },
    undo: () => editor.chain().focus().undo().run(),
    redo: () => editor.chain().focus().redo().run(),
    focus: () => editor.commands.focus(),
    destroy: () => editor.destroy(),
  };
}
