import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { isWindowDragTarget } from "../window-interaction.js";
import { home, catalog, library, search, wallpaperDetail } from "../pages.js";
import { initialState } from "../state.js";
import { settingsFrame } from "../settings-frame.js";

// Like dom-integration.mjs, reuse an external development dependency, never a
// shipped one. Set LUME_JSDOM_PATH to the absolute path to jsdom/lib/api.js.
const jsdomPath = process.env.LUME_JSDOM_PATH;
const JSDOM = jsdomPath ? createRequire(import.meta.url)(jsdomPath).JSDOM : null;
const indexHtml = readFileSync(new URL("../index.html", import.meta.url), "utf8");
const domTest = (name, run) => test(name, {
  skip: !JSDOM && "Set LUME_JSDOM_PATH to run the real DOM hit tests",
}, run);

function mount(t, markup) {
  const dom = new JSDOM(indexHtml, { url: "https://lume.local/" });
  const previousElement = Object.getOwnPropertyDescriptor(globalThis, "Element");
  globalThis.Element = dom.window.Element;
  t.after(() => {
    if (previousElement) Object.defineProperty(globalThis, "Element", previousElement);
    else delete globalThis.Element;
    dom.window.close();
  });
  const root = dom.window.document.querySelector("#content");
  root.innerHTML = markup;
  return { window: dom.window, document: dom.window.document, root };
}

function expectTargets(root, selectors, expected) {
  for (const selector of selectors) {
    const targets = root.querySelectorAll(selector);
    assert.ok(targets.length, `Fixture must contain ${selector}`);
    for (const target of targets) {
      assert.equal(isWindowDragTarget(target), expected, target.outerHTML);
    }
  }
}

function wallpaperState(view = "home") {
  const state = initialState();
  const item = {
    id: "official-1", title: "Wallpaper", kind: "video",
    thumbnail: "https://static.lumedesktop.com/cover.jpg",
    url: "https://static.lumedesktop.com/wallpaper.mp4",
    width: 1920, height: 1080, bytes: 1024, libraryId: "",
  };
  state.view = view;
  state.query = view === "search" ? "Wallpaper" : "";
  state.wallpaperStatus = "ready";
  state.wallpaper.catalogStatus = "ready";
  state.wallpaper.catalog = [item, { ...item, id: "official-2" }];
  state.wallpaper.library = [{ ...item, id: "local-1", missing: false }];
  state.wallpaperSelection = { source: "official", id: item.id };
  state.wallpaper.displays = [{
    id: "1", name: "Display", width: 1920, height: 1080, primary: true,
    wallpaperId: "", state: "stopped", fit: "cover", volume: 0,
  }];
  return state;
}

test("non-DOM values cannot start a window drag", () => {
  for (const target of [
    null, undefined, 0, "HomeRoot", {}, { closest: () => null },
    { ownerDocument: { defaultView: {} } },
    { ownerDocument: { defaultView: { Element: {} } } },
  ]) {
    assert.equal(isWindowDragTarget(target), false);
  }
});

domTest("home background and toolbar gaps are explicit drag surfaces", (t) => {
  const { root } = mount(t, home(wallpaperState()));
  assert.equal(isWindowDragTarget(root), true);
  expectTargets(root, [
    ".HomeRoot", ".HomeHero", ".HomeHero > .preview", ".preview-image",
    ".ModuleToolbar", ".toolbar-side.left", ".toolbar-side.right",
  ], true);
  expectTargets(root, [
    ".HomeModuleTabs", ".HomeModuleHighlight", ".module-tab",
    ".ModuleSearch", ".ModuleSearch svg", ".ModuleSearch path", "input",
    ".home-arrow", ".home-arrow path", ".HomeDots", ".dot",
    ".HomeHoverOverlay", ".HoverTitle", ".CardActions", ".card-action",
  ], false);
});

for (const widgets of [false, true]) {
  domTest(`${widgets ? "widget" : "module"} catalog margins drag but cards and their scrolling grid do not`, (t) => {
    const { root } = mount(t, catalog(initialState(), widgets));
    expectTargets(root, [
      ".ExperienceRoot", ".MarketShell", ".DesktopBeautyLayout",
      ...(widgets ? [".list-back"] : [".ModuleToolbar", ".toolbar-side"]),
    ], true);
    expectTargets(root, [
      ".CardGrid", ".Card", ".OpenButton", ".Title", ".Description", ".Toggle",
    ], false);
  });
}

