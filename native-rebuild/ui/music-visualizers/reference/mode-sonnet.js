import {
  compileSonnetProgram
} from "./chunks/chunk-42RMLPHV.js";
import {
  VisualizerSubtitleOverlay_default,
  getLumeEmbeddedTopInset,
  getLumeFullBleedLayerStyle,
  publishLiquidGlassWebGLSceneFrame,
  registerLumeTopVisualLayer,
  unregisterLiquidGlassWebGLSceneCanvas,
  useTranslation
} from "./chunks/chunk-CIZK54KT.js";
import {
  Shell_default,
  useVisualizerRuntime
} from "./chunks/chunk-FTFNLWFP.js";
import "./chunks/chunk-LDALZVSS.js";
import {
  require_jsx_runtime
} from "./chunks/chunk-AS2U3CHD.js";
import {
  require_react
} from "./chunks/chunk-RG3JEOUL.js";
import {
  getLineRenderEndTime
} from "./chunks/chunk-LSTKNWUO.js";
import {
  DEFAULT_SONNET_TUNING
} from "./chunks/chunk-LH7Y2YK3.js";
import {
  resolveThemeFontStack,
  resolveThemeFontWeight
} from "./chunks/chunk-2CN2J6VF.js";
import {
  __toESM
} from "./chunks/chunk-XCBH6NLF.js";

