import test, { afterEach } from "node:test";
import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { createHash } from "node:crypto";
import { createRequire } from "node:module";
import { setLanguage, getLanguage } from "../localization.js";
import { initialState, searchCatalog } from "../state.js";
import * as catalogData from "../catalog.js";
import * as pages from "../pages.js";
import { back } from "../dom.js";
import { clockPage, syncClockControls } from "../clock.js";
import { calendarPage, calendarStatus, calendarNotice, syncCalendarControls } from "../calendar.js";
import { performancePage, performanceStatus, performanceNotice, syncPerformanceControls } from "../performance.js";
import { almanacPage, almanacStatus, almanacNotice, syncAlmanacControls } from "../almanac.js";
import { weatherPage, weatherStatus, weatherNotice, weatherCities, syncWeatherControls } from "../weather.js";
import { organizerPage, organizerStatus, organizerNotice, syncOrganizerControls } from "../organizer.js";
import { desktopIconsPage, desktopIconsStatus, desktopIconsNotice, syncDesktopIconsControls } from "../desktop-icons.js";
import { taskbarPage, taskbarStatus, syncTaskbarControls } from "../taskbar.js";
import { minimizePage, minimizeStatus, syncMinimizeControls } from "../minimize.js";
import { wallpaperError, wallpaperNotice, createWallpaperPreviewController } from "../wallpapers.js";
import { validHostMessage } from "../bridge.js";

afterEach(() => setLanguage("zh-CN"));

const componentPages = {
  clock: clockPage, calendar: calendarPage, performance: performancePage,
  almanac: almanacPage, weather: weatherPage, organizer: organizerPage,
  icons: desktopIconsPage, taskbar: taskbarPage, minimize: minimizePage,
};
const componentSync = {
  clock: syncClockControls, calendar: syncCalendarControls, performance: syncPerformanceControls,
  almanac: syncAlmanacControls, weather: syncWeatherControls, organizer: syncOrganizerControls,
  icons: syncDesktopIconsControls, taskbar: syncTaskbarControls, minimize: syncMinimizeControls,
};
const componentTitles = {
  clock: ["时间", "Clock"], calendar: ["日历", "Calendar"], performance: ["性能", "Performance"],
  almanac: ["黄历", "Almanac"], weather: ["天气", "Weather"], organizer: ["图标收纳", "Icon organizer"],
  icons: ["桌面图标切换与自动隐藏", "Desktop icons"], taskbar: ["任务栏透明化", "Taskbar transparency"],
  minimize: ["macOS 最小化动画", "macOS minimize animation"],
};
// Optional DOM coverage uses the same existing development dependency as other UI tests.
const jsdomPath = process.env.LUME_JSDOM_PATH || process.env.LUME_JSDOM;

function renderCases(state = initialState()) {
  const cases = {
    home: () => pages.home(state),
    toolbar: () => pages.toolbar("wallpaperLocal", "天气 & <图标>"),
    catalog: () => pages.catalog(state, false),
    widgets: () => pages.catalog(state, true),
    library: () => pages.library({ ...state, view: "wallpaperLocal" }),
    search: () => pages.search({ ...state, query: "天气" }),
    detail: () => pages.wallpaperDetail(state),
  };
  for (const [key, page] of Object.entries(componentPages)) {
    for (const tab of ["settings", "details"]) cases[`${key}-${tab}`] = () => page(state, tab);
  }
  return cases;
}

// 中文渲染基线：内页移除重复主开关，音乐卡片接通开关，图标卡片文案与默认值同步当前要求。
const chineseBaseline = {
  "home": "b8f20b67d2e3f24038f28f3ad4838a508b6e6f2f32ee91eb7bb5eb9de65a680c",
  "toolbar": "3c07a88f94e923086c3a240a8100f0cdf976a4a1977fde470eb2dce64db5a39e",
  "catalog": "5dd152880ff45690f2ff9fa9aa5e724bfe644fc9b61a8334ed03d6523c142ddd",
  "widgets": "a5e57d55f69097bdd643737430e44725169adcf615da3dc656fae22396336b09",
  "library": "74785ec620bbd6fe42547319b96a3b4fa2eb88ac97bd2d58bd533cbd2fb97bb1",
  "search": "49860e2b7c44f67c0576eea72d2163402f2794e095825ac381903d2e3ba0d03b",
  "detail": "e7aa3f2e4af81c04d10a86c0f6465160b9ca2aca8ff37ed277f71aa4da91878b",
  "clock-settings": "84f1996828f16216abd76776a6e12fbeca9fa3f00fdebd6e9ea1f5dcad2cd94c",
  "clock-details": "0090a83f21f7505c42d6058ddc25017439fb2043ca2be008473bd17089db7636",
  "calendar-settings": "50f30feef911a53b9adcd309864486362c5a3366671bb0e58b93e2e20e6e6152",
  "calendar-details": "80cd2321f95cc62410183daa43904602271596a8160a21eb487b59f16b97ba5a",
  "performance-settings": "d3f62508d4f7493a74c08374e5719fd6a08d2041e8185c69b1a5e5589dcf75ba",
  "performance-details": "ebfb7bb55efa3b41ca37a35c4e77f1ba47bca7afc5ec2fda3ba16f6158118fbe",
  "almanac-settings": "735ac9e1cba49be00124ebee11b937ceb252da4b40ff8cb6fa8a577416d9f725",
  "almanac-details": "cd749b5bc7193b559b29558bfd192f69e0ab328206124ec0c6c4f127592f40b9",
  "weather-settings": "641f2e48f188c86cf5e67fb0f3395e99c64ae7aa6e9b11b9b30c18b36f698806",
  "weather-details": "9e1d175b4b577b588a132ea82bd86e3eb6cd707ac8d09cd4ba38be5a719e591f",
  "organizer-settings": "5c3054a0707ba0cebe044d942e7cd472aeee85bdf073449ccc1e5f1e2006664f",
  "organizer-details": "a25c8605714b68e43601d08e2f9c95cfc2ee83b458b913cba36ef2c4d7020f1a",
  "icons-settings": "47fb8899e266e01d8d3a43dba6e04acd965d1df1d0cd6a0b575a8861ce72f83d",
  "icons-details": "dfb7be808d636fd586c84590eeb7372dac4d422463e8cdb989fd77ceae130187",
  "taskbar-settings": "d47b3782d722ff3b1e9b5bff7722b092a186c6e40b7112cb907ad3fd6594e87b",
  "taskbar-details": "e24d332aa778e46e70fb4e64449c3c15e18b05c435742bea496cd30e686ab375",
  "minimize-settings": "04ec4ffbbf625321b3d32e7f49bcb42214a39c0ea3772d77c86603a809f08e19",
  "minimize-details": "2dd19a9f1116c018354dbe873231f462ebd7ba2ba765d234e9e128f909e41cd7",
};