for (const view of ["wallpaperLocal", "wallpaperOfficial"]) {
  domTest(`${view} margins drag but filters, cards, scrollbars and pagination do not`, (t) => {
    const { root } = mount(t, library(wallpaperState(view)));
    expectTargets(root, [
      ".ExperienceRoot", ".MarketShell", ".WallpaperLibraryLayout",
      ".ModuleToolbar", ".toolbar-side", ".wallpaper-library-footer",
    ], true);
    expectTargets(root, [
      ".wallpaper-select", ".wallpaper-select-trigger span", ".wallpaper-select-menu",
      ".wallpaper-grid", ".wallpaper-card", ".wallpaper-card-placeholder",
      ".CardTitle", ".wallpaper-pagination", ".wallpaper-page-list",
      ".wallpaper-page-summary", "#wallpaper-page-jump",
    ], false);
  });
}

domTest("search margins and header gaps drag without claiming results or the search field", (t) => {
  const { root } = mount(t, search(wallpaperState("search")));
  expectTargets(root, [
    ".ExperienceRoot", ".MarketShell", ".SearchResultsLayout", ".SearchResultsHead",
  ], true);
  expectTargets(root, [
    ".wallpaper-grid", ".wallpaper-card", ".ModuleSearch", ".ModuleSearch svg",
    ".SearchInput", ".wallpaper-pagination", ".availability",
  ], false);
});

domTest("wallpaper detail media, action bar, target menu and sidebar remain non-draggable", (t) => {
  const state = wallpaperState("wallpaperDetail");
  state.wallpaperPanel = "targets";
  const { root } = mount(t, wallpaperDetail(state));
  expectTargets(root, [".HomeRoot"], true);
  expectTargets(root, [
    ".wallpaper-detail-media", ".wallpaper-detail-poster", ".wallpaper-preview-video",
    ".wallpaper-detail-shade", ".wallpaper-detail-bar", ".wallpaper-detail-info",
    ".wallpaper-detail-title", ".wallpaper-detail-meta", ".wallpaper-detail-actions",
    ".wallpaper-detail-target-menu", ".wallpaper-info-sidebar", ".wallpaper-info-meta",
    ".wallpaper-detail-notice",
  ], false);
});

domTest("settings pages do not become draggable through their scrollable padding or card", (t) => {
  const { root } = mount(t, settingsFrame({
    title: "Settings", description: "Description", tab: "settings",
    body: '<div class="HomeRoot">Editable setting</div>',
  }));
  expectTargets(root, [
    ".settings", ".settings-card", ".settings-head", ".settings-tabs",
    ".settings-body", ".HomeRoot",
  ], false);
});

domTest("an allowed ancestor never implicitly permits unknown content or text", (t) => {
  const { root, document } = mount(t, `<div class="HomeRoot">
    <div id="unknown">Content</div><h2>Title</h2><span>Text</span>
    <svg><path></path></svg><img alt="Content"><video></video>
  </div>`);
  expectTargets(root, ["#unknown", "h2", "span", "svg", "path", "img", "video"], false);
  assert.equal(isWindowDragTarget(document.body), false);
  assert.equal(isWindowDragTarget(document.documentElement), false);
  assert.equal(isWindowDragTarget(document), false);
  assert.equal(isWindowDragTarget(root.querySelector("#unknown").firstChild), false);
});

domTest("preview classes outside the home background cannot opt content into dragging", (t) => {
  const { root } = mount(t, '<div class="preview"><img class="preview-image"></div>');
  expectTargets(root, [".preview", ".preview-image"], false);
});

const blockedRegions = [
  "HomeModuleTabs", "ModuleSearch", "HomeDots", "HomeHoverOverlay",
  "Card", "wallpaper-card", "CardGrid", "wallpaper-grid", "settings",
  "wallpaper-detail-media", "wallpaper-detail-bar", "wallpaper-info-sidebar",
  "wallpaper-info-meta", "wallpaper-select", "wallpaper-select-menu",
  "wallpaper-pagination", "wallpaper-detail-target-menu", "lume-titlebar__controls",
];
domTest("interaction regions override allowed classes on both the target and its ancestors", (t) => {
  const { root } = mount(t, blockedRegions.map(name => `
    <div class="${name} HomeRoot"><div class="MarketShell"></div></div>
  `).join(""));
  expectTargets(root, [".HomeRoot", ".MarketShell"], false);
});

