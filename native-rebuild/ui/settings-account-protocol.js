const exact = (value, keys) => value !== null && typeof value === "object" &&
  !Array.isArray(value) && keys.length === Object.keys(value).length &&
  keys.every((key) => Object.hasOwn(value, key));
const integer = (value) => Number.isSafeInteger(value) && value >= 0;
const text = (value, max) => typeof value === "string" && value.length <= max &&
  !/[\u0000-\u001f\u007f-\u009f]/u.test(value) &&
  ![...value].some((character) => /^[\ud800-\udfff]$/u.test(character));
const errorCode = (value) => typeof value === "string" && /^[a-z0-9-]{0,64}$/.test(value);

export function validAccountName(value) {
  return text(value, 64) && [...value.trim()].length >= 2 && [...value].length <= 32;
}

export function validAccountCommand(action, fields) {
  if (action === "updateName")
    return exact(fields, ["value"]) && validAccountName(fields.value);
  return ["snapshot", "login", "cancelLogin", "logout", "refresh", "chooseAvatar"].includes(action) &&
    exact(fields, []);
}

export function validApplicationSettingsCommand(action, fields) {
  if (action === "language")
    return exact(fields, ["value"]) && ["zh-CN", "en"].includes(fields.value);
  if (["balancedMode", "autostart"].includes(action))
    return exact(fields, ["value"]) && typeof fields.value === "boolean";
  return ["snapshot", "checkUpdate", "downloadUpdate", "installUpdate",
    "chooseWallpaperDirectory", "chooseDataDirectory"].includes(action) && exact(fields, []);
}

function validAvatar(value) {
  return typeof value === "string" && (value === "" ||
    (value.length <= 2_796_244 &&
      /^data:image\/(?:png|jpeg|webp);base64,[A-Za-z0-9+/]+={0,2}$/.test(value)));
}

export function validAccountState(message) {
  if (!exact(message, ["version", "type", "revision", "status", "busy", "error", "profile"]) ||
    message.version !== 1 || message.type !== "account-state" || !integer(message.revision) ||
    !["signed-out", "restoring", "authorizing", "signed-in"].includes(message.status) ||
    typeof message.busy !== "boolean" || !errorCode(message.error)) return false;
  if (message.status !== "signed-in") return message.profile === null;
  const p = message.profile;
  return exact(p, ["id", "displayName", "avatarUrl", "vip", "vipExpiresAt"]) &&
    text(p.id, 512) && p.id.length > 0 && text(p.displayName, 256) &&
    validAvatar(p.avatarUrl) && typeof p.vip === "boolean" && text(p.vipExpiresAt, 64);
}

export function validApplicationSettingsState(message) {
  const base = ["version", "type", "revision", "language", "balancedMode", "autostart", "busy",
    "error", "update"];
  const withPaths = [...base, "dataDirectory", "wallpaperDirectory"];
  // Older states carry no paths; accept both shapes so a host upgrade cannot
  // lock the page out, but validate the paths whenever they are present.
  if (!exact(message, withPaths) && !exact(message, base)) return false;
  if (exact(message, withPaths) &&
    !(text(message.dataDirectory, 260) && text(message.wallpaperDirectory, 260))) return false;
  if (message.version !== 1 || message.type !== "app-settings-state" || !integer(message.revision) ||
    !["zh-CN", "en"].includes(message.language) ||
    !["balancedMode", "autostart", "busy"].every((key) => typeof message[key] === "boolean") ||
    !errorCode(message.error)) return false;
  const update = message.update;
  return exact(update, ["status", "version", "progress"]) &&
    ["idle", "checking", "current", "available", "incompatible", "downloading", "ready",
      "installing", "error"].includes(update.status) &&
    text(update.version, 64) && (update.version === "" || /^\d+(?:\.\d+){1,3}$/.test(update.version)) &&
    Number.isFinite(update.progress) && update.progress >= 0 && update.progress <= 100;
}

export function validUpdateNotice(message) {
  return exact(message, ["version","type","kind","updateVersion","error","manual"]) &&
    message.version === 1 && message.type === "app-update-notice" &&
    ["current","incompatible","disabled","error","available"].includes(message.kind) &&
    text(message.updateVersion,64) &&
    (message.kind === "error" ? message.updateVersion === "" : /^\d+(?:\.\d+){1,3}$/.test(message.updateVersion)) &&
    errorCode(message.error) && typeof message.manual === "boolean";
}