function wallpaperState() {
  const state = initialState();
  const item = { id: "custom-1", title: "天气 & <我的收藏>", kind: "video", width: 1920, height: 1080,
    thumbnail: "", url: "", bytes: 2048, durationSeconds: 90, libraryId: "" };
  state.wallpaper = { ...state.wallpaper, catalogStatus: "ready", playerAvailable: true, catalog: [item],
    library: [{ ...item, id: "local-1", official: false, missing: false }],
    displays: [{ id: "1", name: "工作显示器", primary: true, state: "stopped", wallpaperId: "", error: "" }] };
  state.wallpaperStatus = "idle";
  state.wallpaperSelection = { source: "official", id: item.id };
  state.wallpaperDisplay = "1";
  return state;
}

function selectors(html) {
  return html.match(/\b(?:class|id|data-[\w-]+|role|style|value|min|max|step|aria-(?:checked|selected|current|expanded|controls))="[^"]*"/g);
}

test("localization exposes an explicit bilingual API with a Chinese default", async () => {
  assert.ok(existsSync(new URL("../localization.js", import.meta.url)), "localization module exists");
  const { setLanguage, getLanguage, t } = await import("../localization.js");
  assert.equal(getLanguage(), "zh-CN");
  assert.equal(t("设置", "Settings"), "设置");
  assert.equal(setLanguage("en"), "en");
  assert.equal(getLanguage(), "en");
  assert.equal(t("设置", "Settings"), "Settings");
  assert.equal(t("用户原文"), "用户原文");
  for (const language of ["en-US", "zh", "", null, undefined, {}, "__proto__"]) {
    assert.equal(setLanguage(language), "zh-CN");
    assert.equal(getLanguage(), "zh-CN");
  }
  setLanguage("en");
  assert.equal(setLanguage("zh-CN"), "zh-CN");
  assert.equal(t("设置", "Settings"), "设置");
});

test("默认中文渲染与已确认的页面快照逐字节一致", () => {
  assert.equal(getLanguage(), "zh-CN");
  for (const [name, render] of Object.entries(renderCases())) {
    assert.equal(createHash("sha256").update(render()).digest("hex"), chineseBaseline[name], name);
  }
});

test("English navigation, search and wallpaper type controls keep their original selectors", () => {
  const chinese = pages.toolbar("wallpaperLocal", "我的壁纸");
  setLanguage("en");
  const html = pages.toolbar("wallpaperLocal", "我的壁纸");
  for (const label of ["Home", "My wallpapers", "Official wallpapers", "Desktop styles",
    "All wallpapers", "Dynamic wallpapers", "Static wallpapers", "Clear search"]) assert.ok(html.includes(label), label);
  assert.match(html, /placeholder="Search wallpapers, desktop styles, and mods"/);
  assert.match(html, /value="我的壁纸"/);
  assert.deepEqual(selectors(html), selectors(chinese));
  assert.match(back(), /aria-label="Back"/);
});

test("catalog localizes only known module display fields and never mutates Chinese search data", () => {
  const raw = structuredClone([catalogData.modules, catalogData.widgets]);
  setLanguage("en");
  assert.equal(typeof catalogData.localizedItem, "function");
  const item = catalogData.widgets.find(entry => entry.id === "weather");
  const display = catalogData.localizedItem(item);
  assert.equal(display.id, "weather");
  assert.equal(display.title, "Weather");
  assert.notEqual(display.description, item.description);
  assert.equal(searchCatalog(catalogData.widgets, "天气")[0], item);
  assert.equal(searchCatalog(catalogData.widgets, "Weather").length, 0, "search data is intentionally unchanged");
  const userItem = { id: "weather", title: "天气", description: "用户的天气收藏" };
  assert.equal(catalogData.localizedItem(userItem), userItem);
  assert.deepEqual([catalogData.modules, catalogData.widgets], raw);
  const html = pages.catalog(initialState(), false) + pages.catalog(initialState(), true);
  for (const label of ["Taskbar transparency", "Desktop icons", "Desktop widgets",
    "Clock", "Calendar", "Weather", "Almanac", "Performance", "Music", "Icon organizer"]) assert.ok(html.includes(label), label);
  // The minimize animation is temporarily hidden from the catalog.
  assert.ok(!html.includes("macOS minimize animation"), "minimize animation hidden");
  assert.match(pages.search({ ...initialState(), query: "天气" }), />Weather<\/h3>/);
});

test("wallpaper pagination and detail actions translate without changing names, IDs or playback state", () => {
  const state = wallpaperState(), before = structuredClone(state);
  const chineseDetail = pages.wallpaperDetail(state);
  setLanguage("en");
  const pagination = pages.wallpaperPagination(2, 9, 100);
  for (const label of ["Previous page", "Next page", "9 pages, go to", 'aria-label="Page number"']) assert.ok(pagination.includes(label), label);
  const detail = pages.wallpaperDetail(state);
  for (const label of ["Download and apply", "Download wallpaper", "Remove from My wallpapers", "Wallpaper settings",
    "Play preview", "All displays", "Resolution", "Size", "Duration"]) assert.ok(detail.includes(label), label);
  assert.match(detail, /天气 &amp; &lt;我的收藏&gt;/);
  assert.match(detail, /工作显示器/);
  assert.deepEqual(selectors(detail), selectors(chineseDetail));
  assert.match(pages.home(state), /Dynamic wallpapers/);
  assert.match(pages.home(state), /天气 &amp; &lt;我的收藏&gt;/);
  assert.match(pages.library({ ...state, view: "wallpaperLocal" }), /title="Add wallpaper"/);
  assert.match(pages.search({ ...state, query: "天气" }), /天气 &amp; &lt;我的收藏&gt;/);
  assert.deepEqual(state, before);
  state.wallpaper.download = { id: "custom-1", status: "running", received: 32, total: 100, error: "" };
  assert.match(pages.wallpaperDetail(state), />32%<\/span>/);
  state.wallpaper.download.total = 0;
  assert.match(pages.wallpaperDetail(state), />Downloading…<\/span>/);
});

test("every component settings and details page resolves translations at render time", () => {
  const state = initialState();
  const keywords = {
    clock: ["Clock style", "Style 2"],
    calendar: ["Calendar theme", "Classic", "Spacious", "Text color", "Highlight color"],
    performance: ["Performance", "Material", "Liquid glass", "Frosted glass"],
    almanac: ["Almanac", "Material"],
    weather: ["Weather", "City", "Automatic location"],
    organizer: ["Icon organizer", "Collected items", "0 items"],
    icons: ["Auto-hide idle desktop icons", "Auto-hide delay", "Restore icons on any input"],
    taskbar: ["Taskbar transparency", "Automatically hide taskbar"],
    minimize: ["Animation duration (ms)", "Animation style", "Modern", "Classic"],
  };
  for (const [key, page] of Object.entries(componentPages)) {
    const chinese = page(state);
    setLanguage("en");
    const html = page(state);
    assert.doesNotMatch(html,/>Enable widget<|>Enable feature</, `${key} 不再显示内部主开关文案`);
    for (const label of [...keywords[key], "Settings", "Details", "Restore defaults"]) assert.ok(html.includes(label), `${key}: ${label}`);
    assert.deepEqual(selectors(html), selectors(chinese), key);
    assert.doesNotMatch(page(state, "details"), /[\u3400-\u9fff]/u, `${key}: English details`);
    setLanguage("zh-CN");
    assert.equal(page(state), chinese, key);
  }
});

