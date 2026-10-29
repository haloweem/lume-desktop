const STORAGE_KEY = "lume-diagnostics-v1";
export const DIAGNOSTICS_ENABLED = false;
const NOTE_KEY = "lume-diagnostic-note-v1";
const HEALTH_KEY = "lume-diagnostic-health-v1";
const NATIVE_KEY = "lume-diagnostic-native-v1";
const MAX_EVENTS = 80;
const MAX_NATIVE_EVENTS = 80;
const MAX_COMPONENTS = 64;
const MAX_TEXT = 240;

const text = (value, max = 240) => {
  if (typeof value !== "string") return "";
  const cleaned = value
    .replace(/[\u0000-\u001f\u007f-\u009f]/g, " ")
    .replace(/(?:https?|file|data):\/\/\S+/gi, "[redacted]")
    .replace(/\b[A-Za-z]:\\[^\s]+/g, "[redacted]")
    .replace(/\b(?:access[_-]?token|refresh[_-]?token|password|passwd|secret|cookie|authorization|token)\s*[:=]\s*(?:bearer\s+)?[^\s,;]+/gi, "[redacted]")
    .replace(/\bbearer\s+[A-Za-z0-9._~+/-]{8,}/gi, "[redacted]")
    .replace(/\b(?:access[_-]?token|refresh[_-]?token|password|passwd|secret|cookie|authorization|token)\b/gi, "[redacted]");
  return cleaned.slice(0, max);
};

const nonNegativeInteger = value => Number.isSafeInteger(value) && value >= 0;
const nativeKinds = ["operation", "error", "effect-mismatch"];
const nativeStorage = ["", "ready", "unavailable", "failed"];

function safeStorage(storage = globalThis.localStorage) {
  return storage && typeof storage.getItem === "function" &&
    typeof storage.setItem === "function" && typeof storage.removeItem === "function" ? storage : null;
}

function readJson(storage, key, fallback) {
  try {
    const value = JSON.parse(storage?.getItem(key) || "");
    return value ?? fallback;
  } catch {
    return fallback;
  }
}

function sanitizeCode(value, max = 96) {
  const result = text(value, max);
  return result && /^[A-Za-z0-9_.:-]+$/.test(result) ? result : "";
}

function eventDetails(message) {
  if (!message || typeof message !== "object") return {};
  const details = {};
  for (const key of [
    "error", "catalogError", "loadStatus", "catalogStatus", "status", "result",
    "materialStatus", "mode", "page", "section", "scope", "operationId",
    "module", "kind", "action", "stage", "expected", "actual", "confidence",
    "reason", "state",
  ]) {
    if (typeof message[key] === "string" && message[key]) details[key] = text(message[key], MAX_TEXT);
  }
  if (message.download && typeof message.download === "object" &&
      typeof message.download.status === "string" && message.download.status !== "idle")
    details.downloadStatus = sanitizeCode(message.download.status);
  return details;
}

const healthFields = {
  "dock-state": ["enabled", "running", "pinned", "added", "visibleItems", "layersVisible", "materialStatus", "loadStatus", "error"],
  "taskbar-state": ["enabled", "running", "transparentTaskbar", "autoHideTaskbar", "applying", "pendingRecovery", "taskbars", "mode", "error", "loadStatus"],
  "wallpaper-state": ["status", "loadStatus", "catalogStatus", "catalogError", "playerAvailable", "pauseFullscreen"],
  "icons-state": ["enabled", "running", "desktops", "autoHidden", "pendingRestores", "loadStatus", "error"],
  "organizer-state": ["enabled", "running", "count", "mode", "layout", "loadStatus", "error"],
  "weather-state": ["enabled", "running", "loading", "hasWeather", "weatherError", "loadStatus", "error"],
  "calendar-settings": ["saved", "materialStatus", "error"],
  "performance-state": ["enabled", "running", "materialStatus", "loadStatus", "error"],
  "almanac-state": ["enabled", "running", "materialStatus", "loadStatus", "error"],
  "minimize-state": ["enabled", "running", "hosts", "hooks", "pendingCleanup", "loadStatus", "error"],
};

function healthSnapshot(message) {
  const fields = healthFields[message?.type];
  if (!fields) return null;
  const value = {};
  for (const key of fields) {
    const item = message[key];
    if (typeof item === "string") value[key] = text(item, MAX_TEXT);
    else if (typeof item === "boolean" || nonNegativeInteger(item)) value[key] = item;
  }
  return value;
}

