import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { installStartupBrowserApis } from "./startup-dom.mjs";
const { JSDOM, VirtualConsole } = createRequire(import.meta.url)(process.argv[2]);
const failures = [], virtualConsole = new VirtualConsole();
virtualConsole.on("jsdomError", (error) => failures.push(error.message));
const dom = new JSDOM('<div id="app"><header id="titlebar"></header><main id="content"></main><div id="notice" hidden></div></div>',
  { url: "https://lume.local/", pretendToBeVisual: true, virtualConsole });
globalThis.window = dom.window;
globalThis.document = dom.window.document;
globalThis.matchMedia = () => ({ matches: true });
const finishStartup = installStartupBrowserApis(dom.window);
const listeners = new Set(), commands = [];
const settings = { version: 1, type: "app-settings-state", revision: 1, language: "zh-CN",
  balancedMode: true, autostart: false, busy: false, error: "",
  update: { status: "idle", version: "", progress: 0 } };
const account = { version: 1, type: "account-state", revision: 1, status: "signed-in",
  busy: false, error: "", profile: { id: "fixture", displayName: "Test user", avatarUrl: "", vip: false, vipExpiresAt: "" } };
const wallpaper = { version: 1, type: "wallpaper-state", revision: 1, status: "idle", error: "", loadStatus: "fresh",
  playerAvailable: true, catalogStatus: "ready", catalogError: "", likedIds: [],
  catalog: Array.from({ length: 140 }, (_, i) => ({ id: `fixture-${i}`, title: `Wallpaper ${i}`, kind: "image",
    thumbnail: "", url: "", width: 1920, height: 1080, bytes: 1234, libraryId: `local-${i}` })),
  library: [], displays: [], download: { id: "", status: "idle", received: 0, total: 0, error: "" }, pauseFullscreen: false };
