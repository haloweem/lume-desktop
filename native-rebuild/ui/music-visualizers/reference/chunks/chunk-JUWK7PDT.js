import {
  DEFAULT_SONNET_TUNING
} from "./chunk-LH7Y2YK3.js";

// src/services/baseThemes.ts
var DEFAULT_THEME = {
  name: "Midnight Default",
  backgroundColor: "#09090b",
  // zinc-950
  primaryColor: "#f4f4f5",
  // zinc-100
  accentColor: "#f4f4f5",
  // zinc-100
  secondaryColor: "#71717a",
  // zinc-500
  fontStyle: "sans",
  animationIntensity: "normal"
};

// reference:reference:preference-theme
var buildVisualizerTheme = ({
  appStyle,
  theme,
  lyricsFontStyle,
  lyricsFontColor,
  lyricsKaraokeFillColor,
  sonnetTuning,
  lyricsFontWeight,
  lyricsCustomFontFamily,
  lyricsFontFallbackFamilies,
  subtitleFontInheritsLyrics,
  subtitleFontStyle,
  subtitleFontWeight,
  subtitleFontFamily,
  subtitleFontFallbackFamilies,
  currentSongId,
  visualizerMode
}) => {
  const visualizerBackgroundColor = String(
    appStyle["--bg-color"] ?? theme.backgroundColor
  );
  const visualizerBackgroundTheme = {
    ...theme,
    fontStyle: lyricsFontStyle,
    fontWeight: lyricsFontWeight ?? void 0,
    fontFamily: lyricsCustomFontFamily ?? void 0,
    fontFamilyStack: lyricsFontFallbackFamilies,
    backgroundColor: visualizerBackgroundColor
  };
  const applyLyricsFontColor = (sourceTheme) => lyricsFontColor ? {
    ...sourceTheme,
    primaryColor: lyricsFontColor,
    accentColor: lyricsFontColor,
    secondaryColor: lyricsFontColor
  } : sourceTheme;
  const isSonnet = visualizerMode === "sonnet";
  const visualizerTheme = {
    ...applyLyricsFontColor(isSonnet && sonnetTuning ? { ...visualizerBackgroundTheme, primaryColor: sonnetTuning.lyricsFontColor, accentColor: sonnetTuning.glowColor, secondaryColor: sonnetTuning.glowColor } : visualizerBackgroundTheme),
    karaokeFillColor: isSonnet && sonnetTuning ? sonnetTuning.karaokeFillColor : lyricsKaraokeFillColor || "#ffffff"
  };
  const visualizerSubtitleTheme = subtitleFontInheritsLyrics ?? true ? visualizerTheme : {
    ...applyLyricsFontColor({
      ...theme,
      fontStyle: subtitleFontStyle ?? "sans",
      fontWeight: subtitleFontWeight ?? void 0,
      fontFamily: subtitleFontFamily ?? void 0,
      fontFamilyStack: subtitleFontFallbackFamilies,
      backgroundColor: visualizerBackgroundColor
    }),
    karaokeFillColor: lyricsKaraokeFillColor || "#ffffff"
  };
  return {
    visualizerTheme,
    visualizerSubtitleTheme,
    visualizerBackgroundTheme,
    visualizerGeometrySeed: currentSongId ?? `geometry-${visualizerMode}`
  };
};
function build(mode, preferences) {
  return Object.freeze(buildVisualizerTheme({
    appStyle: {},
    theme: { ...DEFAULT_THEME, animationIntensity: preferences?.animationIntensity ?? DEFAULT_THEME.animationIntensity },
    lyricsFontStyle: DEFAULT_THEME.fontStyle,
    lyricsFontColor: preferences?.lyricsFontColor ?? "#ffffff",
    lyricsKaraokeFillColor: preferences?.lyricsKaraokeFillColor ?? "#ffffff",
    lyricsCustomFontFamily: preferences?.fontFamily || null,
    sonnetTuning: preferences?.sonnetColors ?? DEFAULT_SONNET_TUNING,
    visualizerMode: mode
  }).visualizerTheme);
}
var themes = Object.freeze(Object.fromEntries(["classic", "cadenza", "partita", "fume", "tilt", "monet", "sonnet", "gallery", "3d"].map((mode) => [mode, build(mode)])));
function resolvePreferenceTheme(mode, preferences) {
  return preferences ? build(mode, preferences) : themes[mode];
}

export {
  themes,
  resolvePreferenceTheme
};
