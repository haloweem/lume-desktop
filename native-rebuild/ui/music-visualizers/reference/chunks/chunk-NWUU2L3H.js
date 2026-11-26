import {
  AnimatePresence,
  motion,
  require_jsx_runtime
} from "./chunk-XSUOVEKG.js";
import {
  __toESM
} from "./chunk-NZLE2WMY.js";

// ../../src/bridges.js
var publishLiquidGlassWebGLSceneFrame = () => {
};
var unregisterLiquidGlassWebGLSceneCanvas = () => {
};
var getLumeEmbeddedTopInset = () => 0;
var getLumeFullBleedLayerStyle = () => ({ top: 0, height: "100%" });
var registerLumeTopVisualLayer = () => ({ requestFrame() {
}, dispose() {
} });
var captureLumeTopVisualFrame = () => false;
var translations = { "ui.waitingForMusic": "\u7B49\u5F85\u97F3\u4E50" };
var t = (key) => translations[key] ?? key;
var useTranslation = () => ({ t });

// ../../../reference-next/source/upstream/src/utils/fontStacks.ts
var MIN_FONT_WEIGHT = 100;
var MAX_FONT_WEIGHT = 900;
var FONT_WEIGHT_STEP = 10;
var normalizeFontWeight = (fontWeight) => {
  if (typeof fontWeight !== "number" || !Number.isFinite(fontWeight)) return null;
  const clamped = Math.min(MAX_FONT_WEIGHT, Math.max(MIN_FONT_WEIGHT, fontWeight));
  return Math.round(clamped / FONT_WEIGHT_STEP) * FONT_WEIGHT_STEP;
};
var resolveThemeFontWeight = (theme, fallback) => normalizeFontWeight(theme?.fontWeight) ?? fallback;
var SUGAR_SERIF_FAMILY = '"\u7345\u5C3E\u56DB\u5B63\u6625\u52A0\u7CD6SC"';
var BUILTIN_FONT_STACKS = {
  sans: '"Inter", "Noto Sans CJK SC", "Noto Sans JP", "Source Han Sans SC", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", "Helvetica Neue", Arial, sans-serif',
  serif: `${SUGAR_SERIF_FAMILY}, "Iowan Old Style", "Noto Serif CJK SC", "Noto Serif JP", "Source Han Serif SC", "Songti SC", "STSong", "Georgia", serif`,
  mono: '"IBM Plex Mono", "Sarasa Mono SC", "Noto Sans Mono CJK SC", "Noto Sans Mono", "SFMono-Regular", Consolas, monospace'
};
var TRANSLATION_FONT_STACKS = {
  sans: 'system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", "Inter", "Helvetica Neue", Arial, "Noto Sans CJK SC", "Source Han Sans SC", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", "Noto Sans JP", "Source Han Sans JP", "Hiragino Sans", "Yu Gothic", Meiryo, sans-serif',
  serif: `${SUGAR_SERIF_FAMILY}, "Folia Noto Serif SC", "Iowan Old Style", Georgia, "Times New Roman", "Noto Serif CJK SC", "Source Han Serif SC", "Songti SC", "STSong", "SimSun", "Noto Serif JP", "Source Han Serif JP", "Yu Mincho", "MS PMincho", serif`,
  mono: 'Consolas, "IBM Plex Mono", "SFMono-Regular", Menlo, Monaco, "Sarasa Mono SC", "Noto Sans Mono CJK SC", "SimHei", "DengXian", "Microsoft YaHei UI", "Microsoft YaHei", "Noto Sans Mono CJK JP", "MS Gothic", monospace'
};
var CSS_GENERIC_FONT_FAMILIES = /* @__PURE__ */ new Set([
  "serif",
  "sans-serif",
  "monospace",
  "cursive",
  "fantasy",
  "system-ui",
  "ui-serif",
  "ui-sans-serif",
  "ui-monospace",
  "ui-rounded",
  "emoji",
  "math",
  "fangsong"
]);
var quoteFontFamily = (fontFamily) => `"${fontFamily.replace(/["\\]/g, "\\$&")}"`;
var normalizeFontFamilyName = (fontFamily) => fontFamily.trim().replace(/^['"]|['"]$/g, "").trim();
var formatFontFamily = (fontFamily) => {
  const normalized = normalizeFontFamilyName(fontFamily);
  if (!normalized) return null;
  return CSS_GENERIC_FONT_FAMILIES.has(normalized.toLowerCase()) ? normalized : quoteFontFamily(normalized);
};
var normalizeFontFamilyStack = (fontFamilies) => {
  const seen = /* @__PURE__ */ new Set();
  const stack = [];
  fontFamilies?.forEach((fontFamily) => {
    const normalized = normalizeFontFamilyName(fontFamily ?? "");
    if (!normalized) return;
    const key = normalized.toLocaleLowerCase();
    if (seen.has(key)) return;
    seen.add(key);
    stack.push(normalized);
  });
  return stack;
};
var buildCustomFontFamilyStack = (theme) => {
  const familiesToNormalize = [
    ...theme.fontFamily ? [theme.fontFamily] : [],
    ...theme.fontFamilyStack ?? []
  ];
  if (familiesToNormalize.length === 0) {
    return [];
  }
  return normalizeFontFamilyStack(familiesToNormalize).map(formatFontFamily).filter((fontFamily) => Boolean(fontFamily));
};
var resolveThemeTranslationFontStack = (theme) => {
  const fallbackStack = TRANSLATION_FONT_STACKS[theme.fontStyle] ?? TRANSLATION_FONT_STACKS.sans;
  const customFontStack = buildCustomFontFamilyStack(theme);
  if (customFontStack.length === 0) {
    return fallbackStack;
  }
  return `${customFontStack.join(", ")}, ${fallbackStack}`;
};

// ../../../reference-next/source/upstream/src/utils/lyrics/alternateText.ts
var findAlternateText = (alternateTexts, role) => alternateTexts?.find((entry) => entry.role === role && entry.text.trim())?.text.trim() ?? null;
var resolveTrackText = (source, role) => {
  const directText = role === "translation" ? source.translation : source.romanization;
  return directText?.trim() || findAlternateText(source.alternateTexts, role);
};
var resolveLyricSubtitleTracks = (source, mode, isReadable = () => true) => {
  if (!source || mode === "none") {
    return [];
  }
  const roles = mode === "both" ? ["romanization", "translation"] : [mode];
  const tracks = [];
  for (const role of roles) {
    const text = resolveTrackText(source, role);
    if (!text || !isReadable(text)) {
      continue;
    }
    if (tracks.some((track) => track.text === text)) {
      continue;
    }
    tracks.push({ role, text });
  }
  return tracks;
};
var resolveSubtitleContentMode = (mode, legacyShowTranslation = true) => mode ?? (legacyShowTranslation ? "translation" : "none");

// ../../../reference-next/source/upstream/src/utils/lyrics/subtitleTrackStyle.ts
var SECONDARY_TRACK_SIZE_FACTOR = 0.88;
var SECONDARY_TRACK_OPACITY = 0.82;
var SECONDARY_TRACK_FONT_WEIGHT_FALLBACK = 400;
var SECONDARY_TRACK_GAP_EM = 0.25;

// ../../../reference-next/source/upstream/src/components/visualizer/textMeasureCache.ts
var rememberBounded = (cache, key, value, limit) => {
  if (!cache.has(key) && cache.size >= limit) {
    const oldest = cache.keys().next();
    if (!oldest.done) cache.delete(oldest.value);
  }
  cache.set(key, value);
  return value;
};

// ../../../reference-next/source/upstream/src/components/visualizer/colorMix.ts
var clamp = (value, min, max) => Math.max(min, Math.min(max, value));
var FALLBACK_RGB = { r: 255, g: 255, b: 255 };
var isFiniteChannel = (value) => Number.isFinite(value);
var formatRgba = (channels, alpha) => `rgba(${Math.round(clamp(channels.r, 0, 255))}, ${Math.round(clamp(channels.g, 0, 255))}, ${Math.round(clamp(channels.b, 0, 255))}, ${alpha})`;
var readColor = (normalizedColor) => {
  if (!normalizedColor) return "fallback";
  if (normalizedColor.startsWith("#")) {
    const hex = normalizedColor.slice(1);
    const parse = (value) => Number.parseInt(value, 16);
    if (/^[0-9a-fA-F]{3}$/.test(hex)) {
      return {
        r: parse(hex[0] + hex[0]),
        g: parse(hex[1] + hex[1]),
        b: parse(hex[2] + hex[2])
      };
    }
    if (/^[0-9a-fA-F]{6}$/.test(hex)) {
      return {
        r: parse(hex.slice(0, 2)),
        g: parse(hex.slice(2, 4)),
        b: parse(hex.slice(4, 6))
      };
    }
    return "fallback";
  }
  const rgbMatch = normalizedColor.match(/^rgba?\(([^)]+)\)$/);
  if (rgbMatch) {
    const [r, g, b] = rgbMatch[1].split(",").slice(0, 3).map((part) => Number.parseFloat(part.trim()));
    if ([r, g, b].every(isFiniteChannel)) {
      return { r, g, b };
    }
    return "fallback";
  }
  return "passthrough";
};
var parsedColors = /* @__PURE__ */ new Map();
var PARSED_COLOR_LIMIT = 512;
var parseColor = (color) => {
  const normalized = typeof color === "string" ? color.trim() : "";
  let parsed = parsedColors.get(normalized);
  if (parsed === void 0) {
    parsed = rememberBounded(parsedColors, normalized, readColor(normalized), PARSED_COLOR_LIMIT);
  }
  return { normalized, parsed };
};
var colorWithAlpha = (color, alpha) => {
  const normalizedAlpha = clamp(alpha, 0, 1);
  const { normalized, parsed } = parseColor(color);
  if (parsed === "passthrough") return normalized;
  return formatRgba(parsed === "fallback" ? FALLBACK_RGB : parsed, normalizedAlpha);
};

// reference:reference:subtitle-position
var usePlayerSubtitleBottomPx = (hidden) => hidden ? 32 : 112;

// ../../../reference-next/source/upstream/src/components/visualizer/VisualizerSubtitleOverlay.tsx
var import_jsx_runtime = __toESM(require_jsx_runtime());
var hasReadableText = (text) => !!text && /[\p{L}\p{N}]/u.test(text);
var getUpcomingLyricsClassName = (blur = true) => `truncate max-w-2xl mx-auto transition-all duration-500${blur ? " blur-[1px]" : ""}`;
var resolveVisualizerSubtitleOverlayContent = ({
  showText,
  activeLine,
  recentCompletedLine,
  nextLines,
  hideTranslationSubtitle = false,
  showSubtitleTranslation = true,
  subtitleContentMode
}) => {
  if (!showText || hideTranslationSubtitle) {
    return {
      shouldRenderOverlay: false,
      subtitleText: null,
      subtitleTracks: [],
      upcomingLines: []
    };
  }
  const resolvedMode = resolveSubtitleContentMode(subtitleContentMode, showSubtitleTranslation);
  const subtitleTracks = [activeLine, recentCompletedLine].map((line) => resolveLyricSubtitleTracks(line, resolvedMode, hasReadableText)).find((tracks) => tracks.length > 0) ?? [];
  const subtitleText = subtitleTracks.length > 0 ? subtitleTracks.map((track) => track.text).join(" / ") : null;
  const previewLines = nextLines.filter((line) => hasReadableText(line.fullText));
  return {
    shouldRenderOverlay: true,
    subtitleText,
    subtitleTracks,
    upcomingLines: subtitleText ? [] : activeLine ? previewLines : []
  };
};
var VisualizerSubtitleOverlay = ({
  showText,
  activeLine,
  recentCompletedLine,
  nextLines,
  theme,
  subtitleTheme,
  translationFontSize,
  upcomingFontSize,
  subtitleFontScale = 1,
  opacity = 0.6,
  subtitleOverlayOpacity,
  subtitleOverlayBackground = true,
  subtitleUpcomingLyricsBlur = true,
  isPlayerChromeHidden = false,
  hideTranslationSubtitle = false,
  showSubtitleTranslation = true,
  subtitleContentMode
}) => {
  const subtitleBottomPx = usePlayerSubtitleBottomPx(Boolean(isPlayerChromeHidden));
  const { shouldRenderOverlay, subtitleText, subtitleTracks, upcomingLines } = resolveVisualizerSubtitleOverlayContent({
    showText,
    activeLine,
    recentCompletedLine,
    nextLines,
    hideTranslationSubtitle,
    showSubtitleTranslation,
    subtitleContentMode
  });
  const resolvedOpacity = subtitleOverlayOpacity ?? opacity;
  const scaleFontSize = (fontSize, factor = 1) => fontSize.replace(/(-?\d*\.?\d+)(rem|vw|px)/g, (_match, value, unit) => `${(Number(value) * subtitleFontScale * factor).toFixed(3)}${unit}`);
  const contentClassName = subtitleOverlayBackground ? "relative isolate inline-block px-1.5 py-0.5" : "inline-block";
  const subtitleGlowStyle = subtitleOverlayBackground ? {
    background: `radial-gradient(ellipse 115% 130% at center, ${colorWithAlpha(theme.backgroundColor, 0.96)} 0%, ${colorWithAlpha(theme.backgroundColor, 0.78)} 62%, transparent 100%)`,
    transform: "translateZ(0)",
    WebkitTransform: "translateZ(0)",
    WebkitBackfaceVisibility: "hidden"
  } : void 0;
  const textShadow = `0 1px 2px ${colorWithAlpha(theme.backgroundColor, 0.24)}`;
  return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(AnimatePresence, { children: shouldRenderOverlay && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
    motion.div,
    {
      initial: { opacity: 0, y: 20 },
      animate: { opacity: resolvedOpacity, y: 0 },
      exit: { opacity: 0, y: 20 },
      transition: {
        opacity: { duration: 0.24, ease: "easeOut" },
        y: { duration: 0.24, ease: "easeOut" }
      },
      style: { bottom: subtitleBottomPx },
      className: "absolute left-0 right-0 text-center space-y-2 px-4 z-20 pointer-events-none",
      children: subtitleText ? /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", { className: contentClassName, children: [
        subtitleOverlayBackground && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          "div",
          {
            "aria-hidden": "true",
            className: "pointer-events-none absolute -inset-x-10 -inset-y-6 z-0 blur-2xl",
            style: subtitleGlowStyle
          }
        ),
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          motion.div,
          {
            initial: { opacity: 0, y: 10 },
            animate: { opacity: 1, y: 0 },
            exit: { opacity: 0 },
            className: "relative z-10 max-w-4xl mx-auto",
            children: subtitleTracks.map((track, index) => {
              const isSecondaryRow = index > 0;
              return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                "div",
                {
                  "data-font-debug-target": "visualizer-translation",
                  "data-subtitle-track": track.role,
                  style: {
                    color: theme.secondaryColor,
                    fontSize: scaleFontSize(translationFontSize, isSecondaryRow ? SECONDARY_TRACK_SIZE_FACTOR : 1),
                    fontFamily: resolveThemeTranslationFontStack(subtitleTheme ?? theme),
                    fontWeight: resolveThemeFontWeight(subtitleTheme ?? theme, isSecondaryRow ? SECONDARY_TRACK_FONT_WEIGHT_FALLBACK : 500),
                    opacity: isSecondaryRow ? SECONDARY_TRACK_OPACITY : void 0,
                    marginTop: isSecondaryRow ? `${SECONDARY_TRACK_GAP_EM}em` : void 0,
                    textShadow
                  },
                  children: track.text
                },
                track.role
              );
            })
          },
          `trans-${activeLine?.startTime || recentCompletedLine?.startTime}`
        )
      ] }) : activeLine && upcomingLines.length > 0 ? /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", { className: `${contentClassName} space-y-2`, children: [
        subtitleOverlayBackground && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          "div",
          {
            "aria-hidden": "true",
            className: "pointer-events-none absolute -inset-x-10 -inset-y-6 z-0 blur-2xl",
            style: subtitleGlowStyle
          }
        ),
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", { className: "relative z-10 space-y-2", children: upcomingLines.map((line, index) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          "p",
          {
            className: getUpcomingLyricsClassName(subtitleUpcomingLyricsBlur),
            style: {
              color: theme.secondaryColor,
              fontSize: scaleFontSize(upcomingFontSize),
              fontWeight: resolveThemeFontWeight(subtitleTheme ?? theme, 400),
              textShadow
            },
            children: line.fullText
          },
          index
        )) })
      ] }) : null
    }
  ) });
};
var VisualizerSubtitleOverlay_default = VisualizerSubtitleOverlay;

export {
  publishLiquidGlassWebGLSceneFrame,
  unregisterLiquidGlassWebGLSceneCanvas,
  getLumeEmbeddedTopInset,
  getLumeFullBleedLayerStyle,
  registerLumeTopVisualLayer,
  captureLumeTopVisualFrame,
  useTranslation,
  VisualizerSubtitleOverlay_default
};
