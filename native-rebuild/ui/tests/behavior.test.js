import test from "node:test";
import assert from "node:assert/strict";
import { createBridge } from "../bridge.js";
import {
  initialState,
  reduce,
  searchCatalog,
  createVisibilityTimer,
  createClockPreviewTimer,
  createVisibilityCleanup,
  settingsNotice,
} from "../state.js";

test("bridge emits exact protocol strings and rejects outside the allowlist", () => {
  const sent = [];
  const bridge = createBridge({ postMessage: (m) => sent.push(m) });
  assert.equal(bridge.send("ready"), true);
  assert.equal(bridge.send("window", "drag"), true);
  assert.equal(bridge.send("window", "dropzone-local"), true);
  assert.equal(bridge.send("window", "dropzone-none"), true);
  assert.equal(bridge.send("clock", "style", "insideDream"), true);
  assert.equal(bridge.send("clock", "enabled", false), true);
  for (const args of [
    ["window", "delete"],
    ["window", "dropzone-official"],
    ["clock", "style", "evil"],
    ["clock", "enabled", "true"],
    ["ready", "extra"],
  ])
    assert.equal(bridge.send(...args), false);
  assert.deepEqual(sent, [
    "v1|ready",
    "v1|window|drag",
    "v1|window|dropzone-local",
    "v1|window|dropzone-none",
    "v1|clock|style|insideDream",
    "v1|clock|enabled|false",
  ]);
});

test("bridge emits a validated lifecycle cleanup acknowledgement", () => {
  const sent = [];
  const bridge = createBridge({ postMessage: (message) => sent.push(message) });
  assert.equal(bridge.lifecycle("cleanup", 7), true);
  assert.equal(bridge.lifecycle("cleanup", 0), false);
  assert.equal(bridge.lifecycle("cleanup", 0xffffffff + 1), false);
  assert.deepEqual(sent, [{ version: 1, scope: "lifecycle", action: "cleanup", generation: 7 }]);
});

test("visibility cleanup acknowledges only after release and only once per generation", () => {
  const events = [];
  const cleanup = createVisibilityCleanup(
    generation => events.push(`cleanup:${generation}`),
    generation => { events.push(`ack:${generation}`); return true; },
  );
  assert.equal(cleanup.acknowledge(0), false);
  assert.deepEqual(events, []);
  assert.equal(cleanup.acknowledge(3), true);
  assert.deepEqual(events, ["cleanup:3", "ack:3"]);
  assert.equal(cleanup.acknowledge(3), false);
  assert.equal(cleanup.acknowledge(2), false);
  assert.equal(cleanup.acknowledge(4), true);
  assert.deepEqual(events, ["cleanup:3", "ack:3", "cleanup:4", "ack:4"]);
});

function previewTimerHarness() {
  const scheduled = new Map();
  let nextId = 0;
  let refreshes = 0;
  const timer = createClockPreviewTimer(() => refreshes++, {
    setInterval(callback, delay) {
      assert.equal(delay, 1000);
      scheduled.set(++nextId, callback);
      return nextId;
    },
    clearInterval(id) {
      scheduled.delete(id);
    },
  });
  const surface = {
    nativeVisible: true,
    documentVisible: true,
    view: "clock",
    tab: "settings",
    previewOpen: false,
  };
  return {
    timer,
    update: (changes = {}) => timer.update({ ...surface, ...changes }),
    tick: () => [...scheduled.values()].forEach((callback) => callback()),
    get pending() {
      return scheduled.size;
    },
    get refreshes() {
      return refreshes;
    },
  };
}

test("collapsed clock preview schedules no work; opening refreshes immediately and closing stops", () => {
  const preview = previewTimerHarness();
  preview.update();
  assert.equal(preview.pending, 0);
  assert.equal(preview.refreshes, 0);
  preview.update({ previewOpen: true });
  assert.equal(preview.refreshes, 1);
  assert.equal(preview.pending, 1);
  preview.update({ previewOpen: true });
  assert.equal(preview.refreshes, 1);
  assert.equal(preview.pending, 1);
  preview.tick();
  assert.equal(preview.refreshes, 2);
  preview.update({ previewOpen: false });
  assert.equal(preview.pending, 0);
  preview.tick();
  assert.equal(preview.refreshes, 2);
});

test("switching away from the settings preview cancels ticks and reopening refreshes current time", () => {
  const preview = previewTimerHarness();
  preview.update({ previewOpen: true });
  preview.update({ previewOpen: true, tab: "details" });
  assert.equal(preview.pending, 0);
  preview.tick();
  assert.equal(preview.refreshes, 1);
  preview.update({ previewOpen: true, view: "widgets" });
  assert.equal(preview.pending, 0);
  preview.update({ previewOpen: true });
  assert.equal(preview.refreshes, 2);
  assert.equal(preview.pending, 1);
  preview.timer.dispose();
  assert.equal(preview.pending, 0);
});

