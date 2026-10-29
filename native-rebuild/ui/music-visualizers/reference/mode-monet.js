import {
  lucide_react_exports
} from "./chunks/chunk-VROG7HKI.js";
import {
  layoutWithLines,
  prepareWithSegments
} from "./chunks/chunk-KECZQD7W.js";
import {
  buildWordColorRangesFromMatchers,
  prepareWordColorMatchers,
  resolveTokenColorMap
} from "./chunks/chunk-FIS2XO7C.js";
import {
  Shell_default,
  colorWithAlpha,
  mixColors,
  useVisualizerRuntime
} from "./chunks/chunk-FTFNLWFP.js";
import {
  buildLineGraphemeTimeline,
  buildWordGraphemeTimings
} from "./chunks/chunk-LDALZVSS.js";
import {
  AnimatePresence,
  motion,
  require_jsx_runtime,
  useMotionValue,
  useMotionValueEvent,
  useTransform
} from "./chunks/chunk-AS2U3CHD.js";
import {
  require_react
} from "./chunks/chunk-RG3JEOUL.js";
import {
  getLineRenderEndTime
} from "./chunks/chunk-LSTKNWUO.js";
import {
  DEFAULT_MONET_TUNING
} from "./chunks/chunk-LH7Y2YK3.js";
import {
  resolveThemeFontStack,
  resolveThemeFontWeight,
  resolveThemeTranslationFontStack
} from "./chunks/chunk-2CN2J6VF.js";
import {
  __toESM
} from "./chunks/chunk-XCBH6NLF.js";

// src/components/visualizer/monet/VisualizerMonet.tsx
var import_react4 = __toESM(require_react(), 1);

// src/components/visualizer/monet/AudioOverlay.tsx
var import_react = __toESM(require_react(), 1);
var import_jsx_runtime = __toESM(require_jsx_runtime(), 1);
var BAR_COUNT = 72;
var MIN_RAW_SPECTRUM_BIN = 6;
var buildSpectrumAnchors = (bands) => {
  const [bass, lowMid, mid, vocal, treble] = bands;
  return [
    bass * 1.08,
    bass * 0.96,
    bass * 0.74 + lowMid * 0.26,
    lowMid,
    lowMid * 0.58 + mid * 0.42,
    mid,
    mid * 0.42 + vocal * 0.58,
    vocal,
    vocal * 0.48 + treble * 0.52,
    treble,
    treble * 0.82
  ].map((value) => Math.max(0, Math.min(1, value)));
};
var sampleSpectrumProfile = (bands, normalizedIndex) => {
  const clampedIndex = Math.max(0, Math.min(1, normalizedIndex));
  const anchors = buildSpectrumAnchors(bands);
  const logIndex = Math.log10(1 + clampedIndex * 9);
  const scaledIndex = logIndex * (anchors.length - 1);
  const lowerIndex = Math.floor(scaledIndex);
  const upperIndex = Math.min(lowerIndex + 1, anchors.length - 1);
  const interpolation = scaledIndex - lowerIndex;
  const lowerValue = anchors[lowerIndex] ?? 0;
  const upperValue = anchors[upperIndex] ?? lowerValue;
  const interpolated = lowerValue + (upperValue - lowerValue) * interpolation;
  const contour = 0.92 + Math.sin(clampedIndex * Math.PI * 3.4 - Math.PI * 0.3) * 0.08 + Math.sin(clampedIndex * Math.PI * 9.2 + Math.PI * 0.2) * 0.035;
  return Math.max(0, Math.min(1, interpolated * contour));
};
var sampleRawSpectrumProfile = (rawSpectrum, normalizedIndex) => {
  if (rawSpectrum.length <= MIN_RAW_SPECTRUM_BIN) {
    return 0;
  }
  const clampedIndex = Math.max(0, Math.min(1, normalizedIndex));
  const usableBinCount = Math.max(1, rawSpectrum.length - MIN_RAW_SPECTRUM_BIN);
  const mappedIndex = Math.expm1(clampedIndex * Math.log(usableBinCount + 1));
  const centerIndex = MIN_RAW_SPECTRUM_BIN + mappedIndex;
  const edgeSoftness = 1 - Math.abs(clampedIndex - 0.5) * 2;
  const windowRadius = Math.max(1, Math.round(5 - edgeSoftness * 3));
  const start = Math.max(MIN_RAW_SPECTRUM_BIN, Math.floor(centerIndex - windowRadius));
  const end = Math.min(rawSpectrum.length - 1, Math.ceil(centerIndex + windowRadius));
  let weightedSum = 0;
  let weightTotal = 0;
  for (let index = start; index <= end; index += 1) {
    const distance = Math.abs(index - centerIndex);
    const weight = Math.max(0.1, 1 - distance / (windowRadius + 1));
    weightedSum += rawSpectrum[index] * weight;
    weightTotal += weight;
  }
  const averaged = weightTotal > 0 ? weightedSum / weightTotal : 0;
  const noiseFloor = 16 + (1 - clampedIndex) * 12;
  const normalized = Math.max(0, Math.min(1, (averaged - noiseFloor) / (255 - noiseFloor)));
  const power = 1.8 + (1 - clampedIndex) * 0.4;
  const processed = Math.pow(normalized, power);
  const lowFrequencyCompensation = 0.52 + clampedIndex * 0.62;
  const presenceLift = 0.92 + Math.sqrt(clampedIndex) * 0.14;
  return Math.max(0, Math.min(1, processed * lowFrequencyCompensation * presenceLift));
};
var drawStaticBars = (context, width, height, theme, mode) => {
  context.clearRect(0, 0, width, height);
  context.strokeStyle = colorWithAlpha(theme.primaryColor, 0.96);
  context.fillStyle = colorWithAlpha(theme.primaryColor, 0.9);
  context.lineWidth = 1.5;
  context.lineCap = "round";
  if (mode === "line") {
    const points = [];
    for (let index = 0; index < BAR_COUNT; index += 1) {
      const x = index / (BAR_COUNT - 1) * width;
      const spectrumIndex = index / (BAR_COUNT - 1);
      const envelope = Math.sin(spectrumIndex * Math.PI);
      const val = 0.12 + (Math.sin(index * 0.35) * 0.5 + 0.5) * 0.42;
      const y = height - height * val * envelope;
      points.push({ x, y });
    }
    const drawCurve = (ctx) => {
      if (points.length === 0) return;
      ctx.moveTo(points[0].x, points[0].y);
      for (let i = 0; i < points.length - 1; i++) {
        const xc = (points[i].x + points[i + 1].x) / 2;
        const yc = (points[i].y + points[i + 1].y) / 2;
        ctx.quadraticCurveTo(points[i].x, points[i].y, xc, yc);
      }
      ctx.lineTo(points[points.length - 1].x, points[points.length - 1].y);
    };
    context.beginPath();
    drawCurve(context);
    context.lineTo(width, height);
    context.lineTo(0, height);
    context.closePath();
    const fillGradient = context.createLinearGradient(0, 0, 0, height);
    fillGradient.addColorStop(0, colorWithAlpha(theme.primaryColor, 0.24));
    fillGradient.addColorStop(1, colorWithAlpha(theme.primaryColor, 0));
    context.fillStyle = fillGradient;
    context.fill();
    context.beginPath();
    drawCurve(context);
    context.strokeStyle = colorWithAlpha(theme.primaryColor, 0.96);
    context.lineWidth = 2;
    context.stroke();
    return;
  }
  const gap = width / BAR_COUNT;
  for (let index = 0; index < BAR_COUNT; index += 1) {
    const spectrumIndex = index / (BAR_COUNT - 1);
    const envelope = Math.sin(spectrumIndex * Math.PI);
    const val = 0.12 + (Math.sin(index * 0.35) * 0.5 + 0.5) * 0.42;
    const barHeight = height * (0.02 + val * 0.8 * envelope);
    const x = index * gap + gap * 0.14;
    context.fillRect(x, height - barHeight, Math.max(1.35, gap * 0.34), barHeight);
  }
};
var AudioOverlay = ({
  audioPower,
  audioBands,
  theme,
  mode,
  staticMode = false,
  isPreviewMode = false
}) => {
  const canvasRef = (0, import_react.useRef)(null);
  (0, import_react.useEffect)(() => {
    const canvas = canvasRef.current;
    if (!canvas) {
      return;
    }
    const context = canvas.getContext("2d");
    if (!context) {
      return;
    }
    let frameId = 0;
    let canvasWidth = 0;
    let canvasHeight = 0;
    const resizeCanvas = () => {
      const rect = canvas.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const nextWidth = Math.max(1, Math.floor(rect.width * dpr));
      const nextHeight = Math.max(1, Math.floor(rect.height * dpr));
      canvasWidth = rect.width;
      canvasHeight = rect.height;
      if (canvas.width !== nextWidth || canvas.height !== nextHeight) {
        canvas.width = nextWidth;
        canvas.height = nextHeight;
      }
      context.setTransform(dpr, 0, 0, dpr, 0, 0);
    };
    const draw = () => {
      const width = canvasWidth;
      const height = canvasHeight;
      if (width <= 0 || height <= 0) {
        return;
      }
      context.clearRect(0, 0, width, height);
      const bands = [
        audioBands.bass.get() / 255,
        audioBands.lowMid.get() / 255,
        audioBands.mid.get() / 255,
        audioBands.vocal.get() / 255,
        audioBands.treble.get() / 255
      ];
      const rawSpectrum = audioBands.spectrum?.get() ?? new Uint8Array(0);
      const hasRawSpectrum = rawSpectrum.length > MIN_RAW_SPECTRUM_BIN;
      if (audioPower.get() === 0 && bands.every((value) => value === 0) && !rawSpectrum.some((value) => value !== 0)) return;
      const energy = Math.min(1, Math.max(0.08, audioPower.get() / 255));
      const primaryInk = colorWithAlpha(theme.primaryColor, 0.94);
      const softInk = colorWithAlpha(theme.primaryColor, 0.72);
      const gradient = context.createLinearGradient(0, 0, width, 0);
      gradient.addColorStop(0, softInk);
      gradient.addColorStop(0.5, primaryInk);
      gradient.addColorStop(1, softInk);
      context.fillStyle = gradient;
      context.strokeStyle = gradient;
      context.lineWidth = 1.5;
      context.lineCap = "round";
      if (mode === "line") {
        const points = [];
        for (let index = 0; index < BAR_COUNT; index += 1) {
          const x = index / (BAR_COUNT - 1) * width;
          const spectrumIndex = index / (BAR_COUNT - 1);
          const band = hasRawSpectrum ? sampleRawSpectrumProfile(rawSpectrum, spectrumIndex) : sampleSpectrumProfile(bands, spectrumIndex);
          const wave = Math.sin(index * 0.24 + performance.now() * 4e-3) * 0.08 + Math.sin(index * 0.68 + performance.now() * 28e-4) * 0.05;
          const envelope = Math.sin(spectrumIndex * Math.PI);
          const amplitude = energy * 0.04 + band * 0.8 + wave * 0.04;
          const y = height - Math.max(0, height * amplitude * envelope);
          points.push({ x, y });
        }
        const drawCurve = (ctx) => {
          if (points.length === 0) return;
          ctx.moveTo(points[0].x, points[0].y);
          for (let i = 0; i < points.length - 1; i++) {
            const xc = (points[i].x + points[i + 1].x) / 2;
            const yc = (points[i].y + points[i + 1].y) / 2;
            ctx.quadraticCurveTo(points[i].x, points[i].y, xc, yc);
          }
          ctx.lineTo(points[points.length - 1].x, points[points.length - 1].y);
        };
        context.beginPath();
        drawCurve(context);
        context.lineTo(width, height);
        context.lineTo(0, height);
        context.closePath();
        const fillGradient = context.createLinearGradient(0, 0, 0, height);
        fillGradient.addColorStop(0, colorWithAlpha(theme.primaryColor, 0.24));
        fillGradient.addColorStop(1, colorWithAlpha(theme.primaryColor, 0));
        context.fillStyle = fillGradient;
        context.fill();
        context.beginPath();
        drawCurve(context);
        context.strokeStyle = gradient;
        context.lineWidth = 2;
        context.stroke();
      } else {
        const gap = width / BAR_COUNT;
        for (let index = 0; index < BAR_COUNT; index += 1) {
          const spectrumIndex = index / (BAR_COUNT - 1);
          const band = hasRawSpectrum ? sampleRawSpectrumProfile(rawSpectrum, spectrumIndex) : sampleSpectrumProfile(bands, spectrumIndex);
          const pulse = Math.sin(index * 0.45 + performance.now() * 6e-3) * 0.5 + 0.5;
          const envelope = Math.sin(spectrumIndex * Math.PI);
          const barHeight = height * (0.02 + (energy * 0.04 + band * 0.82 + pulse * 0.02) * envelope);
          const x = index * gap + gap * 0.14;
          context.fillRect(x, height - barHeight, Math.max(1.35, gap * 0.34), barHeight);
        }
      }
    };
    const handleResize = () => {
      resizeCanvas();
      if ((staticMode || isPreviewMode) && canvasWidth > 0 && canvasHeight > 0) {
        drawStaticBars(context, canvasWidth, canvasHeight, theme, mode);
      }
    };
    resizeCanvas();
    window.addEventListener("resize", handleResize);
    if (staticMode || isPreviewMode) {
      if (canvasWidth > 0 && canvasHeight > 0) {
        drawStaticBars(context, canvasWidth, canvasHeight, theme, mode);
      }
      return () => window.removeEventListener("resize", handleResize);
    }
    const loop = () => {
      draw();
      frameId = window.requestAnimationFrame(loop);
    };
    loop();
    return () => {
      window.cancelAnimationFrame(frameId);
      window.removeEventListener("resize", handleResize);
    };
  }, [audioBands, audioPower, isPreviewMode, mode, staticMode, theme]);
  return /* @__PURE__ */ (0, import_jsx_runtime.jsx)("canvas", { ref: canvasRef, className: "h-full w-full" });
};
var AudioOverlay_default = AudioOverlay;