function clone(value) {
  try { return structuredClone(value); } catch { return JSON.parse(JSON.stringify(value)); }
}

function defaultNativeSnapshot() {
  return {
    version: 1, type: "diagnostics-state", schemaVersion: 1, revision: 0,
    available: false, storage: "unavailable", generatedAt: 0,
    events: [], health: {}, mismatches: [], faults: [],
  };
}

function validNativeEvent(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const allowed = ["at", "firstAt", "repeatCount", "operationId", "module", "kind", "action", "status", "error", "expected", "actual"];
  if (Object.keys(value).some(key => !allowed.includes(key))) return false;
  if (!nonNegativeInteger(value.at) || !nativeKinds.includes(value.kind)) return false;
  for (const key of ["operationId", "module", "action", "status", "error", "expected", "actual"]) {
    if (value[key] !== undefined && typeof value[key] !== "string") return false;
    if (typeof value[key] === "string" && value[key].length > 240) return false;
  }
  if (value.firstAt !== undefined && !nonNegativeInteger(value.firstAt)) return false;
  if (value.repeatCount !== undefined && (!nonNegativeInteger(value.repeatCount) || value.repeatCount < 1 || value.repeatCount > 0xffffffff)) return false;
  if (value.status !== undefined && !/^[A-Za-z0-9_.:-]{0,96}$/.test(value.status)) return false;
  return true;
}

function sanitizeNativeEvent(value) {
  if (!validNativeEvent(value)) return null;
  const result = {at: value.at, kind: value.kind, firstAt: value.firstAt ?? value.at,
    repeatCount: value.repeatCount ?? 1};
  for (const key of ["operationId", "module", "action", "status", "error", "expected", "actual"]) {
    if (typeof value[key] === "string" && value[key]) result[key] = key === "error" || key === "status"
      ? sanitizeCode(value[key]) || text(value[key], MAX_TEXT)
      : text(value[key], MAX_TEXT);
  }
  return result;
}

function validNativeHealth(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)) return false;
  const allowed = ["at", "status", "result", "error", "action", "mode", "layout", "loadStatus",
    "catalogStatus", "catalogError", "weatherError", "materialStatus",
    "enabled", "running", "visible", "loaded", "layersVisible", "transparentTaskbar", "autoHideTaskbar",
    "applying", "pendingRecovery", "playerAvailable", "pauseFullscreen", "loading", "hasWeather",
    "visibleItems", "pinned", "added", "taskbars", "desktops", "autoHidden", "pendingRestores",
    "count", "hosts", "hooks", "pendingCleanup"];
  if (Object.keys(value).some(key => !allowed.includes(key))) return false;
  if (value.at !== undefined && !nonNegativeInteger(value.at)) return false;
  for (const key of ["status", "result", "error", "action", "mode", "layout", "loadStatus",
    "catalogStatus", "catalogError", "weatherError", "materialStatus"]) {
    if (value[key] !== undefined && typeof value[key] !== "string") return false;
    if (typeof value[key] === "string" && value[key].length > MAX_TEXT) return false;
  }
  if (["status", "result", "error", "action", "mode", "layout", "loadStatus",
    "catalogStatus", "catalogError", "weatherError", "materialStatus"].some(key =>
      value[key] !== undefined && !/^[A-Za-z0-9_.:-]{0,96}$/.test(value[key]))) return false;
  for (const key of ["enabled", "running", "visible", "loaded", "layersVisible", "transparentTaskbar",
      "autoHideTaskbar", "applying", "pendingRecovery", "playerAvailable", "pauseFullscreen", "loading", "hasWeather"])
    if (value[key] !== undefined && typeof value[key] !== "boolean") return false;
  for (const key of ["visibleItems", "pinned", "added", "taskbars", "desktops", "autoHidden", "pendingRestores",
      "count", "hosts", "hooks", "pendingCleanup"])
    if (value[key] !== undefined && !nonNegativeInteger(value[key])) return false;
  return true;
}

