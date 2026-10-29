
import {DEFAULT_THEME} from 'H:/refactor/history/lume1.0.1/third_party/folia-major/upstream/src/services/baseThemes.ts';
import {DEFAULT_SONNET_TUNING} from 'H:/refactor/history/lume1.0.1/third_party/folia-major/upstream/src/types.ts';
export {resolveThemeFontStack} from 'reference:fonts';
const buildVisualizerTheme = ({
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
    visualizerMode,
}: {
    appStyle: CSSProperties;
    theme: Theme;
    lyricsFontStyle: Theme['fontStyle'];
    lyricsFontColor?: string | null;
    lyricsKaraokeFillColor?: string | null;
    sonnetTuning?: { lyricsFontColor: string; karaokeFillColor: string; glowColor: string };
    lyricsFontWeight?: number | null;
    lyricsCustomFontFamily: string | null;
    lyricsFontFallbackFamilies?: string[];
    subtitleFontInheritsLyrics?: boolean;
    subtitleFontStyle?: Theme['fontStyle'];
    subtitleFontWeight?: number | null;
    subtitleFontFamily?: string | null;
    subtitleFontFallbackFamilies?: string[];
    currentSongId?: MediaId | null;
    visualizerMode: VisualizerMode;
}) => {
    const visualizerBackgroundColor = String(
        (appStyle as CSSProperties & { '--bg-color'?: string })['--bg-color'] ?? theme.backgroundColor,
    );
    const visualizerBackgroundTheme: Theme = {
        ...theme,
        fontStyle: lyricsFontStyle,
        fontWeight: lyricsFontWeight ?? undefined,
        fontFamily: lyricsCustomFontFamily ?? undefined,
        fontFamilyStack: lyricsFontFallbackFamilies,
        backgroundColor: visualizerBackgroundColor,
    };
    const applyLyricsFontColor = (sourceTheme: Theme): Theme => lyricsFontColor
        ? {
            ...sourceTheme,
            primaryColor: lyricsFontColor,
            accentColor: lyricsFontColor,
            secondaryColor: lyricsFontColor,
        }
        : sourceTheme;
    const isSonnet = visualizerMode === 'sonnet';
    const visualizerTheme = {
        ...applyLyricsFontColor(isSonnet && sonnetTuning ? { ...visualizerBackgroundTheme, primaryColor: sonnetTuning.lyricsFontColor, accentColor: sonnetTuning.glowColor, secondaryColor: sonnetTuning.glowColor } : visualizerBackgroundTheme),
        karaokeFillColor: isSonnet && sonnetTuning ? sonnetTuning.karaokeFillColor : (lyricsKaraokeFillColor || '#ffffff'),
    };
    const visualizerSubtitleTheme: Theme = (subtitleFontInheritsLyrics ?? true)
        ? visualizerTheme
        : {
            ...applyLyricsFontColor({
            ...theme,
            fontStyle: subtitleFontStyle ?? 'sans',
            fontWeight: subtitleFontWeight ?? undefined,
            fontFamily: subtitleFontFamily ?? undefined,
            fontFamilyStack: subtitleFontFallbackFamilies,
            backgroundColor: visualizerBackgroundColor,
            }),
            karaokeFillColor: lyricsKaraokeFillColor || '#ffffff',
        };

    return {
        visualizerTheme,
        visualizerSubtitleTheme,
        visualizerBackgroundTheme,
        visualizerGeometrySeed: currentSongId ?? `geometry-${visualizerMode}`,
    };
};
function build(mode,preferences) {
  return Object.freeze(buildVisualizerTheme({appStyle:{},theme:{...DEFAULT_THEME,animationIntensity:preferences?.animationIntensity??DEFAULT_THEME.animationIntensity},
    lyricsFontStyle:DEFAULT_THEME.fontStyle,lyricsFontColor:preferences?.lyricsFontColor??"#ffffff",
    lyricsKaraokeFillColor:preferences?.lyricsKaraokeFillColor??"#ffffff",
    lyricsCustomFontFamily:preferences?.fontFamily||null,sonnetTuning:preferences?.sonnetColors??DEFAULT_SONNET_TUNING,visualizerMode:mode}).visualizerTheme);
}
export const themes=Object.freeze(Object.fromEntries(["classic","cadenza","partita","fume","tilt","monet","sonnet","gallery","3d"].map(mode=>[mode,build(mode)])));
export function resolvePreferenceTheme(mode,preferences) {return preferences?build(mode,preferences):themes[mode];}
