import {
  validAccountCommand, validAccountState,
  validApplicationSettingsCommand, validApplicationSettingsState,
  validUpdateNotice,
} from "./settings-account-protocol.js";
import {validMusicPreferencesCommand,validMusicPreferencesMessage} from './music-preferences.js';
import {validMusicBackgroundCommand,validMusicBackgroundMessage,validMusicBackgroundCatalogMessage} from './music-background.js';
import {validMusicProviderAccountCommand,validMusicProviderAccountMessage} from './music-provider-account.js';
import {validMusicFontCommand,validMusicFontMessage} from './music-font-query.js';
import {validMusicFontAssetCommand,validMusicFontAssetMessage} from './music-font-asset-query.js';
import {validMusicOnlineCommand,validMusicOnlineMessage} from './music-online.js';
import {validMusicLibraryCommand,validMusicLibraryMessage} from './music-library.js';
import {validMusicDesktopCommand,validMusicDesktopMessage} from './music-desktop.js';
import {validNotesCommand,validNotesMessage} from './notes.js';
import {validRadialShortcut,validRadialState} from './radial.js';
import {validDockState} from './dock.js';
import {validIslandState} from './dynamic-island.js';
const styles = ["welfare", "mond", "insideDream"];
const themes = ["spacious", "split", "todo"];
const materials = ["liquid-glass", "frosted-glass"];
// The calendar also has an opaque "solid" panel.
const calendarMaterials = [...materials, "solid"];
const cities = ["auto","beijing","shanghai","guangzhou","shenzhen","hangzhou","chengdu","wuhan","nanjing"];
const taskbarErrors = ["","load","save","busy","unsupported","missing-payload","payload-integrity","conflict",
  "discover","read-appearance","apply-appearance","read-auto-hide","apply-auto-hide","core-start",
  "core-exited","core-stop","cancelled","recovery","watch","system-effects-disabled","runtime","read-visibility","apply-visibility"];
const minimizeErrors = ["","save","rollback","invalid-settings","missing-host","already-running","ipc",
  "host-start","hook-install","stop-timeout","host-exited","unsupported-architecture","runtime"];
const integerIn = (value,min,max) => Number.isSafeInteger(value) && value>=min && value<=max;
const exactKeys = (value, required, optional = []) => value && typeof value === "object" && !Array.isArray(value) &&
  required.every(key => Object.hasOwn(value,key)) && Object.keys(value).every(key => required.includes(key) || optional.includes(key));
const wallpaperId = value => {
  if (typeof value !== "string" || value.length < 1 || value.length > 96 ||
    value === "." || value === ".." || value.endsWith(".") ||
    !/^[A-Za-z0-9_.-]+$/.test(value)) return false;
  const base = value.slice(0, value.indexOf(".") === -1 ? value.length : value.indexOf(".")).toLowerCase();
  return !["con", "prn", "aux", "nul"].includes(base) && !/^(?:com|lpt)[1-9]$/.test(base);
};
const wallpaperDisplay = value => typeof value === "string" && /^[1-9]\d{0,19}$/.test(value) &&
  BigInt(value) <= 18446744073709551615n;