test("expanded clock preview stays stopped while either native or document surface is hidden", () => {
  const preview = previewTimerHarness();
  preview.update({ previewOpen: true });
  preview.update({ previewOpen: true, nativeVisible: false });
  assert.equal(preview.pending, 0);
  preview.update({ previewOpen: true, documentVisible: false });
  assert.equal(preview.pending, 0);
  assert.equal(preview.refreshes, 1);
  preview.update({ previewOpen: true });
  assert.equal(preview.refreshes, 2);
  assert.equal(preview.pending, 1);
});

test("catalog and search receive explicit errors and disconnected feedback from setting state", () => {
  assert.equal(settingsNotice("error"), "保存失败，已恢复此前设置，请重试");
  assert.equal(settingsNotice("unavailable"), "宿主未连接，设置未保存");
  assert.equal(settingsNotice("saving"), null);
});

test("success notice requires a pending save and does not claim a desktop clock was opened", () => {
  assert.equal(
    settingsNotice("saved", { previousStatus: "saving" }),
    "已应用并保存",
  );
  assert.equal(settingsNotice("saved", { previousStatus: "loading" }), null);
  assert.equal(settingsNotice("error", { hasInlineStatus: true }), null);
});
test("bridge validates inbound host data and removes its listener", () => {
  let listener, removed;
  const bridge = createBridge({
    postMessage() {},
    addEventListener: (name, fn) => (listener = fn),
    removeEventListener: (name, fn) => (removed = fn),
  });
  const messages = [];
  const dispose = bridge.subscribe((m) => messages.push(m));
  listener({
    data: {
      version: 1,
      type: "settings",
      clock: { enabled: true, style: "mond" },
      saved: true,
    },
  });
  listener({
    data: {
      version: 1,
      type: "settings",
      clock: { enabled: "true", style: "mond" },
      saved: true,
    },
  });
  listener({ data: { version: 2, type: "visibility", visible: true } });
  listener({ data: { version: 1, type: "visibility", visible: false } });
  assert.equal(messages.length, 2);
  dispose();
  assert.equal(removed, listener);
});
test("unavailable bridge never pretends a setting was sent", () =>
  assert.equal(createBridge().send("clock", "enabled", true), false));
test("navigation returns clock to widgets and widgets to desktop beauty", () => {
  let s = reduce(initialState(), { type: "navigate", view: "desktopBeauty" });
  s = reduce(s, { type: "navigate", view: "widgets" });
  s = reduce(s, { type: "navigate", view: "clock" });
  s = reduce(s, { type: "back" });
  assert.equal(s.view, "widgets");
  s = reduce(s, { type: "back" });
  assert.equal(s.view, "desktopBeauty");
});
test("pending changes do not claim save until host acknowledgement", () => {
  let s = reduce(initialState(), {
    type: "request",
    key: "style",
    value: "mond",
  });
  assert.equal(s.status, "saving");
  assert.equal(s.clock.style, "welfare");
  s = reduce(s, {
    type: "host",
    message: {
      version: 1,
      type: "settings",
      clock: { enabled: true, style: "mond" },
      saved: false,
    },
  });
  assert.equal(s.status, "error");
  assert.equal(s.clock.style, "mond");
  s = reduce(s, {
    type: "host",
    message: {
      version: 1,
      type: "settings",
      clock: { enabled: true, style: "mond" },
      saved: true,
    },
  });
  assert.equal(s.status, "saved");
});
test("carousel wraps and search matches case-insensitively", () => {
  let s = reduce(initialState(), { type: "carousel", delta: -1, count: 10 });
  assert.equal(s.slide, 9);
  s = reduce(s, { type: "carousel", delta: 1, count: 10 });
  assert.equal(s.slide, 0);
  assert.deepEqual(
    searchCatalog(
      [{ title: "Mond", description: "时间" }, { title: "天气" }],
      " mond ",
    ),
    [{ title: "Mond", description: "时间" }],
  );
});
test("timers stop when native OR document surface is hidden and restart only when both visible", () => {
  let count = 0,
    cleared = 0;
  const timer = createVisibilityTimer(() => {}, 1000, {
    setInterval() {
      count++;
      return count;
    },
    clearInterval() {
      cleared++;
    },
  });
  timer.update(true, true);
  timer.update(true, true);
  assert.equal(count, 1);
  timer.update(false, true);
  assert.equal(cleared, 1);
  timer.update(false, false);
  timer.update(true, false);
  assert.equal(count, 1);
  timer.update(true, true);
  assert.equal(count, 2);
  timer.dispose();
  assert.equal(cleared, 2);
});
