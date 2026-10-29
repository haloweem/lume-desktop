import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { installStartupBrowserApis } from "./startup-dom.mjs";
import { initialState } from "../state.js";
const { JSDOM } = createRequire(import.meta.url)(process.env.LUME_JSDOM_PATH || process.argv[2]);
const dom = new JSDOM('<div id="app"><header id="titlebar"></header><main id="content"></main><div id="notice" hidden></div></div>',
  { url: "https://lume.local/", pretendToBeVisual: true });
globalThis.window = dom.window;
globalThis.document = dom.window.document;
globalThis.matchMedia = () => ({ matches: false });
const finishStartup = installStartupBrowserApis(window);
const commands = [], listeners = new Set(), timers = new Map();
const receive = data => listeners.forEach(fn => fn({ data }));
const originalSetTimeout = globalThis.setTimeout, originalClearTimeout = globalThis.clearTimeout;
const setTimeout = (fn, delay, ...args) => {
  if (![2500, 180].includes(delay)) return originalSetTimeout(fn, delay, ...args);
  const id = {}; timers.set(id, { fn, delay }); return id;
};
const clearTimeout = id => { if (!timers.delete(id)) originalClearTimeout(id); };
globalThis.setTimeout = setTimeout; globalThis.clearTimeout = clearTimeout;
window.chrome = { webview: {
  addEventListener: (_, fn) => listeners.add(fn), removeEventListener: (_, fn) => listeners.delete(fn),
  postMessage: message => commands.push(message),
} };
const click = selector => {
  const node = document.querySelector(selector); assert.ok(node, `缺少控件 ${selector}`); node.click();
};
const finish = delay => {
  const entry = [...timers].find(([, value]) => value.delay === delay);
  assert.ok(entry, `提示应创建 ${delay}ms 计时`);
  timers.delete(entry[0]); entry[1].fn();
};
const calendar = { ...initialState().calendar };
const message = fields => ({ version: 1, type: "calendar-settings", calendar, saved: false,
  error: "", loadStatus: "loaded", result: "snapshot", materialStatus: "inactive", ...fields });
try {
  await import("../app.js?component-notice");
  await finishStartup(commands);
  receive(message());
  click('[data-view="desktopBeauty"]'); click('[data-open="widgets"]');
  click('[data-action="calendar-toggle"]');
  receive(message({ calendar: { ...calendar, enabled: true }, saved: true, result: "applied" }));
  const notice = document.querySelector("#notice");
  assert.equal(notice.textContent, "日历已应用并保存");
  assert.equal(notice.hidden, false);
  finish(2500); assert.ok(notice.classList.contains("is-hiding"));
  finish(180); assert.equal(notice.hidden, true);
  click('[data-action="calendar-toggle"]');
  receive(message({ saved: true, result: "applied" }));
  assert.equal(timers.size, 1);
  receive(message({ error: "runtime", result: "applied" }));
  assert.match(notice.textContent, /启动失败/);
  assert.equal(notice.hidden, false);
  assert.equal(timers.size, 0, "错误替换成功提示后清除成功计时");
  click('[data-action="back"]');
  assert.equal(notice.hidden, true);
  console.log("PASS: 真实 app 日历成功提示淡出、错误保留、导航清理");
} finally {
  window.dispatchEvent(new window.Event("beforeunload"));
  assert.equal(timers.size, 0);
  globalThis.setTimeout = originalSetTimeout; globalThis.clearTimeout = originalClearTimeout;
  dom.window.close();
}