test("known component status and recovery messages translate at the point of use", () => {
  setLanguage("en");
  for (const [scope, status, notice] of [
    ["calendar", calendarStatus, calendarNotice], ["performance", performanceStatus, performanceNotice],
    ["almanac", almanacStatus, almanacNotice], ["weather", weatherStatus, weatherNotice],
    ["organizer", organizerStatus, organizerNotice], ["icons", desktopIconsStatus, desktopIconsNotice],
  ]) {
    for (const value of ["loading", "saving", "unavailable", "error"]) {
      const state = { ...initialState(), [`${scope}Status`]: value, [`${scope}Error`]: value === "error" ? "runtime" : "" };
      assert.ok(status(state), `${scope}: ${value}`);
      assert.doesNotMatch(status(state), /[\u3400-\u9fff]/u, `${scope}: ${value}`);
    }
    const saved = { ...initialState(), [`${scope}Status`]: "saved" };
    assert.match(notice(saved, { previousStatus: "saving" }), /applied and saved/i);
    assert.equal(notice(saved, { previousStatus: "saving", hasInlineStatus: true }), null);
  }
  assert.doesNotMatch(taskbarStatus({ ...initialState(), taskbarStatus: "error", taskbarError: "core-start" }), /[\u3400-\u9fff]/u);
  const state = initialState();
  state.minimizeStatus = "error";
  state.minimizeError = "missing-host";
  state.minimize.systemError = 2;
  assert.match(minimizeStatus(state), /system error: 2/i);
});

test("language round trips restore every captured page and leave user data untouched", () => {
  const state = wallpaperState();
  state.weather.city = "shanghai";
  state.organizer.title = "我的收藏";
  const before = structuredClone(state), cities = structuredClone(weatherCities);
  for (const [name, render] of Object.entries(renderCases(state))) {
    const chinese = render();
    setLanguage("en");
    assert.notEqual(render(), chinese, name);
    setLanguage("zh-CN");
    assert.equal(render(), chinese, name);
  }
  setLanguage("en");
  assert.match(weatherPage(state), /<span>上海<\/span>/);
  assert.deepEqual(weatherCities, cities);
  assert.deepEqual(state, before);
});

test("partial preview, download and clock updates use the active language", () => {
  const button = { setAttribute(key, value) { this[key] = value; } };
  const label = {};
  const root = { querySelector: selector => selector === ".wallpaper-preview-button" ? button : label };
  setLanguage("en");
  pages.updateWallpaperPreviewButton(root, true);
  assert.equal(button["aria-label"], "Pause preview");
  const state = wallpaperState();
  pages.updateWallpaperDownloadControls(root, state);
  assert.equal(label.textContent, "Download and apply");
  const span = {};
  const trigger = { querySelector: () => span, nextElementSibling: { querySelectorAll: () => [] } };
  const clockRoot = {
    querySelectorAll: () => [],
    querySelector: selector => selector === "#clock-style" ? trigger : null,
  };
  state.clock.style = "mond";
  syncClockControls(clockRoot, state);
  assert.equal(span.textContent, "Style 2");
  setLanguage("zh-CN");
  syncClockControls(clockRoot, state);
  assert.equal(span.textContent, "样式二");
  pages.updateWallpaperPreviewButton(root, false);
  assert.equal(button["aria-label"], "播放预览");
});

test("wallpaper names identical to UI labels remain literal data in both languages", () => {
  const state = wallpaperState();
  for (const title of ["时间", "天气", "设置", "任务栏透明化", "桌面收纳", "正在保存..."]) {
    state.wallpaper.catalog[0].title = title;
    for (const language of ["en", "zh-CN"]) {
      setLanguage(language);
      assert.ok(pages.wallpaperCard(state.wallpaper.catalog[0]).includes(`<div class="CardTitle">${title}</div>`));
      assert.ok(pages.home(state).includes(`<div class="HoverTitle">${title}</div>`));
      assert.ok(pages.wallpaperDetail(state).includes(`title="${title}">${title}</h2>`));
      assert.equal(state.wallpaper.catalog[0].title, title);
    }
  }
});

test("translated apply labels still require confirmed playback and preserve disabled controls", () => {
  const state = wallpaperState();
  state.wallpaper.catalog[0].libraryId = "local-1";
  setLanguage("en");
  assert.match(pages.wallpaperDetail(state), />Apply<\/span>/);
  state.wallpaper.displays[0].state = "playing";
  state.wallpaper.displays[0].wallpaperId = "local-1";
  assert.match(pages.wallpaperDetail(state), />Applied<\/span>/);
  state.wallpaper.displays[0].error = "WALLPAPER_PLAYER_EXITED";
  assert.match(pages.wallpaperDetail(state), />Apply<\/span>/);
  for (const [change, expected] of [
    [() => { state.wallpaper.library[0].missing = true; }, "File missing. Please import it again."],
    [() => { state.wallpaper.library[0].missing = false; state.wallpaper.playerAvailable = false; },
      "Player unavailable. Check the installation files."],
  ]) {
    change();
    const html = pages.wallpaperDetail(state);
    assert.ok(html.includes(expected), expected);
    assert.match(html, /data-wallpaper-panel="targets"[^>]* disabled>/);
  }
  state.wallpaper.likedIds = ["custom-1"];
  assert.match(pages.wallpaperDetail(state), /aria-label="Unlike"/);
});

test("empty, unavailable and static wallpaper surfaces translate only their owned copy", () => {
  setLanguage("en");
  const state = initialState();
  state.view = "wallpaperOfficial";
  for (const [status, expected] of [
    ["idle", "Preparing official wallpapers"], ["failed", "Could not load the official library"],
    ["ready", "No official wallpapers"],
  ]) {
    state.wallpaper.catalogStatus = status;
    assert.ok(pages.library(state).includes(expected), status);
  }
  state.wallpaperBrowse.official.kind = "image";
  assert.match(pages.library(state), /No matching wallpapers found/);
  assert.match(pages.search({ ...state, query: "not-a-real-wallpaper" }), /No matching content found/);
  assert.match(pages.unavailablePage("Custom <name>"), /Custom &lt;name&gt; is not available in this version/);
  const imageState = wallpaperState();
  imageState.wallpaper.catalog[0].kind = "image";
  assert.match(pages.home(imageState), /Static wallpapers/);
  assert.match(pages.wallpaperCard(imageState.wallpaper.catalog[0]), /Image wallpaper/);
  assert.doesNotMatch(pages.wallpaperDetail(imageState), /<video /);
  assert.match(pages.wallpaperCard({ ...imageState.wallpaper.catalog[0], libraryId: "local-1" }), /Downloaded/);
  assert.match(pages.wallpaperCard({ ...imageState.wallpaper.catalog[0], missing: true }), /File missing/);
});

