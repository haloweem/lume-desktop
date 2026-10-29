// ../../../../../workspace/lume1.0.7/native-rebuild/ui/music-preference-values.js
var MODES = ["partita", "fume", "sonnet", "gallery", "3d", "lumiere", "cadenza", "tilt", "cappella", "diorama", "pendolo", "classic", "tempera"];
var EFFECTS = ["none", "rain-window", "snow", "star-river"];
var KEYS = ["version", "mode", "fontFamily", "animationIntensity", "lyricsFontColor", "lyricsKaraokeFillColor"];
var SONNET_COLOR_KEYS = Object.freeze(["lyricsFontColor", "karaokeFillColor", "glowColor"]);
var exact = (value, keys) => value !== null && typeof value === "object" && !Array.isArray(value) && Object.keys(value).length === keys.length && keys.every((key) => Object.hasOwn(value, key));
var color = (value) => typeof value === "string" && /^#[0-9a-f]{6}$/.test(value);
var DEFAULT_SONNET_COLORS = Object.freeze({
  lyricsFontColor: "#ffffff",
  karaokeFillColor: "#38bdf8",
  glowColor: "#60a5fa"
});
var DEFAULT_MUSIC_PREFERENCES = Object.freeze({
  version: 3,
  mode: "partita",
  fontFamily: "",
  animationIntensity: "normal",
  lyricsFontColor: "#ffffff",
  lyricsKaraokeFillColor: "#ffffff",
  sonnetColors: DEFAULT_SONNET_COLORS,
  effectMode: "none"
});
function fontBytes(value) {
  let bytes = 0;
  for (let i = 0; i < value.length; i++) {
    const code = value.charCodeAt(i);
    if (code >= 55296 && code <= 56319) {
      const low = value.charCodeAt(++i);
      if (!(low >= 56320 && low <= 57343)) return Infinity;
      bytes += 4;
    } else if (code >= 56320 && code <= 57343) return Infinity;
    else bytes += code <= 127 ? 1 : code <= 2047 ? 2 : 3;
  }
  return bytes;
}
function validMusicPreferences(value) {
  if (![1, 2, 3].includes(value?.version) || !exact(value, value.version === 1 ? KEYS : [...KEYS, "sonnetColors", ...value.version === 3 ? ["effectMode"] : []])) return false;
  return MODES.includes(value.mode) && typeof value.fontFamily === "string" && fontBytes(value.fontFamily) <= 128 && !/[\u0000-\u001f\u007f-\u009f]/u.test(value.fontFamily) && ["calm", "normal", "chaotic"].includes(value.animationIntensity) && color(value.lyricsFontColor) && color(value.lyricsKaraokeFillColor) && (value.version === 1 || exact(value.sonnetColors, SONNET_COLOR_KEYS) && SONNET_COLOR_KEYS.every((key) => color(value.sonnetColors[key]))) && (value.version !== 3 || EFFECTS.includes(value.effectMode));
}
function sameMusicPreferences(a, b) {
  return !!a && !!b && KEYS.every((key) => a[key] === b[key]) && a.effectMode === b.effectMode && SONNET_COLOR_KEYS.every((key) => a.sonnetColors?.[key] === b.sonnetColors?.[key]);
}
function musicPreferenceSnapshot(previous, value) {
  if (value === void 0) return previous;
  if (!validMusicPreferences(value)) throw new TypeError("Invalid music preferences");
  const normalized = value.version === 1 ? { ...value, version: 3, sonnetColors: DEFAULT_SONNET_COLORS, effectMode: "none" } : value.version === 2 ? { ...value, version: 3, effectMode: "none" } : value;
  if (sameMusicPreferences(previous, normalized)) return previous;
  return Object.freeze({ ...normalized, sonnetColors: Object.freeze({ ...normalized.sonnetColors }) });
}

