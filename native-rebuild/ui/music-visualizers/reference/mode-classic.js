import {
  buildDisplayWordsFromLayoutUnits,
  buildPostLyricLayoutUnits
} from "./chunks/chunk-QRS5KLOB.js";
import {
  resolveWordColor
} from "./chunks/chunk-FIS2XO7C.js";
import {
  VisualizerSubtitleOverlay_default,
  useTranslation
} from "./chunks/chunk-CIZK54KT.js";
import {
  Shell_default,
  useVisualizerRuntime
} from "./chunks/chunk-FTFNLWFP.js";
import {
  buildWordGraphemeTimings
} from "./chunks/chunk-LDALZVSS.js";
import {
  AnimatePresence,
  motion,
  require_jsx_runtime,
  useMotionValueEvent
} from "./chunks/chunk-AS2U3CHD.js";
import {
  require_react
} from "./chunks/chunk-RG3JEOUL.js";
import {
  getLineRenderEndTime,
  getLineRenderHints
} from "./chunks/chunk-LSTKNWUO.js";
import {
  DEFAULT_CLASSIC_TUNING
} from "./chunks/chunk-LH7Y2YK3.js";
import {
  resolveThemeFontStack,
  resolveThemeFontWeight
} from "./chunks/chunk-2CN2J6VF.js";
import {
  __toESM
} from "./chunks/chunk-XCBH6NLF.js";