test("all defined component error codes and recovered settings stay reversible", () => {
  const cases = [
    ["calendar", calendarStatus, ["invalid", "runtime", "save", "rollback", "load"]],
    ["performance", performanceStatus, ["runtime", "save", "rollback"]],
    ["almanac", almanacStatus, ["runtime", "save", "rollback"]],
    ["weather", weatherStatus, ["runtime", "save", "rollback"]],
    ["organizer", organizerStatus, ["runtime", "save", "rollback", "editor", "open", "invalid", "target", "limit"]],
    ["icons", desktopIconsStatus, ["runtime", "input-busy", "visibility", "save", "rollback"]],
    ["taskbar", taskbarStatus, ["load", "save", "busy", "unsupported", "missing-payload", "payload-integrity",
      "conflict", "discover", "read-appearance", "apply-appearance", "read-auto-hide", "apply-auto-hide",
      "core-start", "core-exited", "core-stop", "cancelled", "recovery", "watch", "system-effects-disabled", "runtime"]],
    ["minimize", minimizeStatus, ["save", "rollback", "invalid-settings", "missing-host", "already-running",
      "ipc", "host-start", "hook-install", "stop-timeout", "host-exited", "unsupported-architecture", "runtime"]],
  ];
  for (const [scope, status, errors] of cases) {
    for (const error of errors) {
      const state = { ...initialState(), [`${scope}Status`]: "error", [`${scope}Error`]: error };
      const chinese = status(state);
      setLanguage("en");
      assert.ok(status(state), `${scope}: ${error}`);
      assert.doesNotMatch(status(state), /[\u3400-\u9fff]/u, `${scope}: ${error}`);
      assert.doesNotMatch(status(state), /applied and saved/i, `${scope}: no false success`);
      setLanguage("zh-CN");
      assert.equal(status(state), chinese, `${scope}: ${error}`);
    }
  }
  for (const [scope, status] of cases.filter(([scope]) => scope !== "taskbar")) {
    const state = { ...initialState(), [`${scope}Status`]: "recovered",
      [`${scope}LoadStatus`]: scope === "calendar" ? "recovered-backup" : "recovered" };
    setLanguage("en");
    assert.match(status(state), /restored from backup/i, scope);
    setLanguage("zh-CN");
    assert.match(status(state), /备份/, scope);
  }
});

test("carousel partial updates retain original titles, IDs and query isolation", () => {
  const state = wallpaperState();
  const originalDocument = Object.getOwnPropertyDescriptor(globalThis, "document");
  const title = {}, metadata = {}, action = { dataset: {} }, previous = {};
  const created = [];
  const hero = {
    querySelector(selector) {
      return { ".preview.current": previous, ".HoverTitle": title,
        ".HoverMetaRow": metadata, "[data-wallpaper-id]": action }[selector] || null;
    },
    querySelectorAll: () => [],
    prepend: node => created.push(node),
  };
  const root = { querySelector(selector) {
    assert.equal(selector, ".HomeHero", "carousel must not touch the toolbar or search input");
    return hero;
  } };
  Object.defineProperty(globalThis, "document", { configurable: true, value: {
    createElement: tag => {
      assert.equal(tag, "div");
      return { style: { setProperty() {} } };
    },
  } });
  try {
    for (const [language, expected] of [["en", "Dynamic wallpapers"], ["zh-CN", "动态壁纸"]]) {
      setLanguage(language);
      pages.updateHomeCarousel(root, state);
      assert.equal(title.textContent, "天气 & <我的收藏>");
      assert.equal(metadata.textContent, `1920x1080 · ${expected}`);
      assert.equal(action.dataset.wallpaperId, "custom-1");
      assert.equal(previous.className, "preview prev");
    }
    assert.equal(created.length, 2);
  } finally {
    if (originalDocument) Object.defineProperty(globalThis, "document", originalDocument);
    else delete globalThis.document;
  }
});

test("wallpaper error mappings preserve every Chinese message and resolve English at call time", () => {
  const cases = [
    [["WALLPAPER_CATALOG_OFFLINE"], "暂时无法更新官方目录，正在显示已缓存的壁纸", "Cannot update the official catalog. Showing cached wallpapers."],
    [["WALLPAPER_CATALOG_CACHE_SAVE"], "官方目录已更新，但本地缓存保存失败", "The official catalog was updated, but its local cache could not be saved."],
    [["WALLPAPER_PLAYER_EXITED"], "壁纸播放器意外退出，请点击“应用”重试", 'The wallpaper player exited unexpectedly. Click "Apply" to try again.'],
    [["WALLPAPER_RUNTIME_FAILED"], "壁纸服务异常，请重新启动 Lume", "The wallpaper service failed. Restart Lume."],
    [["WALLPAPER_BUSY", "busy"], "正在处理壁纸，请稍候", "Processing wallpaper. Please wait."],
    [["WALLPAPER_SAVE_FAILED", "save"], "壁纸设置保存失败，请重试", "Could not save wallpaper settings. Please try again."],
    [["WALLPAPER_FAVORITES_LOAD"], "本地喜欢记录读取失败，请重试", "Could not load local likes. Please try again."],
    [["WALLPAPER_FAVORITES_SAVE"], "本地喜欢记录保存失败，请重试", "Could not save local likes. Please try again."],
    [["WALLPAPER_CATALOG_CONFLICT"], "官方壁纸信息已更改，请刷新目录后重试", "Official wallpaper information has changed. Refresh the catalog and try again."],
    [["WALLPAPER_MEDIA_IN_USE"], "这张壁纸正在使用，请先停止壁纸和预览", "This wallpaper is in use. Stop wallpaper playback and its preview first."],
    [["WALLPAPER_FILE_BUSY"], "壁纸文件正在使用，请停止播放后重试", "The wallpaper file is in use. Stop playback and try again."],
    [["WALLPAPER_DISPLAY_MISSING"], "所选显示器已断开，请重新选择显示器", "The selected display was disconnected. Select a display again."],
    [["WALLPAPER_PLAYER_MISSING", "MPV_EXECUTABLE_NOT_FOUND", "player-unavailable", "missing-player"], "播放器不可用，请检查安装文件", "Player unavailable. Check the installation files."],
    [["WALLPAPER_FILE_MISSING"], "文件已丢失，请重新导入或下载", "File missing. Import or download it again."],
    [["WALLPAPER_IMPORT_MISSING"], "文件已丢失，请重新选择文件", "File missing. Select the file again."],
    [["WALLPAPER_NOT_PLAYING"], "所选显示器尚未播放壁纸", "No wallpaper is playing on the selected display."],
    [["WALLPAPER_IMPORT_FORMAT", "unsupported"], "不支持此壁纸格式", "This wallpaper format is not supported."],
    [["WALLPAPER_IMPORT_LIMIT"], "一次最多导入 32 个壁纸文件", "You can import up to 32 wallpaper files at a time."],
    [["WALLPAPER_CANCELLED"], "操作已取消", "Operation cancelled."],
    [["WALLPAPER_MEDIA_SERVER"], "壁纸预览暂时不可用，请重试", "Wallpaper preview is temporarily unavailable. Please try again."],
    [["WALLPAPER_MEDIA_CAPABILITY"], "壁纸媒体暂时不可用，请重新应用", "Wallpaper media is temporarily unavailable. Apply it again."],
    [["WALLPAPER_SYSTEM_EFFECTS_DISABLED"], "此验证环境未启用桌面壁纸", "Desktop wallpapers are disabled in this validation environment."],
    [["player-start"], "壁纸启动失败，请重试", "Could not start the wallpaper. Please try again."],
    [["missing", "missing-file"], "文件已丢失，请重新导入", "File missing. Please import it again."],
    [["download"], "下载失败，请重试", "Download failed. Please try again."],
    [["network"], "网络连接失败，请重试", "Network connection failed. Please try again."],
    [["load"], "壁纸库读取失败，请重试", "Could not load the wallpaper library. Please try again."],
    [["import", "WALLPAPER_IMPORT_FAILED"], "导入失败，请检查文件后重试", "Import failed. Check the file and try again."],
    [["cancelled"], "下载已取消", "Download cancelled."],
    [["WALLPAPER_HTTP_503"], "官方壁纸暂时无法访问，请稍后重试", "Official wallpapers are temporarily unavailable. Please try again later."],
    [["WALLPAPER_PROTECTION_FAILED", "WALLPAPER_PROTECTED_FILE_READ"], "壁纸文件读取失败，请重新下载", "Could not read the wallpaper file. Download it again."],
    [["WALLPAPER_LIBRARY_WRITE"], "壁纸库操作失败，请重试", "Wallpaper library operation failed. Please try again."],
    [["WALLPAPER_CATALOG_FAILED", "WALLPAPER_DOWNLOAD_FAILED", "WALLPAPER_CONTENT_FAILED"], "官方壁纸读取失败，请刷新后重试", "Could not load official wallpapers. Refresh and try again."],
    [["MPV_FAILED"], "壁纸播放失败，请重新应用壁纸", "Wallpaper playback failed. Apply the wallpaper again."],
  ];
  for (const language of ["zh-CN", "en", "zh-CN"]) {
    setLanguage(language);
    for (const [codes, chinese, english] of cases) {
      const expected = language === "en" ? english : chinese;
      for (const code of codes) {
        assert.equal(wallpaperError(code), expected, `${language}: ${code}`);
        assert.equal(wallpaperError(`${code}:后端详情`), expected, `${code}: existing code precedence`);
        assert.equal(wallpaperError(`video:${code}`), `${language === "en" ? "Dynamic wallpapers: " : "动态壁纸："}${expected}`);
        assert.equal(wallpaperError(`image:${code}`), `${language === "en" ? "Static wallpapers: " : "静态壁纸："}${expected}`);
      }
    }
    assert.equal(wallpaperError("WALLPAPER_CATALOG_CACHED"), "", "ordinary cache use stays silent");
    assert.equal(wallpaperError(""), "");
  }
});