// src/components/visualizer/monet/MonetFloatingDecor.tsx
var import_react2 = __toESM(require_react(), 1);
var import_jsx_runtime2 = __toESM(require_jsx_runtime(), 1);
var SakuraPetal = ({ size, color }) => /* @__PURE__ */ (0, import_jsx_runtime2.jsxs)("svg", { width: size, height: size, viewBox: "0 0 24 24", fill: "none", xmlns: "http://www.w3.org/2000/svg", children: [
  /* @__PURE__ */ (0, import_jsx_runtime2.jsx)(
    "path",
    {
      d: "M12 3C14.5 5.5 16.8 9 16 14C15.2 19 13.5 21 12 21C10.5 21 8.8 19 8 14C7.2 9 9.5 5.5 12 3Z",
      fill: color
    }
  ),
  /* @__PURE__ */ (0, import_jsx_runtime2.jsx)(
    "path",
    {
      d: "M12 6C12 6 11.3 10.5 11.3 14.5C11.3 17.5 12 20 12 20",
      stroke: colorWithAlpha(color, 0.35),
      strokeWidth: "0.45",
      strokeLinecap: "round"
    }
  )
] });
var PARTICLE_COUNT = 10;
var buildParticles = (availableIcons) => Array.from({ length: PARTICLE_COUNT }, (_, i) => {
  const useIcon = availableIcons.length > 0 && (i * 37 + 11) % 100 > 20;
  return {
    id: i,
    x: (i * 127 + 43) % 80 + 10,
    y: (i * 211 + 17) % 80 + 5,
    size: 46 + i * 53 % 40,
    rotation: i * 97 % 360,
    duration: 20 + i * 71 % 20,
    delay: i * 41 % 80 / 10,
    opacity: 0.06 + i * 31 % 12 / 100,
    iconName: useIcon ? availableIcons[i % availableIcons.length] : null,
    reverse: i % 2 === 0
  };
});
var MonetFloatingDecor = ({
  theme,
  staticMode = false
}) => {
  const availableIcons = theme.lyricsIcons ?? [];
  const particles = (0, import_react2.useMemo)(
    () => buildParticles(availableIcons),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [availableIcons.join(",")]
  );
  const petalColor = colorWithAlpha(theme.secondaryColor, 0.55);
  const renderParticleContent = (p) => {
    if (p.iconName) {
      const Icon = lucide_react_exports[p.iconName];
      if (Icon) {
        return /* @__PURE__ */ (0, import_jsx_runtime2.jsx)(Icon, { size: p.size, strokeWidth: 1, absoluteStrokeWidth: true, color: theme.secondaryColor });
      }
    }
    return /* @__PURE__ */ (0, import_jsx_runtime2.jsx)(SakuraPetal, { size: p.size, color: petalColor });
  };
  if (staticMode) {
    return /* @__PURE__ */ (0, import_jsx_runtime2.jsx)("div", { className: "pointer-events-none absolute inset-0 z-[5] overflow-hidden", children: particles.map((p) => /* @__PURE__ */ (0, import_jsx_runtime2.jsx)(
      "div",
      {
        className: "absolute",
        style: {
          left: `${p.x}%`,
          top: `${p.y}%`,
          opacity: p.opacity,
          transform: `rotate(${p.rotation}deg)`
        },
        children: renderParticleContent(p)
      },
      p.id
    )) });
  }
  return /* @__PURE__ */ (0, import_jsx_runtime2.jsx)("div", { className: "pointer-events-none absolute inset-0 z-[5] overflow-hidden", children: particles.map((p) => /* @__PURE__ */ (0, import_jsx_runtime2.jsx)(
    motion.div,
    {
      className: "absolute",
      style: { left: `${p.x}%`, top: `${p.y}%` },
      animate: {
        y: p.reverse ? [-40, 60, -20, 40, -40] : [40, -60, 20, -40, 40],
        x: p.reverse ? [15, -25, 10, -20, 15] : [-15, 25, -10, 20, -15],
        rotate: [p.rotation, p.rotation + (p.reverse ? -120 : 120), p.rotation + (p.reverse ? -60 : 60), p.rotation + (p.reverse ? -180 : 180), p.rotation],
        opacity: [p.opacity * 0.6, p.opacity * 1.2, p.opacity, p.opacity * 1.3, p.opacity * 0.7]
      },
      whileInView: void 0,
      transition: {
        duration: p.duration,
        repeat: Infinity,
        ease: "easeInOut",
        delay: p.delay
      },
      children: renderParticleContent(p)
    },
    p.id
  )) });
};
var MonetFloatingDecor_default = MonetFloatingDecor;

// src/components/visualizer/monet/MonetLyricsRail.tsx
var import_react3 = __toESM(require_react(), 1);