function sanitizeNativeHealth(value) {
  if (!validNativeHealth(value)) return null;
  const result = {};
  if (nonNegativeInteger(value.at)) result.at = value.at;
  for (const key of ["status", "result", "error", "action", "mode", "layout", "loadStatus",
    "catalogStatus", "catalogError", "weatherError", "materialStatus"]) {
    if (typeof value[key] === "string" && value[key])
      result[key] = key === "error" ? sanitizeCode(value[key]) || text(value[key], MAX_TEXT) : text(value[key], MAX_TEXT);
  }
  for (const key of ["enabled", "running", "visible", "loaded", "layersVisible", "transparentTaskbar",
      "autoHideTaskbar", "applying", "pendingRecovery", "playerAvailable", "pauseFullscreen", "loading", "hasWeather"])
    if (typeof value[key] === "boolean") result[key] = value[key];
  for (const key of ["visibleItems", "pinned", "added", "taskbars", "desktops", "autoHidden", "pendingRestores",
      "count", "hosts", "hooks", "pendingCleanup"])
    if (nonNegativeInteger(value[key])) result[key] = value[key];
  return result;
}

function nativeMismatches(snapshot) {
  const mismatches = [];
  for (const event of snapshot.events || []) {
    if (event.kind !== "effect-mismatch") continue;
    mismatches.push({
      component: text(event.module || "unknown", 64),
      expected: text(event.expected || "", MAX_TEXT),
      actual: text(event.actual || "", MAX_TEXT),
      confidence: "confirmed",
      ...(event.error ? {reason: text(event.error, MAX_TEXT)} : {}),
    });
  }
  return mismatches;
}

function sanitizeNativeSnapshot(message) {
  const base = defaultNativeSnapshot();
  if (!message || typeof message !== "object" ||
      message.version !== 1 || message.type !== "diagnostics-state" ||
      message.schemaVersion !== 1 || !nonNegativeInteger(message.revision) ||
      !Array.isArray(message.events) || message.events.length > MAX_NATIVE_EVENTS ||
      !message.health || typeof message.health !== "object" || Array.isArray(message.health)) return base;
  const events = message.events.map(sanitizeNativeEvent);
  if (events.some(event => !event)) return base;
  const health = {};
  const names = Object.keys(message.health).slice(0, MAX_COMPONENTS);
  if (names.some(name => !/^[A-Za-z0-9_.:-]{1,64}$/.test(name) || !validNativeHealth(message.health[name]))) return base;
  for (const name of names) health[name] = sanitizeNativeHealth(message.health[name]);
  const result = {
    ...base,
    version: 1, type: "diagnostics-state", schemaVersion: 1,
    revision: message.revision, events, health,
    available: message.available === undefined ? true : message.available === true,
    storage: message.storage === undefined ? "ready" : (nativeStorage.includes(message.storage) ? message.storage : "failed"),
    generatedAt: nonNegativeInteger(message.generatedAt) ? message.generatedAt : 0,
  };
  result.mismatches = Array.isArray(message.mismatches) ? message.mismatches.slice(0, MAX_EVENTS).filter(item =>
    item && typeof item === "object" && typeof item.component === "string" &&
    typeof item.expected === "string" && typeof item.actual === "string" &&
    ["confirmed", "suspected", "unknown"].includes(item.confidence)).map(item => ({
      component: text(item.component, 64), expected: text(item.expected, MAX_TEXT),
      actual: text(item.actual, MAX_TEXT), confidence: item.confidence,
      ...(item.reason ? {reason: text(item.reason, MAX_TEXT)} : {}),
    })) : nativeMismatches(result);
  result.faults = Array.isArray(message.faults) ? message.faults.slice(0, MAX_EVENTS).filter(item =>
    item && typeof item === "object" && typeof item.component === "string" &&
    typeof item.code === "string" && ["info", "warning", "error"].includes(item.severity)).map(item => ({
      component: text(item.component, 64), code: sanitizeCode(item.code) || text(item.code, 64),
      severity: item.severity, ...(item.message ? {message: text(item.message, MAX_TEXT)} : {}),
      ...(nonNegativeInteger(item.firstAt) ? {firstAt: item.firstAt} : {}),
      ...(nonNegativeInteger(item.lastAt) ? {lastAt: item.lastAt} : {}),
      ...(nonNegativeInteger(item.count) ? {count: Math.min(item.count, 0xffffffff)} : {}),
    })) : result.events.filter(event => event.kind === "error").map(event => ({
      component: text(event.module || "unknown", 64),
      code: sanitizeCode(event.error || event.status) || "native-error",
      severity: "error",
      ...(event.error ? {message: text(event.error, MAX_TEXT)} : {}),
      firstAt: event.firstAt,
      lastAt: event.at,
      count: event.repeatCount,
    }));
  return result;
}