test("wallpaper notices retain precedence, quiet cache behavior and untouched backend text", () => {
  for (const language of ["zh-CN", "en", "zh-CN"]) {
    setLanguage(language);
    const english = language === "en", state = wallpaperState();
    for (const [fields, chinese, translated] of [
      [{ wallpaperStatus: "unavailable", loadStatus: "failed", error: "network" }, "宿主未连接，壁纸操作不可用", "Host disconnected. Wallpaper operations are unavailable."],
      [{ wallpaperStatus: "idle", loadStatus: "failed", error: "network" }, "壁纸库读取失败，请重试", "Could not load the wallpaper library. Please try again."],
      [{ wallpaperStatus: "idle", loadStatus: "recovered", error: "network" }, "网络连接失败，请重试", "Network connection failed. Please try again."],
      [{ wallpaperStatus: "idle", loadStatus: "recovered", error: "" }, "壁纸库已从备份恢复", "Wallpaper library restored from backup."],
      [{ wallpaperStatus: "idle", loadStatus: "recovered", error: "WALLPAPER_CATALOG_CACHED" }, "", ""],
      [{ wallpaperStatus: "idle", loadStatus: "fresh", error: "" }, "", ""],
    ]) {
      state.wallpaperStatus = fields.wallpaperStatus;
      Object.assign(state.wallpaper, { loadStatus: fields.loadStatus, error: fields.error });
      const before = structuredClone(state);
      assert.equal(wallpaperNotice(state), english ? translated : chinese);
      assert.deepEqual(state, before);
    }
    for (const error of ["天气 & <img src=x>", "保存失败，已恢复此前设置，请重试", "custom:后台中文"]) {
      const prefix = english ? "Wallpaper operation failed: " : "壁纸操作失败：";
      state.wallpaper.error = error;
      assert.equal(wallpaperNotice(state), `${prefix}${error}`, "backend text is never looked up as UI copy");
      assert.equal(wallpaperError(`image:${error}`), `${english ? "Static wallpapers: " : "静态壁纸："}${prefix}${error}`);
      const html = pages.library({ ...state, view: "wallpaperLocal" });
      assert.ok(html.includes(`${prefix}${error.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;")}`));
      assert.doesNotMatch(html, /<img src=x>/);
    }
  }
});

test("pending preview failures use the current language without changing error suppression or cleanup", async () => {
  const messages = [], changes = [];
  let rejectPlay;
  const video = {
    src: "", pause() {}, load() {},
    removeAttribute(name) { if (name === "src") this.src = ""; },
    play: () => new Promise((_, reject) => { rejectPlay = reject; }),
  };
  const controller = createWallpaperPreviewController({
    getVideo: () => video, onError: text => messages.push(text), onChange: playing => changes.push(playing),
  });
  const item = { id: "local-1", kind: "video", title: "时间", url: "https://lume-wallpaper.local/feedback.mp4" };
  const surface = { view: "wallpaperDetail", nativeVisible: true, documentVisible: true, id: item.id };
  try {
    for (const language of ["en", "zh-CN", "en"]) {
      const pending = controller.play(item, surface);
      setLanguage(language);
      rejectPlay(Error("后端原始解码错误"));
      assert.equal(await pending, false);
      assert.equal(messages.at(-1), language === "en"
        ? "Preview playback failed. Check the network or media file." : "预览播放失败，请检查网络或媒体文件");
      assert.equal(video.src, "");
      assert.equal(controller.playing, false);
      assert.deepEqual(changes.slice(-2), [true, false]);
      assert.equal(item.title, "时间");
    }
    const count = messages.length;
    const silent = controller.play(item, surface, { reportError: false });
    rejectPlay(Error("decode"));
    assert.equal(await silent, false);
    assert.equal(messages.length, count);
    const stale = controller.play(item, surface);
    controller.stop();
    rejectPlay(Error("late decode"));
    assert.equal(await stale, false);
    assert.equal(messages.length, count);
  } finally {
    controller.dispose();
  }
});