// src/components/visualizer/sonnet/VisualizerSonnet.tsx
var import_react = __toESM(require_react(), 1);
var import_jsx_runtime = __toESM(require_jsx_runtime(), 1);
var EMPTY_SONNET_LINES = [];
var VisualizerSonnet = (props) => {
  const {
    currentTime,
    currentLineIndex,
    lines,
    theme,
    audioPower,
    audioBands,
    showText = true,
    lyricsFontScale = 1,
    staticMode = false,
    paused = false,
    seed = "sonnet",
    songTitle,
    songArtist,
    songAlbum,
    isPlayerChromeHidden = false,
    sonnetTuning = DEFAULT_SONNET_TUNING
  } = props;
  const { t } = useTranslation();
  const hostRef = (0, import_react.useRef)(null);
  const runtimeRef = (0, import_react.useRef)(null);
  const pausedRef = (0, import_react.useRef)(paused);
  pausedRef.current = paused;
  const latestSongMetadataRef = (0, import_react.useRef)({
    title: songTitle,
    artist: songArtist,
    album: songAlbum
  });
  latestSongMetadataRef.current = {
    title: songTitle,
    artist: songArtist,
    album: songAlbum
  };
  const [runtimeFailed, setRuntimeFailed] = (0, import_react.useState)(false);
  const [isInstrumental, setIsInstrumental] = (0, import_react.useState)(false);
  const topInset = getLumeEmbeddedTopInset();
  const topVisualLayerRef = (0, import_react.useRef)(null);
  const lyricsSig = lines.length === 0 ? "" : `${lines.length}|${lines[0]?.fullText ?? ""}`;
  const seedRef = (0, import_react.useRef)(seed);
  (0, import_react.useEffect)(() => {
    if (lyricsSig !== "") {
      setIsInstrumental(false);
      seedRef.current = seed;
      return void 0;
    }
    if (seed !== seedRef.current) {
      setIsInstrumental(false);
      seedRef.current = seed;
    }
    let raf = 0;
    let sawReset = false;
    const startWall = performance.now();
    const watch = () => {
      const t2 = currentTime.get();
      const capped = performance.now() - startWall >= 3e3;
      if (!sawReset && t2 < 1) sawReset = true;
      if (sawReset && t2 >= 2 || capped) {
        setIsInstrumental(true);
        return;
      }
      raf = requestAnimationFrame(watch);
    };
    raf = requestAnimationFrame(watch);
    return () => cancelAnimationFrame(raf);
  }, [seed, lyricsSig, currentTime]);
  const virtualLines = (0, import_react.useMemo)(() => {
    if (!isInstrumental) return EMPTY_SONNET_LINES;
    const generated = [];
    for (let i = 0; i < 60; i++) {
      generated.push({
        id: `virtual-staff-${i}`,
        startTime: i * 8,
        endTime: i * 8 + 6,
        fullText: "\u266A",
        words: [],
        isChorus: false
      });
    }
    return generated;
  }, [isInstrumental]);
  const programLines = showText ? lines.length > 0 ? lines : virtualLines : EMPTY_SONNET_LINES;
  const program = (0, import_react.useMemo)(
    () => compileSonnetProgram(programLines, seed),
    [programLines, seed]
  );
  const { activeLine, recentCompletedLine } = useVisualizerRuntime({
    currentTime,
    currentLineIndex,
    lines,
    getLineEndTime: getLineRenderEndTime
  });
  const latestThemeRef = (0, import_react.useRef)(theme);
  latestThemeRef.current = theme;
  (0, import_react.useEffect)(() => {
    runtimeRef.current?.setTheme(theme);
  }, [theme]);
  (0, import_react.useEffect)(() => {
    const host = hostRef.current;
    if (!host) return void 0;
    let disposed = false;
    let createdRuntime = null;
    const abortController = new AbortController();
    setRuntimeFailed(false);
    void import("./chunks/createSonnetPixiRuntime-HZLFFSUT.js").then(({ SonnetPixiRuntime }) => {
      const metadata = latestSongMetadataRef.current;
      return SonnetPixiRuntime.create({
        host,
        program,
        theme: latestThemeRef.current,
        tuning: sonnetTuning,
        currentTime,
        audioPower,
        audioBands,
        lyricsFontScale,
        staticMode,
        shellOwnsWallpaper: props.background?.shellOwnsWallpaper ?? false,
        paused: pausedRef.current,
        onVisualFrame: () => {
          const canvas = runtimeRef.current?.getCanvas();
          if (canvas) publishLiquidGlassWebGLSceneFrame(canvas);
          topVisualLayerRef.current?.requestFrame();
        },
        songTitle: metadata.title,
        songArtist: metadata.artist,
        songAlbum: metadata.album,
        signal: abortController.signal
      });
    }).then((runtime) => {
      if (disposed) {
        runtime.destroy();
        return;
      }
      createdRuntime = runtime;
      runtimeRef.current = runtime;
      const lyricCanvas = runtime.getCanvas();
      lyricCanvas.setAttribute("data-lumenic-liquid-glass-scene-lyrics", "true");
      publishLiquidGlassWebGLSceneFrame(lyricCanvas);
      topVisualLayerRef.current = registerLumeTopVisualLayer({
        canvas: lyricCanvas,
        order: 20,
        role: "lyrics"
      });
      runtime.setTheme(latestThemeRef.current);
      runtime.setSongMetadata(latestSongMetadataRef.current);
      runtime.setPaused(pausedRef.current);
    }).catch((error) => {
      if (error instanceof DOMException && error.name === "AbortError") return;
      console.error("[Sonnet] Pixi runtime initialization failed", error);
      if (!disposed) window.dispatchEvent(new Event("lume-reference-renderer-error"));
      if (!disposed) setRuntimeFailed(true);
    });
    return () => {
      disposed = true;
      abortController.abort();
      topVisualLayerRef.current?.dispose();
      topVisualLayerRef.current = null;
      if (createdRuntime) {
        unregisterLiquidGlassWebGLSceneCanvas(createdRuntime.getCanvas());
        createdRuntime.destroy();
        if (runtimeRef.current === createdRuntime) runtimeRef.current = null;
      } else if (runtimeRef.current) {
        unregisterLiquidGlassWebGLSceneCanvas(runtimeRef.current.getCanvas());
        runtimeRef.current.destroy();
        runtimeRef.current = null;
      }
      host.replaceChildren();
    };
  }, [
    currentTime,
    lyricsFontScale,
    program,
    sonnetTuning,
    staticMode,
    props.background?.shellOwnsWallpaper
  ]);
  (0, import_react.useEffect)(() => {
    runtimeRef.current?.setSongMetadata(latestSongMetadataRef.current);
  }, [songAlbum, songArtist, songTitle]);
  (0, import_react.useEffect)(() => {
    runtimeRef.current?.setPaused(paused);
  }, [paused]);
  (0, import_react.useEffect)(() => currentTime.on("change", () => {
    if (paused) runtimeRef.current?.renderOnce();
  }), [currentTime, paused]);
  const fallbackFontFamily = resolveThemeFontStack(theme);
  const fallbackFontWeight = resolveThemeFontWeight(theme, 600);
  const finalLine = lines.at(-1);
  const creditsRecentCompletedLine = recentCompletedLine === finalLine ? null : recentCompletedLine;
  return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(
    Shell_default,
    {
      theme,
      audioPower,
      audioBands,
      sharedProps: props,
      children: [
        /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", { className: "pointer-events-none absolute inset-0 z-10 overflow-hidden", children: [
          /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            "div",
            {
              ref: hostRef,
              className: "absolute inset-x-0 bottom-0 z-10",
              style: getLumeFullBleedLayerStyle(topInset),
              "aria-hidden": "true"
            }
          ),
          (runtimeFailed || program.paragraphs.length === 0) && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            "div",
            {
              className: "absolute inset-0 flex items-center justify-center px-10 text-center transition-opacity duration-300",
              style: {
                color: theme.primaryColor,
                fontFamily: fallbackFontFamily,
                fontWeight: fallbackFontWeight,
                fontSize: `clamp(2rem, ${5.4 * lyricsFontScale}vw, 5.6rem)`
              },
              children: showText && !isInstrumental ? activeLine?.fullText || t("ui.waitingForMusic") : null
            }
          )
        ] }),
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          VisualizerSubtitleOverlay_default,
          {
            showText,
            activeLine,
            recentCompletedLine: creditsRecentCompletedLine,
            nextLines: [],
            theme,
            translationFontSize: `clamp(${1.05 * lyricsFontScale}rem, ${2.2 * lyricsFontScale}vw, ${1.25 * lyricsFontScale}rem)`,
            upcomingFontSize: `clamp(${0.9 * lyricsFontScale}rem, ${1.8 * lyricsFontScale}vw, ${1.05 * lyricsFontScale}rem)`,
            isPlayerChromeHidden,
            hideTranslationSubtitle: false,
            showSubtitleTranslation: true,
            subtitleContentMode: "translation"
          }
        )
      ]
    }
  );
};
var VisualizerSonnet_default = VisualizerSonnet;
export {
  VisualizerSonnet_default as default
};