function localMismatches(type, snapshot) {
  if (!snapshot || snapshot.enabled !== true) return [];
  const component = type.replace(/-state$/, "");
  const result = [];
  if (type === "dock-state" && snapshot.running === true && snapshot.layersVisible === false) {
    result.push({component: "dock", expected: "layers-visible", actual: "hidden", confidence: "confirmed"});
  }
  if (type === "taskbar-state" && snapshot.enabled === true && snapshot.transparentTaskbar === true &&
      snapshot.taskbars > 0 && snapshot.applying !== true && snapshot.pendingRecovery !== true && snapshot.running === false) {
    result.push({component: "taskbar", expected: "transparent-taskbar-active", actual: "not-running", confidence: "confirmed"});
  }
  return result;
}

function mismatchKey(item) {
  return [item.component, item.expected, item.actual].join("|");
}

function nativeEventMatchesLocal(nativeEvent, localEvent) {
  const component=localEvent?.type?.replace(/-state$/,'');
  const details=localEvent?.details||{};
  if(!component||component!==(nativeEvent.module||''))return false;
  if(nativeEvent.kind==='effect-mismatch')
    return details.expected===nativeEvent.expected&&details.actual===nativeEvent.actual;
  if(nativeEvent.kind!=='error')return false;
  const localCode=details.error||details.catalogError||
    (details.loadStatus==='failed'?'load-failed':'')||details.status||'';
  return !!localCode&&(nativeEvent.error===localCode||nativeEvent.status===localCode);
}

function defaultStorage() {
  try { return globalThis.localStorage; } catch { return null; }
}

function sanitizeHealthMap(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)) return {};
  const result = {};
  for (const [type, snapshot] of Object.entries(value).slice(-MAX_COMPONENTS)) {
    if (!Object.hasOwn(healthFields, type) || !snapshot || typeof snapshot !== "object" || Array.isArray(snapshot)) continue;
    const clean = {};
    const allowed = new Set(["at", ...healthFields[type]]);
    for (const [key, item] of Object.entries(snapshot).slice(0, 32)) {
      if (!allowed.has(key)) continue;
      if (typeof item === "string") clean[key] = text(item, MAX_TEXT);
      else if (typeof item === "boolean" || nonNegativeInteger(item)) clean[key] = item;
    }
    result[type] = clean;
  }
  return result;
}

// Normal states (empty library, restoring account, cached wallpaper catalog,
// a superseded taskbar request) are not failures and are not kept as events.
function meaningfulDetails(type, details) {
  if (details.kind === "effect-mismatch") return true;
  const error = type === "taskbar-state" && details.error === "cancelled" ? "" : details.error;
  const catalogError = details.catalogError === "WALLPAPER_CATALOG_CACHED" ? "" : details.catalogError;
  return Boolean(error || catalogError ||
    ["failed", "error", "blocked", "degraded"].includes(details.status) ||
    ["failed", "recovered"].includes(details.loadStatus) ||
    details.result === "failed" || details.materialStatus === "failed");
}

