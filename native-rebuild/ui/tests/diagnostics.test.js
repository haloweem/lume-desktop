import test from "node:test";
import assert from "node:assert/strict";
import {buildDiagnosticReport, createDiagnosticsRecorder, diagnosticReportText} from "../diagnostics.js";

function storage() {
  const values = new Map();
  return {getItem:key=>values.get(key) ?? null,setItem:(key,value)=>values.set(key,String(value)),
    removeItem:key=>values.delete(key)};
}

test("本版禁用诊断时不读取、记录或写入用户数据", () => {
  const forbidden=()=>{throw new Error('disabled diagnostics touched storage');};
  const recorder=createDiagnosticsRecorder({enabled:false,storage:{getItem:forbidden,setItem:forbidden,removeItem:forbidden}});
  recorder.record({type:'dock-state',enabled:true,running:false,error:'fault'});
  recorder.saveNote({summary:'private'});recorder.clear();recorder.clearNote();
  assert.deepEqual(recorder.list(),[]);assert.deepEqual(recorder.health(),{});
  assert.equal(recorder.diagnosticCount(),0);assert.equal(recorder.native().available,false);
});

test("诊断记录只保留脱敏错误摘要并可跨会话恢复", () => {
  const store=storage(), recorder=createDiagnosticsRecorder({storage:store,now:()=>1700000000000});
  recorder.record({type:"dock-state",running:true,enabled:true,pinned:3,added:4,visibleItems:0,layersVisible:false,
    materialStatus:"native-ready",loadStatus:"loaded",error:""});
  recorder.record({type:"wallpaper-state",error:"WALLPAPER_CATALOG_OFFLINE",catalogStatus:"ready",catalog:["private"]});
  recorder.record({type:"account-state",profile:{token:"secret"}});
  const events=recorder.list();
  assert.equal(events.length,2);
  assert.deepEqual(events.find(event=>event.type==="wallpaper-state").details,{error:"WALLPAPER_CATALOG_OFFLINE",catalogStatus:"ready"});
  assert.deepEqual(recorder.health()["dock-state"],{
    at:1700000000000, running:true, enabled:true, pinned:3, added:4, visibleItems:0, layersVisible:false,
    materialStatus:"native-ready", loadStatus:"loaded", error:"",
  });
  const restored=createDiagnosticsRecorder({storage:store});
  assert.equal(restored.list().length,2);
});

test("诊断报告包含环境、问题描述和运行事件但不带敏感字段", () => {
  const report=buildDiagnosticReport({
    version:"1.0.3.3",language:"zh-CN",view:"wallpaperOfficial",
    note:{summary:"动态壁纸不显示",steps:"打开详情页",expected:"看到预览",actual:"只有空白"},
    events:[{at:1700000000000,type:"wallpaper-state",details:{error:"WALLPAPER_CATALOG_OFFLINE"}}],
    health:{"taskbar-state":{at:1700000000000,enabled:true,running:false,transparentTaskbar:true,autoHideTaskbar:true,error:"core-exited"}},
    userAgent:"Windows test",platform:"Win32",generatedAt:"2023-11-14T22:13:20.000Z",
  });
  assert.equal(report.format,"lume-diagnostic-v1");
  assert.equal(report.issue.summary,"动态壁纸不显示");
  assert.equal(report.recentEvents[0].details.error,"WALLPAPER_CATALOG_OFFLINE");
  assert.equal(report.health["taskbar-state"].error,"core-exited");
  assert.doesNotMatch(diagnosticReportText(report),/secret|access_token|session_cookie/i);
});

