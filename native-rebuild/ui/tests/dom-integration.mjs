// 通过 LUME_JSDOM_PATH 或首个参数加载既有 jsdom 依赖。
// 测试宿主和 DOM 依赖不随产品发布。
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { installStartupBrowserApis } from "./startup-dom.mjs";
const { JSDOM } = createRequire(import.meta.url)(process.env.LUME_JSDOM_PATH || process.argv[2]);
const originalSetInterval = globalThis.setInterval;
const originalClearInterval = globalThis.clearInterval;

async function setup(mode) {
  const dom = new JSDOM(
    '<div id="app"><header id="titlebar"></header><main id="content"></main><div id="notice" hidden></div></div>',
    { url: "https://lume.local/", pretendToBeVisual: true },
  );
  globalThis.window = dom.window;
  globalThis.document = dom.window.document;
  const finishStartup = installStartupBrowserApis(dom.window);
  globalThis.matchMedia = () => ({ matches: false });
  const intervals = new Map();
  let nextId = 0;
  globalThis.setInterval = (callback, delay) => {
    intervals.set(++nextId, { callback, delay });
    return nextId;
  };
  globalThis.clearInterval = (id) => intervals.delete(id);
  const callbacks = new Set();
  const hostState = { enabled: false, style: "welfare" };
  const receive = (data) => callbacks.forEach((fn) => fn({ data }));
  const commands = [];
  if (mode !== "disconnected") {
    window.chrome = {
      webview: {
        addEventListener: (_, fn) => callbacks.add(fn),
        removeEventListener: (_, fn) => callbacks.delete(fn),
        postMessage(message) {
          commands.push(message);
          if(mode === "manual") return;
          let saved = true;
          if (message.startsWith("v1|clock|")) {
            const [, , key, value] = message.split("|");
            if(mode === "success") {
              if(key === "reset") hostState.style="welfare";
              else hostState[key] = key === "enabled" ? value === "true" : value;
            }
            saved = mode === "success";
          }
          queueMicrotask(() =>
            receive({
              version: 1,
              type: "settings",
              clock: { ...hostState },
              saved,
            }),
          );
        },
      },
    };
  }
  await import(`../app.js?dom-test=${mode}`);
  await finishStartup(commands, mode !== "disconnected");
  const click = (selector) => {
    const target = document.querySelector(selector);
    assert.ok(target, `Expected UI control ${selector}`);
    target.click();
  };
  return {
    click,
    receive,
    commands,
    previewTimers: () =>
      [...intervals.values()].filter((timer) => timer.delay === 1000),
    openWidgets() {
      click('[data-view="desktopBeauty"]');
      click('[data-open="widgets"]');
    },
    cleanup() {
      window.dispatchEvent(new window.Event("beforeunload"));
      assert.equal(intervals.size, 0);
      dom.window.close();
    },
  };
}