export function createDiagnosticsRecorder({
  enabled = true,
  storage = defaultStorage(),
  now = () => Date.now(),
} = {}) {
  if (!enabled) return {
    record() {}, list: () => [], health: () => ({}), native: defaultNativeSnapshot,
    mismatches: () => [], diagnosticCount: () => 0, clear() {},
    note: () => ({}), saveNote: () => ({}), clearNote() {},
  };
  const store = safeStorage(storage);
  let events = readJson(store, STORAGE_KEY, []);
  if (!Array.isArray(events)) events = [];
  events = events.filter(event => event && nonNegativeInteger(event.at) && typeof event.type === "string")
    .slice(-MAX_EVENTS).map(event => ({
      at: event.at, firstAt: nonNegativeInteger(event.firstAt) ? event.firstAt : event.at,
      type: text(event.type, 64), details: eventDetails(event.details || {}),
      repeatCount: nonNegativeInteger(event.repeatCount) ? Math.min(event.repeatCount, 0xffffffff) : 1,
    })).filter(event => meaningfulDetails(event.type, event.details));
  let health = sanitizeHealthMap(readJson(store, HEALTH_KEY, {}));
  // Native revisions belong to the connected process; a cached revision can
  // otherwise prevent a restarted or cleared native collector from updating.
  let nativeSnapshot = defaultNativeSnapshot();
  const localMismatchMap = new Map();
  const persist = () => {
    try { store?.setItem(STORAGE_KEY, JSON.stringify(events.slice(-MAX_EVENTS))); } catch { /* optional storage */ }
  };
  const persistHealth = () => {
    try { store?.setItem(HEALTH_KEY, JSON.stringify(health)); } catch { /* optional storage */ }
  };
  const persistNative = () => {
    try { store?.setItem(NATIVE_KEY, JSON.stringify(nativeSnapshot)); } catch { /* optional storage */ }
  };
  const addEvent = (type, details, at = now()) => {
    const cleanDetails = eventDetails(details);
    const fingerprint = `${type}|${JSON.stringify(cleanDetails)}`;
    const previous = events.at(-1);
    const previousFingerprint = previous && `${previous.type}|${JSON.stringify(previous.details)}`;
    if (previous && fingerprint === previousFingerprint) {
      if (at < previous.at || at - previous.at < 60000) return;
      previous.at = at;
      previous.repeatCount = Math.min((previous.repeatCount || 1) + 1, 0xffffffff);
      persist();
      return;
    }
    events.push({at, firstAt: at, type: text(type, 64), details: cleanDetails, repeatCount: 1});
    events = events.slice(-MAX_EVENTS);
    persist();
  };
  return {
    record(message) {
      if (!message || typeof message !== "object" || typeof message.type !== "string") return;
      if (message.type === "diagnostics-state") {
        const snapshot = sanitizeNativeSnapshot(message);
        if (snapshot.revision >= nativeSnapshot.revision) {
          nativeSnapshot = snapshot;
          persistNative();
        }
        return;
      }
      const snapshot = healthSnapshot(message);
      if (snapshot) {
        const at = now();
        health[message.type] = {at, ...snapshot};
        persistHealth();
        const detected = localMismatches(message.type, snapshot);
        const current = new Set(detected.map(mismatch => mismatchKey(mismatch)));
        for (const mismatch of detected) {
          const key = mismatchKey(mismatch);
          if (!localMismatchMap.has(key)) addEvent(message.type, {
            ...eventDetails(message), kind: "effect-mismatch", module: mismatch.component,
            expected: mismatch.expected, actual: mismatch.actual, confidence: mismatch.confidence,
            reason: mismatch.reason || "",
          }, at);
          localMismatchMap.set(key, mismatch);
        }
        for (const key of [...localMismatchMap.keys()]) {
          if (key.startsWith(`${message.type.replace(/-state$/, "")}|`) && !current.has(key)) localMismatchMap.delete(key);
        }
      }
      const details = eventDetails(message);
      if (meaningfulDetails(message.type, details)) addEvent(message.type, details);
    },
    list() {
      return events.map(event => ({
        at: event.at, firstAt: event.firstAt, type: event.type,
        details: {...event.details}, repeatCount: event.repeatCount,
      }));
    },
    health() { return clone(health); },
    native() { return clone(nativeSnapshot); },
    mismatches() {
      return [...(nativeSnapshot.available ? [] : localMismatchMap.values()), ...(nativeSnapshot.mismatches || [])]
        .filter((item, index, items) => index === items.findIndex(other => mismatchKey(other) === mismatchKey(item)))
        .map(item => ({...item}));
    },
    diagnosticCount() {
      const keys=new Set();
      const moduleName=type=>type.replace(/-state$/,'');
      for(const event of events) {
        const details=event.details||{};
        if(details.kind==='effect-mismatch')
          keys.add([details.module||moduleName(event.type),details.kind,details.expected||'',details.actual||''].join('|'));
        else keys.add([moduleName(event.type),details.error||details.catalogError||details.status||JSON.stringify(details)].join('|'));
      }
      for(const event of nativeSnapshot.events||[]) {
        if(events.some(local=>nativeEventMatchesLocal(event,local)))continue;
        if(event.kind==='effect-mismatch')
          keys.add([event.module||'unknown',event.kind,event.expected||'',event.actual||''].join('|'));
        else keys.add([event.module||'unknown',event.error||event.status||event.kind].join('|'));
      }
      return keys.size;
    },
    clear() {
      events = [];
      health = {};
      localMismatchMap.clear();
      nativeSnapshot = defaultNativeSnapshot();
      try {
        store?.removeItem(STORAGE_KEY);
        store?.removeItem(HEALTH_KEY);
        store?.removeItem(NATIVE_KEY);
      } catch { /* optional storage */ }
    },
    note() {
      const value = readJson(store, NOTE_KEY, {});
      return value && typeof value === "object" ? {
        summary: text(value.summary),
        steps: text(value.steps),
        expected: text(value.expected),
        actual: text(value.actual),
        occurredAt: text(value.occurredAt),
      } : {summary: "", steps: "", expected: "", actual: "", occurredAt: ""};
    },
    saveNote(note) {
      const value = {
        summary: text(note?.summary),
        steps: text(note?.steps),
        expected: text(note?.expected),
        actual: text(note?.actual),
        occurredAt: text(note?.occurredAt),
      };
      try { store?.setItem(NOTE_KEY, JSON.stringify(value)); } catch { /* optional storage */ }
      return value;
    },
    clearNote() {
      try { store?.removeItem(NOTE_KEY); } catch { /* optional storage */ }
    },
  };
}