test("running component DOM retains language through pending, error and confirmed updates", {
  skip: !jsdomPath,
}, () => {
  const { JSDOM } = createRequire(import.meta.url)(jsdomPath);
  const dom = new JSDOM("<main></main>", { url: "https://lume.local/", pretendToBeVisual: true });
  const root = dom.window.document.querySelector("main");
  try {
    for (const [scope, page] of Object.entries(componentPages)) {
      const state = initialState(), sync = componentSync[scope];
      state[scope].enabled = true;
      state[scope].running = true;
      state.clock.style = "mond";
      state.calendar.theme = "split";
      state.weather.city = "shanghai";
      const statusKey = scope === "clock" ? "status" : `${scope}Status`;
      const errorKey = scope === "clock" ? "error" : `${scope}Error`;
      const toggleSelector = `[data-action="${scope === "clock" ? "toggle" : `${scope}-toggle`}"]`;
      for (const language of ["en", "zh-CN", "en", "zh-CN"]) {
        const english = language === "en";
        state[statusKey] = "saving";
        state[errorKey] = "";
        const pendingState = structuredClone(state);
        setLanguage(language);
        root.innerHTML = page(state) + pages.card({ id: scope, title: componentTitles[scope][Number(english)], toggle: true }, state);
        const surface = root.firstElementChild, toggle = root.querySelector(`.Card ${toggleSelector}`);
        const reset = surface.querySelector(`[data-action="${scope === "clock" ? "reset" : `${scope}-reset`}"]`);
        assert.equal(surface.querySelector(toggleSelector), null, `${scope} 内页没有主开关`);
        assert.equal(root.querySelector(".settings-head h3").textContent, componentTitles[scope][Number(english)], scope);
        assert.equal(toggle.getAttribute("aria-checked"), "true", scope);
        assert.equal(toggle.disabled, true, scope);
        assert.equal(reset.disabled, true, `${scope} 保存期间重置不可用`);
        sync(root, state);
        assert.deepEqual(state, pendingState, `${scope}: language does not change pending/native state`);

        state[statusKey] = "error";
        state[errorKey] = "runtime";
        sync(root, state);
        const status = root.querySelector(".status");
        assert.equal(status.getAttribute("role"), "alert", scope);
        assert.equal(status.hidden, false, scope);
        assert.ok(status.textContent, scope);
        if (english) assert.doesNotMatch(status.textContent, /[\u3400-\u9fff]/u, scope);
        else assert.match(status.textContent, /[\u3400-\u9fff]/u, scope);
        assert.equal(toggle.disabled, false, scope);
        assert.equal(reset.disabled, false, `${scope} 错误后恢复重置`);

        state[statusKey] = scope === "taskbar" ? "applied" : "saved";
        state[errorKey] = "";
        const confirmedState = structuredClone(state);
        sync(root, state);
        assert.equal(root.firstElementChild, surface, `${scope}: snapshots do not replace the page`);
        assert.equal(root.querySelector(toggleSelector), toggle, `${scope}: snapshots retain controls`);
        assert.equal(toggle.getAttribute("aria-checked"), "true", scope);
        assert.equal(toggle.disabled, false, scope);
        assert.equal(surface.querySelector('[data-action$="reset"]'), reset, `${scope} 保留内页控件节点`);
        assert.equal(reset.disabled, false, `${scope} 确认后重置可用`);
        assert.deepEqual(state, confirmedState, scope);
        for (const control of root.querySelectorAll('[role="combobox"]')) {
          const selected = control.nextElementSibling.querySelector('[aria-selected="true"]');
          assert.ok(selected, `${scope}: selected option is retained`);
          assert.equal(control.querySelector("span").textContent,
            selected.firstChild.textContent, `${scope}: trigger and menu keep the same language`);
        }
        if (scope === "weather") assert.equal(root.querySelector("#weather-city span").textContent, "上海");
        if (scope === "clock") assert.equal(root.querySelector("#clock-style span").textContent, english ? "Style 2" : "样式二");
        if (scope === "calendar") assert.equal(root.querySelector("#calendar-theme span").textContent, english ? "Spacious" : "舒展");
        if (scope === "minimize") assert.equal(root.querySelector("#minimize-engine span").textContent, english ? "Modern" : "现代样式");
        const material = root.querySelector(`#${scope}-material span`);
        if (material) assert.equal(material.textContent, english ? "Liquid glass" : "液态玻璃", scope);
      }
    }
  } finally {
    dom.window.close();
  }
});

