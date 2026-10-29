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
  shouldPreheatLine,
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
  DEFAULT_PARTITA_TUNING
} from "./chunks/chunk-LH7Y2YK3.js";
import {
  resolveThemeFontWeight
} from "./chunks/chunk-2CN2J6VF.js";
import {
  __toESM
} from "./chunks/chunk-XCBH6NLF.js";

// src/components/visualizer/partita/VisualizerPartita.tsx
var import_react = __toESM(require_react(), 1);
var import_jsx_runtime = __toESM(require_jsx_runtime(), 1);
var EMPTY_PARTITA_LAYOUT = {
  columns: [],
  totalGraphemes: 0,
  lineConfig: {
    perspective: 1e3,
    justifyContent: "justify-center",
    alignItems: "items-center",
    columnGap: "1.6rem"
  }
};
var PARTITA_LAYOUT_CACHE_LIMIT = 48;
var PARTITA_PREHEAT_WINDOW = {
  minLead: 0.18,
  maxLead: 1.2
};
var graphemeSegmenter = typeof Intl !== "undefined" ? new Intl.Segmenter(void 0, { granularity: "grapheme" }) : null;
var splitGraphemes = (text) => {
  if (!text) return [];
  if (graphemeSegmenter) {
    return Array.from(graphemeSegmenter.segment(text), ({ segment }) => segment);
  }
  return Array.from(text);
};
var clamp = (value, min, max) => Math.max(min, Math.min(max, value));
var clampPartitaStagger = (value, fallback) => Number.isFinite(value) ? clamp(value, 0, 180) : fallback;
var resolvePartitaTuning = (tuning) => {
  const rawMin = clampPartitaStagger(tuning?.staggerMin ?? DEFAULT_PARTITA_TUNING.staggerMin, DEFAULT_PARTITA_TUNING.staggerMin);
  const rawMax = clampPartitaStagger(tuning?.staggerMax ?? DEFAULT_PARTITA_TUNING.staggerMax, DEFAULT_PARTITA_TUNING.staggerMax);
  return {
    showGuideLines: tuning?.showGuideLines ?? DEFAULT_PARTITA_TUNING.showGuideLines,
    useSemanticLayout: tuning?.useSemanticLayout ?? DEFAULT_PARTITA_TUNING.useSemanticLayout,
    staggerMin: Math.min(rawMin, rawMax),
    staggerMax: Math.max(rawMin, rawMax)
  };
};
var resolvePartitaLineRenderProfile = (line) => {
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
var getPartitaWordActiveEndTime = (word, renderProfile) => {
  if (renderProfile.wordRevealMode === "instant") {
    return renderProfile.lineRenderEndTime;
  }
  if (renderProfile.wordRevealMode === "fast") {
    return Math.min(renderProfile.lineRenderEndTime, Math.max(word.endTime, word.startTime + 0.12));
  }
  return word.endTime;
};
var getPartitaWordDisplayDuration = (word, renderProfile) => {
  const activeEndTime = getPartitaWordActiveEndTime(word, renderProfile);
  const minDuration = renderProfile.wordRevealMode === "instant" ? 0.08 : renderProfile.wordRevealMode === "fast" ? 0.12 : 0.1;
  return Math.max(activeEndTime - word.startTime, minDuration);
};
var getPartitaLineContainerMotion = (renderProfile) => {
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
var getActiveColor = (wordText, theme) => {
  return resolveWordColor(wordText, theme.wordColors, theme.accentColor);
};
var buildSequentialColumns = (line, theme, windowHeight, tuning) => {
  const intensity = theme.animationIntensity;
  const isChaotic = intensity === "chaotic";
  const isCalm = intensity === "calm";
  const totalGraphemes = Math.max(splitGraphemes(line.fullText.replace(/\s+/g, "")).length, line.words.length, 1);
  const layoutUnits = buildPostLyricLayoutUnits(line, {
    semantic: tuning.useSemanticLayout,
    sticky: true
  });
  const columns = [];
  let currentWords = [];
  const baseRowHeight = 100;
  const availableHeight = windowHeight * 0.65;
  const targetRowCount = Math.max(1, Math.floor(availableHeight / baseRowHeight));
  const actualRowCount = Math.min(layoutUnits.length, targetRowCount);
  let seed = line.startTime;
  const random = () => {
    const x = Math.sin(seed++) * 1e4;
    return x - Math.floor(x);
  };
  const chunks = [];
  let remainingUnits = layoutUnits.length;
  let remainingChunks = actualRowCount;
  let unitIndex = 0;
  for (let c = 0; c < actualRowCount; c++) {
    const isLastChunk = c === actualRowCount - 1;
    const avg = remainingUnits / remainingChunks;
    let chunkLength = 1;
    if (isLastChunk) {
      chunkLength = remainingUnits;
    } else {
      const max = Math.ceil(avg * 1.5);
      const min = 1;
      const randVal = random();
      chunkLength = Math.max(min, Math.min(max, Math.round(avg + (randVal - 0.5) * avg)));
    }
    chunkLength = Math.max(1, Math.min(chunkLength, remainingUnits - (remainingChunks - 1)));
    chunks.push(layoutUnits.slice(unitIndex, unitIndex + chunkLength));
    unitIndex += chunkLength;
    remainingUnits -= chunkLength;
    remainingChunks--;
  }
  chunks.forEach((chunkUnits, rowIndex) => {
    const chunkWords = chunkUnits.flatMap((unit) => unit.words);
    const displayWords = buildDisplayWordsFromLayoutUnits(chunkUnits);
    if (chunkWords.length === 0) return;
    const mergedTextForConfig = chunkWords.map((w) => w.text.trim()).join("");
    const graphemeCount = splitGraphemes(mergedTextForConfig.replace(/\s+/g, "")).length;
    const rowBias = rowIndex - (actualRowCount - 1) / 2;
    const isStaggeredLeft = rowIndex % 2 === 0;
    const staggerMagnitude = tuning.staggerMin + random() * Math.max(tuning.staggerMax - tuning.staggerMin, 0);
    const staggerX = isStaggeredLeft ? -staggerMagnitude : staggerMagnitude;
    const staggerScale = isCalm ? 1 : 0.8 + random() * 0.9;
    currentWords.push({
      chunkUnits,
      chunkWords,
      displayWords,
      order: rowIndex,
      rowIndex,
      config: {
        id: `${rowIndex}-${chunkWords[0].startTime}`,
        x: staggerX,
        y: isCalm ? 0 : rowBias * 2.5,
        rotate: isChaotic ? isStaggeredLeft ? -3 : 3 : 0,
        scale: (isChaotic ? 1 + Math.min(graphemeCount * 0.01, 0.05) : 1) * staggerScale,
        marginBottom: isChaotic ? "0.4rem" : "0.6rem",
        alignSelf: "center",
        passedRotate: (rowIndex % 2 === 0 ? 1 : -1) * (isChaotic ? 6 : 3)
      }
    });
  });
  if (currentWords.length > 0) {
    columns.push({
      id: `column-${line.startTime}-0`,
      words: currentWords
    });
  }
  return {
    columns,
    totalGraphemes,
    lineConfig: {
      perspective: theme.animationIntensity === "chaotic" ? 720 : 1e3,
      alignItems: "items-center",
      justifyContent: "justify-center",
      columnGap: "2rem"
    }
  };
};
var buildPartitaLayoutCacheKey = (line, theme, windowHeight, tuning) => {
  const windowHeightBucket = Math.round(windowHeight / 24);
  return [
    "semantic-layout-v1",
    line.startTime,
    line.endTime,
    line.words.length,
    line.fullText,
    theme.animationIntensity,
    theme.fontWeight ?? "auto",
    windowHeightBucket,
    tuning.staggerMin,
    tuning.staggerMax,
    tuning.showGuideLines ? 1 : 0,
    tuning.useSemanticLayout ? 1 : 0
  ].join("|");
};
var getOrBuildPartitaLayout = (cache, line, theme, windowHeight, tuning) => {
  const cacheKey = buildPartitaLayoutCacheKey(line, theme, windowHeight, tuning);
  const cached = cache.get(cacheKey);
  if (cached) {
    return cached;
  }
  const layout = buildSequentialColumns(line, theme, windowHeight, tuning);
  cache.set(cacheKey, layout);
  if (cache.size > PARTITA_LAYOUT_CACHE_LIMIT) {
    const oldestKey = cache.keys().next().value;
    if (oldestKey) {
      cache.delete(oldestKey);
    }
  }
  return layout;
};
var PartitaWord = ({ word, config, currentTime, theme, layoutVariants, bodyVariants, glowVariants, baseColor, activeColor, renderProfile, isChorus, fontSize }) => {
  const [status, setStatus] = (0, import_react.useState)("waiting");
  const rippleScale = (0, import_react.useMemo)(() => 1.5 + Math.random() * 2, []);
  const duration = getPartitaWordDisplayDuration(word, renderProfile);
  const activeEndTime = getPartitaWordActiveEndTime(word, renderProfile);
  const graphemeTimings = (0, import_react.useMemo)(() => buildWordGraphemeTimings(word), [word]);
  useMotionValueEvent(currentTime, "change", (latest) => {
    let newStatus = "waiting";
    if (latest >= word.startTime - renderProfile.wordLookahead && latest <= activeEndTime) {
      newStatus = "active";
    } else if (latest > activeEndTime) {
      newStatus = "passed";
    }
    if (newStatus !== status) setStatus(newStatus);
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
        lineHeight: 1.22,
        marginRight: "0.8rem"
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
var PartitaChunk = ({ chunkWords, displayWords, config, guideIndex, currentTime, theme, layoutVariants, bodyVariants, glowVariants, baseColor, renderProfile, isChorus, showGuideLines, fontSize }) => {
  const [chunkStatus, setChunkStatus] = (0, import_react.useState)("waiting");
  const chunkStartTime = chunkWords[0].startTime;
  const chunkEndTime = getPartitaWordActiveEndTime(chunkWords[chunkWords.length - 1], renderProfile);
  useMotionValueEvent(currentTime, "change", (latest) => {
    let newStatus = "waiting";
    if (latest >= chunkStartTime - renderProfile.wordLookahead && latest <= chunkEndTime) {
      newStatus = "active";
    } else if (latest > chunkEndTime) {
      newStatus = "passed";
    }
    if (newStatus !== chunkStatus) setChunkStatus(newStatus);
  });
  const activeColor = getActiveColor(displayWords.map((w) => w.text).join(" "), theme);
  const guidePosition = guideIndex % 2 === 0 ? "left" : "right";
  return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(
    motion.div,
    {
      className: "inline-flex origin-center relative whitespace-nowrap items-center justify-center flex-row",
      style: {
        marginBottom: config.marginBottom,
        alignSelf: config.alignSelf,
        lineHeight: 1,
        minWidth: "auto",
        minHeight: "auto",
        padding: "0.2rem 0.5rem"
      },
      animate: {
        opacity: chunkStatus === "waiting" ? 0 : 1,
        scale: chunkStatus === "waiting" ? 0.85 : 1,
        x: chunkStatus === "waiting" ? config.x + (guidePosition === "left" ? -40 : 40) : config.x,
        y: config.y,
        rotate: config.rotate
      },
      transition: chunkStatus === "active" ? {
        type: "spring",
        stiffness: 200,
        damping: 20,
        opacity: { duration: 0.1 }
      } : {
        duration: 0.4,
        ease: "easeOut"
      },
      children: [
        showGuideLines && guidePosition === "left" && /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(import_jsx_runtime.Fragment, { children: [
          /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            motion.span,
            {
              className: "absolute w-px pointer-events-none",
              style: {
                left: "-8px",
                bottom: "-16px",
                height: "32px",
                transformOrigin: "bottom",
                backgroundColor: chunkStatus === "active" ? activeColor : chunkStatus === "passed" ? "rgba(255,255,255,0.22)" : "rgba(255,255,255,0.14)",
                boxShadow: chunkStatus === "active" ? `0 0 10px ${activeColor}45` : "none"
              },
              animate: {
                scaleY: chunkStatus === "waiting" ? 0 : 1,
                opacity: chunkStatus === "waiting" ? 0 : 1
              },
              transition: {
                duration: 0.4,
                ease: "easeOut"
              },
              "aria-hidden": "true"
            }
          ),
          /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            motion.span,
            {
              className: "absolute h-px pointer-events-none",
              style: {
                left: "-16px",
                bottom: "-8px",
                width: "calc(100% + 36px)",
                transformOrigin: "left",
                backgroundColor: chunkStatus === "active" ? activeColor : chunkStatus === "passed" ? "rgba(255,255,255,0.22)" : "rgba(255,255,255,0.14)",
                boxShadow: chunkStatus === "active" ? `0 0 10px ${activeColor}35` : "none"
              },
              animate: {
                scaleX: chunkStatus === "waiting" ? 0 : 1,
                opacity: chunkStatus === "waiting" ? 0 : 1
              },
              transition: {
                duration: 0.4,
                ease: "easeOut"
              },
              "aria-hidden": "true"
            }
          )
        ] }),
        showGuideLines && guidePosition === "right" && /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(import_jsx_runtime.Fragment, { children: [
          /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            motion.span,
            {
              className: "absolute w-px pointer-events-none",
              style: {
                right: "-8px",
                bottom: "-16px",
                height: "32px",
                transformOrigin: "bottom",
                backgroundColor: chunkStatus === "active" ? activeColor : chunkStatus === "passed" ? "rgba(255,255,255,0.22)" : "rgba(255,255,255,0.14)",
                boxShadow: chunkStatus === "active" ? `0 0 10px ${activeColor}45` : "none"
              },
              animate: {
                scaleY: chunkStatus === "waiting" ? 0 : 1,
                opacity: chunkStatus === "waiting" ? 0 : 1
              },
              transition: {
                duration: 0.4,
                ease: "easeOut"
              },
              "aria-hidden": "true"
            }
          ),
          /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            motion.span,
            {
              className: "absolute h-px pointer-events-none",
              style: {
                right: "-16px",
                bottom: "-8px",
                width: "calc(100% + 36px)",
                transformOrigin: "right",
                backgroundColor: chunkStatus === "active" ? activeColor : chunkStatus === "passed" ? "rgba(255,255,255,0.22)" : "rgba(255,255,255,0.14)",
                boxShadow: chunkStatus === "active" ? `0 0 10px ${activeColor}35` : "none"
              },
              animate: {
                scaleX: chunkStatus === "waiting" ? 0 : 1,
                opacity: chunkStatus === "waiting" ? 0 : 1
              },
              transition: {
                duration: 0.4,
                ease: "easeOut"
              },
              "aria-hidden": "true"
            }
          )
        ] }),
        displayWords.map((w, idx) => {
          const wordSeed = displayWords[0].startTime + idx * 7.13;
          const random = (offset) => {
            const x = Math.sin(wordSeed + offset) * 1e4;
            return x - Math.floor(x);
          };
          const intensity = theme.animationIntensity;
          const isCalm = intensity === "calm";
          const isChaotic = intensity === "chaotic";
          const baseSpread = isChaotic ? 15 : isCalm ? 0 : 6;
          const baseRotate = isChaotic ? 8 : isCalm ? 0 : 3;
          const wordConfig = {
            id: `${config.id}-w${idx}`,
            x: (random(1) - 0.5) * baseSpread * 2,
            y: (random(2) - 0.5) * baseSpread * 2,
            rotate: (random(3) - 0.5) * baseRotate * 2,
            scale: config.scale,
            marginBottom: "0",
            alignSelf: "auto",
            passedRotate: (random(8) - 0.5) * 20
          };
          return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            PartitaWord,
            {
              word: w,
              config: wordConfig,
              currentTime,
              theme,
              layoutVariants,
              bodyVariants,
              glowVariants,
              baseColor,
              activeColor: getActiveColor(w.text, theme),
              renderProfile,
              isChorus,
              fontSize
            },
            `${w.text}-${idx}`
          );
        })
      ]
    }
  );
};
var VisualizerPartita = (props) => {
  const {
    currentTime,
    currentLineIndex,
    lines,
    theme,
    subtitleTheme,
    audioPower,
    audioBands,
    showText = true,
    partitaTuning = DEFAULT_PARTITA_TUNING,
    lyricsFontScale = 1,
    subtitleFontScale = 1,
    subtitleOverlayOpacity,
    subtitleOverlayBackground,
    isPlayerChromeHidden = false,
    hideTranslationSubtitle = false,
    showSubtitleTranslation = true,
    subtitleContentMode
  } = props;
  const { t } = useTranslation();
  const [windowHeight, setWindowHeight] = (0, import_react.useState)(800);
  const resolvedPartitaTuning = (0, import_react.useMemo)(() => resolvePartitaTuning(partitaTuning), [partitaTuning]);
  const layoutCacheRef = (0, import_react.useRef)(/* @__PURE__ */ new Map());
  (0, import_react.useEffect)(() => {
    setWindowHeight(window.innerHeight);
    const handleResize = () => setWindowHeight(window.innerHeight);
    window.addEventListener("resize", handleResize);
    return () => window.removeEventListener("resize", handleResize);
  }, []);
  const {
    activeLine,
    upcomingLine,
    recentCompletedLine,
    nextLines
  } = useVisualizerRuntime({
    currentTime,
    currentLineIndex,
    lines,
    getLineEndTime: getLineRenderEndTime
  });
  const activeLineRenderProfile = activeLine ? resolvePartitaLineRenderProfile(activeLine) : null;
  const activeLineContainerMotion = getPartitaLineContainerMotion(activeLineRenderProfile);
  const sequentialLayout = (0, import_react.useMemo)(() => {
    if (!activeLine) {
      return EMPTY_PARTITA_LAYOUT;
    }
    return getOrBuildPartitaLayout(layoutCacheRef.current, activeLine, theme, windowHeight, resolvedPartitaTuning);
  }, [activeLine, theme, windowHeight, resolvedPartitaTuning]);
  const nextLineRef = (0, import_react.useRef)(upcomingLine);
  (0, import_react.useEffect)(() => {
    nextLineRef.current = upcomingLine;
  }, [upcomingLine]);
  useMotionValueEvent(currentTime, "change", (latest) => {
    const nextLine = nextLineRef.current;
    if (!nextLine) {
      return;
    }
    if (!shouldPreheatLine(nextLine, latest, PARTITA_PREHEAT_WINDOW)) {
      return;
    }
    getOrBuildPartitaLayout(layoutCacheRef.current, nextLine, theme, windowHeight, resolvedPartitaTuning);
  });
  const densityScale = sequentialLayout.totalGraphemes > 40 ? 0.8 : 1;
  const mainFontSize = `clamp(${(2.5 * densityScale * lyricsFontScale).toFixed(3)}rem, ${(5.5 * densityScale * lyricsFontScale).toFixed(3)}vw, ${(4.5 * densityScale * lyricsFontScale).toFixed(3)}rem)`;
  const emptyFontSize = `clamp(${(1.2 * lyricsFontScale).toFixed(3)}rem, ${(2.8 * lyricsFontScale).toFixed(3)}vw, ${(1.9 * lyricsFontScale).toFixed(3)}rem)`;
  const translationFontSize = `clamp(${(1.05 * lyricsFontScale).toFixed(3)}rem, ${(2.2 * lyricsFontScale).toFixed(3)}vw, ${(1.2 * lyricsFontScale).toFixed(3)}rem)`;
  const upcomingFontSize = `clamp(${(0.875 * lyricsFontScale).toFixed(3)}rem, ${(1.8 * lyricsFontScale).toFixed(3)}vw, ${(1 * lyricsFontScale).toFixed(3)}rem)`;
  const layoutVariants = {
    waiting: ({ config }) => ({
      opacity: 0,
      scale: 0.5,
      x: config.x + Math.sin(config.y) * 100,
      y: config.y + Math.cos(config.x) * 50,
      rotate: config.rotate + 20,
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
            times: [0, 0.3, 1],
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
          times: [0, 0.9, 1],
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
    const configByIntensity = {
      calm: { distance: 10, duration: 8.5 },
      normal: { distance: 14, duration: 7 },
      chaotic: { distance: 18, duration: 5.8 }
    };
    const { distance, duration } = configByIntensity[theme.animationIntensity];
    return {
      animate: {
        y: [0, -distance, 0, distance * 0.45, 0],
        scale: [1, 1.01, 1, 0.995, 1]
      },
      transition: {
        duration,
        repeat: Infinity,
        ease: "easeInOut"
      }
    };
  }, [theme.animationIntensity]);
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
            animate: lyricContainerFloat.animate,
            transition: lyricContainerFloat.transition,
            children: /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(AnimatePresence, { mode: "popLayout", children: [
              showText && activeLine && activeLineRenderProfile && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                motion.div,
                {
                  initial: activeLineContainerMotion.initial,
                  animate: activeLineContainerMotion.animate,
                  exit: activeLineContainerMotion.exit,
                  className: "flex flex-row-reverse items-stretch justify-center w-full max-w-5xl",
                  style: {
                    perspective: `${sequentialLayout.lineConfig.perspective}px`,
                    gap: sequentialLayout.lineConfig.columnGap,
                    minHeight: "320px"
                  },
                  children: sequentialLayout.columns.map((column) => {
                    return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                      "div",
                      {
                        className: "relative flex min-h-[24rem] min-w-[3.8rem] items-center justify-center px-3",
                        children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", { className: "relative z-10 flex flex-col items-center justify-start", children: column.words.map(({ chunkWords, displayWords, config, rowIndex }) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                          PartitaChunk,
                          {
                            chunkWords,
                            displayWords,
                            config,
                            guideIndex: rowIndex,
                            currentTime,
                            theme,
                            layoutVariants,
                            bodyVariants,
                            glowVariants,
                            baseColor: theme.primaryColor,
                            renderProfile: activeLineRenderProfile,
                            isChorus: activeLine.isChorus,
                            showGuideLines: resolvedPartitaTuning.showGuideLines,
                            fontSize: mainFontSize
                          },
                          `${config.id}`
                        )) })
                      },
                      column.id
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
var VisualizerPartita_default = VisualizerPartita;
export {
  VisualizerPartita_default as default
};
