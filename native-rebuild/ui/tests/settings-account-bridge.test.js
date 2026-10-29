import assert from "node:assert/strict";
import test from "node:test";
import { createBridge, validHostMessage } from "../bridge.js";
import { initialState, reduce } from "../state.js";

const account = (fields = {}) => ({
  version: 1, type: "account-state", revision: 1, status: "signed-out",
  busy: false, error: "", profile: null, ...fields,
});
const settings = (fields = {}) => ({
  version: 1, type: "app-settings-state", revision: 1, language: "zh-CN",
  balancedMode: true, autostart: false, busy: false, error: "",
  update: { status: "idle", version: "", progress: 0 }, ...fields,
});

test("in-app update notices permit only classified public metadata", () => {
  const notice = {version:1,type:"app-update-notice",kind:"incompatible",updateVersion:"1.0.3",
    error:"update-incompatible",manual:false};
  assert.equal(validHostMessage(notice),true);
  assert.equal(validHostMessage({...notice,kind:'available',error:''}),true);
  for(const fields of [{url:"https://evil.test"},{kind:"unknown"},{updateVersion:"<script>"},
    {error:"secret\nvalue"},{manual:"yes"},{token:"private"}])
    assert.equal(validHostMessage({...notice,...fields}),false);
});

test("account bridge permits only bounded profile commands, never tokens or URLs", () => {
  const sent = [];
  const bridge = createBridge({ postMessage: (message) => sent.push(message) });
  assert.equal(typeof bridge.account, "function");
  for (const action of ["snapshot", "login", "cancelLogin", "logout", "refresh", "chooseAvatar"])
    assert.equal(bridge.account(action), true);
  assert.equal(bridge.account("updateName", { value: "Lume user" }), true);
  assert.equal(bridge.account("updateName", { value: "\u6c88\u57ce\ud83c\udf1f" }), true);
  for (const value of ["", "a", "a".repeat(33), "a\nb", "a\u0000b", "a\ud800"])
    assert.equal(bridge.account("updateName", { value }), false);
  for (const fields of [{ value: "ab", token: "secret" }, { path: "C:\\avatar.png" },
    { redirectUri: "https://example.com" }, { accessToken: "secret" }])
    assert.equal(bridge.account("login", fields), false);
  assert.equal(bridge.account("open", { url: "https://example.com" }), false);
  assert.deepEqual(sent[0], { version: 1, scope: "account", action: "snapshot" });
  assert.equal(sent.length, 8);
});

test("application settings bridge validates exact types and rejects arbitrary update locations", () => {
  const sent = [];
  const bridge = createBridge({ postMessage: (message) => sent.push(message) });
  assert.equal(typeof bridge.applicationSettings, "function");
  for (const action of ["snapshot", "checkUpdate", "downloadUpdate", "installUpdate"])
    assert.equal(bridge.applicationSettings(action), true);
  assert.equal(bridge.applicationSettings("language", { value: "en" }), true);
  assert.equal(bridge.applicationSettings("balancedMode", { value: false }), true);
  assert.equal(bridge.applicationSettings("autostart", { value: true }), true);
  assert.equal(bridge.applicationSettings("language", { value: "en-US" }), false);
  assert.equal(bridge.applicationSettings("balancedMode", { value: "true" }), false);
  assert.equal(bridge.applicationSettings("autostart", { value: 1 }), false);
  assert.equal(bridge.applicationSettings("installUpdate", { path: "C:\\anything.exe" }), false);
  assert.equal(bridge.applicationSettings("checkUpdate", { url: "https://example.com" }), false);
  assert.equal(sent.length, 7);
  assert.deepEqual(sent[0], { version: 1, scope: "app-settings", action: "snapshot" });
});

