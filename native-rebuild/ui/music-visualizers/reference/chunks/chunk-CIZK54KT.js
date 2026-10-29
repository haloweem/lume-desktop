import {
  colorWithAlpha
} from "./chunk-FTFNLWFP.js";
import {
  AnimatePresence,
  motion,
  require_jsx_runtime
} from "./chunk-AS2U3CHD.js";
import {
  resolveThemeFontWeight,
  resolveThemeTranslationFontStack
} from "./chunk-2CN2J6VF.js";
import {
  __toESM
} from "./chunk-XCBH6NLF.js";

// ../../../../../workspace/lume1.0.7/native-rebuild/ui/music-visualizers/reference/src/bridges.js
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

// src/utils/lyrics/alternateText.ts
var findAlternateText = (alternateTexts, role) => alternateTexts?.find((entry) => entry.role === role && entry.text.trim())?.text.trim() ?? null;
var resolveLyricAlternateText = (source, mode) => {
  if (!source || mode === "none") {
    return null;
  }
  const directText = mode === "translation" ? source.translation : source.romanization;
  return directText?.trim() || findAlternateText(source.alternateTexts, mode);
};
var resolveSubtitleContentMode = (mode, legacyShowTranslation = true) => mode ?? (legacyShowTranslation ? "translation" : "none");

// src/components/visualizer/VisualizerSubtitleOverlay.tsx
var import_jsx_runtime = __toESM(require_jsx_runtime(), 1);
var hasReadableText = (text) => !!text && /[\p{L}\p{N}]/u.test(text);
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
      upcomingLines: []
    };
  }
  const resolvedMode = resolveSubtitleContentMode(subtitleContentMode, showSubtitleTranslation);
  const subtitleText = [activeLine, recentCompletedLine].map((line) => resolveLyricAlternateText(line, resolvedMode)).find(hasReadableText) ?? null;
  return {
    shouldRenderOverlay: Boolean(subtitleText),
    subtitleText,
    upcomingLines: []
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
  isPlayerChromeHidden = false,
  hideTranslationSubtitle = false,
  showSubtitleTranslation = true,
  subtitleContentMode
}) => {
  const { shouldRenderOverlay, subtitleText, upcomingLines } = resolveVisualizerSubtitleOverlayContent({
    showText,
    activeLine,
    recentCompletedLine,
    nextLines: [],
    hideTranslationSubtitle,
    showSubtitleTranslation,
    subtitleContentMode
  });
  const resolvedOpacity = subtitleOverlayOpacity ?? opacity;
  const scaleFontSize = (fontSize) => fontSize.replace(/(-?\d*\.?\d+)(rem|vw|px)/g, (_match, value, unit) => `${(Number(value) * subtitleFontScale).toFixed(3)}${unit}`);
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
      animate: {
        opacity: resolvedOpacity,
        y: 0,
        bottom: isPlayerChromeHidden ? 32 : 112
      },
      exit: { opacity: 0, y: 20 },
      transition: {
        bottom: { type: "spring", stiffness: 280, damping: 28 },
        opacity: { duration: 0.24, ease: "easeOut" },
        y: { duration: 0.24, ease: "easeOut" }
      },
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
            "data-font-debug-target": "visualizer-translation",
            className: "relative z-10 max-w-4xl mx-auto",
            style: {
              color: theme.secondaryColor,
              fontSize: scaleFontSize(translationFontSize),
              fontFamily: resolveThemeTranslationFontStack(subtitleTheme ?? theme),
              fontWeight: resolveThemeFontWeight(subtitleTheme ?? theme, 500),
              textShadow
            },
            children: subtitleText
          },
          `trans-${activeLine?.startTime || recentCompletedLine?.startTime}`
        )
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
