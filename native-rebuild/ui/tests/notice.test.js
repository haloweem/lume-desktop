import test from "node:test";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

import { createNoticeController } from "../notice.js";
function setup(reduced = false) {
  const classes = new Set(), jobs = new Map(), callbacks = [];
  const element = { hidden: true, textContent: "", classList: {
    add: value => classes.add(value), remove: value => classes.delete(value),
    contains: value => classes.has(value),
  } };
  let now = 0, nextId = 1;
  const timers = {
    setTimeout(fn, delay) {
      const id = nextId++;
      jobs.set(id, { fn, at: now + delay }); callbacks.push(fn); return id;
    },
    clearTimeout(id) { jobs.delete(id); },
  };
  const tick = duration => {
    const end = now + duration;
    for (;;) {
      const entry = [...jobs].filter(([, job]) => job.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
      if (!entry) break;
      now = entry[1].at; jobs.delete(entry[0]); entry[1].fn();
    }
    now = end;
  };
  return { element, jobs, callbacks, tick,
    controller: createNoticeController(element, { timers, reducedMotion: () => reduced }) };
}

test("成功提示停留 2.5 秒，再淡出 180 毫秒并清理内容", () => {
  const { controller, element, tick, jobs } = setup();
  controller.show("已应用并保存", { autoDismiss: true });
  assert.equal(element.hidden, false);
  tick(2499); assert.equal(element.classList.contains("is-hiding"), false);
  tick(1); assert.equal(element.classList.contains("is-hiding"), true);
  assert.equal(element.hidden, false);
  tick(179); assert.equal(element.hidden, false);
  tick(1); assert.equal(element.hidden, true);
  assert.equal(element.textContent, "");
  assert.equal(jobs.size, 0);
});

test("错误等普通提示默认保留，不创建自动关闭计时", () => {
  const { controller, element, tick, jobs } = setup();
  controller.show("保存失败，请重试");
  tick(100000);
  assert.equal(element.hidden, false);
  assert.equal(jobs.size, 0);
});

test("新成功提示重新计时，淡出状态不会遗留", () => {
  const { controller, element, tick } = setup();
  controller.show("旧提示", { autoDismiss: true });
  tick(2500);
  controller.show("新提示", { autoDismiss: true });
  assert.equal(element.classList.contains("is-hiding"), false);
  tick(2499); assert.equal(element.hidden, false);
  assert.equal(element.textContent, "新提示");
  tick(181); assert.equal(element.hidden, true);
});

test("被取消的旧回调不能隐藏新错误提示", () => {
  const { controller, element, tick, callbacks, jobs } = setup();
  controller.show("成功", { autoDismiss: true });
  tick(2500);
  const previousCallbacks = [...callbacks];
  controller.show("新错误");
  previousCallbacks.forEach(fn => fn());
  tick(100000);
  assert.equal(element.textContent, "新错误");
  assert.equal(element.hidden, false);
  assert.equal(element.classList.contains("is-hiding"), false);
  assert.equal(jobs.size, 0);
});

test("主动关闭和销毁清理计时，销毁后不能再显示提示", () => {
  const { controller, element, jobs } = setup();
  controller.show("成功", { autoDismiss: true }); controller.dismiss();
  assert.equal(element.hidden, true); assert.equal(jobs.size, 0);
  controller.show("另一个成功", { autoDismiss: true }); controller.dispose();
  controller.show("不得重新显示");
  assert.equal(element.hidden, true); assert.equal(jobs.size, 0);
});

test("减少动态效果时到期直接隐藏，不保留淡出计时", () => {
  const { controller, element, tick, jobs } = setup(true);
  controller.show("成功", { autoDismiss: true }); tick(2500);
  assert.equal(element.hidden, true);
  assert.equal(element.classList.contains("is-hiding"), false);
  assert.equal(jobs.size, 0);
});

test("真实 app 组件成功和失败提示接入生命周期", { skip: !process.env.LUME_JSDOM_PATH }, () => {
  const result = spawnSync(process.execPath, [
    fileURLToPath(new URL("./component-notice-dom.mjs", import.meta.url)),
    process.env.LUME_JSDOM_PATH,
  ], { encoding: "utf8", timeout: 30000 });
  assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
});