export function validWallpaperMediaUrl(value, allowEmpty = false) {
  if (typeof value !== "string") return false;
  if (value === "") return allowEmpty;
  const lease=value.match(/^http:\/\/127\.0\.0\.1:([1-9]\d{0,4})\/[a-f0-9]{64}\/media\.(?:mp4|webm|mov|m4v|mkv|avi|jpg|jpeg|png|webp|gif|bmp|avif)$/);
  if (lease) return Number(lease[1])<=65535;
  try {
    const url = new URL(value);
    return url.protocol === "https:" && !url.username && !url.password &&
      ["https://lume-wallpaper.local","https://static.lumedesktop.com",
        "https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com"].includes(url.origin) &&
      url.pathname !== "/" && !/[\u0000-\u0020\\]/.test(value);
  } catch {
    return false;
  }
}
function validWallpaperState(m) {
  if (!exactKeys(m,["version","type","revision","status","error","loadStatus","playerAvailable",
    "catalogStatus","catalogError","displays","download","pauseFullscreen"],["catalog","library","likedIds"])) return false;
  const text = value => typeof value === "string";
  const count = value => integerIn(value,0,Number.MAX_SAFE_INTEGER);
  if (!count(m.revision) || !["idle","busy"].includes(m.status) ||
    !["fresh","loaded","recovered","failed"].includes(m.loadStatus) ||
    !["idle","loading","ready","failed"].includes(m.catalogStatus) ||
    !text(m.error) || !text(m.catalogError) || typeof m.playerAvailable !== "boolean" ||
    typeof m.pauseFullscreen !== "boolean") return false;
  const unique = (items, valid) => Array.isArray(items) && items.every(valid) &&
    new Set(items.map(item=>item.id)).size === items.length;
  const media = item => wallpaperId(item.id) && text(item.title) &&
    ["video","image"].includes(item.kind) && validWallpaperMediaUrl(item.thumbnail,true) && count(item.bytes);
  if (Object.hasOwn(m,"likedIds") && (!Array.isArray(m.likedIds) ||
    !m.likedIds.every(wallpaperId) || new Set(m.likedIds).size!==m.likedIds.length)) return false;
  if (Object.hasOwn(m,"catalog") && !unique(m.catalog, item => exactKeys(item,["id","title","kind","thumbnail","url","width","height","bytes","libraryId"],["heroThumbnail","homeOrder"]) &&
    media(item) && validWallpaperMediaUrl(item.url,wallpaperId(item.libraryId)) && count(item.width) && count(item.height) &&
    (!Object.hasOwn(item,"heroThumbnail") || validWallpaperMediaUrl(item.heroThumbnail,true)) &&
    (!Object.hasOwn(item,"homeOrder") || integerIn(item.homeOrder,0,19999)) &&
    (item.libraryId === "" || wallpaperId(item.libraryId)))) return false;
  if (Object.hasOwn(m,"library") && !unique(m.library, item => exactKeys(item,["id","title","kind","thumbnail","url","bytes","missing"],["official","width","height","heroThumbnail","homeOrder"]) &&
    media(item) && typeof item.missing === "boolean" && validWallpaperMediaUrl(item.url,true) &&
    (!Object.hasOwn(item,"heroThumbnail") || validWallpaperMediaUrl(item.heroThumbnail,true)) &&
    (!Object.hasOwn(item,"homeOrder") || integerIn(item.homeOrder,0,19999)) &&
    (!Object.hasOwn(item,"official") || typeof item.official==="boolean") &&
    (!Object.hasOwn(item,"width") || count(item.width)) &&
    (!Object.hasOwn(item,"height") || count(item.height)))) return false;
  if (!unique(m.displays, display => exactKeys(display,["id","name","width","height","primary","wallpaperId",
    "state","autoPaused","fit","volume","error"]) && wallpaperDisplay(display.id) && text(display.name) &&
    count(display.width) && count(display.height) && typeof display.primary === "boolean" &&
    (display.wallpaperId === "" || wallpaperId(display.wallpaperId)) &&
    ["stopped","starting","playing","paused","failed"].includes(display.state) &&
    typeof display.autoPaused === "boolean" && ["cover","contain"].includes(display.fit) &&
    integerIn(display.volume,0,100) && text(display.error))) return false;
  return validWallpaperDownload(m.download);
}
function validWallpaperDownload(download) {
  return exactKeys(download,["id","status","received","total","error"]) &&
    (download.id === "" || wallpaperId(download.id)) &&
    ["idle","running","complete","cancelled","failed"].includes(download.status) &&
    integerIn(download.received,0,Number.MAX_SAFE_INTEGER) &&
    integerIn(download.total,0,Number.MAX_SAFE_INTEGER) && typeof download.error === "string";
}
function validWallpaperEngagement(m) {
  const count=value=>value===null || integerIn(value,0,Number.MAX_SAFE_INTEGER);
  return exactKeys(m,["version","type","revision","id","status","likes","downloads","liked","canLike","busy","error"]) &&
    integerIn(m.revision,0,Number.MAX_SAFE_INTEGER) && (m.id==="" || wallpaperId(m.id)) &&
    ["idle","loading","ready","failed"].includes(m.status) && count(m.likes) && count(m.downloads) &&
    ["liked","canLike","busy"].every(key=>typeof m[key]==="boolean") &&
    typeof m.error==="string" && /^[A-Za-z0-9_-]{0,64}$/.test(m.error);
}
function validWallpaperCommand(action, fields) {
  if (["snapshot","refresh","import","stopAll","cancel","endPreview","endStats"].includes(action)) return exactKeys(fields,[]);
  if (["download","remove","preview","applyAll","stats"].includes(action)) return exactKeys(fields,["id"]) && wallpaperId(fields.id);
  if (action === "like") return exactKeys(fields,["id","value"]) && wallpaperId(fields.id) && typeof fields.value==="boolean";
  if (action === "apply") return exactKeys(fields,["id","display"]) && wallpaperId(fields.id) && wallpaperDisplay(fields.display);
  if (["pause","resume","stop"].includes(action)) return exactKeys(fields,["display"]) && wallpaperDisplay(fields.display);
  if (action === "fit") return exactKeys(fields,["display","value"]) && wallpaperDisplay(fields.display) && ["cover","contain"].includes(fields.value);
  if (action === "volume") return exactKeys(fields,["display","value"]) && wallpaperDisplay(fields.display) && integerIn(fields.value,0,100);
  if (action === "pauseFullscreen") return exactKeys(fields,["value"]) && typeof fields.value === "boolean";
  return false;
}
const color = value => typeof value === "string" && /^#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?$/.test(value);
const widgetKeys=["version","type","enabled","running","locked","material","materialStatus","loadStatus","error","width","height","result","saved"];
function validWidgetState(m,minHeight,minWidth=320,errors=["","runtime","save","rollback","layer"],maxWidth=720,maxHeight=720) {
  return ["enabled","running","locked","saved"].every(key=>typeof m[key]==="boolean") &&
    (materials.includes(m.material)||(m.type==="todayTodo-state"&&m.material==="solid")) &&
    ["native-ready","native-starting","transparent-fallback","inactive"].includes(m.materialStatus) &&
    ["loaded","fresh","recovered","failed"].includes(m.loadStatus) &&
    errors.includes(m.error) &&
    ["snapshot","applied"].includes(m.result) &&
    Number.isInteger(m.width) && m.width>=minWidth && m.width<=maxWidth &&
    Number.isInteger(m.height) && m.height>=minHeight && m.height<=maxHeight &&
    (!m.saved || (m.result==="applied" && m.error==="" && m.loadStatus==="loaded"));
}
function validCalendarMessage(m) {
  if (!exactKeys(m,["version","type","calendar","saved","error","materialStatus"],["loadStatus","result"])) return false;
  if (!exactKeys(m.calendar,["enabled","theme","material","textColor","highlightColor","locked","running"])) return false;
  if (!["enabled","locked","running"].every(key=>typeof m.calendar[key] === "boolean")) return false;
  if (!themes.includes(m.calendar.theme) || !calendarMaterials.includes(m.calendar.material) || !color(m.calendar.textColor) || !color(m.calendar.highlightColor)) return false;
  if (typeof m.saved !== "boolean" || !["","invalid","runtime","save","rollback","load"].includes(m.error) || !["transparent-fallback","native-ready","native-starting","inactive"].includes(m.materialStatus)) return false;
  if (m.loadStatus !== undefined && !["loaded","fresh","recovered-backup","failed"].includes(m.loadStatus)) return false;
  if (m.result !== undefined && !["snapshot","applied","cancelled"].includes(m.result)) return false;
  if (m.saved && (m.error !== "" || (m.result !== undefined && m.result !== "applied") || m.loadStatus === "failed")) return false;
  return true;
}
const musicMembershipBadges = ["qq-green-vip","qq-super-vip","netease-vip","netease-svip","kugou-vip"];
const musicAudioQualities = ["standard","high","lossless","hires","jyeffect","sky","jymaster"];
const musicTrack = value => exactKeys(value, ["id", "title", "artist", "album", "artwork", "duration"], ["source","vipRequired","membershipBadge"]) &&
  typeof value.id === "string" && value.id.length > 0 && value.id.length <= 128 &&
  ["title", "artist", "album", "artwork"].every(key => typeof value[key] === "string" && value[key].length <= 262144) &&
  (value.source===undefined || typeof value.source==="string"&&value.source.length<=4096) &&
  (value.vipRequired===undefined || typeof value.vipRequired==="boolean") &&
  (value.membershipBadge===undefined || musicMembershipBadges.includes(value.membershipBadge)) &&
  Number.isFinite(value.duration) && value.duration >= 0 && value.duration <= 86400;