try {
  const failure = await setup("failure");
  failure.openWidgets();
  failure.click('[data-action="toggle"]');
  await Promise.resolve();
  assert.equal(document.querySelector("#notice").hidden, false);
  assert.equal(
    document.querySelector("#notice").textContent,
    "保存失败，已恢复此前设置，请重试",
  );
  assert.equal(
    document
      .querySelector('[data-action="toggle"]')
      .getAttribute("aria-checked"),
    "false",
  );
  assert.equal(
    document.querySelector('[data-action="toggle"]').disabled,
    false,
  );
  failure.click('[data-action="toggle"]');
  await Promise.resolve();
  assert.equal(
    failure.commands.filter((message) => message.startsWith("v1|clock|"))
      .length,
    2,
  );
  failure.click('[data-open="clock"]');
  assert.equal(failure.previewTimers().length, 0);
  assert.equal(document.querySelector(".clock-preview-disclosure"),null);
  failure.receive({ version: 1, type: "visibility", visible: false });
  assert.equal(failure.previewTimers().length, 0);
  failure.receive({ version: 1, type: "visibility", visible: true });
  assert.equal(failure.previewTimers().length, 0);
  failure.click('[data-tab="details"]');
  assert.equal(failure.previewTimers().length, 0);
  failure.click('[data-tab="settings"]');
  assert.equal(failure.previewTimers().length, 0);
  assert.match(document.querySelector('.settings-body').textContent,/时间样式/);
  failure.click('[data-action="back"]');
  assert.equal(failure.previewTimers().length, 0);
  failure.cleanup();

  const disconnected = await setup("disconnected");
  disconnected.openWidgets();
  disconnected.click('[data-action="toggle"]');
  assert.equal(document.querySelector("#notice").hidden, false);
  assert.equal(
    document.querySelector("#notice").textContent,
    "宿主未连接，设置未保存",
  );
  disconnected.click('[data-open="calendar"]');
  assert.match(document.querySelector('.status').textContent,/宿主未连接/);
  assert.equal(document.querySelector('[data-action="calendar-toggle"]'),null);
  disconnected.click('[data-action="back"]');
  disconnected.click('.Card [data-action="calendar-toggle"]');
  assert.equal(document.querySelector('[data-action="calendar-toggle"]').getAttribute('aria-checked'),'false');
  disconnected.click('[data-open="calendar"]');
  assert.match(document.querySelector('.status').textContent,/宿主未连接/);
  disconnected.cleanup();

  const success = await setup("success");
  assert.equal(document.querySelector("#notice").hidden, true);
  success.openWidgets();
  success.click('[data-action="toggle"]');
  await Promise.resolve();
  assert.equal(
    document.querySelector("#notice").textContent,
    "已应用并保存",
  );
  success.receive({version:1,type:'navigate',page:'clock'});
  assert.ok(document.querySelector('#clock-style'));
  assert.equal(document.querySelector('.clock-preview-disclosure'),null);
  success.click('#clock-style');
  assert.equal(document.querySelector('#clock-style-options').hidden,false);
  success.click('[data-select-value="mond"]');
  await Promise.resolve();
  assert.match(document.querySelector('#clock-style').textContent,/Mond/);
  success.click('[data-action="reset"]');
  await Promise.resolve();
  assert.equal(success.commands.at(-1),'v1|clock|reset');
  success.click('[data-action="back"]');
  assert.equal(document.querySelector('[data-action="toggle"]').getAttribute('aria-checked'),'true');
  success.click('[data-open="clock"]');
  assert.equal(document.querySelector('[data-action="toggle"]'),null);
  success.click('[data-tab="details"]');
  assert.match(document.querySelector('.settings-body').textContent,/组件可独立启用，桌面位置随拖拽保存。退出 Lume 时，组件会一同关闭。/);
  success.receive({version:1,type:'navigate',page:'clock'});
  assert.ok(document.querySelector('#clock-style'), 'native Open Settings restores settings tab even on clock details');
  success.cleanup();

  const manual=await setup('manual');
  const calendar={enabled:false,theme:'spacious',material:'liquid-glass',textColor:'#ffffff80',highlightColor:'#0a84ff',locked:false,running:false};
  const calendarMessage=(fields={})=>({version:1,type:'calendar-settings',calendar:{...calendar},saved:false,error:'',materialStatus:'transparent-fallback',loadStatus:'loaded',result:'snapshot',...fields});
  const clockMessage=(fields={})=>({version:1,type:'settings',clock:{enabled:false,style:'welfare'},saved:true,error:'',...fields});
  manual.receive(calendarMessage());manual.receive(clockMessage());
  manual.openWidgets();manual.click('[data-action="calendar-toggle"]');
  assert.equal(manual.commands.at(-1),'v1|calendar|enabled|true');
  assert.equal(document.querySelector('[data-action="calendar-toggle"]').getAttribute('aria-checked'),'false','no optimistic toggle');
  assert.equal(document.querySelector('[data-action="calendar-toggle"]').disabled,true);
  manual.receive(clockMessage());
  assert.equal(document.querySelector('[data-action="calendar-toggle"]').disabled,true,'clock ack cannot clear calendar pending');
  manual.click('[data-open="calendar"]');
  assert.equal(document.querySelector('#calendar-theme').disabled,true);
  const pendingCount=manual.commands.length;
  document.querySelector('#calendar-theme').dispatchEvent(new window.KeyboardEvent('keydown',{key:'ArrowDown',bubbles:true}));
  assert.equal(document.querySelector('#calendar-theme-options').hidden,true,'disabled select stays closed via keyboard');
  const tabEvent=new window.KeyboardEvent('keydown',{key:'Tab',bubbles:true,cancelable:true});
  document.querySelector('#calendar-theme').dispatchEvent(tabEvent);
  assert.equal(tabEvent.defaultPrevented,false,'pending settings must not trap keyboard focus');
  document.querySelector('[data-select-owner="calendar-theme"][data-select-value="split"]').dispatchEvent(new window.MouseEvent('click',{bubbles:true}));
  assert.equal(manual.commands.length,pendingCount,'disabled select options cannot resubmit');
  manual.receive(calendarMessage({saved:true,result:'applied',calendar:{...calendar,enabled:true,running:true}}));
  manual.click('[data-action="back"]');
  assert.equal(document.querySelector('[data-action="calendar-toggle"]').getAttribute('aria-checked'),'true');
  manual.click('[data-open="calendar"]');
  assert.equal(document.querySelector('[data-action="calendar-toggle"]'),null);
  assert.match(document.querySelector('[data-action="calendar-textColor"]').textContent,/#FFFFFF80/);
  manual.click('#calendar-theme');manual.click('[data-select-owner="calendar-theme"][data-select-value="split"]');
  assert.equal(manual.commands.at(-1),'v1|calendar|theme|split');
  assert.equal(document.querySelector('#calendar-theme-options').hidden,true);
  manual.receive(calendarMessage({error:'save',result:'applied'}));
  const errorText=document.querySelector('.status').textContent;
  assert.match(errorText,/保存失败/);
  manual.receive(clockMessage());
  assert.equal(document.querySelector('.status').textContent,errorText,'clock snapshot must not erase calendar error');
  manual.click('[data-action="calendar-textColor"]');
  assert.equal(manual.commands.at(-1),'v1|calendar|textColor');
  manual.receive(calendarMessage({result:'cancelled'}));
  assert.equal(document.querySelector('.status').textContent,errorText,'cancel preserves previous error');
  assert.equal(document.querySelector('#notice').hidden,true,'cancel must not show fake saved toast');
  assert.equal(document.querySelector('[data-action="calendar-textColor"]').disabled,false);
  manual.receive(calendarMessage({loadStatus:'recovered-backup'}));
  assert.match(document.querySelector('.status').textContent,/备份/);
  manual.click('[data-action="calendar-highlightColor"]');
  assert.equal(manual.commands.at(-1),'v1|calendar|highlightColor');
  manual.receive(calendarMessage({result:'cancelled',loadStatus:'recovered-backup'}));
  assert.match(document.querySelector('.status').textContent,/备份/);
  manual.click('[data-action="calendar-reset"]');
  assert.equal(manual.commands.at(-1),'v1|calendar|reset');
  manual.receive(calendarMessage({saved:true,result:'applied'}));
  manual.click('[data-tab="details"]');
  assert.match(document.querySelector('.settings-body').textContent,/日程仅保存在本机.*从托盘退出 Lume 后不再提醒/);
  manual.receive({version:1,type:'navigate',page:'calendar'});
  assert.ok(document.querySelector('#calendar-theme'));
  manual.click('[data-action="back"]');
  manual.click('[data-action="toggle"]');
  manual.receive(calendarMessage());
  assert.equal(document.querySelector('[data-action="toggle"]').disabled,true,'calendar snapshot cannot clear clock pending');
  manual.receive(clockMessage({saved:false,error:'runtime'}));
  manual.click('[data-open="clock"]');
  manual.receive(calendarMessage());
  assert.match(document.querySelector('.status').textContent,/桌面时钟启动失败/,'calendar response leaves clock status');
  manual.cleanup();
  console.log(
    "DOM 集成回归通过：时钟和日历卡片启停、独立忙碌与错误、取消与读取恢复、鼠标键盘阻断、设置详情返回及生命周期。",
  );
} finally {
  globalThis.setInterval = originalSetInterval;
  globalThis.clearInterval = originalClearInterval;
}