test("原生诊断快照保留效果不一致证据并对重复事件去重", () => {
  let tick = 1700000000000;
  const recorder = createDiagnosticsRecorder({
    storage: storage(),
    now: () => tick++,
  });
  const mismatch = {
    component: "dock",
    expected: "visible",
    actual: "hidden",
    confidence: "confirmed",
    reason: "layers-hidden",
  };
  const event = {
    at: 1700000000100,
    firstAt: 1700000000000,
    repeatCount: 2,
    module: "dock",
    kind: "effect-mismatch",
    action: "apply",
    status: "degraded",
    error: "EFFECT_MISMATCH",
    expected: "visible",
    actual: "hidden",
  };

  recorder.record({
    version: 1,
    type: "diagnostics-state",
    schemaVersion: 1,
    available: true,
    storage: "ready",
    revision: 4,
    generatedAt: 1700000000000,
    mismatches: [mismatch],
    faults: [],
    events: [event],
    health: {dock: {status: "degraded"}},
  });
  recorder.record({
    version: 1,
    type: "diagnostics-state",
    schemaVersion: 1,
    available: true,
    storage: "ready",
    revision: 5,
    generatedAt: 1700000001000,
    mismatches: [mismatch],
    faults: [],
    events: [event],
    health: {dock: {status: "degraded"}},
  });

  const native = recorder.native();
  assert.equal(native.available, true);
  assert.equal(native.revision, 5);
  assert.equal(native.mismatches.length, 1);
  assert.deepEqual(native.mismatches[0], {
    component: "dock",
    expected: "visible",
    actual: "hidden",
    confidence: "confirmed",
    reason: "layers-hidden",
  });
  assert.equal(native.events.length, 1);
  assert.equal(native.events[0].repeatCount, 2);
  assert.equal(native.events[0].error, "EFFECT_MISMATCH");
  assert.doesNotMatch(JSON.stringify(native), /private\.example|token-private|https?:\/\//i);
});

test("原生诊断在恢复快照中清除已解决的不一致并保留故障历史", () => {
  const recorder = createDiagnosticsRecorder({storage: storage(), now: () => 1700000000000});
  recorder.record({
    version: 1,
    type: "diagnostics-state",
    schemaVersion: 1,
    available: true,
    storage: "ready",
    revision: 1,
    mismatches: [{component: "taskbar", expected: "transparent", actual: "opaque", confidence: "suspected", reason: "apply-pending"}],
    events: [{at: 1700000000000, module: "taskbar", kind: "effect-mismatch", expected: "transparent",
      actual: "opaque", error: "TASKBAR_APPLY_FAILED"}],
    health: {},
  });
  recorder.record({
    version: 1,
    type: "diagnostics-state",
    schemaVersion: 1,
    available: true,
    storage: "ready",
    revision: 2,
    mismatches: [],
    events: [],
    health: {},
  });
  const native = recorder.native();
  assert.deepEqual(native.mismatches, []);
  assert.equal(native.events.length, 0);
  assert.equal(native.revision, 2);
});

test("诊断报告合并原生快照且不泄漏未白名单字段", () => {
  const report = buildDiagnosticReport({
    version: "1.0.3.3",
    language: "zh-CN",
    native: {
      version: 1,
      type: "diagnostics-state",
      schemaVersion: 1,
      available: true,
      storage: "ready",
      revision: 9,
      generatedAt: 1700000000000,
      mismatches: [{component: "wallpaper", expected: "playing", actual: "paused", confidence: "unknown", reason: "not-observable token"}],
      faults: [],
      events: [],
      health: {},
    },
  });
  assert.equal(report.native.available, true);
  assert.equal(report.native.revision, 9);
  assert.equal(report.native.mismatches[0].component, "wallpaper");
  assert.doesNotMatch(diagnosticReportText(report), /token|secret|access_token|session_cookie/i);
});

test("用户描述和环境字段在导出时移除凭据、URL 与本机路径", () => {
  const report=buildDiagnosticReport({
    note:{summary:"Authorization: Bearer abcdefghijkl https://private.invalid/C:/Users/admin/file.log C:\\Users\\admin\\secret.txt"},
    userAgent:"Lume token=private-value",platform:"Win32",
  });
  const exported=diagnosticReportText(report);
  assert.doesNotMatch(exported,/abcdefghijkl|private\.invalid|Users\\admin|private-value/i);
  assert.match(exported,/\[redacted\]/);
});