const musicSession = value => Number.isSafeInteger(value) && value >= 0;
function validMusicLyrics(m) {
  if (!exactKeys(m, ["version", "type", "session", "trackId", "lines"]) || !musicSession(m.session) ||
      typeof m.trackId !== "string" || m.trackId.length > 128 || !Array.isArray(m.lines) ||
      m.lines.length > 2000 || (!m.trackId && m.lines.length)) return false;
  let previousStart = 0, textBytes = 0, wordCount=0;
  const encoder = new TextEncoder();
  for (const line of m.lines) {
    if (!exactKeys(line, ["startTime", "endTime", "fullText", "words", "translation"]) ||
        !Number.isFinite(line.startTime) || !Number.isFinite(line.endTime) || line.startTime < previousStart ||
        line.endTime <= line.startTime || line.endTime > 86400 || typeof line.fullText !== "string" ||
        line.fullText.length > 4096 || !Array.isArray(line.words) || line.words.length>1000 ||
        typeof line.translation!=='string'||line.translation.length>4096) return false;
    const bytes = encoder.encode(line.fullText).length,translated=encoder.encode(line.translation).length;
    if (bytes > 4096 || translated>4096 || (textBytes += bytes+translated) > 262144) return false;
    let previousWord=line.startTime;
    for(const word of line.words) {
      if(!exactKeys(word,['startTime','endTime','text'])||!Number.isFinite(word.startTime)||!Number.isFinite(word.endTime)||
        word.startTime<previousWord||word.endTime<word.startTime||word.endTime>line.endTime||
        typeof word.text!=='string'||word.text.length>4096||++wordCount>16000)return false;
      const wordBytes=encoder.encode(word.text).length;
      if(wordBytes>4096||(textBytes+=wordBytes)>262144)return false;
      previousWord=word.startTime;
    }
    previousStart = line.startTime;
  }
  return true;
}
// 音符律动 melody analysis: pitch frames (MIDI × 10, 0 = unvoiced) and beat times.
export function validMusicScore(m) {
  // start/end: the audible span in seconds (absent from older hosts).
  const span = (m.start === undefined && m.end === undefined) ||
    (Number.isFinite(m.start) && Number.isFinite(m.end) && m.start >= 0 && m.start <= m.end && m.end <= 86400);
  return exactKeys(m, ["version", "type", "session", "trackId", "hop", "tempo", "pitch", "beats"], ["start", "end"]) && span && m.version === 1 &&
    musicSession(m.session) && typeof m.trackId === "string" && m.trackId.length > 0 && m.trackId.length <= 128 &&
    Number.isFinite(m.hop) && m.hop >= 0.005 && m.hop <= 0.1 && Number.isFinite(m.tempo) && m.tempo >= 0 && m.tempo <= 400 &&
    Array.isArray(m.pitch) && m.pitch.length <= 90000 &&
    m.pitch.every(value => Number.isInteger(value) && (value === 0 || (value >= 200 && value <= 1100))) &&
    Array.isArray(m.beats) && m.beats.length <= 4000 &&
    m.beats.every((value, index) => Number.isFinite(value) && value >= 0 && value <= 86400 && (!index || value > m.beats[index - 1]));
}
function validMusicSnapshot(m) {
  const track = value => value === null || musicTrack(value);
  const capabilityKeys = ["canPrevious", "canNext", "canToggle", "canSeek"];
  return exactKeys(m, ["version", "type", "status", "queue", "current", "currentIndex", "timeline", "playing", "repeat", "shuffle", "session", "revision", "capabilities", "pageOpen", "pageVisible", "account", "error"],["volume","muted","analysis","audio"]) &&
    ["empty", "ready", "playing", "paused", "error"].includes(m.status) &&
    Array.isArray(m.queue) && m.queue.length <= 512 && m.queue.every(musicTrack) && track(m.current) &&
    Number.isSafeInteger(m.currentIndex) && m.currentIndex >= 0 && Number.isSafeInteger(m.session) &&
    Number.isSafeInteger(m.revision) && m.revision >= 0 && typeof m.playing === "boolean" &&
    ["none", "one", "all"].includes(m.repeat) && typeof m.shuffle === "boolean" &&
    exactKeys(m.timeline, ["position", "duration"]) && Number.isFinite(m.timeline.position) && m.timeline.position >= 0 &&
    Number.isFinite(m.timeline.duration) && m.timeline.duration >= 0 &&
    exactKeys(m.capabilities, capabilityKeys) && capabilityKeys.every(key => typeof m.capabilities[key] === "boolean") &&
    exactKeys(m.account, ["signedIn"]) && typeof m.account.signedIn === "boolean" && typeof m.pageOpen === "boolean" &&
    typeof m.pageVisible === "boolean" && typeof m.error === "string" && m.error.length <= 128 &&
    (m.volume===undefined||Number.isFinite(m.volume)&&m.volume>=0&&m.volume<=1) &&
    (m.muted===undefined||typeof m.muted==='boolean') &&
    (m.audio===undefined||exactKeys(m.audio,['requestedQuality','quality','bitrate'],['switching'])&&
      (m.audio.switching===undefined||typeof m.audio.switching==='boolean')&&
      musicAudioQualities.includes(m.audio.requestedQuality)&&
      (m.audio.quality===''||musicAudioQualities.includes(m.audio.quality))&&
      integerIn(m.audio.bitrate,0,10000000)) &&
    (m.analysis===undefined||exactKeys(m.analysis,['energy','bins'])&&Number.isFinite(m.analysis.energy)&&
      m.analysis.energy>=0&&m.analysis.energy<=1&&Array.isArray(m.analysis.bins)&&m.analysis.bins.length===32&&
      m.analysis.bins.every(value=>Number.isFinite(value)&&value>=0&&value<=1));
}
function validMusicWidgetState(m) {
  return exactKeys(m, ["version", "type", "session", "revision", "title", "artist", "artwork", "status", "playing", "position", "duration", "capabilities"]) &&
    musicSession(m.session) && Number.isSafeInteger(m.revision) && m.revision >= 0 &&
    ["empty", "ready", "playing", "paused", "error"].includes(m.status) &&
    typeof m.title === "string" && typeof m.artist === "string" && typeof m.artwork === "string" &&
    typeof m.playing === "boolean" && Number.isFinite(m.position) && m.position >= 0 && Number.isFinite(m.duration) && m.duration >= 0 &&
    exactKeys(m.capabilities, ["canPrevious", "canNext", "canToggle", "canSeek"]) &&
    Object.values(m.capabilities).every(value => typeof value === "boolean");
}
function validMusicCommand(action, fields) {
  const optionalSession = ["session"];
  const scoped = allowed => exactKeys(fields, [], optionalSession) &&
    (!Object.hasOwn(fields, "session") || musicSession(fields.session)) && allowed.includes(action);
  if (scoped(["open", "close", "snapshot", "login", "logout", "loadHome", "play", "pause", "toggle", "previous", "next", "clearQueue"])) return true;
  if (action === "visibility") return exactKeys(fields, ["visible"], optionalSession) && typeof fields.visible === "boolean" && (!Object.hasOwn(fields, "session") || musicSession(fields.session));
  if (action === "seek") return exactKeys(fields, ["position"], optionalSession) && Number.isFinite(fields.position) && fields.position >= 0 && fields.position <= 1e9 && (!Object.hasOwn(fields, "session") || musicSession(fields.session));
  if (action === "playNow") return exactKeys(fields, ["index","id"], optionalSession) && typeof fields.id==='string' && fields.id.length>0 && fields.id.length<=128 && Number.isSafeInteger(fields.index) && fields.index >= 0 && fields.index <= 100000 && (!Object.hasOwn(fields, "session") || musicSession(fields.session));
  if (action === "loadLyrics" || action === "loadScore") return exactKeys(fields, ["id", "session"]) && typeof fields.id === "string" && fields.id.length > 0 && fields.id.length <= 128 && musicSession(fields.session) && fields.session > 0;
  if (["enqueue", "insertNext", "removeFromQueue"].includes(action)) return exactKeys(fields, ["id"], optionalSession) && typeof fields.id === "string" && fields.id.length <= 128 && (!Object.hasOwn(fields, "session") || musicSession(fields.session));
  if (action === "setRepeat") return exactKeys(fields, ["value"], optionalSession) && ["none", "one", "all"].includes(fields.value) && (!Object.hasOwn(fields, "session") || musicSession(fields.session));
  if (action === "setQuality") return exactKeys(fields, ["value"], optionalSession) && musicAudioQualities.includes(fields.value) && (!Object.hasOwn(fields, "session") || musicSession(fields.session));
  if (["setShuffle","setMuted"].includes(action)) return exactKeys(fields, ["value"], optionalSession) && typeof fields.value === "boolean" && (!Object.hasOwn(fields, "session") || musicSession(fields.session));
  if (action==="setVolume") return exactKeys(fields,["value"],optionalSession)&&Number.isFinite(fields.value)&&
    fields.value>=0&&fields.value<=1&&(!Object.hasOwn(fields,'session')||musicSession(fields.session));
  return false;
}
const resourceRoles = ["Shell","WebViewBrowser","WebViewRenderer","WebViewGpu","WebViewUtility",
  "WebViewCrashpad","MusicHost","Wallpaper","Other"];