// src/components/visualizer/monet/monetLyricsModel.ts
var ROOT_FONT_PX = 16;
var VIEWPORT_WIDTH_FALLBACK_PX = 1280;
var MONET_ACTIVE_TEXT_LINE_LIMIT = 3;
var MONET_INACTIVE_TEXT_LINE_LIMIT = 2;
var MONET_TRANSLATION_LINE_LIMIT = 2;
var MONET_MIN_MEASURE_WIDTH_PX = 180;
var MONET_MEASURE_WIDTH_SAFETY = 0.86;
var MONET_MIXED_SCRIPT_MIN_CHARS = 18;
var MONET_GRAPHEME_OFFSETS_CACHE_LIMIT = 420;
var MONET_VERTICAL_METRICS_CACHE_LIMIT = 420;
var MONET_GLYPH_VERTICAL_SAFETY_PX = 2;
var MONET_TRANSLATION_GAP_PX = 8;
var MONET_COLLISION_SAFETY_PX = 8;
var monetGraphemeOffsetsCache = /* @__PURE__ */ new Map();
var monetVerticalMetricsCache = /* @__PURE__ */ new Map();
var monetVerticalMeasureContext = null;
var graphemeSegmenter = typeof Intl !== "undefined" ? new Intl.Segmenter(void 0, { granularity: "grapheme" }) : null;
var resolveClampFontPx = (minRem, preferredVw, maxRem) => {
  const viewportWidth = typeof window !== "undefined" ? window.innerWidth : VIEWPORT_WIDTH_FALLBACK_PX;
  return Math.min(maxRem * ROOT_FONT_PX, Math.max(minRem * ROOT_FONT_PX, viewportWidth * (preferredVw / 100)));
};
var splitMonetGraphemes = (text) => {
  if (!text) {
    return [];
  }
  if (graphemeSegmenter) {
    return Array.from(graphemeSegmenter.segment(text), ({ segment }) => segment);
  }
  return Array.from(text);
};
var measureTextLineCount = (text, fontSpec, maxWidthPx, lineHeightPx) => {
  const prepared = prepareWithSegments(text || " ", fontSpec);
  const safeWidthPx = Math.max(maxWidthPx * MONET_MEASURE_WIDTH_SAFETY, MONET_MIN_MEASURE_WIDTH_PX);
  const layout = layoutWithLines(prepared, safeWidthPx, lineHeightPx);
  const predictedLineCount = Math.max(layout.lines.length, 1);
  const context = getMonetVerticalMeasureContext();
  if (!context) {
    return predictedLineCount;
  }
  context.font = fontSpec;
  const measuredTextWidthPx = context.measureText(text || " ").width;
  return resolveMonetMeasuredLineCount(predictedLineCount, measuredTextWidthPx, maxWidthPx);
};
var resolveMonetMeasuredLineCount = (predictedLineCount, measuredTextWidthPx, maxWidthPx) => {
  const safeWidthPx = Math.max(maxWidthPx * MONET_MEASURE_WIDTH_SAFETY, MONET_MIN_MEASURE_WIDTH_PX);
  const widthLineCount = Number.isFinite(measuredTextWidthPx) && measuredTextWidthPx > 0 ? Math.ceil(measuredTextWidthPx / safeWidthPx) : 1;
  return Math.max(Math.ceil(predictedLineCount), widthLineCount, 1);
};
var hasEastAsianText = (text) => /[\u3040-\u30ff\u3400-\u9fff\uac00-\ud7af]/u.test(text);
var getMonetVerticalMeasureContext = () => {
  if (monetVerticalMeasureContext) {
    return monetVerticalMeasureContext;
  }
  if (typeof OffscreenCanvas !== "undefined") {
    monetVerticalMeasureContext = new OffscreenCanvas(1, 1).getContext("2d");
    return monetVerticalMeasureContext;
  }
  if (typeof document !== "undefined") {
    monetVerticalMeasureContext = document.createElement("canvas").getContext("2d");
  }
  return monetVerticalMeasureContext;
};
var measureMonetLineHeight = (text, fontSpec, fontPx, defaultLineHeightPx) => {
  const cacheKey = `${fontSpec}|${text}`;
  const cached = monetVerticalMetricsCache.get(cacheKey);
  if (cached !== void 0) {
    return cached;
  }
  const context = getMonetVerticalMeasureContext();
  if (!context) {
    return defaultLineHeightPx;
  }
  context.font = fontSpec;
  const metrics = context.measureText(text || "Hg");
  const glyphHeightPx = metrics.actualBoundingBoxAscent + metrics.actualBoundingBoxDescent;
  const measuredLineHeightPx = glyphHeightPx > 0 ? Math.max(defaultLineHeightPx, Math.ceil(glyphHeightPx + MONET_GLYPH_VERTICAL_SAFETY_PX)) : defaultLineHeightPx;
  if (monetVerticalMetricsCache.size >= MONET_VERTICAL_METRICS_CACHE_LIMIT) {
    const oldestKey = monetVerticalMetricsCache.keys().next().value;
    if (oldestKey) {
      monetVerticalMetricsCache.delete(oldestKey);
    }
  }
  monetVerticalMetricsCache.set(cacheKey, measuredLineHeightPx);
  return measuredLineHeightPx;
};
var measureTextWidthAtPx = (text, fontPx, fontSpec) => {
  const prepared = prepareWithSegments(text || " ", fontSpec);
  const layout = layoutWithLines(prepared, 99999, fontPx * 1.2);
  return layout.lines[0]?.width ?? Math.max(text.length, 1) * fontPx * 0.6;
};
var buildGraphemeOffsetsCacheKey = (text, fontPx, fontSpec) => `${fontPx}|${fontSpec}|${text}`;
var rememberGraphemeOffsets = (key, offsets) => {
  if (monetGraphemeOffsetsCache.size >= MONET_GRAPHEME_OFFSETS_CACHE_LIMIT) {
    const oldestKey = monetGraphemeOffsetsCache.keys().next().value;
    if (oldestKey) {
      monetGraphemeOffsetsCache.delete(oldestKey);
    }
  }
  monetGraphemeOffsetsCache.set(key, offsets);
  return offsets;
};
var measureMonetGraphemeOffsets = (text, fontPx, fontSpec) => {
  const cacheKey = buildGraphemeOffsetsCacheKey(text, fontPx, fontSpec);
  const cached = monetGraphemeOffsetsCache.get(cacheKey);
  if (cached) {
    return cached;
  }
  const graphemes = splitMonetGraphemes(text);
  const offsets = new Array(graphemes.length + 1).fill(0);
  for (let index = 1; index <= graphemes.length; index += 1) {
    offsets[index] = measureTextWidthAtPx(graphemes.slice(0, index).join(""), fontPx, fontSpec);
  }
  return rememberGraphemeOffsets(cacheKey, offsets);
};
var resolveMonetSweepEdgeSoftness = (fontPx) => Math.max(Math.min(fontPx * 0.45, 16), 6);
var resolveMonetSweepEnd = (filledWidthPx, fullWidthPx, edgeSoftnessPx) => {
  if (fullWidthPx <= 0) {
    return 0;
  }
  const progress = Math.min(1, Math.max(0, filledWidthPx / fullWidthPx));
  return filledWidthPx + edgeSoftnessPx * progress;
};
var resolveMonetLineStatus = (line, index, activeIndex, currentTime) => {
  if (index === activeIndex || activeIndex < 0 && currentTime >= line.startTime && currentTime <= line.endTime) {
    return "active";
  }
  if (currentTime > line.endTime || activeIndex >= 0 && index < activeIndex) {
    return "passed";
  }
  return "waiting";
};
var resolveMonetWordStatus = (currentTime, startTime, endTime) => {
  if (currentTime < startTime) {
    return "waiting";
  }
  if (currentTime <= endTime) {
    return "active";
  }
  return "passed";
};
var resolveMonetCollisionScale = (scale) => Math.max(1, scale);
var resolveMonetCollisionHeight = (layout, scale) => {
  const lineSafetyPx = Math.max(
    MONET_COLLISION_SAFETY_PX,
    Math.ceil(Math.max(layout.lineHeightPx, layout.translationLineHeightPx) * 0.12)
  );
  return (layout.visualHeightPx + lineSafetyPx) * resolveMonetCollisionScale(scale);
};
var buildMonetDisplayTokens = (line) => {
  if (line.words.length === 0) {
    return [{
      text: line.fullText,
      startTime: line.startTime,
      endTime: getLineRenderEndTime(line),
      key: `${line.startTime}-full`,
      timed: true,
      startOffset: 0,
      endOffset: line.fullText.length,
      graphemeTimings: buildLineGraphemeTimeline(line)
    }];
  }
  const tokens = [];
  let cursor = 0;
  line.words.forEach((word, index) => {
    const matchIndex = line.fullText.indexOf(word.text, cursor);
    if (matchIndex < 0) {
      return;
    }
    if (matchIndex > cursor) {
      tokens.push({
        text: line.fullText.slice(cursor, matchIndex),
        startTime: null,
        endTime: null,
        key: `${line.startTime}-static-${cursor}`,
        timed: false,
        startOffset: cursor,
        endOffset: matchIndex,
        graphemeTimings: []
      });
    }
    const endOffset = matchIndex + word.text.length;
    tokens.push({
      text: word.text,
      startTime: word.startTime,
      endTime: word.endTime,
      key: `${line.startTime}-${index}-${word.startTime}`,
      timed: true,
      startOffset: matchIndex,
      endOffset,
      graphemeTimings: buildWordGraphemeTimings(word)
    });
    cursor = endOffset;
  });
  if (cursor < line.fullText.length) {
    tokens.push({
      text: line.fullText.slice(cursor),
      startTime: null,
      endTime: null,
      key: `${line.startTime}-tail-${cursor}`,
      timed: false,
      startOffset: cursor,
      endOffset: line.fullText.length,
      graphemeTimings: []
    });
  }
  return tokens.length > 0 ? tokens : [{
    text: line.fullText,
    startTime: line.startTime,
    endTime: getLineRenderEndTime(line),
    key: `${line.startTime}-fallback-full`,
    timed: true,
    startOffset: 0,
    endOffset: line.fullText.length,
    graphemeTimings: buildLineGraphemeTimeline(line)
  }];
};
var resolveMonetLyricContext = (lines, currentLineIndex, activeLine, recentCompletedLine, nextLine) => {
  if (!activeLine) {
    return {
      previousLine: recentCompletedLine,
      activeLine: null,
      nextLine
    };
  }
  return {
    previousLine: currentLineIndex > 0 ? lines[currentLineIndex - 1] ?? null : null,
    activeLine,
    nextLine: lines[currentLineIndex + 1] ?? nextLine
  };
};
var findLineIndex = (lines, target) => {
  if (!target) {
    return -1;
  }
  const directIndex = lines.indexOf(target);
  if (directIndex >= 0) {
    return directIndex;
  }
  return lines.findIndex((line) => line.startTime === target.startTime && line.fullText === target.fullText);
};
var buildMonetVisibleLineEntries = ({
  lines,
  currentLineIndex,
  activeLine,
  recentCompletedLine,
  upcomingLine,
  currentTime,
  before = 2,
  after = 2
}) => {
  if (lines.length === 0) {
    return [];
  }
  const activeIndex = activeLine ? currentLineIndex >= 0 ? currentLineIndex : findLineIndex(lines, activeLine) : -1;
  const upcomingIndex = findLineIndex(lines, upcomingLine);
  const recentIndex = findLineIndex(lines, recentCompletedLine);
  const anchorIndex = activeIndex >= 0 ? activeIndex : upcomingIndex >= 0 ? upcomingIndex : recentIndex;
  if (anchorIndex < 0) {
    return [];
  }
  const startIndex = Math.max(0, anchorIndex - before);
  const endIndex = Math.min(lines.length - 1, anchorIndex + after);
  const entries = [];
  for (let index = startIndex; index <= endIndex; index += 1) {
    const line = lines[index];
    entries.push({
      key: `${index}-${line.startTime}-${line.fullText}`,
      line,
      index,
      offset: index - anchorIndex,
      status: resolveMonetLineStatus(line, index, activeIndex, currentTime)
    });
  }
  return entries;
};
var measureMonetLineLayout = ({
  line,
  status,
  fontPx,
  translationFontPx,
  fontStack,
  translationFontStack,
  fontWeight = 600,
  translationFontWeight = 500,
  maxWidthPx,
  showSubtitleTranslation = true
}) => {
  const fontSpec = `${fontWeight} ${fontPx}px ${fontStack}`;
  const translationFontSpec = `${translationFontWeight} ${translationFontPx}px ${translationFontStack ?? fontStack}`;
  const lineHeightPx = measureMonetLineHeight(line.fullText, fontSpec, fontPx, fontPx * 1.18);
  const translationLineHeightPx = measureMonetLineHeight(line.translation ?? "", translationFontSpec, translationFontPx, translationFontPx * 1.28);
  const textPaddingTopPx = Math.max(fontPx * 0.16, 8);
  const textPaddingBottomPx = Math.max(fontPx * 0.34, 14);
  const translationPaddingTopPx = Math.max(translationFontPx * 0.45, 7) + MONET_TRANSLATION_GAP_PX;
  const translationPaddingBottomPx = Math.max(translationFontPx * 0.18, 5);
  const measuredTextLineCount = measureTextLineCount(line.fullText, fontSpec, maxWidthPx, lineHeightPx);
  const textLineCount = status === "active" && measuredTextLineCount === 1 && hasEastAsianText(line.fullText) && line.fullText.trim().length >= MONET_MIXED_SCRIPT_MIN_CHARS ? 2 : measuredTextLineCount;
  const textLimit = status === "active" ? MONET_ACTIVE_TEXT_LINE_LIMIT : MONET_INACTIVE_TEXT_LINE_LIMIT;
  const visibleTextLineCount = Math.min(textLineCount, textLimit);
  const hasActiveTranslation = showSubtitleTranslation && status === "active" && Boolean(line.translation?.trim());
  const rawTranslationLineCount = hasActiveTranslation ? measureTextLineCount(line.translation ?? "", translationFontSpec, maxWidthPx, translationLineHeightPx) : 0;
  const translationLineCount = Math.min(rawTranslationLineCount, MONET_TRANSLATION_LINE_LIMIT);
  const textContentHeightPx = visibleTextLineCount * lineHeightPx;
  const textHeightPx = textContentHeightPx + textPaddingTopPx + textPaddingBottomPx;
  const translationContentHeightPx = translationLineCount * translationLineHeightPx;
  const translationHeightPx = translationLineCount > 0 ? translationContentHeightPx + translationPaddingTopPx + translationPaddingBottomPx : 0;
  return {
    textLineCount,
    visibleTextLineCount,
    textHeightPx,
    textContentHeightPx,
    textPaddingTopPx,
    textPaddingBottomPx,
    translationLineCount,
    translationHeightPx,
    translationContentHeightPx,
    translationPaddingTopPx,
    translationPaddingBottomPx,
    visualHeightPx: textHeightPx + translationHeightPx,
    lineHeightPx,
    translationLineHeightPx,
    isTextClipped: textLineCount > visibleTextLineCount,
    isTranslationClipped: rawTranslationLineCount > translationLineCount
  };
};