const receive = (message) => listeners.forEach((listener) => listener({ data: message }));
window.chrome = { webview: {
  addEventListener(_, fn) { listeners.add(fn); },
  removeEventListener(_, fn) { listeners.delete(fn); },
  postMessage(command) {
    commands.push(command);
    if (command === "v1|ready") queueMicrotask(() => {
      receive(settings); receive(account); receive(wallpaper);
    });
    if (command.scope === "app-settings" && command.action === "snapshot") queueMicrotask(() => receive(settings));
    if (command.scope === "account" && command.action === "snapshot") queueMicrotask(() => receive(account));
  },
} };
const find = (selector) => {
  const element = document.querySelector(selector);
  assert.ok(element, `missing ${selector}`);
  return element;
};
const click = (selector) => find(selector).click();
try {
  await import("../app.js");
  await finishStartup(commands, true);
  await Promise.resolve();
  click('[data-view="wallpaperOfficial"]');
  const filter = find("#wallpaper-kind");
  filter.value = "image"; filter.dispatchEvent(new window.Event("change", { bubbles: true }));
  click('[data-wallpaper-page="2"]');
  const grid = find(".wallpaper-grid");
  grid.scrollTop = 430;
  click("[data-titlebar-settings]");
  await Promise.resolve();
  const root = find(".application-settings");
  assert.equal(find('[data-setting="balancedMode"]').checked, true);
  click('[data-settings-section="user"]');
  const input = find("[data-account-name-input]");
  input.value = "\u672a\u4fdd\u5b58\u8349\u7a3f";
  input.dispatchEvent(new window.Event("input", { bubbles: true }));
  input.focus();
  receive({ ...account, revision: 2 });
  assert.equal(find("[data-account-name-input]"), input);
  assert.equal(document.activeElement, input);
  assert.equal(input.value, "\u672a\u4fdd\u5b58\u8349\u7a3f");
  click('[data-account-action="updateName"]');
  const unrelatedForm = document.createElement("form");
  unrelatedForm.innerHTML = '<input value="not a wallpaper search">';
  root.append(unrelatedForm);
  unrelatedForm.dispatchEvent(new window.Event("submit", { bubbles: true, cancelable: true }));
  unrelatedForm.remove();
  assert.equal(find(".application-settings"), root, "profile form must not enter wallpaper search");
  assert.equal(document.querySelector(".SearchResultsLayout"), null);
  assert.ok(commands.some((command) => command.scope === "account" && command.action === "updateName"));
  receive({ ...account, revision: 3, error: "network" });
  click("[data-settings-back]");
  assert.equal(find(".wallpaper-grid").scrollTop, 430, "return preserves list scroll");
  assert.equal(find('.wallpaper-page-button[aria-current="page"]').textContent, "2");
  click("[data-titlebar-settings]");
  click('[data-settings-section="user"]');
  assert.equal(find("[data-account-name-input]"), input, "settings keeps the same draft node");
  assert.equal(input.value, "\u672a\u4fdd\u5b58\u8349\u7a3f");
  click('[data-settings-section="general"]');
  const language = find('[data-setting="language"]');
  language.value = "en"; language.dispatchEvent(new window.Event("change", { bubbles: true }));
  receive({ ...settings, revision: 2, language: "en" });
  assert.equal(document.documentElement.lang, "en");
  assert.equal(find('[data-titlebar-settings]').getAttribute("aria-label"), "Settings");
  click("[data-settings-back]");
  assert.equal(find('[data-view="home"]').textContent, "Home");
  receive({ version: 1, type: "navigate", page: "applicationSettings", section: "user" });
  assert.equal(find(".application-settings").dataset.settingsSection, "user");
  assert.equal(find("[data-account-name-input]").value, "\u672a\u4fdd\u5b58\u8349\u7a3f");
  receive({ ...account, revision: 4, status: "signed-out", profile: null });
  assert.equal(find("[data-lume-account-label]").textContent, "Sign in");
  assert.equal(find(".lume-titlebar__avatar").hasAttribute("src"), false);
  const logoutCommands=()=>commands.filter(command=>command?.scope==="account" && command.action==="logout");
  receive({...account,revision:5});
  click("[data-settings-back]");
  click(".lume-titlebar__login");
  click('[data-account-menu-action="logout"]');
  assert.equal(logoutCommands().length,1);
  click("[data-titlebar-settings]");
  click('[data-settings-section="user"]');
  assert.equal(find('[data-account-action="logout"]').getAttribute("aria-disabled"),"true",
    "顶部退出必须进入用户设置共用的账号操作等待状态");
  click('[data-account-action="logout"]');
  assert.equal(logoutCommands().length,1,"顶部退出等待响应时，设置页不能重复发送退出");
  receive({...account,revision:6,status:"signed-out",profile:null,error:"local-logout"});
  assert.equal(find("[data-account-profile]").hidden,true);
  assert.equal(find("[data-account-name-input]").value,"");
  assert.equal(find("[data-lume-account-label]").textContent,"Sign in");
  receive({...account,revision:7});
  click('[data-account-action="logout"]');
  assert.deepEqual(logoutCommands()[1],logoutCommands()[0],"两个入口发送相同的原生退出命令");
  assert.equal(find(".lume-titlebar__login").getAttribute("aria-disabled"),"true",
    "设置页发起退出后，顶部应立即同步忙碌，不必等原生快照");
  click(".lume-titlebar__login");
  click('[data-account-menu-action="logout"]');
  assert.equal(logoutCommands().length,2,"设置页退出等待响应时，顶部不能重复发送退出");
  assert.doesNotMatch(find(".lume-titlebar__login").title,/disconnected|宿主未连接/,
    "阻止重复操作不能误报宿主断开");
  receive({...account,revision:8,busy:true});
  assert.equal(find(".lume-titlebar__login").getAttribute("aria-disabled"),"true");
  receive({...account,revision:7});
  assert.equal(find(".lume-titlebar__login").getAttribute("aria-disabled"),"true","旧快照不能解除忙碌");
  receive({...account,revision:9,error:"storage-failed"});
  assert.equal(find(".lume-titlebar__login").getAttribute("aria-disabled"),"false");
  assert.equal(find('[data-account-action="logout"]').getAttribute("aria-disabled"),"false");
  assert.equal(find("[data-lume-account-label]").textContent,"Test user","失败后保留已确认资料");
  click(".lume-titlebar__login");
  click('[data-account-menu-action="logout"]');
  assert.equal(logoutCommands().length,3,"失败后可从另一个入口重试");
  receive({...account,revision:10,status:"signed-out",profile:null,error:"local-logout"});
  assert.equal(find("[data-lume-account-label]").textContent,"Sign in");
  click('[data-account-action="login"]');
  assert.equal(find(".lume-titlebar__login").getAttribute("aria-disabled"),"true");
  receive({...account,revision:11,status:"authorizing",busy:true,profile:null});
  assert.equal(find(".lume-titlebar__login").getAttribute("aria-disabled"),"false","共享忙碌不能阻止取消登录");
  click(".lume-titlebar__login");
  assert.equal(commands.at(-1).action,"cancelLogin");
  receive({...account,revision:12,status:"signed-out",profile:null,error:"cancelled"});
  assert.equal(find('[data-account-action="login"]').getAttribute("aria-disabled"),"false");
  assert.deepEqual(failures, []);
  console.log("PASS: actual app routing, retained drafts, isolated profile form, list return, language, native navigation and shared logout");
} finally {
  window.dispatchEvent(new window.Event("beforeunload"));
  dom.window.close();
}