test("real app language callbacks rerender every component and roll back failed saves", {
  skip: !jsdomPath,
}, async () => {
  const { JSDOM, VirtualConsole } = createRequire(import.meta.url)(jsdomPath);
  const { installStartupBrowserApis } = await import("./startup-dom.mjs");
  const failures = [], virtualConsole = new VirtualConsole();
  virtualConsole.on("jsdomError", error => failures.push(error.message));
  const dom = new JSDOM('<div id="app"><header id="titlebar"></header><main id="content"></main><div id="notice" hidden></div></div>',
    { url: "https://lume.local/", pretendToBeVisual: true, virtualConsole });
  const originalGlobals = Object.fromEntries(["window", "document", "matchMedia"].map(key =>
    [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
  globalThis.window = dom.window;
  globalThis.document = dom.window.document;
  globalThis.matchMedia = () => ({ matches: true });
  const { document, Event } = dom.window;
  const finishStartup = installStartupBrowserApis(dom.window);
  const listeners = new Set(), commands = [];
  let revision = 1;
  const snapshot = (language = "zh-CN", error = "") => ({
    version: 1, type: "app-settings-state", revision: revision++, language,
    balancedMode: true, autostart: false, busy: false, error,
    update: { status: "idle", version: "", progress: 0 },
  });
  const receive = message => listeners.forEach(listener => listener({ data: message }));
  dom.window.chrome = { webview: {
    addEventListener(_, listener) { listeners.add(listener); },
    removeEventListener(_, listener) { listeners.delete(listener); },
    postMessage(command) {
      commands.push(command);
      if (command === "v1|ready") queueMicrotask(() => receive(snapshot()));
    },
  } };
  const find = selector => {
    const element = document.querySelector(selector);
    assert.ok(element, selector);
    return element;
  };
  try {
    await import("../app.js");
    await finishStartup(commands, true);
    await Promise.resolve();
    for (const scope of Object.keys(componentPages)) {
      receive({ version: 1, type: "navigate", page: scope });
      for (const language of ["en", "zh-CN"]) {
        receive(snapshot(language));
        assert.equal(getLanguage(), language, scope);
        assert.equal(document.documentElement.lang, language, scope);
        assert.equal(find(".settings-head h3").textContent, componentTitles[scope][Number(language === "en")], scope);
        assert.equal(find(".settings").dataset.settingsScope, scope, "language keeps the current component route");
      }
    }
    receive({ version: 1, type: "navigate", page: "clock" });
    find("[data-titlebar-settings]").click();
    const settingsRoot = find(".application-settings"), languageControl = find('[data-setting="language"]');
    const changeLanguage = language => {
      languageControl.value = language;
      languageControl.dispatchEvent(new Event("change", { bubbles: true }));
    };
    changeLanguage("en");
    assert.equal(getLanguage(), "en", "pending selection immediately sets the display language");
    assert.equal(find(".application-settings"), settingsRoot, "settings handles its own DOM without app remounting");
    receive(snapshot("zh-CN", "preferences-save"));
    assert.equal(getLanguage(), "zh-CN", "failed save rolls all page translations back");
    assert.equal(languageControl.value, "zh-CN");
    changeLanguage("en");
    receive(snapshot("en"));
    find("[data-settings-back]").click();
    assert.equal(find(".settings-head h3").textContent, "Clock", "return renders the retained component in English");
    receive({ version: 1, type: "settings", saved: false, error: "runtime",
      clock: { enabled: true, style: "mond", running: false, scale: 100, x: 10, y: 20,
        alpha: 255, color: 0xffffff, locked: false, customColor: false } });
    assert.match(find('[data-settings-scope="clock"] .status').textContent, /Could not start the desktop clock/);
    assert.equal(find("#clock-style span").textContent, "Style 2");
    receive(snapshot("zh-CN"));
    assert.equal(find(".settings-head h3").textContent, "时间");
    assert.match(find('[data-settings-scope="clock"] .status').textContent, /桌面时钟启动失败/);
    assert.equal(find("#clock-style span").textContent, "样式二");
    assert.deepEqual(commands.filter(command => typeof command === "string" &&
      /^v1\|(clock|calendar|performance|almanac|weather|organizer|icons|taskbar|minimize)\|/.test(command)), [],
    "switching language never sends component settings or restart commands");
    assert.deepEqual(failures, []);
  } finally {
    dom.window.dispatchEvent(new Event("beforeunload"));
    dom.window.close();
    for (const [key, descriptor] of Object.entries(originalGlobals)) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else delete globalThis[key];
    }
  }
});

let feedbackAppId = 0;
const flushFeedback = async () => {
  for (let index = 0; index < 8; index++) await Promise.resolve();
};

async function withFeedbackApp(exercise, { startupFailure = false } = {}) {
  setLanguage("zh-CN");
  const { JSDOM, VirtualConsole } = createRequire(import.meta.url)(jsdomPath);
  const { installStartupBrowserApis } = await import("./startup-dom.mjs");
  const failures = [], virtualConsole = new VirtualConsole();
  virtualConsole.on("jsdomError", error => failures.push(error.message));
  const dom = new JSDOM('<div id="app"><header id="titlebar"></header><main id="content"></main><div id="notice" hidden></div></div>',
    { url: "https://lume.local/", pretendToBeVisual: true, virtualConsole });
  const originals = Object.fromEntries(["window", "document", "matchMedia"].map(key =>
    [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
  globalThis.window = dom.window;
  globalThis.document = dom.window.document;
  globalThis.matchMedia = () => ({ matches: true });
  const { document, Event } = dom.window;
  const finishStartup = installStartupBrowserApis(dom.window);
  let rejectFonts, offline = false, revision = 1;
  if (startupFailure) Object.defineProperty(document, "fonts", {
    configurable: true, value: { ready: new Promise((_, reject) => { rejectFonts = reject; }) },
  });
  dom.window.HTMLMediaElement.prototype.pause = function () {};
  dom.window.HTMLMediaElement.prototype.load = function () {};
  dom.window.HTMLMediaElement.prototype.play = () => Promise.reject(Error("test decode failure"));
  // jsdom has no layout engine; geometry is covered by the real-browser suite.
  dom.window.Range.prototype.getBoundingClientRect = () => ({ width: 160 });
  const listeners = new Set(), commands = [];
  const receive = message => {
    assert.equal(validHostMessage(message), true, `valid test snapshot: ${message.type}`);
    listeners.forEach(listener => listener({ data: message }));
  };
  const language = value => receive({
    version: 1, type: "app-settings-state", revision: revision++, language: value,
    balancedMode: true, autostart: false, busy: false, error: "",
    update: { status: "idle", version: "", progress: 0 },
  });
  dom.window.chrome = { webview: {
    addEventListener(_, listener) { listeners.add(listener); },
    removeEventListener(_, listener) { listeners.delete(listener); },
    postMessage(command) {
      if (offline) throw Error("host disconnected");
      commands.push(command);
      if (command === "v1|ready") queueMicrotask(() => language("zh-CN"));
    },
  } };
  const find = selector => {
    assert.deepEqual(failures, [], "page handlers must not throw before checking their rendered feedback");
    const element = document.querySelector(selector);
    assert.ok(element, selector);
    return element;
  };
  try {
    await import(`../app.js?localization-feedback=${feedbackAppId++}`);
    if (!startupFailure) await finishStartup(commands, true);
    await exercise({
      document, Event, receive, commands, find, language,
      offline: value => { offline = value; },
      failStartup: async () => { rejectFonts(Error("missing local resource")); await flushFeedback(); },
    });
    assert.deepEqual(failures, []);
  } finally {
    dom.window.dispatchEvent(new Event("beforeunload"));
    dom.window.close();
    for (const [key, descriptor] of Object.entries(originals)) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else delete globalThis[key];
    }
  }
}

test("real app fixed feedback and unavailable module titles switch languages without changing actions", {
  skip: !jsdomPath,
}, async () => {
  await withFeedbackApp(async ({ Event, receive, commands, find, language, offline }) => {
    const notice = expected => {
      assert.equal(find("#notice").hidden, false, `visible notice: ${expected}; rendered=${find("#notice").textContent}; commands=${JSON.stringify(commands.slice(-4))}`);
      assert.equal(find("#notice").textContent, expected);
    };
    const navigate = page => receive({ version: 1, type: "navigate", page });
    const clock = (error = "", saved = !error) => receive({
      version: 1, type: "settings", clock: { ...initialState().clock }, error, saved,
    });
    let wallpaperRevision = 0;
    const wallpaper = fields => receive({
      version: 1, type: "wallpaper-state", ...initialState().wallpaper,
      revision: wallpaperRevision++, playerAvailable: true, catalogStatus: "ready",
      library: [{ id: "local-1", title: "时间", kind: "video", thumbnail: "",
        url: "https://lume-wallpaper.local/feedback.mp4", bytes: 2048, missing: false }],
      ...fields,
    });
    find('[data-view="desktopBeauty"]').click();
    find('[data-open="widgets"]').click();
    find('[data-open="music"]').click();
    const beforeLanguage = commands.length;
    for (const value of ["zh-CN", "en", "zh-CN"]) {
      language(value);
      const english=value==="en";
      assert.equal(find("[data-titlebar-music]").getAttribute("aria-label"),english?"Wallpaper":"壁纸");
      find("[data-titlebar-music]").click();
      assert.equal(document.querySelector(".MusicRoot"),null,"titlebar leaves the music surface");
      assert.equal(find("[data-titlebar-music]").getAttribute("aria-label"),english?"Music":"音乐");
      find("[data-titlebar-music]").click();
      assert.ok(find(".MusicRoot"),"titlebar re-enters the music surface");
      assert.equal(find("[data-titlebar-music]").getAttribute("aria-label"),english?"Wallpaper":"壁纸");
      assert.doesNotMatch(find("#notice").textContent, /Music is not yet available|音乐暂未接入/);
    }
    const lifecycle=commands.slice(beforeLanguage).filter(command=>command?.scope==="music").map(command=>command.action);
    assert.equal(lifecycle.filter(action=>action==="open").length,3);
    assert.equal(lifecycle.filter(action=>action==="close").length,3);
    navigate("clock");
    find('[data-action="back"]').click();
    const unknown = find('[data-open="music"]');
    unknown.dataset.open = "unknown-module";
    unknown.click();
    for (const value of ["en", "zh-CN"]) {
      language(value);
      assert.equal(find(".settings-head h3").textContent, value === "en" ? "This feature" : "此功能");
    }
    for (const value of ["zh-CN", "en", "zh-CN"]) {
      language(value);
      const english = value === "en";
      navigate("minimize");
      receive({ version: 1, type: "minimize-state", ...initialState().minimize,
        entries: 0, pendingCleanup: 0, loadStatus: "fresh", error: "", result: "snapshot", saved: false });
      for (const [id, invalid, chinese, translated] of [
        ["minimize-durationMs", "49", "动画时长需要填写 50 到 2000 毫秒的整数", "Animation duration must be an integer between 50 and 2000 milliseconds."],
        ["minimize-unhideDurationMs", "5001", "任务栏显示时长需要填写 0 到 5000 毫秒的整数", "Taskbar display duration must be an integer between 0 and 5000 milliseconds."],
      ]) {
        const input = find(`#${id}`), count = commands.length;
        input.value = invalid;
        input.dispatchEvent(new Event("change", { bubbles: true }));
        assert.equal(input.value, "450", "invalid values restore the confirmed native setting");
        assert.equal(commands.length, count, "invalid values do not send a command");
        notice(english ? translated : chinese);
      }
      navigate("icons");
      receive({ version: 1, type: "icons-state", ...initialState().icons,
        loadStatus: "fresh", error: "", result: "snapshot", saved: false });
      const delay = find("#icons-delay"), count = commands.length;
      delay.value = "2";
      delay.dispatchEvent(new Event("change", { bubbles: true }));
      assert.equal(delay.value, "5");
      assert.equal(commands.length, count);
      notice(english ? "Auto-hide delay must be an integer between 3 and 60 seconds." : "自动隐藏延迟需要填写 3 到 60 秒的整数");

      navigate("clock");
      find('[data-action="back"]').click();
      for (const [error, chinese, translated] of [
        ["runtime", "桌面时钟启动失败，请检查字体和安装文件", "Could not start the desktop clock. Check the fonts and installation files."],
        ["rollback", "时钟恢复失败，请重新启用组件", "Could not restore the clock. Enable the widget again."],
        ["save", "保存失败，已恢复此前设置，请重试", "Could not save. Previous settings restored. Please try again."],
      ]) {
        clock(error);
        notice(english ? translated : chinese);
      }
      offline(true);
      find('[data-action="toggle"]').click();
      notice(english ? "Host disconnected. Settings were not saved." : "宿主未连接，设置未保存");
      offline(false);
      find("#notice").hidden = true;
      clock();
      assert.equal(find("#notice").hidden, true, "a snapshot without a pending save stays quiet");
      find('[data-action="toggle"]').click();
      assert.equal(commands.at(-1), "v1|clock|enabled|true");
      clock();
      notice(english ? "Applied and saved" : "已应用并保存");
      find("#notice").hidden = true;
      clock();
      assert.equal(find("#notice").hidden, true, "duplicate snapshots do not repeat success");
      find('[data-open="clock"]').click();
      clock("runtime");
      assert.equal(find("#notice").hidden, true, "inline component errors suppress global notifications");
      find('[data-action="back"]').click();
      find('[data-action="back"]').click();
      wallpaper();
      find('[data-view="wallpaperLocal"]').click();
      for (const [length, chinese, translated] of [
        [1, "无法接收拖入的文件，请使用添加壁纸或更新 WebView2 Runtime", "Could not receive dropped files. Use Add wallpaper or update WebView2 Runtime."],
        [33, "一次最多导入 32 个壁纸文件", "You can import up to 32 wallpaper files at a time."],
      ]) {
        const drop = new Event("drop", { bubbles: true, cancelable: true }), count = commands.length;
        Object.defineProperty(drop, "dataTransfer", { value: { types: ["Files"], files: Array.from({ length }, () => ({})) } });
        find('[data-wallpaper-drop-zone="local"]').dispatchEvent(drop);
        notice(english ? translated : chinese);
        assert.equal(commands.length, count, "rejected drops do not start an import");
      }
      offline(true);
      find('[data-wallpaper-id="local-1"][data-wallpaper-source="local"]').click();
      notice(english ? "Host disconnected. Preview playback is unavailable." : "宿主未连接，无法播放预览");
      assert.equal(find("h2").textContent, "时间", "the user wallpaper name is not the localized Clock label");
      offline(false);
      find(".wallpaper-preview-video").dispatchEvent(new Event("error"));
      notice(english ? "Preview playback failed. Check the network or media file." : "预览播放失败，请检查网络或媒体文件");
      find('[data-wallpaper-action="preview"]').click();
      wallpaper({ error: "WALLPAPER_CATALOG_CACHED" });
      notice(english ? "Wallpaper preview is temporarily unavailable. Please try again." : "壁纸预览暂时不可用，请重试");
      find('[data-wallpaper-action="remove"]').click();
      assert.deepEqual(commands.at(-1), { version: 1, scope: "wallpaper", action: "remove", id: "local-1" });
      wallpaper({ library: [] });
      notice(english ? "Wallpaper removed from My wallpapers." : "壁纸已从我的壁纸移除");
      assert.ok(find(".wallpaper-library"), "confirmed removal still returns to the original library");
      wallpaper({ library: [], error: "后台：天气 & <我的收藏>" });
      assert.equal(find(".wallpaper-inline-notice").textContent,
        `${english ? "Wallpaper operation failed: " : "壁纸操作失败："}后台：天气 & <我的收藏>`);
    }
  });
});

test("startup resource failures use the active language and never report ready", {
  skip: !jsdomPath,
}, async () => {
  for (const value of ["en", "zh-CN"]) {
    await withFeedbackApp(async ({ language, failStartup, find, commands }) => {
      language(value);
      await failStartup();
      assert.equal(find("#notice").hidden, false);
      assert.equal(find("#notice").textContent, value === "en"
        ? "Could not load home page resources. Check the UI resources in the installation package."
        : "首页资源加载失败，请检查安装包内的界面资源。");
      assert.equal(commands.includes("v1|ready"), false);
    }, { startupFailure: true });
  }
});