const nativeControls = [
  '<button class="HomeRoot" disabled><span class="MarketShell"></span></button>',
  '<a class="HomeRoot" href="#"><span class="MarketShell"></span></a>',
  '<input class="HomeRoot" readonly>',
  '<textarea class="HomeRoot"></textarea>',
  '<select class="HomeRoot"><option>Choice</option></select>',
  '<label class="HomeRoot"><span class="MarketShell"></span></label>',
  '<form class="HomeRoot"><div class="MarketShell"></div></form>',
  '<details class="HomeRoot"><summary class="MarketShell">More</summary></details>',
  '<video class="HomeRoot" controls></video>',
  '<audio class="HomeRoot" controls></audio>',
  '<iframe class="HomeRoot"></iframe>',
];
domTest("native controls and disabled or read-only controls always win over blank-surface classes", (t) => {
  const { root } = mount(t, nativeControls.join(""));
  expectTargets(root, [".HomeRoot", ".MarketShell"], false);
});

const interactiveRoles = [
  "button", "link", "textbox", "checkbox", "switch", "radio", "slider", "spinbutton",
  "combobox", "listbox", "option", "tab", "tablist", "menu", "menubar", "menuitem",
  "menuitemcheckbox", "menuitemradio", "scrollbar", "tree", "treeitem", "grid", "gridcell",
  "dialog",
];
domTest("ARIA widgets including custom scrollbars never start a window drag", (t) => {
  const { root } = mount(t, interactiveRoles.map(role => `
    <div class="HomeRoot" role="${role}"><span class="MarketShell"></span></div>
  `).join(""));
  expectTargets(root, [".HomeRoot", ".MarketShell"], false);
});

domTest("focusable, draggable, resize and explicit no-drag regions cannot opt into window dragging", (t) => {
  const attributes = [
    'tabindex="0"', 'tabindex="-1"', 'draggable="true"',
    "data-no-drag", 'data-no-drag="false"', 'data-resize-edge="right"',
  ];
  const { root } = mount(t, attributes.map(attribute => `
    <div class="HomeRoot" ${attribute}><span class="MarketShell"></span></div>
  `).join(""));
  expectTargets(root, [".HomeRoot", ".MarketShell"], false);
});

for (const value of ["", "true", "TRUE", "plaintext-only"]) {
  domTest(`contenteditable=${JSON.stringify(value)} protects itself and nested editable or noneditable children`, (t) => {
    const { root } = mount(t, `<div class="HomeRoot" contenteditable="${value}">
      <div class="MarketShell"></div>
      <div class="ExperienceRoot" contenteditable="false"></div>
    </div>`);
    expectTargets(root, [".HomeRoot", ".MarketShell", ".ExperienceRoot"], false);
  });
}

domTest("explicit noneditable and nondraggable attributes do not disable a genuine blank surface", (t) => {
  const { root } = mount(t, '<div class="HomeRoot" contenteditable="false" draggable="false"></div>');
  expectTargets(root, [".HomeRoot"], true);
});

for (const overflow of ["overflow: auto", "overflow: scroll", "overflow-y: auto", "overflow-x: scroll"]) {
  domTest(`${overflow} reserves both the native scrollbar target and nested blank surfaces for scrolling`, (t) => {
    const { root } = mount(t, `<div class="HomeRoot" style="${overflow}">
      <div class="MarketShell"></div>
    </div>`);
    expectTargets(root, [".HomeRoot", ".MarketShell"], false);
  });
}

domTest("stylesheet-defined scroll containers also protect descendant blank surfaces", (t) => {
  const { root } = mount(t, `<style>.scroller { overflow-y: scroll; }</style>
    <div class="scroller"><div class="HomeRoot"></div></div>`);
  expectTargets(root, [".HomeRoot"], false);
});

domTest("non-scrollable clipping used by the real page layouts does not disable dragging", (t) => {
  const { root } = mount(t, `<div class="HomeRoot" style="overflow: hidden">
    <div class="MarketShell" style="overflow: clip"></div>
  </div>`);
  expectTargets(root, [".HomeRoot", ".MarketShell"], true);
});

domTest("DOM hit testing uses the target document without requiring a global Element constructor", (t) => {
  const { root } = mount(t, '<div class="HomeRoot"></div>');
  delete globalThis.Element;
  expectTargets(root, [".HomeRoot"], true);
});
