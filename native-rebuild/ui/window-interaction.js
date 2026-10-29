const blankSurfaceSelector = [
  "#content",
  ".HomeRoot",
  ".ExperienceRoot",
  ".HomeHero",
  ".HomeHero > .preview",
  ".HomeHero > .preview > .preview-image",
  ".ModuleToolbar",
  ".ModuleToolbar > .toolbar-side",
  ".MarketShell",
  ".DesktopBeautyLayout",
  ".DesktopBeautyLayout > .list-back",
  ".WallpaperLibraryLayout",
  ".WallpaperLibraryLayout > .wallpaper-library-footer",
  ".SearchResultsLayout",
  ".SearchResultsHead",
].join(", ");

const excludedSelector = [
  "button",
  "a",
  "input",
  "textarea",
  "select",
  "option",
  "label",
  "form",
  "details",
  "summary",
  "video",
  "audio",
  "iframe",
  "[contenteditable]:not([contenteditable='false' i])",
  "[tabindex]",
  "[draggable='true' i]",
  "[data-no-drag]",
  "[data-resize-edge]",
  ...[
    "button", "link", "textbox", "checkbox", "switch", "radio", "slider",
    "spinbutton", "combobox", "listbox", "option", "tab", "tablist", "menu",
    "menubar", "menuitem", "menuitemcheckbox", "menuitemradio", "scrollbar",
    "tree", "treeitem", "grid", "gridcell", "dialog",
  ].map((role) => `[role~='${role}']`),
  ".lume-titlebar__controls",
  ".HomeModuleTabs",
  ".ModuleSearch",
  ".HomeDots",
  ".HomeHoverOverlay",
  ".wallpaper-grid",
  ".wallpaper-card",
  ".CardGrid",
  ".Card",
  ".settings",
  ".wallpaper-detail-media",
  ".wallpaper-detail-bar",
  ".wallpaper-info-sidebar",
  ".wallpaper-info-meta",
  ".wallpaper-detail-target-menu",
  ".wallpaper-select",
  ".wallpaper-select-menu",
  ".wallpaper-pagination",
].join(", ");

export function isWindowDragTarget(target) {
  const view = target?.ownerDocument?.defaultView;
  if (typeof view?.Element !== "function" || !(target instanceof view.Element)) return false;
  // Only the surface itself is blank; an arbitrary descendant is not.
  if (!target.matches(blankSurfaceSelector) || target.closest(excludedSelector)) return false;

  // Native scrollbar events target their scroll container. Without pointer
  // coordinates, reserve the whole scrolling subtree for its own interaction.
  for (let element = target; element; element = element.parentElement) {
    const style = view.getComputedStyle(element);
    if (/\b(auto|scroll|overlay)\b/.test(`${style.overflow} ${style.overflowX} ${style.overflowY}`)) {
      return false;
    }
  }
  return true;
}