const resourceCoverages = ["unavailable","partial","complete"];
const diagnosticKinds = ["operation","error","effect-mismatch"];
const diagnosticStorage = ["","ready","unavailable","failed"];
const diagnosticText = value => typeof value === "string" && value.length <= 240 &&
  !/[\u0000-\u001f\u007f-\u009f]/u.test(value);
const diagnosticCode = value => diagnosticText(value) && /^[A-Za-z0-9_.:-]{0,96}$/.test(value);
function validDiagnosticsEvent(value) {
  if (!exactKeys(value, ["at","kind"], ["firstAt","repeatCount","operationId","module","action","status","error","expected","actual"]) ||
      !integerIn(value.at, 0, Number.MAX_SAFE_INTEGER) || !diagnosticKinds.includes(value.kind)) return false;
  for (const key of ["operationId","module","action","status","error","expected","actual"]) {
    if (value[key] !== undefined && (!diagnosticText(value[key]) || value[key].length > 240)) return false;
  }
  return (value.firstAt === undefined || integerIn(value.firstAt, 0, Number.MAX_SAFE_INTEGER)) &&
    (value.repeatCount === undefined || integerIn(value.repeatCount, 1, 0xffffffff)) &&
    (value.status === undefined || /^[A-Za-z0-9_.:-]{0,96}$/.test(value.status));
}
function validDiagnosticsHealth(value) {
  const booleans=["enabled","running","visible","loaded","layersVisible","transparentTaskbar","autoHideTaskbar","applying","pendingRecovery",
    "playerAvailable","pauseFullscreen","loading","hasWeather"];
  const counts=["visibleItems","pinned","added","taskbars","desktops","autoHidden","pendingRestores","count","hosts","hooks","pendingCleanup"];
  if (!exactKeys(value, [], ["at","status","result","error","action","mode","loadStatus",
      "layout","catalogStatus","catalogError","weatherError","materialStatus",...booleans,...counts])) return false;
  if (value.at !== undefined && !integerIn(value.at, 0, Number.MAX_SAFE_INTEGER)) return false;
  for (const key of ["status","result","error","action","mode","layout","loadStatus","catalogStatus","catalogError","weatherError","materialStatus"])
    if (value[key] !== undefined && (!diagnosticText(value[key]) || value[key].length > 240)) return false;
  if (["status","result","error","action","mode","layout","loadStatus","catalogStatus","catalogError","weatherError","materialStatus"].some(key =>
      value[key] !== undefined && !/^[A-Za-z0-9_.:-]{0,96}$/.test(value[key]))) return false;
  return booleans.every(key => value[key] === undefined || typeof value[key] === "boolean") &&
    counts.every(key => value[key] === undefined || integerIn(value[key], 0, Number.MAX_SAFE_INTEGER));
}
function validDiagnosticsState(m) {
  if (!exactKeys(m, ["version","type","schemaVersion","revision","events","health"],
      ["available","storage","generatedAt","mismatches","faults"]) ||
      m.version !== 1 || m.type !== "diagnostics-state" || m.schemaVersion !== 1 ||
      !integerIn(m.revision, 0, Number.MAX_SAFE_INTEGER) ||
      !Array.isArray(m.events) || m.events.length > 80 || !m.events.every(validDiagnosticsEvent) ||
      !m.health || typeof m.health !== "object" || Array.isArray(m.health)) return false;
  const healthKeys = Object.keys(m.health);
  if (healthKeys.length > 64 || healthKeys.some(key => !/^[A-Za-z0-9_.:-]{1,64}$/.test(key) ||
      !validDiagnosticsHealth(m.health[key]))) return false;
  if (m.available !== undefined && typeof m.available !== "boolean") return false;
  if (m.storage !== undefined && !diagnosticStorage.includes(m.storage)) return false;
  if (m.generatedAt !== undefined && !integerIn(m.generatedAt, 0, Number.MAX_SAFE_INTEGER)) return false;
  if (m.mismatches !== undefined && (!Array.isArray(m.mismatches) || m.mismatches.length > 80 ||
      !m.mismatches.every(value => exactKeys(value, ["component","expected","actual","confidence"], ["reason"]) &&
        diagnosticText(value.component) && diagnosticText(value.expected) && diagnosticText(value.actual) &&
        ["confirmed","suspected","unknown"].includes(value.confidence) &&
        (value.reason === undefined || diagnosticText(value.reason))))) return false;
  if (m.faults !== undefined && (!Array.isArray(m.faults) || m.faults.length > 80 ||
      !m.faults.every(value => exactKeys(value, ["component","code","severity"], ["message","firstAt","lastAt","count"]) &&
        diagnosticText(value.component) && diagnosticText(value.code) &&
        ["info","warning","error"].includes(value.severity) &&
        (value.message === undefined || diagnosticText(value.message)) &&
        (value.firstAt === undefined || integerIn(value.firstAt, 0, Number.MAX_SAFE_INTEGER)) &&
        (value.lastAt === undefined || integerIn(value.lastAt, 0, Number.MAX_SAFE_INTEGER)) &&
        (value.count === undefined || integerIn(value.count, 0, 0xffffffff))))) return false;
  return true;
}
function validResourceNumber(value) {
  return value === null || integerIn(value, 0, Number.MAX_SAFE_INTEGER);
}
function validResourceProcess(value) {
  return exactKeys(value, ["pid","parentPid","name","role","workingSetBytes","privateBytes",
    "gpuDedicatedBytes","gpuSharedBytes"]) &&
    integerIn(value.pid, 0, 0xffffffff) && integerIn(value.parentPid, 0, 0xffffffff) &&
    typeof value.name === "string" && value.name.length > 0 && value.name.length <= 260 &&
    resourceRoles.includes(value.role) &&
    ["workingSetBytes","privateBytes","gpuDedicatedBytes","gpuSharedBytes"].every(key => validResourceNumber(value[key]));
}
function validResourceGroup(value) {
  return exactKeys(value, ["workingSetBytes","privateBytes","gpuDedicatedBytes","gpuSharedBytes",
    "processCount","coverage","processes"]) &&
    ["workingSetBytes","privateBytes","gpuDedicatedBytes","gpuSharedBytes"].every(key => validResourceNumber(value[key])) &&
    integerIn(value.processCount, 0, 4096) && resourceCoverages.includes(value.coverage) &&
    Array.isArray(value.processes) && value.processes.length <= 4096 && value.processes.every(validResourceProcess);
}
function validResourceSnapshot(m) {
  return exactKeys(m, ["version","type","schemaVersion","sampledAt","shell","webview","music",
    "gpu","wallpaper","nonWallpaper","processes"]) &&
    m.schemaVersion === 1 &&
    exactKeys(m.sampledAt, ["low","high"]) &&
    integerIn(m.sampledAt.low, 0, 0xffffffff) && integerIn(m.sampledAt.high, 0, 0xffffffff) &&
    ["shell","webview","music","gpu","wallpaper","nonWallpaper"].every(key => validResourceGroup(m[key])) &&
    Array.isArray(m.processes) && m.processes.length <= 4096 && m.processes.every(validResourceProcess);
}
export function validHostMessage(m) {
  if (!m || m.version !== 1) return false;
  if (m.type === 'radial-state') return validRadialState(m);
  if (m.type === 'dock-state') return validDockState(m);
  if (m.type === 'dynamicIsland-state') return validIslandState(m);
  if (m.type === "account-state") return validAccountState(m);
  if (m.type === "app-settings-state") return validApplicationSettingsState(m);
  if (m.type === "app-update-notice") return validUpdateNotice(m);
  if (m.type === "music-state") return validMusicSnapshot(m);
  if (m.type === "music-preferences-state") return validMusicPreferencesMessage(m);
  if (m.type === "music-background-state") return validMusicBackgroundMessage(m);
  if (m.type === "music-provider-account-state") return validMusicProviderAccountMessage(m);
  if (m.type === "music-online-state") return validMusicOnlineMessage(m);
  if (m.type === "music-library-state") return validMusicLibraryMessage(m);
  if (m.type === "music-desktop-state") return validMusicDesktopMessage(m);
  if (m.type === "notes-state") return validNotesMessage(m);
  if (m.type === "music-fonts-state") return validMusicFontMessage(m);
  if (m.type === "music-font-assets-state") return validMusicFontAssetMessage(m);
  if (m.type === "music-background-catalog") return validMusicBackgroundCatalogMessage(m);
  if (m.type === "music-lyrics") return validMusicLyrics(m);
  if (m.type === "music-score") return validMusicScore(m);
  if (m.type === "music-widget-state") return validMusicWidgetState(m);
  if (m.type === "resource-snapshot") return validResourceSnapshot(m);
  if (m.type === "diagnostics-state") return validDiagnosticsState(m);
  if (m.type === "diagnostic-report-state") return exactKeys(m,["version","type","status"],["action","file","error","checks"]) &&
    ["working","done","failed"].includes(m.status) &&
    (!Object.hasOwn(m,"action") || ["create","copy","self-check"].includes(m.action)) &&
    (!Object.hasOwn(m,"checks") || (Array.isArray(m.checks) && m.checks.length <= 16 && m.checks.every(check =>
      exactKeys(check,["id","status","title","detail","fix"]) && ["ok","problem","unknown"].includes(check.status) &&
      ["id","title","detail","fix"].every(key => typeof check[key] === "string" && check[key].length <= 2000)))) &&
    (!Object.hasOwn(m,"file") || (typeof m.file === "string" && m.file.length <= 260)) &&
    (!Object.hasOwn(m,"error") || (typeof m.error === "string" && m.error.length <= 240));
  if (m.type === "wallpaper-state") return validWallpaperState(m);
  if (m.type === "wallpaper-engagement-state") return validWallpaperEngagement(m);
  if (m.type === "wallpaper-engagement-notice") return exactKeys(m,["version","type","id","error"]) &&
    wallpaperId(m.id) && ["WALLPAPER_DOWNLOAD_STATS_FAILED","WALLPAPER_DOWNLOAD_STATS_QUOTA"].includes(m.error);
  if (m.type === "wallpaper-progress") return exactKeys(m,["version","type","download"]) && validWallpaperDownload(m.download);
  if (m.type === "visibility") return exactKeys(m,["version","type","visible"],["generation"]) &&
    typeof m.visible === "boolean" &&
    (!Object.hasOwn(m,"generation") || integerIn(m.generation,1,0xffffffff));
  if (m.type === "window") return typeof m.fullscreen === "boolean";
  if (m.type === "navigate") return m.page === "applicationSettings"
    ? exactKeys(m,["version","type","page","section"]) && ["general","user","autostart","diagnostics","support","about"].includes(m.section)
    : m.page==='radial'?exactKeys(m,['version','type','page'],['section'])&&
      (!Object.hasOwn(m,'section')||m.section==='shortcut')
    : m.page==="music"&&m.section==="desktop"?exactKeys(m,["version","type","page","section"])
    : exactKeys(m,["version","type","page"]) && ["clock","calendar","performance","spectrum","almanac","todayTodo","weather","organizer","icons","taskbar","minimize","music","dock","dynamicIsland"].includes(m.page);
  if(m.type==='taskbar-state') {
    return exactKeys(m,['version','type','enabled','running','transparentTaskbar','autoHideTaskbar','hideBehavior',
      'applying','pendingRecovery','taskbars','mode','error','loadStatus','revision']) &&
      ['enabled','running','transparentTaskbar','autoHideTaskbar','applying','pendingRecovery'].every(key=>typeof m[key]==='boolean') &&
      ['auto-hide','fully-hidden'].includes(m.hideBehavior) &&
      integerIn(m.taskbars,0,32) && integerIn(m.revision,0,Number.MAX_SAFE_INTEGER) &&
      ['unavailable','classic','windows11-x64','windows11-arm64'].includes(m.mode) &&
      ['fresh','loaded','failed'].includes(m.loadStatus) && taskbarErrors.includes(m.error);
  }
  if(m.type==='minimize-state') {
    return exactKeys(m,['version','type','enabled','running','durationMs','restore','launch','multiMonitor',
      'unhideTaskbar','unhideDurationMs','engine','x64','x86','hosts','hooks','workers','entries','pendingCleanup','loadStatus',
      'error','systemError','result','saved']) &&
      ['enabled','running','restore','launch','multiMonitor','unhideTaskbar','x64','x86','saved'].every(key=>typeof m[key]==='boolean') &&
      integerIn(m.durationMs,50,2000) && integerIn(m.unhideDurationMs,0,5000) &&
      ['modern','classic'].includes(m.engine) && integerIn(m.hosts,0,2) && integerIn(m.hooks,0,4) &&
      integerIn(m.workers,0,0xffffffff) && integerIn(m.entries,0,0xffffffff) &&
      integerIn(m.pendingCleanup,0,0xffffffff) && integerIn(m.systemError,0,0xffffffff) &&
      m.hosts===Number(m.x64)+Number(m.x86) && (!m.running || (m.hosts>0 && m.hooks===m.hosts*2)) &&
      ['fresh','loaded','recovered','failed'].includes(m.loadStatus) &&
      minimizeErrors.includes(m.error) && ['snapshot','applied'].includes(m.result) &&
      (!m.saved || (m.result==='applied' && m.loadStatus==='loaded' && m.error==='' && m.enabled===m.running));
  }
  if(m.type==='icons-state') {
    return exactKeys(m,['version','type','enabled','running','enableAutoHide','autoHideDelay','showIconsOnAnyInput',
      'manuallyHidden','desktops','autoHidden','pendingRestores','loadStatus','error','result','saved']) &&
      ['enabled','running','enableAutoHide','showIconsOnAnyInput','manuallyHidden','saved'].every(k=>typeof m[k]==='boolean') &&
      Number.isInteger(m.autoHideDelay)&&m.autoHideDelay>=3&&m.autoHideDelay<=60 &&
      ['desktops','autoHidden','pendingRestores'].every(k=>Number.isInteger(m[k])&&m[k]>=0&&m[k]<=8) &&
      m.autoHidden<=m.desktops && (!m.running||m.enabled) &&
      ['fresh','loaded','recovered','failed'].includes(m.loadStatus) &&
      ['','runtime','save','rollback','input-busy','visibility'].includes(m.error) &&
      ['snapshot','applied'].includes(m.result) &&
      // saved acknowledges persistence; the policy's visibility request can
      // still fail after that commit. Do not turn it into a save failure or
      // discard the reply and its saved preferences.
      (!m.saved||(m.result==='applied'&&m.loadStatus==='loaded'&&
        (m.error===''||(m.error==='visibility'&&m.enabled&&m.running))));
  }
  if (m.type === "organizer-state") {
    const required=[...widgetKeys,"title","count","mode","layout","collections"];
    if(!exactKeys(m,Object.hasOwn(m,"iconScale")?[...required,"iconScale"]:required) ||
      (Object.hasOwn(m,"iconScale")&&!integerIn(m.iconScale,50,120)))return false;
    const title=value=>typeof value==="string"&&value.length>0&&[...value].length<=80;
    const collapsed=m.mode==="collapsed";
    if(!validWidgetState(m,216,120,["","runtime","save","rollback","editor","open","invalid","target","limit"],1200,720) ||
      (collapsed && (m.width!==180 || m.height!==216)) || !title(m.title) ||
      !integerIn(m.count,0,120) || !["expanded","collapsed"].includes(m.mode) || !["icons","list"].includes(m.layout) ||
      !Array.isArray(m.collections) || m.collections.length<1 || m.collections.length>64)return false;
    const ids=new Set();
    for(const collection of m.collections) {
      if(!exactKeys(collection,["id","title","enabled","running","count"]) ||
        typeof collection.id!=="string" || (collection.id!=="default" && !/^[\da-f]{8}(?:-[\da-f]{4}){3}-[\da-f]{12}$/i.test(collection.id)) ||
        ids.has(collection.id) || !title(collection.title) || typeof collection.enabled!=="boolean" ||
        typeof collection.running!=="boolean" || !integerIn(collection.count,0,120))return false;
      ids.add(collection.id);
    }
    const main=m.collections.find(collection=>collection.id==="default");
    return !!main && main.title===m.title && main.enabled===m.enabled && main.count===m.count;
  }
  if (m.type === "weather-state") {
    const required=[...widgetKeys,"city","loading","weatherError","cacheWriteFailed","hasWeather","systemLocation"];
    const data=["source","fetchedAt","sourceUpdatedAt","validAt","remainingDay"];
    if(!exactKeys(m,required,m.hasWeather?data:[]))return false;
    return validWidgetState(m,152) && cities.includes(m.city) &&
      ["loading","cacheWriteFailed","hasWeather"].every(key=>typeof m[key]==="boolean") &&
      ["","location","unavailable","retry-delayed","cancelled"].includes(m.weatherError) &&
      ["on","off","unknown"].includes(m.systemLocation) &&
      (!m.hasWeather || (data.every(key=>Object.hasOwn(m,key)) &&
        ["qweather","met-norway"].includes(m.source) && typeof m.remainingDay==="boolean" &&
        ["fetchedAt","sourceUpdatedAt","validAt"].every(key=>Number.isSafeInteger(m[key])&&m[key]>=0)));
  }
  if (["performance-state","almanac-state","todayTodo-state"].includes(m.type)) {
    return exactKeys(m,widgetKeys) && validWidgetState(m,m.type!=="performance-state"?152:204,m.type==="todayTodo-state"?220:320);
  }
  if (m.type === "spectrum-state") {
    // A bare widget over the wallpaper: no material, wide and short bounds.
    const window={...m,material:"liquid-glass"};
    const bar=value=>exactKeys(value,["barWidth","barGap"]) && integerIn(value.barWidth,2,24) && integerIn(value.barGap,0,12);
    return exactKeys(m,[...widgetKeys,"style","scale","pulse","blocks","waves"]) && m.material==="none" &&
      ["pulse","blocks","waves"].includes(m.style) && integerIn(m.scale,50,300) && bar(m.pulse) && bar(m.blocks) && bar(m.waves) &&
      validWidgetState(window,60,200,undefined,2640,600);
  }
  if (m.type === "calendar-settings") return validCalendarMessage(m);
  if (m.type === "reminder-status") return exactKeys(m,["version","type","available"]) && typeof m.available === "boolean";
  if (m.error !== undefined && !["", "save", "runtime", "rollback"].includes(m.error)) return false;
  if (m.clock?.running !== undefined && typeof m.clock.running !== "boolean") return false;
  if (m.clock?.scale !== undefined && (!Number.isInteger(m.clock.scale) || m.clock.scale < 50 || m.clock.scale > 300 || m.clock.scale % 10)) return false;
  for (const key of ["x", "y"]) if (m.clock?.[key] !== undefined && (!Number.isInteger(m.clock[key]) || m.clock[key] < -32768 || m.clock[key] > 32767)) return false;
  for (const [key, max] of [["alpha",255],["color",0xffffff]]) if (m.clock?.[key] !== undefined && (!Number.isInteger(m.clock[key]) || m.clock[key] < 0 || m.clock[key] > max)) return false;
  for (const key of ["locked", "customColor"]) if (m.clock?.[key] !== undefined && typeof m.clock[key] !== "boolean") return false;
  return (
    m.type === "settings" &&
    typeof m.clock?.enabled === "boolean" &&
    styles.includes(m.clock.style) &&
    typeof m.saved === "boolean"
  );
}
export function createBridge(webview = globalThis.window?.chrome?.webview) {
  const postScoped = (scope, action, fields, validate) => {
    if (!validate(action, fields) || typeof webview?.postMessage !== "function") return false;
    try {
      webview.postMessage({ version: 1, scope, action, ...fields });
      return true;
    } catch {
      return false;
    }
  };
  return {
    account(action, fields = {}) {
      return postScoped("account", action, fields, validAccountCommand);
    },
    applicationSettings(action, fields = {}) {
      return postScoped("app-settings", action, fields, validApplicationSettingsCommand);
    },
    diagnostics(action) {
      if (!["snapshot", "clear"].includes(action) || typeof webview?.postMessage !== "function") return false;
      try {
        webview.postMessage({version: 1, scope: "diagnostics", action});
        return true;
      } catch {
        return false;
      }
    },
    // Builds the diagnostic report zip on the desktop, or opens the log folder.
    diagnosticReport(action, note = "") {
      if (!["create", "open-logs", "copy", "self-check"].includes(action) || typeof note !== "string" || note.length > 1000 ||
          typeof webview?.postMessage !== "function") return false;
      try {
        webview.postMessage({version: 1, scope: "diagnostic-report", action, note});
        return true;
      } catch {
        return false;
      }
    },
    music(action, fields = {}) {
      return postScoped("music", action, fields, validMusicCommand);
    },
    musicPreferences(action, fields = {}) {
      return postScoped('music-preferences',action,fields,validMusicPreferencesCommand);
    },
    musicBackground(action, fields = {}) {
      return postScoped('music-background',action,fields,validMusicBackgroundCommand);
    },
    musicProviderAccount(action, fields = {}) {
      return postScoped('music-provider-account',action,fields,validMusicProviderAccountCommand);
    },
    musicOnline(action, fields = {}) {
      return postScoped('music-online',action,fields,validMusicOnlineCommand);
    },
    musicLibrary(action, fields = {}) {
      return postScoped('music-library',action,fields,validMusicLibraryCommand);
    },
    musicDesktop(action, fields = {}) {
      return postScoped('music-desktop',action,fields,validMusicDesktopCommand);
    },
    notes(action, fields = {}) {
      return postScoped('notes',action,fields,validNotesCommand);
    },
    musicFonts(action, fields = {}) {
      return postScoped('music-fonts',action,fields,validMusicFontCommand);
    },
    musicFontAssets(action, fields = {}) {
      return postScoped('music-font-assets',action,fields,validMusicFontAssetCommand);
    },
    musicDropFiles(files) {
      if (!Array.isArray(files) || !files.length || files.length > 32 ||
        typeof globalThis.File !== "function" || !files.every(file => file instanceof File) ||
        typeof webview?.postMessageWithAdditionalObjects !== "function") return false;
      try {
        webview.postMessageWithAdditionalObjects("v1|music|dropFiles", files);
        return true;
      } catch {
        return false;
      }
    },
    dropFiles(files) {
      if (!Array.isArray(files) || !files.length || files.length>32 ||
        typeof globalThis.File!=="function" || !files.every(file=>file instanceof File) ||
        typeof webview?.postMessageWithAdditionalObjects!=="function") return false;
      try {
        webview.postMessageWithAdditionalObjects("v1|wallpaper|dropFiles",files);
        return true;
      } catch {
        return false;
      }
    },
    wallpaper(action, fields = {}) {
      if (!validWallpaperCommand(action,fields) || typeof webview?.postMessage !== "function") return false;
      try {
        webview.postMessage({version:1,scope:"wallpaper",action,...fields});
        return true;
      } catch {
        return false;
      }
    },
    lifecycle(action, generation) {
      if (action !== "cleanup" || !integerIn(generation,1,0xffffffff) ||
        typeof webview?.postMessage !== "function") return false;
      try {
        webview.postMessage({ version: 1, scope: "lifecycle", action, generation });
        return true;
      } catch {
        return false;
      }
    },
    send(...parts) {
      const [kind, action, value] = parts;
      const allowed =
        (kind === 'dynamicIsland' && ((parts.length===2 && ['reset','expand','collapse','focus','stopFocus'].includes(action)) ||
          (parts.length===3 && ((['enabled','locked','hideFullscreen','topmost','hover','autoHide'].includes(action) && typeof value==='boolean') ||
            (action==='size' && ['small','standard','large'].includes(value)) ||
            (action==='material' && ['none','liquid-glass','frosted-glass'].includes(value)))))) ||
        (kind === "ready" && parts.length === 1) ||
        (kind === 'radial' && ((parts.length===2 && action==='reset') ||
          (parts.length===3 && ((['enabled','capture'].includes(action) && typeof value==='boolean') ||
            (action==='material' && materials.includes(value)) ||
            (action==='shortcut' && validRadialShortcut(value)))))) ||
        (kind === 'dock' && ((parts.length===2 && action==='reset') ||
          (parts.length===3 && ((['enabled','reflection'].includes(action) && typeof value==='boolean') ||
            (action==='iconScale' && integerIn(value,50,120)) ||
            (action==='launchpadScale' && integerIn(value,60,140)) ||
            (action==='launchpadIconScale' && integerIn(value,50,120)) ||
            (action==='position' && integerIn(value,0,100)) ||
            (action==='bottomOffset' && integerIn(value,0,160)) ||
            (action==='iconSpacing' && integerIn(value,4,24)) ||
            (action==='material' && [...materials,'none'].includes(value)))))) ||
        (kind === 'taskbar' && ((parts.length===2 && action==='reset') ||
          (parts.length===3 && ((['enabled','transparent','autoHide'].includes(action) && typeof value==='boolean') ||
            (action==='hideBehavior' && ['auto-hide','fully-hidden'].includes(value)))))) ||
        (kind === 'minimize' && ((parts.length===2 && action==='reset') ||
          (parts.length===3 && ((['enabled','restore','launch','multiMonitor','unhideTaskbar'].includes(action) && typeof value==='boolean') ||
            (action==='durationMs' && integerIn(value,50,2000)) ||
            (action==='unhideDurationMs' && integerIn(value,0,5000)) ||
            (action==='engine' && ['modern','classic'].includes(value)))))) ||
        (kind === 'icons' && ((parts.length===2&&action==='reset') ||
          (parts.length===3 && ((['enabled','autoHide','restoreOnInput'].includes(action)&&typeof value==='boolean') ||
           (action==='delay'&&Number.isInteger(value)&&value>=3&&value<=60))))) ||
        (kind === "clock" && action === "reset" && parts.length === 2) ||
        (["performance","almanac","todayTodo","weather","organizer"].includes(kind) && action === "reset" && parts.length === 2) ||
        (kind === "performance" && action === "resource-snapshot" && parts.length === 2) ||
        (kind === "spectrum" && ((parts.length===2 && action==="reset") ||
          (parts.length===3 && ((action==="enabled" && typeof value==="boolean") ||
            (action==="style" && ["pulse","blocks","waves"].includes(value)) || (action==="size" && integerIn(value,50,300)))) ||
          (parts.length===4 && ["pulse","blocks","waves"].includes(action) && ((value==="barWidth" && integerIn(parts[3],2,24)) ||
            (value==="barGap" && integerIn(parts[3],0,12)))))) ||
        (kind === "weather" && action === "refresh" && parts.length === 2) ||
        (kind === "weather" && action === "location-settings" && parts.length === 2) ||
        (kind === "weather" && action === "city" && parts.length === 3 && cities.includes(value)) ||
        (kind === "organizer" && action === "iconScale" && parts.length === 3 && integerIn(value,50,120)) ||
        (["performance","almanac","todayTodo","weather","organizer"].includes(kind) && parts.length === 3 &&
          ((action === "enabled" && typeof value === "boolean") ||
           (action === "material" && (materials.includes(value)||(kind==="todayTodo"&&value==="solid"))))) ||
        (kind === "calendar" && parts.length === 2 && ["textColor","highlightColor","reset"].includes(action)) ||
        (kind === "calendar" && parts.length === 3 &&
          ((action === "enabled" && typeof value === "boolean") ||
           (action === "theme" && themes.includes(value)) || (action === "material" && calendarMaterials.includes(value)))) ||
        (kind === "window" &&
          parts.length === 2 &&
          ["minimize", "maximize", "close", "drag", "dropzone-local", "dropzone-music", "dropzone-none"].includes(action)) ||
        (kind === "clock" &&
          parts.length === 3 &&
          ((action === "enabled" && typeof value === "boolean") ||
            (action === "style" && styles.includes(value))));
      if (!allowed || typeof webview?.postMessage !== "function") return false;
      try {
        webview.postMessage(["v1", ...parts].join("|"));
        return true;
      } catch {
        return false;
      }
    },
    subscribe(callback) {
      const listener = (event) => {
        if (validHostMessage(event.data)) callback(event.data);
      };
      webview?.addEventListener?.("message", listener);
      return () => webview?.removeEventListener?.("message", listener);
    },
  };
}