test("diagnostics bridge accepts bounded native snapshots and only snapshot or clear commands", () => {
  const sent=[];
  const bridge=createBridge({postMessage:message=>sent.push(message)});
  assert.equal(bridge.diagnostics("snapshot"),true);
  assert.equal(bridge.diagnostics("clear"),true);
  assert.equal(bridge.diagnostics("export"),false);
  assert.deepEqual(sent,[
    {version:1,scope:"diagnostics",action:"snapshot"},
    {version:1,scope:"diagnostics",action:"clear"},
  ]);
  const state={version:1,type:"diagnostics-state",schemaVersion:1,revision:4,available:true,storage:"ready",
    generatedAt:1700000000000,events:[{at:1700000000000,firstAt:1699999999000,repeatCount:2,module:"dock",
      kind:"effect-mismatch",action:"apply",status:"degraded",error:"dock-layers-hidden",
      expected:"layers-visible",actual:"hidden"}],
    health:{dock:{at:1700000000000,status:"ready",error:"",enabled:true,running:true,layersVisible:false}},
    mismatches:[{component:"dock",expected:"layers-visible",actual:"hidden",confidence:"confirmed",reason:"dock-layers-hidden"}],
  };
  assert.equal(validHostMessage(state),true);
  assert.equal(validHostMessage({...state,token:"secret"}),false);
  assert.equal(validHostMessage({...state,events:[{...state.events[0],path:"C:\\private\\diagnostics.json"}]}),false);
  assert.equal(validHostMessage({...state,health:{dock:{...state.health.dock,extra:"not allowed"}}}),false);
});

test("public account snapshots reject secrets, invalid avatar schemes, and malformed states", () => {
  assert.equal(validHostMessage(account()), true);
  const profile = { id: "test-user", displayName: "Test user", avatarUrl: "", vip: false, vipExpiresAt: "" };
  assert.equal(validHostMessage(account({ status: "signed-in", profile })), true);
  assert.equal(validHostMessage(account({ status: "signed-in", profile: {
    ...profile, avatarUrl: "data:image/png;base64,aGVsbG8=",
  } })), true);
  for (const avatarUrl of ["javascript:alert(1)", "file:///C:/file.png",
    "https://untrusted.example/avatar.png", "data:image/svg+xml;base64,PHN2Zz4="])
    assert.equal(validHostMessage(account({ status: "signed-in", profile: { ...profile, avatarUrl } })), false);
  for (const fields of [{ accessToken: "secret" }, { refreshToken: "secret" },
    { state: "transaction" }, { revision: -1 }, { status: "unknown" }, { busy: 1 }])
    assert.equal(validHostMessage(account(fields)), false);
  assert.equal(validHostMessage(account({ status: "signed-in", profile: null })), false);
  assert.equal(validHostMessage(account({ profile })), false);
  assert.equal(validHostMessage(account({ status: "signed-in", profile: { ...profile, token: "secret" } })), false);
});

test("application snapshots require complete confirmed fields and finite progress", () => {
  assert.equal(validHostMessage(settings()), true);
  assert.equal(validHostMessage(settings({ language: "en", busy: true })), true);
  for (const fields of [{ language: "de" }, { balancedMode: "true" }, { autostart: null },
    { revision: NaN }, { revision: Number.MAX_SAFE_INTEGER + 1 }, { arbitrary: true }])
    assert.equal(validHostMessage(settings(fields)), false);
  for (const update of [{ status: "idle", version: "", progress: -1 },
    { status: "downloading", version: "1.0.2.2", progress: 101 },
    { status: "ready", version: "1.0.2.2", progress: 100, path: "C:\\file.exe" }])
    assert.equal(validHostMessage(settings({ update })), false);
  assert.equal(validHostMessage(settings({
    update: { status: "incompatible", version: "1.0.3", progress: 0 },
  })), true);
});

test("settings navigation returns to the prior page without resetting wallpaper state", () => {
  let state = initialState();
  state = reduce(state, { type: "navigate", view: "wallpaperOfficial" });
  state = reduce(state, { type: "wallpaper-page", page: 3 });
  state = reduce(state, { type: "navigate", view: "applicationSettings" });
  assert.equal(state.view, "applicationSettings");
  state = reduce(state, { type: "back" });
  assert.equal(state.view, "wallpaperOfficial");
  assert.equal(state.wallpaperBrowse.official.page, 3);
});

test("disconnected and throwing hosts fail new commands without claiming persistence", () => {
  for (const bridge of [createBridge(null), createBridge({ postMessage() { throw new Error("closed"); } })]) {
    assert.equal(bridge.account("login"), false);
    assert.equal(bridge.applicationSettings("language", { value: "en" }), false);
  }
});