export function buildDiagnosticReport({
  version = "1.0.8",
  language = "zh-CN",
  view = "",
  events = [],
  health = {},
  native = null,
  mismatches = [],
  note = {},
  userAgent = globalThis.navigator?.userAgent || "",
  platform = globalThis.navigator?.platform || "",
  screen = globalThis.screen,
  generatedAt = new Date().toISOString(),
} = {}) {
  const nativeReport = sanitizeNativeSnapshot(native);
  const localEvents = Array.isArray(events) ? events.slice(-MAX_EVENTS)
    .filter(local=>!nativeReport.events.some(event=>nativeEventMatchesLocal(event,local))).map(event => ({
    at: nonNegativeInteger(event?.at) ? new Date(event.at).toISOString() : "",
    firstAt: nonNegativeInteger(event?.firstAt) ? new Date(event.firstAt).toISOString() : "",
    type: text(event?.type, 64),
    details: event?.details && typeof event.details === "object" ? eventDetails(event.details) : {},
    repeatCount: nonNegativeInteger(event?.repeatCount) ? event.repeatCount : 1,
  })) : [];
  const reportMismatches = [...(Array.isArray(mismatches) ? mismatches : []), ...(nativeReport.mismatches || [])]
    .filter(item => item && typeof item === "object" && typeof item.component === "string")
    .map(item => ({
      component: text(item.component, 64), expected: text(item.expected, MAX_TEXT),
      actual: text(item.actual, MAX_TEXT),
      confidence: ["confirmed", "suspected", "unknown"].includes(item.confidence) ? item.confidence : "unknown",
      ...(item.reason ? {reason: text(item.reason, MAX_TEXT)} : {}),
    }))
    .filter((item, index, items) => index === items.findIndex(other => mismatchKey(other) === mismatchKey(item)));
  return {
    format: "lume-diagnostic-v1",
    generatedAt: text(generatedAt, 40),
    app: {name: "LumeDesktop", version: text(version, 64), language: ["zh-CN", "en"].includes(language) ? language : "zh-CN", view: text(view, 64)},
    environment: {
      platform: text(platform),
      userAgent: text(userAgent),
      screen: screen ? {
        width: Number.isFinite(screen.width) ? screen.width : null,
        height: Number.isFinite(screen.height) ? screen.height : null,
        devicePixelRatio: Number.isFinite(globalThis.devicePixelRatio) ? globalThis.devicePixelRatio : null,
      } : null,
    },
    issue: {
      summary: text(note.summary), steps: text(note.steps), expected: text(note.expected),
      actual: text(note.actual), occurredAt: text(note.occurredAt),
    },
    recentEvents: localEvents,
    health: sanitizeHealthMap(health),
    native: nativeReport,
    mismatches: reportMismatches,
    privacy: "仅包含应用版本、运行环境摘要、错误类型和用户主动填写的问题描述；不会自动上传账号、令牌、Cookie、完整文件路径、URL 或壁纸内容。",
  };
}

export function diagnosticReportText(report) {
  return JSON.stringify(report, null, 2);
}

export {MAX_EVENTS, sanitizeNativeSnapshot};