// ../../../../../workspace/lume1.0.7/native-rebuild/ui/music-visualizers/reference/src/protocol.js
var preferenceSnapshot = musicPreferenceSnapshot;
var MODES2 = Object.freeze(["classic", "cadenza", "partita", "fume", "tilt", "monet", "sonnet"]);
var text = (value) => typeof value === "string" ? value : "";
var currentKeys = ["id", "source", "title", "artist", "album", "artwork"];
var EMPTY_ANALYSIS = Object.freeze({ energy: 0, bins: Object.freeze(Array(32).fill(0)) });
function analysisSnapshot(previous, value) {
  if (value == null) return EMPTY_ANALYSIS;
  const unit = (number) => Number.isFinite(number) && number >= 0 && number <= 1;
  if (typeof value !== "object" || Array.isArray(value) || !unit(value.energy) || !Array.isArray(value.bins) || value.bins.length !== 32 || Array.from(value.bins).some((bin) => !unit(bin))) throw new TypeError("Invalid audio analysis");
  if (previous?.energy === value.energy && value.bins.every((bin, index) => bin === previous.bins[index])) return previous;
  return Object.freeze({ energy: value.energy, bins: Object.freeze([...value.bins]) });
}
function applyAnalysis(analysis, audioPower, audioBands) {
  audioPower.set(analysis.energy * 255);
  for (const [key, start, end] of [["bass", 0, 1], ["lowMid", 1, 2], ["mid", 2, 4], ["vocal", 4, 8], ["treble", 8, 32]]) {
    const values = analysis.bins.slice(start, end);
    audioBands[key].set(values.reduce((sum, value) => sum + value, 0) / values.length * 255);
  }
  audioBands.spectrum.set(Uint8Array.from({ length: 128 }, (_, index) => Math.round(analysis.bins[index >>> 2] * 255)));
}
function acceptsMessage(event, parent, origin, mode) {
  return parent !== globalThis && event.source === parent && origin !== "null" && event.origin === origin && event.data?.type === "lume-lyric-stage" && MODES2.includes(event.data.mode) && event.data.mode === mode && event.data.state !== null && typeof event.data.state === "object" && !Array.isArray(event.data.state);
}
function localArtwork(value, base) {
  if (typeof value !== "string" || !value || value.length > 262144) return void 0;
  if (/^data:image\/(?:png|jpeg|webp|gif|avif);base64,[a-z\d+/=\s]+$/i.test(value)) return value;
  try {
    const url = new URL(value, base);
    if (url.username || url.password) return void 0;
    if (url.origin === new URL(base).origin && ["http:", "https:", "blob:"].includes(url.protocol)) return url.href;
    if (/^http:\/\/127\.0\.0\.1:[1-9]\d{0,4}\/[a-f0-9]{64}\/media\.png$/.test(value) && Number(url.port) <= 65535) return url.href;
    if (url.protocol === "https:" && !url.port && [
      "p1.music.126.net",
      "p2.music.126.net",
      "p3.music.126.net",
      "p4.music.126.net",
      "y.gtimg.cn",
      "imge.kugou.com",
      "imgessl.kugou.com"
    ].includes(url.hostname)) return url.href;
  } catch {
  }
  return void 0;
}
function createStateReducer(prepareLines) {
  let linesKey = "[]";
  let state = { lines: [], current: null, currentTime: 0, duration: 0, activeLineIndex: -1, playing: false, visible: true, analysis: EMPTY_ANALYSIS };
  return {
    get state() {
      return state;
    },
    update(delta) {
      let structural = false;
      const next = { ...state };
      const preferences = preferenceSnapshot(state.preferences, delta.preferences);
      if (preferences !== state.preferences) {
        next.preferences = preferences;
        structural = true;
      }
      let nextLinesKey = linesKey;
      if (delta.lines !== void 0) {
        if (delta.lines !== null && !Array.isArray(delta.lines)) throw new TypeError("Invalid lyric lines");
        const raw = delta.lines ?? [], key = JSON.stringify(raw);
        if (key !== linesKey) {
          next.lines = prepareLines(raw);
          nextLinesKey = key;
          structural = true;
        }
      }
      if (delta.current !== void 0) {
        if (delta.current !== null && (typeof delta.current !== "object" || Array.isArray(delta.current))) throw new TypeError("Invalid track");
        const current = delta.current === null ? null : Object.fromEntries(currentKeys.map((key) => [key, text(delta.current[key])]));
        if (current === null !== (state.current === null) || current && currentKeys.some((key) => current[key] !== state.current?.[key])) {
          next.current = current;
          structural = true;
        }
      }
      for (const key of ["currentTime", "duration"]) if (delta[key] !== void 0) {
        if (!Number.isFinite(delta[key]) || delta[key] < 0) throw new TypeError(`Invalid ${key}`);
        next[key] = delta[key];
      }
      if (delta.activeLineIndex !== void 0) {
        if (!Number.isInteger(delta.activeLineIndex) || delta.activeLineIndex < -1) throw new TypeError("Invalid active line");
        next.activeLineIndex = delta.activeLineIndex < next.lines.length ? delta.activeLineIndex : -1;
        structural ||= next.activeLineIndex !== state.activeLineIndex;
      }
      for (const key of ["playing", "visible"]) if (delta[key] !== void 0) {
        if (typeof delta[key] !== "boolean") throw new TypeError(`Invalid ${key}`);
        structural ||= next[key] !== delta[key];
        next[key] = delta[key];
      }
      const analysis = delta.analysis === void 0 ? state.analysis : analysisSnapshot(state.analysis, delta.analysis);
      const changedTrack = state.current !== null && ["id", "source"].some((key) => state.current?.[key] !== next.current?.[key]);
      next.analysis = !next.current || !next.playing || !next.visible || changedTrack ? EMPTY_ANALYSIS : analysis;
      linesKey = nextLinesKey;
      state = next;
      return { state, structural };
    }
  };
}

export {
  preferenceSnapshot,
  MODES2 as MODES,
  EMPTY_ANALYSIS,
  analysisSnapshot,
  applyAnalysis,
  acceptsMessage,
  localArtwork,
  createStateReducer
};