// src/components/visualizer/monet/MonetLyricsRail.tsx
var import_jsx_runtime3 = __toESM(require_jsx_runtime(), 1);
var MONET_RAIL_WIDTH_FALLBACK_PX = 680;
var MONET_RAIL_HEIGHT_FALLBACK_PX = 340;
var MONET_ACTIVE_GAP_PX = 18;
var MONET_INACTIVE_GAP_PX = 14;
var MONET_EDGE_PADDING_PX = 12;
var MONET_GLOW_RISE_DURATION_SCALE = 1.18;
var MONET_GLOW_PASS_TAIL_SECONDS = 1.05;
var MONET_SCROLL_IDLE_RESET_MS = 1800;
var MONET_SCROLL_STEP_PX = 72;
var MONET_TOUCH_STEP_PX = 52;
var MONET_SCROLL_BEFORE = 4;
var MONET_SCROLL_AFTER = 4;
var MONET_LAYOUT_CACHE_LIMIT = 240;
var MONET_RAIL_SCROLL_EVENT_OPTIONS = { passive: false };
var MONET_SCROLL_TRANSITION = {
  y: { type: "spring", stiffness: 142, damping: 28, mass: 0.82 },
  scale: { type: "spring", stiffness: 150, damping: 30, mass: 0.78 },
  opacity: { duration: 0.28, ease: [0.32, 0.72, 0, 1] },
  filter: { duration: 0.32, ease: [0.32, 0.72, 0, 1] }
};
var clamp = (value, min, max) => Math.min(max, Math.max(min, value));
var clampScrollSteps = (steps) => Math.max(-1, Math.min(1, steps));
var getScrollDirection = (delta) => delta === 0 ? 0 : delta > 0 ? 1 : -1;
var resolveLineTone = (entry, theme, inactiveScale) => {
  if (entry.status === "active") {
    return {
      opacity: 1,
      scale: 1,
      blurPx: 0,
      baseColor: colorWithAlpha(theme.primaryColor, 0.34),
      fontWeight: 600,
      zIndex: 4
    };
  }
  const distance = Math.max(Math.abs(entry.offset), 1);
  const isWaiting = entry.status === "waiting";
  const scale = clamp(inactiveScale * Math.pow(0.9, distance - 1), 0.68, 0.92);
  return {
    opacity: isWaiting ? clamp(0.72 - (distance - 1) * 0.18, 0.36, 0.72) : clamp(0.52 - (distance - 1) * 0.12, 0.28, 0.52),
    scale,
    blurPx: isWaiting ? distance === 1 ? 0.7 : 1.8 + (distance - 2) * 0.8 : 1.1 + (distance - 1) * 0.7,
    baseColor: colorWithAlpha(theme.primaryColor, isWaiting ? 0.46 : 0.36),
    fontWeight: 500,
    zIndex: isWaiting ? 3 - distance : 2 - distance
  };
};
var resolveLineGap = (previous, next) => previous.status === "active" || next.status === "active" ? MONET_ACTIVE_GAP_PX : MONET_INACTIVE_GAP_PX;
var resolveCollisionInset = (entry) => Math.max(0, (entry.collisionHeight - entry.scaledHeight) / 2);
var resolveRailLineStatus = (lineIndex, activeLineIndex) => {
  if (lineIndex === activeLineIndex) {
    return "active";
  }
  if (activeLineIndex >= 0 && lineIndex < activeLineIndex) {
    return "passed";
  }
  return "waiting";
};
var buildScrollableRailEntries = (lines, anchorIndex, activeLineIndex) => {
  if (lines.length === 0) {
    return [];
  }
  const safeAnchorIndex = Math.round(clamp(anchorIndex, 0, lines.length - 1));
  const startIndex = Math.max(0, safeAnchorIndex - MONET_SCROLL_BEFORE);
  const endIndex = Math.min(lines.length - 1, safeAnchorIndex + MONET_SCROLL_AFTER);
  const nextEntries = [];
  for (let index = startIndex; index <= endIndex; index += 1) {
    const line = lines[index];
    nextEntries.push({
      key: `${index}-${line.startTime}-${line.fullText}`,
      line,
      index,
      offset: index - safeAnchorIndex,
      status: resolveRailLineStatus(index, activeLineIndex)
    });
  }
  return nextEntries;
};
var trimOldestCacheEntry = (cache, limit) => {
  if (cache.size < limit) {
    return;
  }
  const oldestKey = cache.keys().next().value;
  if (oldestKey) {
    cache.delete(oldestKey);
  }
};
var buildMonetLayoutCacheKey = (entry, fontPx, translationFontPx, fontStack, translationFontStack, fontWeight, translationFontWeight, maxWidthPx, showSubtitleTranslation) => [
  entry.index,
  entry.line.startTime,
  entry.line.endTime,
  entry.line.fullText,
  entry.line.translation ?? "",
  entry.status,
  fontPx,
  translationFontPx,
  fontStack,
  translationFontStack,
  fontWeight,
  translationFontWeight,
  maxWidthPx,
  showSubtitleTranslation ? 1 : 0
].join("");
var getOrMeasureMonetLineLayout = (cache, entry, fontPx, translationFontPx, fontStack, translationFontStack, fontWeight, translationFontWeight, maxWidthPx, showSubtitleTranslation) => {
  const cacheKey = buildMonetLayoutCacheKey(entry, fontPx, translationFontPx, fontStack, translationFontStack, fontWeight, translationFontWeight, maxWidthPx, showSubtitleTranslation);
  const cached = cache.get(cacheKey);
  if (cached) {
    return cached;
  }
  let layout;
  try {
    layout = measureMonetLineLayout({
      line: entry.line,
      status: entry.status,
      fontPx,
      translationFontPx,
      fontStack,
      translationFontStack,
      fontWeight,
      translationFontWeight,
      maxWidthPx,
      showSubtitleTranslation
    });
  } catch {
    const lineHeightPx = fontPx * 1.18;
    const textPaddingTopPx = Math.max(fontPx * 0.16, 8);
    const textPaddingBottomPx = Math.max(fontPx * 0.34, 14);
    const textLineCount = Math.max(1, Math.min(3, Math.ceil(Array.from(entry.line.fullText).length / Math.max(1, Math.floor(maxWidthPx / Math.max(fontPx * 0.9, 1))))));
    const textContentHeightPx = textLineCount * lineHeightPx;
    const textHeightPx = textContentHeightPx + textPaddingTopPx + textPaddingBottomPx;
    layout = {
      textLineCount,
      visibleTextLineCount: textLineCount,
      textHeightPx,
      textContentHeightPx,
      textPaddingTopPx,
      textPaddingBottomPx,
      translationLineCount: 0,
      translationHeightPx: 0,
      translationContentHeightPx: 0,
      translationPaddingTopPx: 0,
      translationPaddingBottomPx: 0,
      visualHeightPx: textHeightPx,
      lineHeightPx,
      translationLineHeightPx: translationFontPx * 1.28,
      isTextClipped: false,
      isTranslationClipped: false
    };
  }
  trimOldestCacheEntry(cache, MONET_LAYOUT_CACHE_LIMIT);
  cache.set(cacheKey, layout);
  return layout;
};
var useMonetRailSize = (ref) => {
  const [size, setSize] = (0, import_react3.useState)({ width: 0, height: 0 });
  (0, import_react3.useEffect)(() => {
    const node = ref.current;
    if (!node) {
      return;
    }
    const updateSize = () => {
      const nextWidth = Math.round(node.clientWidth);
      const nextHeight = Math.round(node.clientHeight);
      setSize((current) => current.width === nextWidth && current.height === nextHeight ? current : { width: nextWidth, height: nextHeight });
    };
    updateSize();
    if (typeof ResizeObserver === "undefined") {
      return;
    }
    const observer = new ResizeObserver(updateSize);
    observer.observe(node);
    return () => observer.disconnect();
  }, [ref]);
  return size;
};
var buildPositionedEntries = (entries, railSize, theme, lyricFontPx, inactiveFontPx, translationFontPx, fontStack, translationFontStack, fontWeight, translationFontWeight, glowBufferPx, showSubtitleTranslation, layoutCache) => {
  const railWidth = railSize.width || MONET_RAIL_WIDTH_FALLBACK_PX;
  const railHeight = railSize.height || MONET_RAIL_HEIGHT_FALLBACK_PX;
  const inactiveScale = clamp(inactiveFontPx / Math.max(lyricFontPx, 1), 0.72, 0.92);
  const contentWidthPx = Math.max(railWidth - glowBufferPx * 2, 0);
  const measuredEntries = entries.map((entry) => {
    const tone = {
      ...resolveLineTone(entry, theme, inactiveScale),
      fontWeight
    };
    const layout = getOrMeasureMonetLineLayout(
      layoutCache,
      entry,
      lyricFontPx,
      translationFontPx,
      fontStack,
      translationFontStack,
      fontWeight,
      translationFontWeight,
      contentWidthPx - 8,
      showSubtitleTranslation
    );
    return {
      ...entry,
      y: 0,
      tone,
      layout,
      scaledHeight: layout.visualHeightPx * tone.scale,
      collisionHeight: resolveMonetCollisionHeight(layout, tone.scale)
    };
  });
  if (measuredEntries.length === 0) {
    return [];
  }
  const anchorIndex = Math.max(0, measuredEntries.findIndex((entry) => entry.offset === 0));
  const focusCenterY = railHeight * 0.46;
  const anchor = measuredEntries[anchorIndex];
  const anchorInset = resolveCollisionInset(anchor);
  const idealAnchorY = focusCenterY - anchor.scaledHeight / 2;
  const minAnchorY = MONET_EDGE_PADDING_PX + anchorInset;
  const maxAnchorY = railHeight - MONET_EDGE_PADDING_PX - anchor.scaledHeight - anchorInset;
  anchor.y = maxAnchorY >= minAnchorY ? clamp(idealAnchorY, minAnchorY, maxAnchorY) : Math.max(0, (railHeight - anchor.scaledHeight) / 2);
  for (let index = anchorIndex + 1; index < measuredEntries.length; index += 1) {
    const previous = measuredEntries[index - 1];
    const current = measuredEntries[index];
    current.y = previous.y + previous.scaledHeight + resolveCollisionInset(previous) + resolveCollisionInset(current) + resolveLineGap(previous, current);
  }
  for (let index = anchorIndex - 1; index >= 0; index -= 1) {
    const current = measuredEntries[index];
    const next = measuredEntries[index + 1];
    current.y = next.y - current.scaledHeight - resolveCollisionInset(current) - resolveCollisionInset(next) - resolveLineGap(current, next);
  }
  return measuredEntries;
};
var getLineMask = (isClipped, fadePx) => isClipped ? `linear-gradient(180deg, black 0%, black calc(100% - ${fadePx}px), transparent 100%)` : void 0;
var MonetTimedTokenSpan = ({ entry, currentTime, accentColor, fontPx, fontStack, wordColorMatchers, isChorus, chorusAccentColor, audioPower, renderStaticPassed = false }) => {
  const lineRenderEndTime = (0, import_react3.useMemo)(() => getLineRenderEndTime(entry.line), [entry.line]);
  const tokens = (0, import_react3.useMemo)(() => buildMonetDisplayTokens(entry.line), [entry.line]);
  const wordColorRanges = (0, import_react3.useMemo)(
    () => buildWordColorRangesFromMatchers(entry.line.fullText, wordColorMatchers),
    [entry.line.fullText, wordColorMatchers]
  );
  const tokenColors = (0, import_react3.useMemo)(
    () => resolveTokenColorMap(tokens, wordColorRanges),
    [tokens, wordColorRanges]
  );
  const fontSpec = (0, import_react3.useMemo)(
    () => `${entry.tone.fontWeight} ${fontPx}px ${fontStack}`,
    [entry.tone.fontWeight, fontPx, fontStack]
  );
  const resolvedAccentColor = isChorus && chorusAccentColor ? mixColors(accentColor, chorusAccentColor, 0.48) : accentColor;
  return /* @__PURE__ */ (0, import_jsx_runtime3.jsx)("span", { className: "block w-full min-w-0 max-w-full whitespace-pre-wrap break-words", children: tokens.map((token) => renderStaticPassed ? /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
    "span",
    {
      style: {
        color: token.timed ? tokenColors.get(token.key) ?? resolvedAccentColor : entry.tone.baseColor
      },
      children: token.text
    },
    token.key
  ) : token.timed && token.startTime !== null && token.endTime !== null ? /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
    MonetWordSweep,
    {
      text: token.text,
      startTime: token.startTime,
      endTime: token.endTime,
      graphemeTimings: token.graphemeTimings,
      lineRenderEndTime,
      currentTime,
      lineStatus: entry.status,
      wordColor: tokenColors.get(token.key) ?? resolvedAccentColor,
      baseColor: entry.tone.baseColor,
      fontPx,
      fontSpec,
      isChorus,
      audioPower
    },
    token.key
  ) : /* @__PURE__ */ (0, import_jsx_runtime3.jsx)("span", { style: { color: entry.tone.baseColor }, children: token.text }, token.key)) });
};
var MonetWordSweep = ({
  text,
  startTime,
  endTime,
  graphemeTimings,
  lineRenderEndTime,
  currentTime,
  lineStatus,
  wordColor,
  baseColor,
  fontPx,
  fontSpec,
  isChorus,
  audioPower
}) => {
  const isLineActive = lineStatus === "active";
  const canRenderGlow = lineStatus === "active" || lineStatus === "passed";
  const graphemeOffsets = (0, import_react3.useMemo)(
    () => {
      try {
        return measureMonetGraphemeOffsets(text, fontPx, fontSpec);
      } catch {
        const offsets = [0];
        for (const grapheme of Array.from(text)) {
          offsets.push(offsets[offsets.length - 1] + Math.max(fontPx * 0.55, grapheme.trim() ? fontPx * 0.9 : fontPx * 0.45));
        }
        return offsets;
      }
    },
    [text, fontPx, fontSpec]
  );
  const wordStatus = useTransform(currentTime, (latest) => isLineActive ? resolveMonetWordStatus(latest, startTime, endTime) : lineStatus);
  const wordProgress = useTransform(currentTime, (latest) => {
    if (!isLineActive || latest <= startTime) return 0;
    if (latest >= endTime) return 1;
    return (latest - startTime) / Math.max(1e-3, endTime - startTime);
  });
  const fillWidth = useTransform(currentTime, (latest) => {
    const fullWidth = graphemeOffsets[graphemeOffsets.length - 1] ?? 0;
    if (!isLineActive || latest <= startTime) return 0;
    if (latest >= endTime) return fullWidth;
    const timingCount = Math.min(graphemeTimings.length, graphemeOffsets.length - 1);
    if (timingCount > 0) {
      for (let index = 0; index < timingCount; index += 1) {
        const timing = graphemeTimings[index];
        const timingStart = Math.max(startTime, timing.startTime);
        const timingEnd = Math.max(timingStart, timing.endTime);
        const startWidth2 = graphemeOffsets[index] ?? 0;
        const endWidth2 = graphemeOffsets[index + 1] ?? startWidth2;
        if (latest < timingStart) {
          return startWidth2;
        }
        if (latest <= timingEnd) {
          const duration = Math.max(1e-3, timingEnd - timingStart);
          const progress2 = (latest - timingStart) / duration;
          return startWidth2 + (endWidth2 - startWidth2) * progress2;
        }
      }
      return graphemeOffsets[timingCount] ?? fullWidth;
    }
    const progress = (latest - startTime) / Math.max(1e-3, endTime - startTime);
    if (progress <= 0) return 0;
    if (progress >= 1) return fullWidth;
    const graphemeCount = graphemeOffsets.length - 1;
    const floatIndex = progress * graphemeCount;
    const wholeIndex = Math.floor(floatIndex);
    const fractional = floatIndex - wholeIndex;
    const startWidth = graphemeOffsets[Math.min(wholeIndex, graphemeOffsets.length - 1)] ?? 0;
    const endWidth = graphemeOffsets[Math.min(wholeIndex + 1, graphemeOffsets.length - 1)] ?? startWidth;
    return startWidth + (endWidth - startWidth) * fractional;
  });
  const maskImage = useTransform(fillWidth, (latest) => {
    const edgeSoftness = resolveMonetSweepEdgeSoftness(fontPx);
    const fullWidth = graphemeOffsets[graphemeOffsets.length - 1] ?? 0;
    const sweepEnd = resolveMonetSweepEnd(latest, fullWidth, edgeSoftness);
    const solidEnd = Math.max(sweepEnd - edgeSoftness, 0);
    const featherStart = Math.max(sweepEnd - edgeSoftness * 0.55, 0);
    const featherEnd = Math.max(sweepEnd, 0);
    return `linear-gradient(90deg, rgba(0, 0, 0, 1) 0px, rgba(0, 0, 0, 1) ${solidEnd}px, rgba(0, 0, 0, 0.92) ${featherStart}px, rgba(0, 0, 0, 0) ${featherEnd}px, rgba(0, 0, 0, 0) 100%)`;
  });
  const fillGradient = useTransform(wordProgress, (progress) => {
    const color = mixColors(baseColor, wordColor, Math.min(progress, 1));
    return `linear-gradient(90deg, ${color} 0%, ${colorWithAlpha(color, 0.92)} 68%, ${colorWithAlpha(color, 0.72)} 100%)`;
  });
  const resolvedBaseColor = useTransform(
    wordStatus,
    (status) => isLineActive && status === "passed" || lineStatus === "passed" ? wordColor : baseColor
  );
  const glowShadow = useTransform(currentTime, (latest) => {
    if (!canRenderGlow || latest <= startTime) return "none";
    const wordDuration = Math.max(1e-3, endTime - startTime);
    const glowRiseDuration = wordDuration * MONET_GLOW_RISE_DURATION_SCALE;
    const glowPeakTime = startTime + glowRiseDuration;
    const glowTailEndTime = Math.max(lineRenderEndTime, endTime + MONET_GLOW_PASS_TAIL_SECONDS);
    let intensity;
    if (latest <= glowPeakTime) {
      const progress = Math.min(1, Math.max(0, (latest - startTime) / glowRiseDuration));
      intensity = progress * progress * (3 - 2 * progress);
    } else {
      const decayDuration = Math.max(0.18, glowTailEndTime - glowPeakTime);
      const decayProgress = Math.min(1, Math.max(0, (latest - glowPeakTime) / decayDuration));
      const remaining = 1 - decayProgress;
      intensity = remaining * remaining * (3 - 2 * remaining);
    }
    if (intensity <= 0) return "none";
    const radiusOne = Math.round(fontPx * (isChorus ? 0.45 : 0.28));
    const radiusTwo = Math.round(fontPx * (isChorus ? 0.9 : 0.65));
    const maxAlpha = isChorus ? 1 : 0.88;
    const glowColor = mixColors(baseColor, wordColor, intensity, intensity * maxAlpha);
    return `0 0 ${radiusOne}px ${glowColor}, 0 0 ${radiusTwo}px ${glowColor}`;
  });
  return /* @__PURE__ */ (0, import_jsx_runtime3.jsxs)("span", { className: "relative inline-block whitespace-pre-wrap break-words", children: [
    /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(motion.span, { style: { color: resolvedBaseColor, textShadow: glowShadow }, children: text }),
    isLineActive ? /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
      motion.span,
      {
        "aria-hidden": true,
        className: "pointer-events-none absolute inset-0 block whitespace-pre-wrap break-words",
        style: {
          WebkitMaskImage: maskImage,
          maskImage,
          WebkitMaskSize: "100% 100%",
          maskSize: "100% 100%",
          WebkitMaskRepeat: "no-repeat",
          maskRepeat: "no-repeat",
          textShadow: "none"
        },
        children: /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
          motion.span,
          {
            className: "block whitespace-pre-wrap break-words",
            style: {
              color: "transparent",
              WebkitTextFillColor: "transparent",
              backgroundImage: fillGradient,
              WebkitBackgroundClip: "text",
              backgroundClip: "text"
            },
            children: text
          }
        )
      }
    ) : null
  ] });
};
var MonetRailLine = ({ entry, currentTime, theme, lyricFontPx, translationFontPx, fontStack, translationFontStack, translationFontWeight, glowBufferPx, vGlowBufferPx, wordColorMatchers, showSubtitleTranslation, audioPower, onLineSeek, canSeek = false, disableEntryMotion = false, renderStaticPassed = false }) => {
  const initialOffset = entry.offset >= 0 ? 34 : -34;
  const exitOffset = entry.status === "passed" || entry.offset < 0 ? -38 : 38;
  const textMask = getLineMask(entry.layout.isTextClipped, Math.max(lyricFontPx * 0.55, 12));
  const translationMask = getLineMask(entry.layout.isTranslationClipped, Math.max(translationFontPx * 0.65, 10));
  const handleSeek = (event) => {
    if (!canSeek) {
      return;
    }
    event.stopPropagation();
    onLineSeek?.(entry.line);
  };
  const handleClickSeek = (event) => {
    handleSeek(event);
    event.currentTarget.blur();
  };
  return /* @__PURE__ */ (0, import_jsx_runtime3.jsxs)(
    motion.div,
    {
      role: canSeek ? "button" : void 0,
      tabIndex: canSeek ? 0 : void 0,
      onClick: canSeek ? handleClickSeek : void 0,
      onKeyDown: canSeek ? (event) => {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          handleSeek(event);
        }
      } : void 0,
      className: `absolute top-0 min-w-0 will-change-transform ${canSeek ? "cursor-pointer" : ""}`,
      initial: disableEntryMotion ? false : {
        opacity: 0,
        y: entry.y + initialOffset,
        scale: entry.tone.scale * 0.98,
        filter: "blur(5px)"
      },
      animate: {
        opacity: entry.tone.opacity,
        y: entry.y,
        scale: entry.tone.scale,
        filter: `blur(${entry.tone.blurPx}px)`
      },
      exit: disableEntryMotion ? void 0 : {
        opacity: 0,
        y: entry.y + exitOffset,
        scale: entry.tone.scale * 0.98,
        filter: "blur(6px)",
        transition: { duration: 0.2, ease: [0.32, 0.72, 0, 1] }
      },
      transition: MONET_SCROLL_TRANSITION,
      style: {
        left: `${glowBufferPx}px`,
        right: `${glowBufferPx}px`,
        height: entry.layout.visualHeightPx,
        transformOrigin: "left top",
        zIndex: entry.tone.zIndex
      },
      children: [
        entry.line.isChorus && /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
          motion.div,
          {
            className: "absolute inset-0 pointer-events-none -z-10 rounded-2xl",
            initial: { opacity: 0 },
            animate: {
              opacity: entry.status === "active" ? 1 : 0,
              scale: entry.status === "active" ? 1.02 : 0.96
            },
            transition: { duration: 0.45, ease: "easeOut" },
            style: {
              background: `radial-gradient(circle at 50% 45%, ${colorWithAlpha(theme.accentColor, 0.14)} 0%, ${colorWithAlpha(theme.accentColor, 0.04)} 55%, transparent 82%)`,
              filter: "blur(10px)"
            }
          }
        ),
        /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
          "div",
          {
            className: "min-w-0 overflow-hidden pointer-events-none",
            style: {
              marginLeft: `-${glowBufferPx}px`,
              marginRight: `-${glowBufferPx}px`,
              paddingLeft: `${glowBufferPx}px`,
              paddingRight: `${glowBufferPx}px`,
              marginTop: `-${vGlowBufferPx}px`,
              marginBottom: `-${vGlowBufferPx}px`,
              paddingTop: `${entry.layout.textPaddingTopPx + vGlowBufferPx}px`,
              paddingBottom: `${entry.layout.textPaddingBottomPx + vGlowBufferPx}px`,
              height: `${entry.layout.textHeightPx + vGlowBufferPx * 2}px`,
              boxSizing: "border-box",
              fontFamily: fontStack,
              fontSize: lyricFontPx,
              fontWeight: entry.tone.fontWeight,
              lineHeight: `${entry.layout.lineHeightPx}px`,
              letterSpacing: 0,
              WebkitMaskImage: textMask,
              maskImage: textMask,
              WebkitMaskRepeat: "no-repeat",
              maskRepeat: "no-repeat",
              WebkitMaskSize: "100% 100%",
              maskSize: "100% 100%",
              textShadow: entry.status === "active" ? `0 14px 34px ${colorWithAlpha(theme.backgroundColor, 0.22)}` : "none"
            },
            children: /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
              MonetTimedTokenSpan,
              {
                entry,
                currentTime,
                accentColor: colorWithAlpha(theme.karaokeFillColor ?? "#ffffff", 0.98),
                fontPx: lyricFontPx,
                fontStack,
                wordColorMatchers,
                isChorus: entry.line.isChorus,
                chorusAccentColor: theme.accentColor,
                audioPower,
                renderStaticPassed
              }
            )
          }
        ),
        showSubtitleTranslation && entry.status === "active" && entry.line.translation ? /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
          motion.div,
          {
            className: "min-w-0 overflow-hidden whitespace-pre-wrap break-words",
            initial: { opacity: 0, y: 8 },
            animate: { opacity: 1, y: 0 },
            transition: { duration: 0.28, ease: [0.32, 0.72, 0, 1] },
            style: {
              marginLeft: `-${glowBufferPx}px`,
              marginRight: `-${glowBufferPx}px`,
              paddingLeft: `${glowBufferPx}px`,
              paddingRight: `${glowBufferPx}px`,
              height: entry.layout.translationHeightPx,
              paddingTop: entry.layout.translationPaddingTopPx,
              paddingBottom: entry.layout.translationPaddingBottomPx,
              boxSizing: "border-box",
              color: colorWithAlpha(theme.primaryColor, 0.68),
              fontFamily: translationFontStack,
              fontSize: translationFontPx,
              fontWeight: translationFontWeight,
              lineHeight: `${entry.layout.translationLineHeightPx}px`,
              letterSpacing: 0,
              WebkitMaskImage: translationMask,
              maskImage: translationMask,
              WebkitMaskRepeat: "no-repeat",
              maskRepeat: "no-repeat",
              WebkitMaskSize: "100% 100%",
              maskSize: "100% 100%"
            },
            children: entry.line.translation
          }
        ) : null
      ]
    }
  );
};
var MonetLyricsRail = ({
  entries,
  lines,
  currentLineIndex,
  currentTime,
  theme,
  lyricFontPx,
  inactiveFontPx,
  translationFontPx,
  fontStack,
  translationFontStack = fontStack,
  subtitleTheme,
  keywordColoringEnabled,
  emptyText,
  showSubtitleTranslation = true,
  audioPower,
  audioBands,
  onLyricLineSeek,
  seekDisabled = false,
  fillContainer = false
}) => {
  const railRef = (0, import_react3.useRef)(null);
  const layoutCacheRef = (0, import_react3.useRef)(/* @__PURE__ */ new Map());
  const manualScrollResetRef = (0, import_react3.useRef)(null);
  const wheelAccumulatorRef = (0, import_react3.useRef)(0);
  const wheelDirectionRef = (0, import_react3.useRef)(0);
  const touchLastYRef = (0, import_react3.useRef)(null);
  const touchAccumulatorRef = (0, import_react3.useRef)(0);
  const touchDirectionRef = (0, import_react3.useRef)(0);
  const [manualScrollAnchorIndex, setManualScrollAnchorIndex] = (0, import_react3.useState)(null);
  const railSize = useMonetRailSize(railRef);
  const glowBufferPx = Math.round(lyricFontPx * 1.2);
  const vGlowBufferPx = Math.round(lyricFontPx * 1.2);
  const canSeek = Boolean(onLyricLineSeek) && !seekDisabled;
  const lyricFontWeight = resolveThemeFontWeight(theme, 600);
  const translationFontWeight = resolveThemeFontWeight(subtitleTheme ?? theme, 500);
  const visibleEntries = (0, import_react3.useMemo)(
    () => manualScrollAnchorIndex === null ? entries : buildScrollableRailEntries(lines, manualScrollAnchorIndex, currentLineIndex),
    [currentLineIndex, entries, lines, manualScrollAnchorIndex]
  );
  const isManualScrolling = manualScrollAnchorIndex !== null;
  const positionedEntries = (0, import_react3.useMemo)(
    () => buildPositionedEntries(
      visibleEntries,
      railSize,
      theme,
      lyricFontPx,
      inactiveFontPx,
      translationFontPx,
      fontStack,
      translationFontStack,
      lyricFontWeight,
      translationFontWeight,
      glowBufferPx,
      showSubtitleTranslation,
      layoutCacheRef.current
    ),
    [visibleEntries, railSize, theme, lyricFontPx, inactiveFontPx, translationFontPx, fontStack, translationFontStack, lyricFontWeight, translationFontWeight, glowBufferPx, showSubtitleTranslation]
  );
  const wordColorMatchers = (0, import_react3.useMemo)(
    () => prepareWordColorMatchers(theme.wordColors, keywordColoringEnabled),
    [keywordColoringEnabled, theme.wordColors]
  );
  const getFallbackAnchorIndex = (0, import_react3.useCallback)(() => {
    if (manualScrollAnchorIndex !== null) {
      return manualScrollAnchorIndex;
    }
    if (currentLineIndex >= 0) {
      return currentLineIndex;
    }
    return entries.find((entry) => entry.offset === 0)?.index ?? 0;
  }, [currentLineIndex, entries, manualScrollAnchorIndex]);
  const scheduleManualScrollReset = (0, import_react3.useCallback)(() => {
    if (manualScrollResetRef.current !== null) {
      window.clearTimeout(manualScrollResetRef.current);
    }
    manualScrollResetRef.current = window.setTimeout(() => {
      setManualScrollAnchorIndex(null);
      wheelAccumulatorRef.current = 0;
      wheelDirectionRef.current = 0;
      touchAccumulatorRef.current = 0;
      touchDirectionRef.current = 0;
      manualScrollResetRef.current = null;
    }, MONET_SCROLL_IDLE_RESET_MS);
  }, []);
  const moveManualScrollAnchor = (0, import_react3.useCallback)((steps) => {
    if (lines.length === 0) {
      return;
    }
    setManualScrollAnchorIndex((current) => {
      const baseIndex = current ?? getFallbackAnchorIndex();
      return Math.round(clamp(baseIndex + steps, 0, lines.length - 1));
    });
    scheduleManualScrollReset();
  }, [getFallbackAnchorIndex, lines.length, scheduleManualScrollReset]);
  const handleRailWheel = (0, import_react3.useCallback)((event) => {
    if (lines.length === 0) {
      return;
    }
    if (event.cancelable) {
      event.preventDefault();
    }
    event.stopPropagation();
    const direction = getScrollDirection(event.deltaY);
    if (direction !== 0 && wheelDirectionRef.current !== 0 && direction !== wheelDirectionRef.current) {
      wheelAccumulatorRef.current = 0;
    }
    wheelDirectionRef.current = direction || wheelDirectionRef.current;
    wheelAccumulatorRef.current += event.deltaY;
    const steps = clampScrollSteps(Math.trunc(wheelAccumulatorRef.current / MONET_SCROLL_STEP_PX));
    if (steps !== 0) {
      wheelAccumulatorRef.current = 0;
      moveManualScrollAnchor(steps);
    } else {
      scheduleManualScrollReset();
    }
  }, [lines.length, moveManualScrollAnchor, scheduleManualScrollReset]);
  const handleRailTouchStart = (0, import_react3.useCallback)((event) => {
    if (lines.length === 0) {
      return;
    }
    event.stopPropagation();
    touchLastYRef.current = event.touches[0]?.clientY ?? null;
    touchAccumulatorRef.current = 0;
    touchDirectionRef.current = 0;
    setManualScrollAnchorIndex(getFallbackAnchorIndex());
    scheduleManualScrollReset();
  }, [getFallbackAnchorIndex, lines.length, scheduleManualScrollReset]);
  const handleRailTouchMove = (0, import_react3.useCallback)((event) => {
    if (lines.length === 0 || touchLastYRef.current === null) {
      return;
    }
    event.stopPropagation();
    const nextY = event.touches[0]?.clientY;
    if (typeof nextY !== "number") {
      return;
    }
    const deltaY = touchLastYRef.current - nextY;
    touchLastYRef.current = nextY;
    const direction = getScrollDirection(deltaY);
    if (direction !== 0 && touchDirectionRef.current !== 0 && direction !== touchDirectionRef.current) {
      touchAccumulatorRef.current = 0;
    }
    touchDirectionRef.current = direction || touchDirectionRef.current;
    touchAccumulatorRef.current += deltaY;
    const steps = clampScrollSteps(Math.trunc(touchAccumulatorRef.current / MONET_TOUCH_STEP_PX));
    if (steps !== 0) {
      touchAccumulatorRef.current = 0;
      moveManualScrollAnchor(steps);
    } else {
      scheduleManualScrollReset();
    }
  }, [lines.length, moveManualScrollAnchor, scheduleManualScrollReset]);
  const handleRailTouchEnd = (0, import_react3.useCallback)(() => {
    touchLastYRef.current = null;
    touchDirectionRef.current = 0;
    touchAccumulatorRef.current = 0;
    scheduleManualScrollReset();
  }, [scheduleManualScrollReset]);
  (0, import_react3.useEffect)(() => {
    const rail = railRef.current;
    if (!rail) {
      return void 0;
    }
    rail.addEventListener("wheel", handleRailWheel, MONET_RAIL_SCROLL_EVENT_OPTIONS);
    rail.addEventListener("touchstart", handleRailTouchStart, MONET_RAIL_SCROLL_EVENT_OPTIONS);
    rail.addEventListener("touchmove", handleRailTouchMove, MONET_RAIL_SCROLL_EVENT_OPTIONS);
    rail.addEventListener("touchend", handleRailTouchEnd, MONET_RAIL_SCROLL_EVENT_OPTIONS);
    rail.addEventListener("touchcancel", handleRailTouchEnd, MONET_RAIL_SCROLL_EVENT_OPTIONS);
    return () => {
      rail.removeEventListener("wheel", handleRailWheel, MONET_RAIL_SCROLL_EVENT_OPTIONS);
      rail.removeEventListener("touchstart", handleRailTouchStart, MONET_RAIL_SCROLL_EVENT_OPTIONS);
      rail.removeEventListener("touchmove", handleRailTouchMove, MONET_RAIL_SCROLL_EVENT_OPTIONS);
      rail.removeEventListener("touchend", handleRailTouchEnd, MONET_RAIL_SCROLL_EVENT_OPTIONS);
      rail.removeEventListener("touchcancel", handleRailTouchEnd, MONET_RAIL_SCROLL_EVENT_OPTIONS);
    };
  }, [handleRailTouchEnd, handleRailTouchMove, handleRailTouchStart, handleRailWheel]);
  const handleLineSeek = (0, import_react3.useCallback)((line) => {
    if (!canSeek) {
      return;
    }
    onLyricLineSeek?.(line.startTime);
    setManualScrollAnchorIndex(null);
  }, [canSeek, onLyricLineSeek]);
  (0, import_react3.useEffect)(() => {
    return () => {
      if (manualScrollResetRef.current !== null) {
        window.clearTimeout(manualScrollResetRef.current);
      }
    };
  }, []);
  return /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
    "div",
    {
      ref: railRef,
      "data-monet-lyrics-viewport": "true",
      className: "pointer-events-auto relative h-[clamp(260px,42vh,400px)] max-w-[720px] select-none overflow-hidden",
      style: {
        height: fillContainer ? "100%" : void 0,
        marginLeft: `-${glowBufferPx}px`,
        marginRight: `-${glowBufferPx}px`,
        paddingLeft: `${glowBufferPx}px`,
        paddingRight: `${glowBufferPx}px`,
        touchAction: "none",
        userSelect: "none",
        WebkitUserSelect: "none",
        WebkitMaskImage: "linear-gradient(to bottom, transparent 0%, black 11%, black 88%, transparent 100%)",
        maskImage: "linear-gradient(to bottom, transparent 0%, black 11%, black 88%, transparent 100%)"
      },
      children: positionedEntries.length > 0 ? /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(AnimatePresence, { initial: false, children: positionedEntries.map((entry) => /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
        MonetRailLine,
        {
          entry,
          currentTime,
          theme,
          lyricFontPx,
          translationFontPx,
          fontStack,
          translationFontStack,
          translationFontWeight,
          glowBufferPx,
          vGlowBufferPx,
          wordColorMatchers,
          showSubtitleTranslation,
          audioPower,
          onLineSeek: handleLineSeek,
          canSeek,
          disableEntryMotion: isManualScrolling,
          renderStaticPassed: isManualScrolling && entry.index !== currentLineIndex
        },
        entry.key
      )) }) : emptyText ? /* @__PURE__ */ (0, import_jsx_runtime3.jsx)(
        "div",
        {
          className: "absolute left-0 top-1/2 -translate-y-1/2",
          style: {
            color: theme.primaryColor,
            fontSize: "clamp(1.8rem, 4.2vw, 3.2rem)",
            fontWeight: lyricFontWeight,
            letterSpacing: 0,
            opacity: 0.72
          },
          children: emptyText
        }
      ) : null
    }
  );
};
var MonetLyricsRail_default = MonetLyricsRail;