// src/components/visualizer/classic/Visualizer.tsx
var import_react = __toESM(require_react(), 1);
var import_jsx_runtime = __toESM(require_jsx_runtime(), 1);
var clampClassicBreathingFloatMultiplier = (value) => Math.min(2, Math.max(0, value));
var clampClassicWordSpacing = (value) => Math.min(2, Math.max(0, value));
var resolveClassicTuning = (tuning) => ({
  enableWordRotation: tuning?.enableWordRotation ?? DEFAULT_CLASSIC_TUNING.enableWordRotation,
  breathingFloatMultiplier: clampClassicBreathingFloatMultiplier(
    tuning?.breathingFloatMultiplier ?? DEFAULT_CLASSIC_TUNING.breathingFloatMultiplier
  ),
  useLegacyLayout: tuning?.useLegacyLayout ?? DEFAULT_CLASSIC_TUNING.useLegacyLayout,
  wordSpacing: clampClassicWordSpacing(tuning?.wordSpacing ?? DEFAULT_CLASSIC_TUNING.wordSpacing ?? 0.7)
});
var resolveClassicLineRenderProfile = (line) => {
  if (!line) {
    return null;
  }
  const renderHints = getLineRenderHints(line);
  const wordRevealMode = renderHints?.wordRevealMode ?? "normal";
  return {
    renderHints,
    lineRenderEndTime: getLineRenderEndTime(line),
    lineTransitionMode: renderHints?.lineTransitionMode ?? "normal",
    wordRevealMode,
    wordLookahead: wordRevealMode === "instant" ? 0.03 : wordRevealMode === "fast" ? 0.08 : 0.15
  };
};
var getClassicWordActiveEndTime = (word, renderProfile) => {
  if (renderProfile.wordRevealMode === "instant") {
    return renderProfile.lineRenderEndTime;
  }
  if (renderProfile.wordRevealMode === "fast") {
    return Math.min(renderProfile.lineRenderEndTime, Math.max(word.endTime, word.startTime + 0.12));
  }
  return word.endTime;
};
var getClassicWordDisplayDuration = (word, renderProfile) => {
  const activeEndTime = getClassicWordActiveEndTime(word, renderProfile);
  const minDuration = renderProfile.wordRevealMode === "instant" ? 0.08 : renderProfile.wordRevealMode === "fast" ? 0.12 : 0.1;
  return Math.max(activeEndTime - word.startTime, minDuration);
};
var getClassicLineContainerMotion = (renderProfile) => {
  if (renderProfile?.lineTransitionMode === "none") {
    return {
      initial: { opacity: 1, scale: 1, filter: "blur(0px)" },
      animate: { opacity: 1, scale: 1, filter: "blur(0px)", transitionEnd: { filter: "none" } },
      exit: { opacity: 0, scale: 1.02, filter: "blur(6px)", transition: { duration: 0.12, ease: "easeOut" } }
    };
  }
  if (renderProfile?.lineTransitionMode === "fast") {
    return {
      initial: { opacity: 0.35, scale: 0.96, filter: "blur(4px)" },
      animate: {
        opacity: 1,
        scale: 1,
        filter: "blur(0px)",
        transition: { duration: 0.16, ease: "easeOut" },
        transitionEnd: { filter: "none" }
      },
      exit: {
        opacity: 0,
        scale: 1.04,
        filter: "blur(10px)",
        transition: { duration: 0.16, ease: "easeInOut" }
      }
    };
  }
  return {
    initial: { opacity: 0, scale: 0.9, filter: "blur(10px)" },
    animate: { opacity: 1, scale: 1, filter: "blur(0px)", transitionEnd: { filter: "none" } },
    exit: { opacity: 0, scale: 1.1, filter: "blur(20px)", transition: { duration: 0.3 } }
  };
};
var classicMeasureCanvas = null;
var measureWordWidth = (text, pxSize, fontStack, fontWeight) => {
  if (typeof document === "undefined") {
    return text.length * pxSize * 0.65;
  }
  if (!classicMeasureCanvas) {
    classicMeasureCanvas = document.createElement("canvas");
  }
  const context = classicMeasureCanvas.getContext("2d");
  if (!context) {
    return text.length * pxSize * 0.65;
  }
  context.font = `${fontWeight} ${pxSize}px ${fontStack}`;
  return context.measureText(text).width;
};
var Word = ({ word, config, currentTime, theme, layoutVariants, bodyVariants, glowVariants, baseColor, activeColor, renderProfile, isChorus, fontSize }) => {
  const [status, setStatus] = (0, import_react.useState)("waiting");
  const rippleScale = (0, import_react.useMemo)(() => 1.5 + Math.random() * 2, []);
  const duration = getClassicWordDisplayDuration(word, renderProfile);
  const activeEndTime = getClassicWordActiveEndTime(word, renderProfile);
  const graphemeTimings = (0, import_react.useMemo)(() => buildWordGraphemeTimings(word), [word]);
  useMotionValueEvent(currentTime, "change", (latest) => {
    let newStatus = "waiting";
    if (latest >= word.startTime - renderProfile.wordLookahead && latest <= activeEndTime) {
      newStatus = "active";
    } else if (latest > activeEndTime) {
      newStatus = "passed";
    } else {
      newStatus = "waiting";
    }
    if (newStatus !== status) {
      setStatus(newStatus);
    }
  });
  return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(
    motion.div,
    {
      custom: {
        config,
        activeColor,
        baseColor,
        duration,
        wordRevealMode: renderProfile.wordRevealMode
      },
      variants: layoutVariants,
      initial: "waiting",
      animate: status,
      className: "inline-block origin-center relative will-change-transform whitespace-nowrap",
      style: {
        fontSize,
        fontWeight: resolveThemeFontWeight(theme, 700),
        marginRight: config.marginRight,
        alignSelf: config.alignSelf,
        lineHeight: 1.22
      },
      children: [
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          "span",
          {
            className: "absolute inset-0 select-none pointer-events-none block",
            "aria-hidden": "true",
            children: graphemeTimings.length > 1 ? graphemeTimings.map((timing, index) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
              motion.span,
              {
                variants: glowVariants,
                custom: {
                  config,
                  activeColor,
                  baseColor,
                  duration,
                  index,
                  total: graphemeTimings.length,
                  charStartTime: timing.startTime,
                  charEndTime: timing.endTime,
                  wordStartTime: word.startTime,
                  wordRevealMode: renderProfile.wordRevealMode
                },
                children: timing.char
              },
              index
            )) : /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
              motion.span,
              {
                variants: glowVariants,
                custom: { config, activeColor, baseColor, duration, wordRevealMode: renderProfile.wordRevealMode },
                children: word.text
              }
            )
          }
        ),
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          motion.span,
          {
            variants: bodyVariants,
            custom: { config, activeColor, baseColor, duration, wordRevealMode: renderProfile.wordRevealMode },
            className: "relative z-10 block",
            children: word.text
          }
        ),
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)(AnimatePresence, { children: isChorus && status === "active" && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          motion.span,
          {
            className: "absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 h-[150%] aspect-square rounded-full border-1 pointer-events-none z-0",
            style: { borderColor: activeColor, filter: "blur(1px)" },
            initial: { scale: 0.2, opacity: 0.8 },
            animate: { scale: rippleScale, opacity: 0 },
            exit: { opacity: 0 },
            transition: { duration: 0.5, ease: "easeOut" }
          },
          "ripple"
        ) })
      ]
    },
    `${config.id}`
  );
};
var Visualizer = (props) => {
  const {
    currentTime,
    currentLineIndex,
    lines,
    theme,
    subtitleTheme,
    audioPower,
    audioBands,
    showText = true,
    lyricsFontScale = 1,
    subtitleFontScale = 1,
    subtitleOverlayOpacity,
    subtitleOverlayBackground,
    isPlayerChromeHidden = false,
    hideTranslationSubtitle = false,
    showSubtitleTranslation = true,
    subtitleContentMode,
    classicTuning
  } = props;
  const { t } = useTranslation();
  const resolvedClassicTuning = (0, import_react.useMemo)(() => resolveClassicTuning(classicTuning), [classicTuning]);
  const {
    activeLine,
    recentCompletedLine,
    nextLines
  } = useVisualizerRuntime({
    currentTime,
    currentLineIndex,
    lines,
    getLineEndTime: getLineRenderEndTime
  });
  const activeLineRenderProfile = activeLine ? resolveClassicLineRenderProfile(activeLine) : null;
  const activeWordRenderProfile = activeLineRenderProfile ?? (activeLine ? resolveClassicLineRenderProfile(activeLine) : null);
  const activeLineContainerMotion = getClassicLineContainerMotion(activeLineRenderProfile);
  const [viewportWidth, setViewportWidth] = (0, import_react.useState)(() => typeof window === "undefined" ? 1200 : window.innerWidth);
  (0, import_react.useEffect)(() => {
    const handleResize = () => {
      setViewportWidth(window.innerWidth);
    };
    window.addEventListener("resize", handleResize);
    return () => window.removeEventListener("resize", handleResize);
  }, []);
  const displayWords = (0, import_react.useMemo)(() => {
    if (!activeLine) return [];
    if (resolvedClassicTuning.useLegacyLayout) {
      return activeLine.words;
    }
    const layoutUnits = buildPostLyricLayoutUnits(activeLine, { semantic: true, sticky: true });
    return buildDisplayWordsFromLayoutUnits(layoutUnits);
  }, [activeLine, resolvedClassicTuning.useLegacyLayout]);
  const mainFontSize = `clamp(${(2.25 * lyricsFontScale).toFixed(3)}rem, ${(6 * lyricsFontScale).toFixed(3)}vw, ${(4.5 * lyricsFontScale).toFixed(3)}rem)`;
  const emptyFontSize = `clamp(${(1.5 * lyricsFontScale).toFixed(3)}rem, ${(3.5 * lyricsFontScale).toFixed(3)}vw, ${(2.25 * lyricsFontScale).toFixed(3)}rem)`;
  const translationFontSize = `clamp(${(1.125 * lyricsFontScale).toFixed(3)}rem, ${(2.6 * lyricsFontScale).toFixed(3)}vw, ${(1.25 * lyricsFontScale).toFixed(3)}rem)`;
  const upcomingFontSize = `clamp(${(0.875 * lyricsFontScale).toFixed(3)}rem, ${(2 * lyricsFontScale).toFixed(3)}vw, ${(1 * lyricsFontScale).toFixed(3)}rem)`;
  const { wordConfigs, lineConfig } = (0, import_react.useMemo)(() => {
    if (!activeLine) return { wordConfigs: [], lineConfig: { justifyContent: "center", alignItems: "center", perspective: 1e3 } };
    const seed = activeLine.startTime;
    const intensity = theme.animationIntensity;
    const isChaotic = intensity === "chaotic";
    const isCalm = intensity === "calm";
    const justifyOptions = isCalm ? ["justify-center"] : ["justify-start", "justify-center", "justify-end", "justify-around", "justify-between"];
    const alignOptions = isCalm ? ["items-center"] : ["items-start", "items-center", "items-end"];
    const isInterlude = activeLine.fullText === "......";
    const lineConfig2 = {
      justifyContent: isInterlude ? "justify-center" : justifyOptions[Math.floor(seed % justifyOptions.length)],
      // deterministic random
      alignItems: alignOptions[Math.floor(seed * 2 % alignOptions.length)],
      perspective: isChaotic ? 500 + seed % 500 : 1e3
    };
    const fontStack = resolveThemeFontStack(theme);
    const fontWeight = resolveThemeFontWeight(theme, 700);
    const getPixelFontSize = (fontScale, width) => {
      const rem = 16;
      const minPx = 2.25 * fontScale * rem;
      const valPx = 6 * fontScale * width / 100;
      const maxPx = 4.5 * fontScale * rem;
      return Math.max(minPx, Math.min(valPx, maxPx));
    };
    const pxFontSize = getPixelFontSize(lyricsFontScale, viewportWidth);
    const wordWidths = displayWords.map((w) => measureWordWidth(w.text, pxFontSize, fontStack, fontWeight));
    const baseSpread = isChaotic ? 60 : isCalm ? 0 : 20;
    const baseRotate = isChaotic ? 30 : isCalm ? 0 : 5;
    const wordConfigs2 = displayWords.map((w, i) => {
      const wordSeed = seed + i;
      const random = (offset) => {
        const x = Math.sin(wordSeed + offset) * 1e4;
        return x - Math.floor(x);
      };
      if (isInterlude) {
        return {
          id: `${w.text}-${i}-${seed}`,
          x: 0,
          y: (random(2) - 0.5) * 15,
          // Slight vertical randomness
          rotate: 0,
          scale: 1.5,
          marginRight: "3rem",
          alignSelf: "center",
          passedRotate: 0
        };
      }
      const wordConfigScale = isChaotic ? 0.8 + random(4) * 0.6 : 1.1 + random(4) * 0.2;
      const xVal = (random(1) - 0.5) * baseSpread * 2;
      const yVal = (random(2) - 0.5) * baseSpread * 2;
      let marginRight = isChaotic ? `${random(5) * 1.5}rem` : "0.8rem";
      if (!resolvedClassicTuning.useLegacyLayout) {
        const w_i = wordWidths[i] ?? 0;
        const s_i = wordConfigScale * 1.4;
        let w_next = 0;
        let s_next = 1;
        let x_next = 0;
        if (i + 1 < displayWords.length) {
          const nextSeed = seed + (i + 1);
          const nextRandom = (offset) => {
            const x = Math.sin(nextSeed + offset) * 1e4;
            return x - Math.floor(x);
          };
          const nextConfigScale = isChaotic ? 0.8 + nextRandom(4) * 0.6 : 1.1 + nextRandom(4) * 0.2;
          s_next = nextConfigScale * 1.4;
          x_next = (nextRandom(1) - 0.5) * baseSpread * 2;
          w_next = wordWidths[i + 1] ?? 0;
        }
        const spacingMultiplier = resolvedClassicTuning.wordSpacing ?? 0.7;
        const gap = 0.05 * pxFontSize;
        const halfOverflow_i = w_i * (s_i - 1) / 2;
        const halfOverflow_next = w_next * (s_next - 1) / 2;
        const xOffsetDiff = xVal - x_next;
        const calculatedMargin = (halfOverflow_i + halfOverflow_next + xOffsetDiff + gap) * spacingMultiplier;
        const minMargin = (isChaotic ? 0.08 * pxFontSize : 0.12 * pxFontSize) * spacingMultiplier;
        const finalMargin = Math.max(minMargin, calculatedMargin);
        marginRight = `${finalMargin.toFixed(1)}px`;
      }
      return {
        id: `${w.text}-${i}-${seed}`,
        x: xVal,
        y: yVal,
        rotate: resolvedClassicTuning.enableWordRotation ? (random(3) - 0.5) * baseRotate * 2 : 0,
        scale: wordConfigScale,
        marginRight,
        alignSelf: isChaotic && random(6) > 0.7 ? random(7) > 0.5 ? "flex-start" : "flex-end" : "auto",
        passedRotate: resolvedClassicTuning.enableWordRotation ? (random(8) - 0.5) * 45 : 0
      };
    });
    return { wordConfigs: wordConfigs2, lineConfig: lineConfig2 };
  }, [activeLine, displayWords, resolvedClassicTuning.enableWordRotation, resolvedClassicTuning.useLegacyLayout, resolvedClassicTuning.wordSpacing, theme, lyricsFontScale, viewportWidth]);
  const layoutVariants = {
    waiting: ({ config }) => ({
      opacity: 0,
      scale: 0.5,
      x: config.x + Math.sin(config.y) * 100,
      y: config.y + Math.cos(config.x) * 50,
      rotate: resolvedClassicTuning.enableWordRotation ? config.rotate + 20 : 0,
      transition: { duration: 0.4 }
    }),
    active: ({ config }) => ({
      opacity: 1,
      scale: isNaN(config.scale) ? 1.5 : config.scale * 1.4,
      x: config.x,
      y: config.y,
      rotate: config.rotate,
      transition: {
        type: "spring",
        stiffness: 200,
        damping: 20,
        opacity: { duration: 0.1 }
      }
    }),
    passed: ({ config }) => ({
      opacity: theme.animationIntensity === "chaotic" ? 0.9 : 0.82,
      scale: config.scale || 1,
      x: config.x,
      y: config.y,
      rotate: config.rotate + config.passedRotate,
      transition: {
        duration: 0.5,
        rotate: {
          duration: 5,
          ease: "linear"
        }
      }
    })
  };
  const bodyVariants = {
    waiting: ({ baseColor }) => ({
      color: baseColor,
      filter: "blur(10px)",
      transition: { duration: 0.4 }
    }),
    active: ({ activeColor, duration, wordRevealMode }) => ({
      color: activeColor,
      filter: "none",
      transition: {
        color: { duration: duration || 0.2, ease: "linear" },
        filter: { type: "tween", duration: wordRevealMode === "instant" ? 0.08 : wordRevealMode === "fast" ? 0.12 : 0.2 }
      },
      transitionEnd: {
        filter: "none"
      }
    }),
    passed: ({ baseColor, wordRevealMode }) => ({
      color: baseColor,
      filter: "blur(0px)",
      transition: {
        color: { duration: wordRevealMode === "instant" ? 0.12 : wordRevealMode === "fast" ? 0.24 : 0.8, ease: "easeInOut" },
        filter: { duration: wordRevealMode === "instant" ? 0.12 : wordRevealMode === "fast" ? 0.2 : 0.5 }
      },
      transitionEnd: {
        filter: "none"
      }
    })
  };
  const glowVariants = {
    waiting: {
      color: "transparent",
      textShadow: "none"
    },
    active: ({ activeColor, duration, index, total, charStartTime, charEndTime, wordStartTime, wordRevealMode }) => {
      if (wordRevealMode === "instant") {
        return {
          color: "transparent",
          textShadow: [
            "none",
            `0 0 14px ${activeColor}, 0 0 24px ${activeColor}`,
            "none"
          ],
          transition: {
            duration: Math.min(duration || 0.08, 0.12),
            times: [0, 0.35, 1],
            ease: "easeOut"
          }
        };
      }
      if (wordRevealMode === "fast") {
        return {
          color: "transparent",
          textShadow: [
            "none",
            `0 0 18px ${activeColor}, 0 0 32px ${activeColor}`,
            "none"
          ],
          transition: {
            duration: Math.min(Math.max(duration || 0.12, 0.12), 0.2),
            times: [0, 0.4, 1],
            ease: "easeInOut"
          }
        };
      }
      if (total !== void 0 && total > 1) {
        const singleDuration = duration / total;
        const hasCharTiming = typeof charStartTime === "number" && typeof charEndTime === "number" && typeof wordStartTime === "number";
        const resolvedCharDuration = hasCharTiming ? charEndTime - charStartTime : 0;
        const charDuration = hasCharTiming ? Math.max(resolvedCharDuration, 1e-3) : singleDuration;
        const charDelay = hasCharTiming ? Math.max(0, charStartTime - wordStartTime) : singleDuration * index;
        return {
          color: "transparent",
          textShadow: [
            "none",
            `0 0 20px ${activeColor}, 0 0 40px ${activeColor}`,
            "none"
          ],
          transition: {
            duration: charDuration * 6,
            // stretch the fade over a few letters
            times: [0, 0.3, 1],
            // peak early, then fade
            delay: charDelay,
            ease: "easeInOut"
          }
        };
      }
      return {
        color: "transparent",
        textShadow: [
          "none",
          `0 0 20px ${activeColor}, 0 0 40px ${activeColor}`,
          `0 0 20px ${activeColor}, 0 0 40px ${activeColor}`
        ],
        transition: {
          duration: duration || 0.1,
          // stretch the fade over the word duration
          times: [0, 0.9, 1],
          // peak early, then fade
          ease: "easeInOut"
        }
      };
    },
    passed: ({ wordRevealMode }) => ({
      color: "transparent",
      textShadow: "none",
      transition: { duration: wordRevealMode === "instant" ? 0.12 : wordRevealMode === "fast" ? 0.22 : 0.9, ease: "easeOut" }
    })
  };
  const lyricContainerFloat = (0, import_react.useMemo)(() => {
    const multiplier = resolvedClassicTuning.breathingFloatMultiplier;
    if (multiplier <= 0) {
      return null;
    }
    const configByIntensity = {
      calm: { distance: 10, duration: 8.5 },
      normal: { distance: 14, duration: 7 },
      chaotic: { distance: 18, duration: 5.8 }
    };
    const { distance, duration } = configByIntensity[theme.animationIntensity];
    const scaledDistance = distance * multiplier;
    return {
      animate: {
        y: [0, -scaledDistance, 0, scaledDistance * 0.45, 0],
        scale: [1, 1 + 0.01 * multiplier, 1, 1 - 5e-3 * multiplier, 1]
      },
      transition: {
        duration,
        repeat: Infinity,
        ease: "easeInOut"
      }
    };
  }, [resolvedClassicTuning.breathingFloatMultiplier, theme.animationIntensity]);
  return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(
    Shell_default,
    {
      theme,
      audioPower,
      audioBands,
      sharedProps: props,
      children: [
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          motion.div,
          {
            className: "relative z-10 w-full h-[70vh] flex items-center justify-center p-8 pointer-events-none will-change-transform",
            animate: lyricContainerFloat?.animate,
            transition: lyricContainerFloat?.transition,
            children: /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(AnimatePresence, { mode: "popLayout", children: [
              showText && activeLine && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                motion.div,
                {
                  initial: activeLineContainerMotion.initial,
                  animate: activeLineContainerMotion.animate,
                  exit: activeLineContainerMotion.exit,
                  className: `flex flex-wrap w-full max-w-6xl content-center ${lineConfig.justifyContent} ${lineConfig.alignItems}`,
                  style: { perspective: `${lineConfig.perspective}px`, minHeight: "300px" },
                  children: displayWords.map((word, idx) => {
                    const config = wordConfigs[idx] || { id: `fallback-${idx}`, x: 0, y: 0, rotate: 0, scale: 1, marginRight: "0.5rem", alignSelf: "auto", passedRotate: 0 };
                    const activeColor = resolveWordColor(word.text, theme.wordColors, theme.accentColor);
                    return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                      Word,
                      {
                        word,
                        config,
                        currentTime,
                        theme,
                        layoutVariants,
                        bodyVariants,
                        glowVariants,
                        baseColor: theme.primaryColor,
                        activeColor,
                        renderProfile: activeWordRenderProfile,
                        isChorus: activeLine.isChorus,
                        fontSize: mainFontSize
                      },
                      `${word.text}-${idx}-${activeLine.startTime}`
                    );
                  })
                },
                activeLine.startTime
              ),
              showText && !activeLine && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                motion.div,
                {
                  initial: { opacity: 0 },
                  animate: { opacity: 1 },
                  exit: { opacity: 0 },
                  className: "text-2xl opacity-50 absolute",
                  style: {
                    color: theme.secondaryColor,
                    fontSize: emptyFontSize
                  },
                  children: t("ui.waitingForMusic")
                },
                "empty"
              )
            ] })
          }
        ),
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          VisualizerSubtitleOverlay_default,
          {
            showText,
            activeLine,
            recentCompletedLine,
            nextLines: [],
            theme,
            subtitleTheme,
            translationFontSize,
            upcomingFontSize,
            subtitleOverlayOpacity,
            subtitleOverlayBackground,
            subtitleFontScale,
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
var Visualizer_default = Visualizer;
export {
  Visualizer_default as default
};