// src/components/visualizer/monet/VisualizerMonet.tsx
var import_jsx_runtime4 = __toESM(require_jsx_runtime(), 1);
var VisualizerMonet = (props) => {
  const {
    currentTime,
    currentLineIndex,
    lines,
    theme,
    subtitleTheme,
    subtitleFontScale = 1,
    audioPower,
    audioBands,
    showText = true,
    songTitle,
    songArtist,
    songAlbum,
    coverUrl,
    staticMode = false,
    isPreviewMode = false,
    monetTuning = DEFAULT_MONET_TUNING,
    monetPortraitImage = null,
    onLyricLineSeek,
    seed
  } = props;
  const displayLines = lines;
  const initialOffsetX = monetTuning.portraitOffsetX ?? 0;
  const offsetX = useMotionValue(initialOffsetX);
  (0, import_react4.useEffect)(() => {
    offsetX.set(initialOffsetX);
  }, [initialOffsetX, offsetX]);
  const [introKey, setIntroKey] = (0, import_react4.useState)(0);
  const lastTimeRef = (0, import_react4.useRef)(0);
  useMotionValueEvent(currentTime, "change", (latest) => {
    const wasAtEnd = lastTimeRef.current > 2;
    const isAtStart = latest < 0.1;
    if (isAtStart && wasAtEnd) {
      setIntroKey((prev) => prev + 1);
    }
    lastTimeRef.current = latest;
  });
  const songIdentifier = seed ?? songTitle ?? "";
  (0, import_react4.useEffect)(() => {
    setIntroKey((prev) => prev + 1);
  }, [songIdentifier]);
  const {
    activeLine,
    recentCompletedLine,
    upcomingLine,
    currentTimeValue
  } = useVisualizerRuntime({
    currentTime,
    currentLineIndex,
    lines: displayLines,
    getLineEndTime: getLineRenderEndTime
  });
  const visibleLineEntries = (0, import_react4.useMemo)(() => buildMonetVisibleLineEntries({
    lines: displayLines,
    currentLineIndex,
    activeLine,
    recentCompletedLine,
    upcomingLine,
    currentTime: currentTimeValue,
    before: 2,
    after: 2
  }), [
    activeLine,
    currentLineIndex,
    currentTimeValue,
    displayLines,
    recentCompletedLine,
    upcomingLine
  ]);
  const lyricFontStack = (0, import_react4.useMemo)(() => resolveThemeFontStack(theme), [theme]);
  const translationFontStack = (0, import_react4.useMemo)(
    () => resolveThemeTranslationFontStack(subtitleTheme ?? theme),
    [subtitleTheme, theme]
  );
  const fontScale = monetTuning.fontScale;
  const lyricFontPx = resolveClampFontPx(
    1.34,
    2.75,
    2.28
  ) * fontScale;
  const inactiveFontPx = resolveClampFontPx(1.08, 2, 1.48) * fontScale;
  const translationFontPx = resolveClampFontPx(0.94, 1.28, 1.14) * fontScale * subtitleFontScale;
  const primaryMetaLabel = songArtist?.trim() || "Monet";
  const secondaryMetaLabel = songAlbum?.trim() || "Monet";
  const portraitUrl = monetTuning.portraitSource === "custom" ? monetPortraitImage?.url ?? coverUrl : coverUrl ?? monetPortraitImage?.url;
  return /* @__PURE__ */ (0, import_jsx_runtime4.jsxs)(
    Shell_default,
    {
      theme,
      audioPower,
      audioBands,
      sharedProps: props,
      children: [
        showText && /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
          motion.div,
          {
            initial: { opacity: 0 },
            animate: { opacity: 1 },
            transition: { duration: 2.2, ease: "easeOut" },
            children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(MonetFloatingDecor_default, { theme, staticMode })
          },
          `decor-${introKey}`
        ),
        /* @__PURE__ */ (0, import_jsx_runtime4.jsx)("div", { className: "relative z-10 flex h-full w-full items-center justify-center overflow-hidden", children: /* @__PURE__ */ (0, import_jsx_runtime4.jsxs)("div", { className: "flex h-full w-full max-w-[1520px] flex-row items-center overflow-hidden", children: [
          showText && /* @__PURE__ */ (0, import_jsx_runtime4.jsxs)("div", { className: "flex min-h-0 min-w-0 flex-1 flex-col justify-center px-5 py-5 sm:px-8 sm:py-6 lg:px-14 lg:py-8", children: [
            /* @__PURE__ */ (0, import_jsx_runtime4.jsxs)("div", { className: "mb-3 space-y-1.5", children: [
              /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                motion.div,
                {
                  initial: { opacity: 0, x: -30, y: -10 },
                  animate: { opacity: 1, x: 0, y: 0 },
                  transition: { duration: 1.2, ease: [0.25, 1, 0.5, 1], delay: 0.15 },
                  className: "text-[clamp(1rem,1.8vw,1.8rem)] italic",
                  style: { color: colorWithAlpha(theme.primaryColor, 0.96), letterSpacing: 0 },
                  children: primaryMetaLabel
                },
                `artist-${introKey}`
              ),
              /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                motion.div,
                {
                  initial: { scaleY: 0 },
                  animate: { scaleY: 1 },
                  transition: { duration: 1.5, ease: [0.25, 1, 0.5, 1], delay: 0.5 },
                  className: "h-14 w-px rounded-full",
                  style: {
                    originY: 0,
                    background: `linear-gradient(180deg, ${colorWithAlpha(theme.primaryColor, 0.72)}, transparent)`
                  }
                },
                `line-${introKey}`
              )
            ] }),
            /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
              motion.div,
              {
                initial: { opacity: 0, x: -40 },
                animate: { opacity: 1, x: 0 },
                transition: { duration: 1.3, ease: [0.25, 1, 0.5, 1], delay: 0.3 },
                children: /* @__PURE__ */ (0, import_jsx_runtime4.jsxs)("div", { className: "mb-6 space-y-1", children: [
                  /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                    "div",
                    {
                      className: "font-semibold leading-[1.06]",
                      style: {
                        color: theme.primaryColor,
                        fontSize: "clamp(1.45rem, 3.3vw, 2.8rem)",
                        letterSpacing: 0,
                        textShadow: `0 14px 36px ${colorWithAlpha(theme.backgroundColor, 0.28)}`
                      },
                      children: songTitle || "Monet"
                    }
                  ),
                  /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                    "div",
                    {
                      className: "text-sm uppercase",
                      style: { color: colorWithAlpha(theme.secondaryColor, 0.84), letterSpacing: 0 },
                      children: secondaryMetaLabel
                    }
                  )
                ] })
              },
              `title-${introKey}`
            ),
            /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
              motion.div,
              {
                initial: { opacity: 0, y: 20 },
                animate: { opacity: 1, y: 0 },
                transition: { duration: 1.2, ease: [0.25, 1, 0.5, 1], delay: 0.65 },
                children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                  MonetLyricsRail_default,
                  {
                    entries: visibleLineEntries,
                    lines: displayLines,
                    currentLineIndex,
                    currentTime,
                    theme,
                    lyricFontPx,
                    inactiveFontPx,
                    translationFontPx,
                    fontStack: lyricFontStack,
                    translationFontStack,
                    subtitleTheme,
                    keywordColoringEnabled: monetTuning.keywordColoringEnabled,
                    emptyText: "",
                    showSubtitleTranslation: true,
                    audioPower,
                    audioBands,
                    onLyricLineSeek,
                    seekDisabled: isPreviewMode
                  }
                )
              },
              `rail-${introKey}`
            )
          ] }),
          showText ? /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
            motion.div,
            {
              initial: { opacity: 0, x: 50, scale: 0.95, rotate: 1 },
              animate: { opacity: 1, x: 0, scale: 1, rotate: 0 },
              transition: { duration: 1.6, ease: [0.25, 1, 0.5, 1], delay: 0.25 },
              className: "hidden min-w-0 items-center justify-center overflow-visible px-3 pr-5 sm:pr-8 md:flex lg:justify-end lg:pr-10 xl:pr-12 select-none",
              style: { flex: "0 0 clamp(220px, 28vw, 430px)" },
              children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)("div", { className: "relative w-full max-w-[clamp(210px,26vw,380px)]", children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                motion.div,
                {
                  style: { x: offsetX },
                  className: "w-full relative",
                  children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                    motion.div,
                    {
                      animate: theme.animationIntensity === "chaotic" ? {
                        y: [0, -18, 0, 18, 0],
                        x: [0, -9, 0, 9, 0],
                        rotate: [0, 1.2, 0, -1.2, 0]
                      } : {
                        y: 0,
                        x: 0,
                        rotate: 0
                      },
                      transition: theme.animationIntensity === "chaotic" ? { duration: 9, repeat: Infinity, ease: "easeInOut", delay: 0.2 } : { duration: 0.8, ease: "easeOut" },
                      className: "relative",
                      style: {
                        width: monetTuning.portraitStyle === "square" ? "135.135%" : "100%",
                        marginLeft: monetTuning.portraitStyle === "square" ? "-35.135%" : "0%",
                        willChange: "transform",
                        transform: "translateZ(0)"
                      },
                      children: monetTuning.portraitStyle === "square" ? /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                        "div",
                        {
                          className: "relative w-full aspect-square overflow-hidden rounded-[2rem] bg-center",
                          style: {
                            boxShadow: `0 36px 80px ${colorWithAlpha(theme.backgroundColor, 0.45)}, 0 20px 42px ${colorWithAlpha(theme.accentColor, 0.22)}, 0 0 0 1px ${colorWithAlpha(theme.primaryColor, 0.06)}`,
                            backgroundColor: colorWithAlpha(theme.primaryColor, 0.08)
                          },
                          children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                            "img",
                            {
                              src: portraitUrl || void 0,
                              decoding: "async",
                              alt: "",
                              className: "w-full h-full object-cover",
                              style: { opacity: portraitUrl ? 1 : 0, transition: "opacity 1s ease" },
                              draggable: false
                            }
                          )
                        }
                      ) : /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                        "div",
                        {
                          className: "relative w-full aspect-[0.74] p-1.5 rounded-[2.15rem] border backdrop-blur-sm",
                          style: {
                            borderColor: colorWithAlpha(theme.primaryColor, 0.1),
                            backgroundColor: colorWithAlpha(theme.backgroundColor, 0.15),
                            boxShadow: `0 24px 60px ${colorWithAlpha(theme.backgroundColor, 0.35)}`
                          },
                          children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                            "div",
                            {
                              className: "w-full h-full overflow-hidden rounded-[1.85rem] bg-center",
                              style: {
                                backgroundColor: colorWithAlpha(theme.primaryColor, 0.08)
                              },
                              children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
                                "img",
                                {
                                  src: portraitUrl || void 0,
                                  decoding: "async",
                                  alt: "",
                                  className: "w-full h-full object-cover",
                                  style: { opacity: portraitUrl ? 1 : 0, transition: "opacity 1s ease" },
                                  draggable: false
                                }
                              )
                            }
                          )
                        }
                      )
                    }
                  )
                }
              ) })
            },
            `portrait-${introKey}`
          ) : null
        ] }) }),
        showText && /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
          motion.div,
          {
            initial: { opacity: 0, y: 15 },
            animate: { opacity: 1, y: 0 },
            transition: { duration: 1.2, ease: [0.25, 1, 0.5, 1], delay: 0.8 },
            className: "absolute bottom-0 left-0 z-20 h-10 overflow-hidden px-5 sm:px-8 lg:px-14",
            style: { width: "min(450px, 55vw)" },
            children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)("div", { className: "h-full w-full", children: /* @__PURE__ */ (0, import_jsx_runtime4.jsx)(
              AudioOverlay_default,
              {
                audioPower,
                audioBands,
                theme,
                mode: monetTuning.audioStyle,
                staticMode,
                isPreviewMode
              }
            ) })
          },
          `audio-${introKey}`
        )
      ]
    }
  );
};
var VisualizerMonet_default = VisualizerMonet;
export {
  buildMonetDisplayTokens,
  VisualizerMonet_default as default,
  resolveMonetLyricContext
};
