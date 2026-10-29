import {
  Hourglass
} from "./chunks/chunk-VROG7HKI.js";
import {
  layoutWithLines,
  prepareWithSegments
} from "./chunks/chunk-KECZQD7W.js";
import {
  resolveWordColor
} from "./chunks/chunk-FIS2XO7C.js";
import {
  VisualizerSubtitleOverlay_default,
  captureLumeTopVisualFrame,
  getLumeEmbeddedTopInset,
  getLumeFullBleedLayerStyle,
  publishLiquidGlassWebGLSceneFrame,
  registerLumeTopVisualLayer,
  unregisterLiquidGlassWebGLSceneCanvas
} from "./chunks/chunk-CIZK54KT.js";
import {
  Shell_default,
  colorWithAlpha,
  getRecentCompletedLine,
  getUpcomingLines,
  mixColors
} from "./chunks/chunk-FTFNLWFP.js";
import {
  buildWordGraphemeTimings
} from "./chunks/chunk-LDALZVSS.js";
import {
  motion,
  require_jsx_runtime
} from "./chunks/chunk-AS2U3CHD.js";
import {
  require_react
} from "./chunks/chunk-RG3JEOUL.js";
import {
  getLineRenderEndTime,
  getLineRenderHints,
  getLineTransitionTiming
} from "./chunks/chunk-LSTKNWUO.js";
import {
  DEFAULT_FUME_TUNING
} from "./chunks/chunk-LH7Y2YK3.js";
import {
  resolveThemeFontStack,
  resolveThemeFontWeight
} from "./chunks/chunk-2CN2J6VF.js";
import {
  __toESM
} from "./chunks/chunk-XCBH6NLF.js";

// src/components/visualizer/fume/VisualizerFume.tsx
var import_react = __toESM(require_react(), 1);

// src/components/visualizer/FumeBackground.ts
var hash = (input) => {
  let value = 2166136261;
  for (let index = 0; index < input.length; index += 1) {
    value ^= input.charCodeAt(index);
    value = Math.imul(value, 16777619);
  }
  return value >>> 0;
};
var random = (seed, index) => hash(`${seed}:${index}`) % 1e4 / 1e4;
var clamp = (value, min, max) => Math.max(min, Math.min(max, value));
var buildFumeBackgroundScene = ({ viewport, world, seed = "fume" }) => {
  const width = Math.max(viewport.width, world.width);
  const height = Math.max(viewport.height, world.height);
  const bands = ["bass", "lowMid", "mid", "vocal", "treble"];
  const kinds = ["ring", "square", "cross", "spark"];
  const shapes = [];
  const count = clamp(Math.round(width * height / 175e3), 8, 28);
  for (let index = 0; index < count; index += 1) {
    shapes.push({
      kind: kinds[index % kinds.length],
      x: width * (0.08 + random(seed, index * 7) * 0.84),
      y: height * (0.08 + random(seed, index * 7 + 1) * 0.84),
      size: 28 + random(seed, index * 7 + 2) * 140,
      rotation: random(seed, index * 7 + 3) * Math.PI * 2,
      rotationSpeed: (random(seed, index * 7 + 4) - 0.5) * 35e-5,
      opacity: 0.32 + random(seed, index * 7 + 5) * 0.4,
      strokeWidth: 1 + random(seed, index * 7 + 6) * 2,
      color: index % 3 === 0 ? "accent" : "secondary",
      depth: 0.45 + random(seed, index * 11) * 0.55,
      audioBand: bands[index % bands.length]
    });
  }
  return { width, height, shapes };
};
var drawFumeBackground = ({ context, scene, theme, time = 0, audioLevels, parallax, objectOpacityMultiplier = 1 }) => {
  const levels = audioLevels ?? {};
  const originX = parallax?.originX ?? scene.width * 0.5;
  const originY = parallax?.originY ?? scene.height * 0.5;
  for (const shape of scene.shapes) {
    const level = clamp(levels[shape.audioBand] ?? levels.power ?? 0, 0, 1);
    const scale = 1 + level * 0.12;
    const x = shape.x + ((parallax?.cameraX ?? originX) - originX) * shape.depth * 0.08;
    const y = shape.y + ((parallax?.cameraY ?? originY) - originY) * shape.depth * 0.06;
    const color = shape.color === "accent" ? theme.accentColor : theme.secondaryColor;
    const alpha = clamp(shape.opacity * (0.75 + level * 0.55) * objectOpacityMultiplier, 0, 0.42);
    if (alpha <= 2e-3) continue;
    context.save();
    context.translate(x, y);
    context.rotate(shape.rotation + time * shape.rotationSpeed);
    context.scale(scale, scale);
    context.strokeStyle = color;
    context.fillStyle = color;
    context.globalAlpha = alpha;
    context.lineWidth = shape.strokeWidth;
    context.shadowColor = color;
    context.shadowBlur = 8 + level * 12;
    context.beginPath();
    const half = shape.size * 0.5;
    if (shape.kind === "ring") context.arc(0, 0, half, 0.2, Math.PI * 1.86);
    else if (shape.kind === "square") context.rect(-half, -half, shape.size, shape.size);
    else if (shape.kind === "cross") {
      const arm = half * 0.35;
      context.moveTo(-arm, -half);
      context.lineTo(arm, -half);
      context.lineTo(arm, -arm);
      context.lineTo(half, -arm);
      context.lineTo(half, arm);
      context.lineTo(arm, arm);
      context.lineTo(arm, half);
      context.lineTo(-arm, half);
      context.lineTo(-arm, arm);
      context.lineTo(-half, arm);
      context.lineTo(-half, -arm);
      context.lineTo(-arm, -arm);
      context.closePath();
    } else {
      context.moveTo(0, -half);
      context.lineTo(half * 0.22, -half * 0.22);
      context.lineTo(half, 0);
      context.lineTo(half * 0.22, half * 0.22);
      context.lineTo(0, half);
      context.lineTo(-half * 0.22, half * 0.22);
      context.lineTo(-half, 0);
      context.lineTo(-half * 0.22, -half * 0.22);
      context.closePath();
    }
    context.stroke();
    context.restore();
  }
};

// src/components/visualizer/fume/VisualizerFume.tsx
var import_jsx_runtime = __toESM(require_jsx_runtime(), 1);
var FUME_PRETEXT_OPTIONS = { whiteSpace: "pre-wrap" };
var graphemeSegmenter = typeof Intl !== "undefined" ? new Intl.Segmenter(void 0, { granularity: "grapheme" }) : null;
var splitGraphemes = (text) => {
  if (!text) return [];
  if (graphemeSegmenter) {
    return Array.from(graphemeSegmenter.segment(text), ({ segment }) => segment);
  }
  return Array.from(text);
};
var clamp2 = (value, min, max) => Math.max(min, Math.min(max, value));
var mix = (from, to, amount) => from + (to - from) * amount;
var quadraticBezier = (from, control, to, amount) => {
  const normalized = clamp2(amount, 0, 1);
  const inverse = 1 - normalized;
  return inverse * inverse * from + 2 * inverse * normalized * control + normalized * normalized * to;
};
var easeOutCubic = (value) => 1 - Math.pow(1 - clamp2(value, 0, 1), 3);
var easeInCubic = (value) => Math.pow(clamp2(value, 0, 1), 3);
var easeInOutCubic = (value) => {
  const normalized = clamp2(value, 0, 1);
  return normalized < 0.5 ? 4 * normalized * normalized * normalized : 1 - Math.pow(-2 * normalized + 2, 3) / 2;
};
var resolveDelayedGlowEnvelope = (progress, peakProgress = 0.8) => {
  const normalized = clamp2(progress, 0, 1);
  const clampedPeak = clamp2(peakProgress, 0.05, 0.95);
  if (normalized <= clampedPeak) {
    return easeOutCubic(normalized / clampedPeak);
  }
  return 1 - easeInCubic((normalized - clampedPeak) / (1 - clampedPeak));
};
var nowMs = () => typeof performance !== "undefined" ? performance.now() : Date.now();
var createFumeLayoutTiming = () => ({
  lines: 0,
  prepareLayoutMs: 0,
  placementMs: 0,
  renderDetailsMs: 0
});
var lastFumeLayoutCache = null;
var lastFumePassedFadeDurationCache = null;
var isCJK = (text) => /[\u4e00-\u9fff\u3040-\u30ff\uac00-\ud7af]/.test(text);
var CAMERA_SCALE_MIN = 0.22;
var CAMERA_SCALE_MAX = 2.24;
var OVERVIEW_CAMERA_SOURCE = -2;
var LAYOUT_REBUILD_DEBOUNCE_MS = 96;
var FUME_BACKGROUND_PARALLAX_X = 0.9;
var FUME_BACKGROUND_PARALLAX_Y = 0.74;
var FUME_BACKGROUND_SCALE_FACTOR = 0.94;
var FUME_BACKGROUND_VERTICAL_OFFSET_RATIO = 0.22;
var FUME_CAMERA_TELEPORT_TRIGGER_SCREENS = 2.75;
var resolvePassedTextStyle = (variant, textHoldStyle) => textHoldStyle === "dimmed" ? {
  opacity: variant === "hero" ? 0.11 : 0.075,
  glowMultiplier: 0,
  shadowAlphaBase: 0,
  shadowAlphaTrail: 0
} : {
  opacity: variant === "hero" ? 0.74 : 0.58,
  glowMultiplier: 1,
  shadowAlphaBase: 0.1,
  shadowAlphaTrail: 0.16
};
var resolvePassedDimAmount = (currentTimeValue, passedAt, fadeDuration) => {
  if (!Number.isFinite(currentTimeValue) || !Number.isFinite(passedAt) || !Number.isFinite(fadeDuration) || fadeDuration <= 0) {
    return 1;
  }
  const passedAge = Math.max(currentTimeValue - passedAt, 0);
  return easeInCubic(clamp2(passedAge / fadeDuration, 0, 1));
};
var resolveFumePassedFadeDuration = (lines, textHoldRatio) => {
  if (textHoldRatio >= 1) {
    return Number.POSITIVE_INFINITY;
  }
  const timedLines = lines.map((line) => ({
    startTime: line.startTime,
    endTime: getLineRenderEndTime(line)
  })).filter((line) => Number.isFinite(line.startTime) && Number.isFinite(line.endTime) && line.endTime >= line.startTime).sort((left, right) => left.startTime - right.startTime);
  const cacheKey = timedLines.map((line) => `${line.startTime.toFixed(3)}:${line.endTime.toFixed(3)}`).join("|") + `:${textHoldRatio.toFixed(3)}`;
  if (lastFumePassedFadeDurationCache?.key === cacheKey) {
    return lastFumePassedFadeDurationCache.duration;
  }
  if (timedLines.length <= 1) {
    const duration2 = clamp2(8 * textHoldRatio, 2.4, 130);
    lastFumePassedFadeDurationCache = { key: cacheKey, duration: duration2 };
    return duration2;
  }
  const first = timedLines[0];
  const last = timedLines[timedLines.length - 1];
  const totalDuration = Math.max(last.endTime - first.startTime, 0);
  const duration = clamp2(totalDuration * textHoldRatio, 2.4, 130);
  lastFumePassedFadeDurationCache = { key: cacheKey, duration };
  return duration;
};
var getActiveColor = (wordText, theme) => {
  return resolveWordColor(wordText, theme.wordColors, theme.accentColor, {
    cjkMatchMode: "bidirectional-contains"
  });
};
var hashString = (input) => {
  let hash2 = 2166136261;
  for (let index = 0; index < input.length; index += 1) {
    hash2 ^= input.charCodeAt(index);
    hash2 = Math.imul(hash2, 16777619);
  }
  return hash2 >>> 0;
};
var seeded = (seed) => {
  const hash2 = hashString(seed);
  return hash2 % 1e4 / 1e4;
};
var buildSegmentMetas = (prepared) => {
  const segmentMetas = [];
  const graphemes = [];
  let graphemeCursor = 0;
  for (const segment of prepared.segments) {
    const segmentGraphemes = splitGraphemes(segment);
    segmentMetas.push({
      graphemeStart: graphemeCursor,
      graphemeEnd: graphemeCursor + segmentGraphemes.length,
      graphemeCount: segmentGraphemes.length
    });
    graphemes.push(...segmentGraphemes);
    graphemeCursor += segmentGraphemes.length;
  }
  return { graphemes, segmentMetas };
};
var buildWordRangesFromWords = (line, graphemes) => {
  if (line.words.length === 0 || graphemes.length === 0) {
    return [];
  }
  const rangedWords = line.words.filter((word) => splitGraphemes(word.text).length > 0);
  if (rangedWords.length === 0) {
    return [];
  }
  const ranges = [];
  let cursor = 0;
  for (let wordIndex = 0; wordIndex < rangedWords.length; wordIndex += 1) {
    const word = rangedWords[wordIndex];
    const wordGraphemes = splitGraphemes(word.text);
    const start = clamp2(cursor, 0, graphemes.length);
    let end = clamp2(start + wordGraphemes.length, start, graphemes.length);
    while (end < graphemes.length && /\s/.test(graphemes[end] ?? "")) {
      end += 1;
    }
    ranges.push({
      wordIndex,
      word,
      start,
      end,
      colorStart: start,
      colorEnd: end,
      graphemeTimings: buildWordGraphemeTimings(word)
    });
    cursor = end;
  }
  return ranges;
};
var resolveWordRevealProgress = (range, currentTimeValue) => {
  if (range.word.endTime <= range.word.startTime) {
    return currentTimeValue >= range.word.endTime ? 1 : 0;
  }
  const duration = Math.max(range.word.endTime - range.word.startTime, 0.08);
  return clamp2((currentTimeValue - range.word.startTime) / duration, 0, 1);
};
var resolvePrintedGlyphsInRange = (range, currentTimeValue) => {
  const length = Math.max(range.end - range.start, 0);
  if (length === 0) {
    return 0;
  }
  if (currentTimeValue < range.word.startTime) {
    return 0;
  }
  const timedGlyphCount = range.word.syllables?.length ? Math.min(range.graphemeTimings.length, length) : 0;
  if (timedGlyphCount > 0) {
    if (currentTimeValue >= range.word.endTime) {
      return length;
    }
    let printed = 0;
    for (let index = 0; index < timedGlyphCount; index += 1) {
      if (currentTimeValue >= range.graphemeTimings[index].startTime) {
        printed = index + 1;
      }
    }
    return clamp2(printed, 0, length);
  }
  const progress = resolveWordRevealProgress(range, currentTimeValue);
  if (progress >= 1) {
    return length;
  }
  return clamp2(
    Math.floor(progress * length + 0.2),
    progress > 0 ? 1 : 0,
    length
  );
};
var hasRevealCompletedByLineEnd = (line, currentTimeValue) => currentTimeValue >= line.endTime;
var resolveLinePassCutoffTime = (line, nextLineStartTime) => {
  const renderEndTime = getLineRenderEndTime(line);
  if (typeof nextLineStartTime !== "number" || !Number.isFinite(nextLineStartTime)) {
    return renderEndTime;
  }
  return Math.min(renderEndTime, nextLineStartTime);
};
var resolveVisualProgressWithCutoff = (startedAt, duration, currentTimeValue, cutoffTime) => {
  const nominalEndTime = startedAt + Math.max(duration, 1e-3);
  const effectiveEndTime = Math.max(
    startedAt + 1e-3,
    Math.min(nominalEndTime, cutoffTime)
  );
  return clamp2(
    (currentTimeValue - startedAt) / Math.max(effectiveEndTime - startedAt, 1e-3),
    0,
    1
  );
};
var cursorToGlobalOffset = (cursor, segmentMetas) => {
  if (segmentMetas.length === 0) return 0;
  const segment = segmentMetas[cursor.segmentIndex];
  if (!segment) {
    return segmentMetas[segmentMetas.length - 1].graphemeEnd;
  }
  return clamp2(segment.graphemeStart + cursor.graphemeIndex, segment.graphemeStart, segment.graphemeEnd);
};
var getPartialSegmentWidth = (prepared, segmentIndex, segmentMeta, startOffset, endOffset) => {
  const localStart = clamp2(startOffset - segmentMeta.graphemeStart, 0, segmentMeta.graphemeCount);
  const localEnd = clamp2(endOffset - segmentMeta.graphemeStart, 0, segmentMeta.graphemeCount);
  if (localEnd <= localStart) return 0;
  if (localStart === 0 && localEnd === segmentMeta.graphemeCount) {
    return prepared.widths[segmentIndex] ?? 0;
  }
  const breakableFitAdvances = prepared.breakableFitAdvances[segmentIndex];
  if (breakableFitAdvances && breakableFitAdvances.length > 0) {
    let width = 0;
    for (let index = localStart; index < localEnd; index += 1) {
      width += breakableFitAdvances[index] ?? 0;
    }
    return width;
  }
  const fullWidth = prepared.widths[segmentIndex] ?? 0;
  if (segmentMeta.graphemeCount === 0) return fullWidth;
  return fullWidth * ((localEnd - localStart) / segmentMeta.graphemeCount);
};
var widthBetweenOffsets = (prepared, segmentMetas, startOffset, endOffset) => {
  if (endOffset <= startOffset) return 0;
  let width = 0;
  for (let segmentIndex = 0; segmentIndex < segmentMetas.length; segmentIndex += 1) {
    const meta = segmentMetas[segmentIndex];
    if (endOffset <= meta.graphemeStart) break;
    if (startOffset >= meta.graphemeEnd) continue;
    const sliceStart = Math.max(startOffset, meta.graphemeStart);
    const sliceEnd = Math.min(endOffset, meta.graphemeEnd);
    width += getPartialSegmentWidth(prepared, segmentIndex, meta, sliceStart, sliceEnd);
  }
  return width;
};
var buildGlyphOffsets = (prepared, segmentMetas, startOffset, graphemeCount) => {
  const offsets = new Array(graphemeCount);
  for (let index = 0; index < graphemeCount; index += 1) {
    offsets[index] = widthBetweenOffsets(
      prepared,
      segmentMetas,
      startOffset,
      startOffset + index
    );
  }
  return offsets;
};
var resolveGlyphAdvance = (renderLine, graphemeIndex) => {
  const currentOffset = renderLine.glyphOffsets[graphemeIndex] ?? 0;
  const nextOffset = graphemeIndex < renderLine.graphemes.length - 1 ? renderLine.glyphOffsets[graphemeIndex + 1] ?? renderLine.width : renderLine.width;
  return Math.max(nextOffset - currentOffset, 0);
};
var buildRenderSegments = (prepared, segmentMetas, lineStart, lineEnd, fontSpec) => {
  const segments = [];
  for (let segmentIndex = 0; segmentIndex < segmentMetas.length; segmentIndex += 1) {
    const meta = segmentMetas[segmentIndex];
    if (lineEnd <= meta.graphemeStart) {
      break;
    }
    if (lineStart >= meta.graphemeEnd) {
      continue;
    }
    const start = Math.max(lineStart, meta.graphemeStart);
    const end = Math.min(lineEnd, meta.graphemeEnd);
    if (end <= start) {
      continue;
    }
    const localStart = start - lineStart;
    const localEnd = end - lineStart;
    const segmentText = prepared.segments[segmentIndex] ?? "";
    const segmentGraphemes = splitGraphemes(segmentText);
    const text = start === meta.graphemeStart && end === meta.graphemeEnd ? segmentText : segmentGraphemes.slice(start - meta.graphemeStart, end - meta.graphemeStart).join("");
    const measuredGlyphOffsets = measureSegmentGlyphOffsets(text, fontSpec);
    segments.push({
      text,
      start,
      end,
      localStart,
      localEnd,
      x: widthBetweenOffsets(prepared, segmentMetas, lineStart, start),
      width: widthBetweenOffsets(prepared, segmentMetas, start, end),
      isFullSegment: start === meta.graphemeStart && end === meta.graphemeEnd,
      measuredGlyphOffsets
    });
  }
  return segments;
};
var buildFontSpec = (fontPx, variant, fontFamily, theme) => {
  const fontWeight = resolveThemeFontWeight(theme, variant === "hero" ? 780 : 640);
  return `${fontWeight} ${fontPx}px ${fontFamily}`;
};
var segmentMeasureCanvas = null;
var segmentMeasureCache = /* @__PURE__ */ new Map();
var measureSegmentGlyphOffsets = (text, fontSpec) => {
  const cacheKey = `${fontSpec}__${text}`;
  const cached = segmentMeasureCache.get(cacheKey);
  if (cached) {
    return cached;
  }
  const graphemes = splitGraphemes(text);
  const offsets = new Array(graphemes.length + 1).fill(0);
  if (typeof document === "undefined") {
    return offsets;
  }
  if (!segmentMeasureCanvas) {
    segmentMeasureCanvas = document.createElement("canvas");
  }
  const context = segmentMeasureCanvas.getContext("2d");
  if (!context) {
    return offsets;
  }
  context.font = fontSpec;
  for (let index = 1; index <= graphemes.length; index += 1) {
    offsets[index] = context.measureText(graphemes.slice(0, index).join("")).width;
  }
  if (segmentMeasureCache.size >= 2048) segmentMeasureCache.delete(segmentMeasureCache.keys().next().value);
  segmentMeasureCache.set(cacheKey, offsets);
  return offsets;
};
var buildWordRangeIndexByOffset = (graphemeCount, wordRanges, rangeKind = "timing") => {
  const indices = new Array(graphemeCount).fill(-1);
  for (let rangeIndex = 0; rangeIndex < wordRanges.length; rangeIndex += 1) {
    const range = wordRanges[rangeIndex];
    const start = rangeKind === "color" ? range.colorStart : range.start;
    const end = rangeKind === "color" ? range.colorEnd : range.end;
    for (let offset = start; offset < end && offset < graphemeCount; offset += 1) {
      indices[offset] = rangeIndex;
    }
  }
  return indices;
};
var countRenderableGraphemes = (text) => splitGraphemes(text).filter((value) => value.trim().length > 0).length;
var chooseNaturalBlockVariant = (line, index, total) => {
  const graphemeCount = countRenderableGraphemes(line.fullText);
  if (graphemeCount === 0) {
    return "body";
  }
  if (line.isChorus && graphemeCount <= 22) {
    return "hero";
  }
  const shortEnough = graphemeCount >= 4 && graphemeCount <= 28;
  const centered = Math.abs(index - total / 2) / Math.max(total, 1);
  const random2 = seeded(`${line.fullText}:${index}`);
  return shortEnough && centered < 0.72 && ((index + 1) % 6 === 0 || random2 > 0.965) ? "hero" : "body";
};
var chooseFallbackHeroBlockIndex = (entries) => {
  if (entries.length === 0) {
    return -1;
  }
  const hasNaturalHero = entries.some(({ line }, blockIndex) => chooseNaturalBlockVariant(line, blockIndex, entries.length) === "hero");
  if (hasNaturalHero) {
    return -1;
  }
  let bestIndex = -1;
  let bestScore = Number.NEGATIVE_INFINITY;
  entries.forEach(({ line }, blockIndex) => {
    const graphemeCount = countRenderableGraphemes(line.fullText);
    if (graphemeCount === 0) {
      return;
    }
    const isComfortableHeroLength = graphemeCount >= 4 && graphemeCount <= 28;
    const isAcceptableFallbackLength = graphemeCount <= 36;
    if (!isComfortableHeroLength && !isAcceptableFallbackLength) {
      return;
    }
    const centered = Math.abs(blockIndex - entries.length / 2) / Math.max(entries.length, 1);
    const centerScore = 1 - centered;
    const lengthScore = graphemeCount >= 6 && graphemeCount <= 22 ? 1 : graphemeCount <= 28 ? 0.72 : 0.36;
    const chorusScore = line.isChorus ? 0.28 : 0;
    const stableJitter = seeded(`${line.fullText}:${blockIndex}:fallback-hero`) * 0.04;
    const score = centerScore * 0.62 + lengthScore * 0.34 + chorusScore + stableJitter;
    if (score > bestScore) {
      bestScore = score;
      bestIndex = blockIndex;
    }
  });
  if (bestIndex >= 0) {
    return bestIndex;
  }
  let shortestIndex = -1;
  let shortestCount = Number.POSITIVE_INFINITY;
  entries.forEach(({ line }, blockIndex) => {
    const graphemeCount = countRenderableGraphemes(line.fullText);
    if (graphemeCount > 0 && graphemeCount < shortestCount) {
      shortestCount = graphemeCount;
      shortestIndex = blockIndex;
    }
  });
  return shortestIndex;
};
var chooseBlockVariant = (line, index, total, forcedHeroIndex) => index === forcedHeroIndex ? "hero" : chooseNaturalBlockVariant(line, index, total);
var buildPreparedSingleLine = (text, fontFamily, width, variant, lyricsFontScale, densityScale, heroScale, theme) => {
  let low = variant === "hero" ? 18 : 10;
  let high = variant === "hero" ? 58 : 30;
  let best = null;
  for (let iteration = 0; iteration < 8; iteration += 1) {
    const candidateFontPx = (low + high) / 2 * lyricsFontScale * densityScale * (variant === "hero" ? heroScale : 1);
    const fontSpec2 = buildFontSpec(candidateFontPx, variant, fontFamily, theme);
    const prepared2 = prepareWithSegments(text, fontSpec2, FUME_PRETEXT_OPTIONS);
    const layout = layoutWithLines(prepared2, width, Math.round(candidateFontPx * (variant === "hero" ? 1.02 : 1.06)));
    if (layout.lineCount <= 1) {
      best = {
        fontPx: candidateFontPx,
        prepared: prepared2,
        layout
      };
      low = (low + high) / 2;
    } else {
      high = (low + high) / 2;
    }
  }
  if (best) {
    return best;
  }
  const fallbackFontPx = (variant === "hero" ? 18 : 10) * lyricsFontScale * densityScale * (variant === "hero" ? heroScale : 1);
  const fontSpec = buildFontSpec(fallbackFontPx, variant, fontFamily, theme);
  const prepared = prepareWithSegments(text, fontSpec, FUME_PRETEXT_OPTIONS);
  return {
    fontPx: fallbackFontPx,
    prepared,
    layout: layoutWithLines(prepared, width, Math.round(fallbackFontPx * (variant === "hero" ? 1.02 : 1.06)))
  };
};
var buildLayoutCacheKey = (lines, viewport, layoutTheme, lyricsFontScale, fumeTuning) => {
  let linesHash = 2166136261;
  for (const line of lines) {
    const lineKey = `${line.startTime}:${line.endTime}:${line.fullText}:${line.words.length}:${line.isChorus ? 1 : 0}`;
    linesHash ^= hashString(lineKey);
    linesHash = Math.imul(linesHash, 16777619);
  }
  return [
    Math.round(viewport.width),
    Math.round(viewport.height),
    layoutTheme.fontStyle,
    layoutTheme.fontFamily ?? "",
    layoutTheme.fontFamilyStack?.join(",") ?? "",
    layoutTheme.fontWeight ?? "auto",
    layoutTheme.name,
    lyricsFontScale.toFixed(4),
    fumeTuning.heroScale.toFixed(4),
    lines.length,
    linesHash >>> 0
  ].join("|");
};
var resolvePrintedGraphemeCount = (line, wordRanges, graphemeCount, currentTimeValue) => {
  if (graphemeCount === 0) {
    return 0;
  }
  if (currentTimeValue < line.startTime) {
    return 0;
  }
  if (hasRevealCompletedByLineEnd(line, currentTimeValue)) {
    return graphemeCount;
  }
  if (wordRanges.length === 0) {
    const duration = Math.max(line.endTime - line.startTime, 0.12);
    const progress = clamp2((currentTimeValue - line.startTime) / duration, 0, 1);
    return clamp2(Math.floor(progress * graphemeCount + (progress > 0 ? 1 : 0)), 0, graphemeCount);
  }
  let printed = 0;
  for (let index = 0; index < wordRanges.length; index += 1) {
    const range = wordRanges[index];
    const partial = resolvePrintedGlyphsInRange(range, currentTimeValue);
    printed = range.start + partial;
    if (partial < range.end - range.start) {
      return clamp2(printed, 0, graphemeCount);
    }
  }
  return clamp2(printed, 0, graphemeCount);
};
var resolvePrintedGraphemeProgress = (line, wordRanges, graphemeCount, currentTimeValue) => {
  if (graphemeCount === 0) {
    return 0;
  }
  if (currentTimeValue < line.startTime) {
    return 0;
  }
  if (hasRevealCompletedByLineEnd(line, currentTimeValue)) {
    return graphemeCount;
  }
  if (wordRanges.length === 0) {
    const duration = Math.max(line.endTime - line.startTime, 0.12);
    const progress = clamp2((currentTimeValue - line.startTime) / duration, 0, 1);
    return clamp2(progress * graphemeCount, 0, graphemeCount);
  }
  let printed = 0;
  for (let index = 0; index < wordRanges.length; index += 1) {
    const range = wordRanges[index];
    if (currentTimeValue < range.word.startTime) {
      return clamp2(printed, 0, graphemeCount);
    }
    const progress = resolveWordRevealProgress(range, currentTimeValue);
    const length = Math.max(range.end - range.start, 0);
    printed = range.start + progress * length;
    if (progress < 1) {
      return clamp2(printed, 0, graphemeCount);
    }
  }
  return clamp2(printed, 0, graphemeCount);
};
function buildArticleLayoutAttempt(lines, viewport, layoutTheme, lyricsFontScale, fumeTuning, options) {
  if (viewport.width <= 0 || viewport.height <= 0 || lines.length === 0) {
    return null;
  }
  const {
    paperWidth,
    viewportHeight,
    columns,
    gap,
    densityScale,
    seedKey,
    mode = "render",
    timing
  } = options;
  const shouldBuildRenderDetails = mode === "render";
  const horizontalMargin = Math.max(viewport.width * 0.86, 280);
  const verticalMargin = Math.max(viewport.height * 0.82, 220);
  const columnWidth = (paperWidth - gap * (columns - 1)) / columns;
  const fontFamily = resolveThemeFontStack(layoutTheme);
  const filteredLines = lines.map((line, index) => ({ line, index })).filter((entry) => entry.line.fullText.trim().length > 0).sort((left, right) => {
    const leftSeed = seeded(`${seedKey}:${left.index}:${left.line.fullText}`);
    const rightSeed = seeded(`${seedKey}:${right.index}:${right.line.fullText}`);
    return leftSeed - rightSeed;
  });
  const blocks = [];
  const columnHeights = Array.from({ length: columns }, () => verticalMargin);
  let bodyColumnTieCursor = 0;
  let heroPlacementTieCursor = 0;
  const forcedHeroIndex = chooseFallbackHeroBlockIndex(filteredLines);
  filteredLines.forEach(({ line, index }, blockIndex) => {
    timing && (timing.lines += 1);
    const variant = chooseBlockVariant(line, blockIndex, filteredLines.length, forcedHeroIndex);
    const heroSpanColumns = variant === "hero" ? Math.min(columns, columns <= 1 ? 1 : 2) : 1;
    const heroSpanWidth = heroSpanColumns > 1 ? columnWidth * heroSpanColumns + gap * (heroSpanColumns - 1) : paperWidth;
    const blockWidth = variant === "hero" ? heroSpanColumns === 1 ? paperWidth : columns === 2 ? columnWidth * 1.5 + gap * 0.5 : heroSpanWidth : columnWidth;
    const paddingX = 0;
    const paddingY = 0;
    const innerWidth = Math.max(blockWidth - paddingX * 2, 120);
    const prepareLayoutStart = timing ? nowMs() : 0;
    const preparedSingleLine = buildPreparedSingleLine(
      line.fullText,
      fontFamily,
      innerWidth,
      variant,
      lyricsFontScale,
      densityScale,
      fumeTuning.heroScale,
      layoutTheme
    );
    if (timing) {
      timing.prepareLayoutMs += nowMs() - prepareLayoutStart;
    }
    const fontPx = preparedSingleLine.fontPx;
    const lineHeight = Math.round(fontPx * (variant === "hero" ? 1.02 : 1.06));
    const layout = preparedSingleLine.layout;
    const blockGap = variant === "hero" ? Math.max(Math.round(lineHeight * 0.2), 6) : Math.max(Math.round(lineHeight * 0.08), 2);
    const blockHeight = paddingY * 2 + layout.lines.length * lineHeight;
    let x = 0;
    let y = 0;
    const placementStart = timing ? nowMs() : 0;
    if (variant === "hero") {
      if (heroSpanColumns === 1) {
        y = Math.max(...columnHeights);
        x = horizontalMargin;
        columnHeights[0] = y + blockHeight + blockGap;
      } else {
        let bestHeight = Number.POSITIVE_INFINITY;
        let candidateStarts = [];
        for (let startColumn = 0; startColumn <= columns - heroSpanColumns; startColumn += 1) {
          let coveredHeight = 0;
          for (let columnIndex = startColumn; columnIndex < startColumn + heroSpanColumns; columnIndex += 1) {
            coveredHeight = Math.max(coveredHeight, columnHeights[columnIndex] ?? 0);
          }
          if (coveredHeight < bestHeight) {
            bestHeight = coveredHeight;
            candidateStarts = [startColumn];
          } else if (coveredHeight === bestHeight) {
            candidateStarts.push(startColumn);
          }
        }
        const targetStart = candidateStarts.length > 0 ? candidateStarts[heroPlacementTieCursor % candidateStarts.length] : 0;
        heroPlacementTieCursor += 1;
        y = bestHeight;
        x = horizontalMargin + targetStart * (columnWidth + gap) + Math.max((heroSpanWidth - blockWidth) * 0.5, 0);
        for (let columnIndex = targetStart; columnIndex < targetStart + heroSpanColumns; columnIndex += 1) {
          columnHeights[columnIndex] = y + blockHeight + blockGap;
        }
      }
    } else {
      let targetColumn = 0;
      let minHeight = columnHeights[0] ?? 0;
      const candidateColumns = [0];
      for (let columnIndex = 1; columnIndex < columns; columnIndex += 1) {
        const height = columnHeights[columnIndex] ?? 0;
        if (height < minHeight) {
          minHeight = height;
          candidateColumns.length = 0;
          candidateColumns.push(columnIndex);
        } else if (height === minHeight) {
          candidateColumns.push(columnIndex);
        }
      }
      targetColumn = candidateColumns[bodyColumnTieCursor % candidateColumns.length] ?? 0;
      bodyColumnTieCursor += 1;
      x = horizontalMargin + targetColumn * (columnWidth + gap);
      y = columnHeights[targetColumn];
      columnHeights[targetColumn] = y + blockHeight + blockGap;
    }
    if (timing) {
      timing.placementMs += nowMs() - placementStart;
    }
    if (shouldBuildRenderDetails) {
      const renderDetailsStart = timing ? nowMs() : 0;
      const prepared = preparedSingleLine.prepared;
      const fontSpec = buildFontSpec(fontPx, variant, fontFamily, layoutTheme);
      const { graphemes, segmentMetas } = buildSegmentMetas(prepared);
      const wordRanges = buildWordRangesFromWords(line, graphemes);
      const wordRangeIndexByOffset = buildWordRangeIndexByOffset(graphemes.length, wordRanges);
      const colorRangeIndexByOffset = buildWordRangeIndexByOffset(graphemes.length, wordRanges, "color");
      const renderLines = layout.lines.map((layoutLine, lineIndex) => {
        const start = cursorToGlobalOffset(layoutLine.start, segmentMetas);
        const end = cursorToGlobalOffset(layoutLine.end, segmentMetas);
        const lineGraphemes = splitGraphemes(layoutLine.text);
        return {
          id: `${line.startTime}-${lineIndex}`,
          text: layoutLine.text,
          start,
          end,
          graphemes: lineGraphemes,
          glyphOffsets: buildGlyphOffsets(
            prepared,
            segmentMetas,
            start,
            lineGraphemes.length
          ),
          segments: buildRenderSegments(
            prepared,
            segmentMetas,
            start,
            end,
            fontSpec
          ),
          left: variant === "hero" ? Math.max((blockWidth - layoutLine.width) * 0.08, 0) : 0,
          top: paddingY + lineIndex * lineHeight,
          width: layoutLine.width
        };
      });
      blocks.push({
        id: `fume-${line.startTime}-${index}`,
        sourceLineIndex: index,
        line,
        variant,
        x,
        y,
        width: blockWidth,
        height: blockHeight,
        innerWidth,
        fontPx,
        lineHeight,
        prepared,
        layout,
        graphemes,
        segmentMetas,
        wordRanges,
        wordRangeIndexByOffset,
        colorRangeIndexByOffset,
        renderLines
      });
      if (timing) {
        timing.renderDetailsMs += nowMs() - renderDetailsStart;
      }
    }
  });
  const articleHeight = Math.max(0, ...columnHeights) + verticalMargin;
  const metrics = {
    width: paperWidth + horizontalMargin * 2,
    height: articleHeight,
    viewportHeight,
    columns,
    gap,
    paperBounds: {
      left: horizontalMargin,
      top: verticalMargin,
      right: horizontalMargin + paperWidth,
      bottom: Math.max(articleHeight - verticalMargin, verticalMargin)
    }
  };
  if (!shouldBuildRenderDetails) {
    return metrics;
  }
  const chronologicalBlocks = [...blocks].sort((left, right) => left.sourceLineIndex - right.sourceLineIndex);
  const blockBySourceLineIndex = /* @__PURE__ */ new Map();
  for (const block of chronologicalBlocks) {
    blockBySourceLineIndex.set(block.sourceLineIndex, block);
  }
  const firstRenderableStartTime = chronologicalBlocks[0]?.line.startTime ?? Number.POSITIVE_INFINITY;
  const lastChronologicalRenderEndTime = chronologicalBlocks.length > 0 ? getLineRenderEndTime(chronologicalBlocks[chronologicalBlocks.length - 1].line) : Number.NEGATIVE_INFINITY;
  return {
    ...metrics,
    blocks,
    blockBySourceLineIndex,
    chronologicalBlocks,
    firstRenderableStartTime,
    lastChronologicalRenderEndTime
  };
}
var buildArticleLayout = (lines, viewport, layoutTheme, lyricsFontScale, fumeTuning) => {
  if (viewport.width <= 0 || viewport.height <= 0 || lines.length === 0) {
    return null;
  }
  const paperWidth = clamp2(Math.max(viewport.width * 1.95, viewport.width + 520), 920, 2400);
  const viewportHeight = Math.max(viewport.height, 240);
  const maxColumns = paperWidth >= 1120 ? 4 : paperWidth >= 760 ? 3 : paperWidth >= 500 ? 2 : 1;
  const targetHeight = viewportHeight * 2.45;
  const layoutSeedKey = layoutTheme.name;
  let bestOptions = null;
  let bestScore = Number.POSITIVE_INFINITY;
  let bestHeight = 0;
  const totalStart = nowMs();
  const measureTiming = createFumeLayoutTiming();
  const renderTiming = createFumeLayoutTiming();
  const measureColumnTimings = /* @__PURE__ */ new Map();
  let measureAttemptCount = 0;
  for (let columns = maxColumns; columns >= 1; columns -= 1) {
    let low = 0.82;
    let high = 1.42;
    const gap = clamp2(Math.round(paperWidth * (columns >= 4 ? 65e-4 : columns === 3 ? 85e-4 : 0.0115)), 6, 14);
    const columnTiming = createFumeLayoutTiming();
    measureColumnTimings.set(columns, columnTiming);
    for (let iteration = 0; iteration < 8; iteration += 1) {
      const densityScale = (low + high) / 2;
      measureAttemptCount += 1;
      const attemptOptions = {
        paperWidth,
        viewportHeight,
        columns,
        gap,
        densityScale,
        seedKey: `${layoutSeedKey}:${columns}:${paperWidth}`,
        mode: "measure",
        timing: columnTiming
      };
      const layout = buildArticleLayoutAttempt(lines, viewport, layoutTheme, lyricsFontScale, fumeTuning, attemptOptions);
      if (!layout) {
        continue;
      }
      const coveragePenalty = Math.abs(layout.height - targetHeight);
      const overflowPenalty = layout.height < targetHeight ? 0 : (layout.height - targetHeight) * 0.14;
      const score = coveragePenalty + overflowPenalty;
      if (score < bestScore) {
        bestScore = score;
        bestHeight = layout.height;
        bestOptions = {
          paperWidth,
          viewportHeight,
          columns,
          gap,
          densityScale,
          seedKey: `${layoutSeedKey}:${columns}:${paperWidth}`,
          mode: "render",
          timing: renderTiming
        };
      }
      if (layout.height < targetHeight) {
        low = densityScale;
      } else {
        high = densityScale;
      }
    }
    measureTiming.lines += columnTiming.lines;
    measureTiming.prepareLayoutMs += columnTiming.prepareLayoutMs;
    measureTiming.placementMs += columnTiming.placementMs;
    measureTiming.renderDetailsMs += columnTiming.renderDetailsMs;
  }
  const renderStart = nowMs();
  const article = bestOptions ? buildArticleLayoutAttempt(lines, viewport, layoutTheme, lyricsFontScale, fumeTuning, bestOptions) : null;
  const renderMs = nowMs() - renderStart;
  const totalMs = nowMs() - totalStart;
  if (false) {
    console.info("[VisualizerFume] layout timing", {
      totalMs: roundMs(totalMs),
      measureMs: roundMs(measureTiming.prepareLayoutMs + measureTiming.placementMs),
      renderMs: roundMs(renderMs),
      attempts: measureAttemptCount,
      measuredLines: measureTiming.lines,
      renderedLines: renderTiming.lines,
      inputLines: lines.length,
      blocks: article?.blocks.length ?? 0,
      heroBlocks: article?.blocks.filter((block) => block.variant === "hero").length ?? 0,
      viewport: `${Math.round(viewport.width)}x${Math.round(viewport.height)}`,
      paperWidth: Math.round(paperWidth),
      targetHeight: Math.round(targetHeight),
      bestHeight: Math.round(bestHeight),
      finalHeight: Math.round(article?.height ?? 0),
      heightDelta: Math.round((article?.height ?? 0) - targetHeight),
      bestColumns: bestOptions?.columns ?? null,
      bestDensityScale: bestOptions ? Number(bestOptions.densityScale.toFixed(4)) : null,
      measureBreakdown: {
        prepareLayoutMs: roundMs(measureTiming.prepareLayoutMs),
        placementMs: roundMs(measureTiming.placementMs),
        renderDetailsMs: roundMs(measureTiming.renderDetailsMs)
      },
      renderBreakdown: {
        prepareLayoutMs: roundMs(renderTiming.prepareLayoutMs),
        placementMs: roundMs(renderTiming.placementMs),
        renderDetailsMs: roundMs(renderTiming.renderDetailsMs)
      },
      measureByColumns: Array.from(measureColumnTimings.entries()).map(([columns, timing]) => ({
        columns,
        lines: timing.lines,
        prepareLayoutMs: roundMs(timing.prepareLayoutMs),
        placementMs: roundMs(timing.placementMs)
      }))
    });
  }
  return article;
};
var resolveSteppedBlockFocusPoint = (block, printedCount) => {
  if (block.renderLines.length === 0) {
    return {
      x: block.x + block.width * 0.5,
      y: block.y + block.height * 0.5
    };
  }
  const effectiveOffset = clamp2(printedCount, 0, block.graphemes.length);
  const targetLine = block.renderLines.find((renderLine) => effectiveOffset <= renderLine.end) ?? block.renderLines[block.renderLines.length - 1];
  const localOffset = clamp2(effectiveOffset, targetLine.start, targetLine.end);
  const progressWidth = widthBetweenOffsets(
    block.prepared,
    block.segmentMetas,
    targetLine.start,
    localOffset
  );
  const minX = block.x + targetLine.left;
  const maxX = minX + targetLine.width;
  return {
    x: clamp2(minX + progressWidth, minX, maxX),
    y: block.y + targetLine.top + block.lineHeight * 0.5
  };
};
var resolveSmoothBlockFocusPoint = (block, printedProgress) => {
  if (block.renderLines.length === 0) {
    return {
      x: block.x + block.width * 0.5,
      y: block.y + block.height * 0.5
    };
  }
  const effectiveOffset = clamp2(printedProgress, 0, block.graphemes.length);
  const findRenderLineIndex = (offset) => {
    const exactIndex = block.renderLines.findIndex((renderLine) => offset <= renderLine.end);
    return exactIndex >= 0 ? exactIndex : block.renderLines.length - 1;
  };
  const resolvePointOnRenderLine = (lineIndex, offset) => {
    const targetLine = block.renderLines[lineIndex] ?? block.renderLines[block.renderLines.length - 1];
    const clampedOffset = clamp2(offset, targetLine.start, targetLine.end);
    const baseOffset = Math.floor(clampedOffset);
    const fractionalOffset = clampedOffset - baseOffset;
    const baseWidth = widthBetweenOffsets(
      block.prepared,
      block.segmentMetas,
      targetLine.start,
      baseOffset
    );
    const localGlyphIndex = baseOffset - targetLine.start;
    const glyphAdvance = localGlyphIndex >= 0 && localGlyphIndex < targetLine.graphemes.length ? resolveGlyphAdvance(targetLine, localGlyphIndex) : 0;
    const minX = block.x + targetLine.left;
    const maxX = minX + targetLine.width;
    return {
      x: clamp2(minX + baseWidth + glyphAdvance * fractionalOffset, minX, maxX),
      y: block.y + targetLine.top + block.lineHeight * 0.5
    };
  };
  const targetLineIndex = findRenderLineIndex(effectiveOffset);
  let point = resolvePointOnRenderLine(targetLineIndex, effectiveOffset);
  const currentLine = block.renderLines[targetLineIndex];
  const crossLineBlendWindow = 0.7;
  if (targetLineIndex > 0 && effectiveOffset < currentLine.start + crossLineBlendWindow) {
    const previousLine = block.renderLines[targetLineIndex - 1];
    const blend = easeInOutCubic(clamp2(
      1 - (effectiveOffset - previousLine.end) / crossLineBlendWindow,
      0,
      1
    ));
    const previousPoint = resolvePointOnRenderLine(targetLineIndex - 1, previousLine.end);
    point = {
      x: mix(point.x, previousPoint.x, blend),
      y: mix(point.y, previousPoint.y, blend)
    };
  } else if (targetLineIndex < block.renderLines.length - 1 && effectiveOffset > currentLine.end - crossLineBlendWindow) {
    const nextLine = block.renderLines[targetLineIndex + 1];
    const blend = easeInOutCubic(clamp2(
      (effectiveOffset - (currentLine.end - crossLineBlendWindow)) / crossLineBlendWindow,
      0,
      1
    ));
    const nextPoint = resolvePointOnRenderLine(targetLineIndex + 1, nextLine.start);
    point = {
      x: mix(point.x, nextPoint.x, blend),
      y: mix(point.y, nextPoint.y, blend)
    };
  }
  return point;
};
var resolveBlockEntryFocusPoint = (block) => {
  const firstRenderLine = block.renderLines[0];
  if (!firstRenderLine) {
    return {
      x: block.x + block.width * 0.5,
      y: block.y + block.height * 0.5
    };
  }
  return {
    x: block.x + firstRenderLine.left,
    y: block.y + firstRenderLine.top + block.lineHeight * 0.5
  };
};
var buildCanvasFont = (block, theme) => {
  const fontFamily = resolveThemeFontStack(theme);
  return buildFontSpec(block.fontPx, block.variant, fontFamily, theme);
};
var resolveRenderLineOffset = (renderLine, localOffset) => {
  if (localOffset <= 0) {
    return 0;
  }
  if (localOffset >= renderLine.graphemes.length) {
    return renderLine.width;
  }
  return renderLine.glyphOffsets[localOffset] ?? renderLine.width;
};
var drawRenderTextRun = (context, renderLine, segment, runStart, runEnd, baseX, baseY) => {
  if (!segment.text || runEnd <= runStart) {
    return;
  }
  const segmentRunStart = Math.max(runStart - segment.localStart, 0);
  const segmentRunEnd = Math.min(runEnd - segment.localStart, segment.measuredGlyphOffsets.length - 1);
  const clipLeft = segment.measuredGlyphOffsets[segmentRunStart] ?? resolveRenderLineOffset(renderLine, runStart) - segment.x;
  const clipRight = segment.measuredGlyphOffsets[segmentRunEnd] ?? resolveRenderLineOffset(renderLine, runEnd) - segment.x;
  const clipWidth = Math.max(clipRight - clipLeft, 0);
  if (clipWidth <= 0) {
    return;
  }
  context.save();
  context.beginPath();
  context.rect(
    baseX + segment.x + clipLeft,
    baseY - Math.max(clipWidth, 1) - 64,
    clipWidth,
    Math.max(128 + clipWidth * 2, 256)
  );
  context.clip();
  context.fillText(segment.text, baseX + segment.x, baseY);
  context.restore();
};
var drawProgressiveGlyphFill = (context, renderLine, segment, localStart, localEnd, baseX, baseY, fillStyle, fillProgress) => {
  if (fillProgress <= 0 || localEnd <= localStart) return;
  const segmentGlyphStart = clamp2(localStart - segment.localStart, 0, segment.measuredGlyphOffsets.length - 1);
  const segmentGlyphEnd = clamp2(localEnd - segment.localStart, 0, segment.measuredGlyphOffsets.length - 1);
  const glyphLeft = segment.measuredGlyphOffsets[segmentGlyphStart] ?? 0;
  const glyphRight = segment.measuredGlyphOffsets[segmentGlyphEnd] ?? glyphLeft;
  const fillRight = glyphLeft + Math.max(glyphRight - glyphLeft, 0) * clamp2(fillProgress, 0, 1);
  if (fillRight <= glyphLeft) return;
  context.save();
  context.beginPath();
  context.rect(
    baseX + segment.x + glyphLeft,
    baseY - 96,
    Math.max(fillRight - glyphLeft, 1),
    192
  );
  context.clip();
  context.fillStyle = fillStyle;
  context.shadowBlur = 0;
  context.shadowColor = "transparent";
  context.fillText(segment.text, baseX + segment.x, baseY);
  context.restore();
};
var createStaticBlockSnapshot = (block, theme, fillStyle, shadowBlur = 0, shadowColor = "transparent") => {
  if (typeof document === "undefined") {
    return null;
  }
  const rasterScale = clamp2(window.devicePixelRatio || 1, 1, 2);
  const padding = Math.ceil(Math.max(block.fontPx * 0.32, shadowBlur + block.fontPx * 0.08, 4));
  const canvas = document.createElement("canvas");
  canvas.width = Math.max(1, Math.ceil((block.width + padding * 2) * rasterScale));
  canvas.height = Math.max(1, Math.ceil((block.height + padding * 2) * rasterScale));
  const context = canvas.getContext("2d");
  if (!context) {
    return null;
  }
  const baselineOffset = block.lineHeight * (isCJK(block.line.fullText) ? 0.52 : 0.5);
  context.setTransform(rasterScale, 0, 0, rasterScale, 0, 0);
  context.font = buildCanvasFont(block, theme);
  context.textAlign = "left";
  context.textBaseline = "middle";
  context.fillStyle = fillStyle;
  context.shadowBlur = shadowBlur;
  context.shadowColor = shadowColor;
  for (const renderLine of block.renderLines) {
    context.fillText(
      renderLine.text,
      renderLine.left + padding,
      renderLine.top + baselineOffset + padding
    );
  }
  context.shadowBlur = 0;
  context.shadowColor = "transparent";
  return { canvas, padding };
};
var resolveCameraScaleForBlock = (block, viewport) => {
  const minViewportSide = Math.max(Math.min(viewport.width, viewport.height), 1);
  const targetLineHeight = clamp2(minViewportSide * 0.115, 64, 124);
  const heightScale = targetLineHeight / Math.max(block.lineHeight, 1);
  const widthScale = viewport.width * 0.92 / Math.max(block.width, 1);
  return clamp2(Math.min(heightScale, widthScale), 0.72, 2.2);
};
var resolveCameraRetargetDuration = (line) => {
  const hints = getLineRenderHints(line);
  if (!hints) {
    return 0.09;
  }
  const transitionTiming = getLineTransitionTiming(
    hints.rawDuration,
    hints.lineTransitionMode,
    hints.wordRevealMode
  );
  if (hints.lineTransitionMode === "none") {
    return clamp2(Math.max(hints.rawDuration, 0.08) * 0.34, 0.04, 0.075);
  }
  if (hints.lineTransitionMode === "fast") {
    return clamp2(
      transitionTiming.enterDuration * 0.5 + transitionTiming.exitDuration * 0.12,
      0.055,
      0.095
    );
  }
  return clamp2(
    transitionTiming.enterDuration * 0.44 + transitionTiming.linePassHold * 0.22,
    0.075,
    0.13
  );
};
var resolveOverviewRetargetDuration = (viewport) => clamp2(
  Math.min(viewport.width, viewport.height) / 1500,
  0.38,
  0.58
);
var resolveOverviewFlightBridge = ({
  fromX,
  fromY,
  fromScale,
  targetX,
  targetY,
  targetScale,
  overviewCamera,
  viewport
}) => {
  if (!overviewCamera) {
    return null;
  }
  const safeScale = Math.max(fromScale, targetScale, overviewCamera.scale, 1e-3);
  const minViewportSide = Math.max(Math.min(viewport.width, viewport.height), 1);
  const deltaX = fromX - targetX;
  const deltaY = fromY - targetY;
  const worldDistance = Math.hypot(deltaX, deltaY);
  const screenDistance = worldDistance * safeScale;
  if (worldDistance <= 0 || screenDistance < minViewportSide * FUME_CAMERA_TELEPORT_TRIGGER_SCREENS) {
    return null;
  }
  const loftStrength = clamp2(
    (screenDistance / minViewportSide - FUME_CAMERA_TELEPORT_TRIGGER_SCREENS) / 3.4,
    0,
    1
  );
  const midpointX = mix(fromX, targetX, 0.5);
  const midpointY = mix(fromY, targetY, 0.5);
  const waypointCenterBias = mix(0.18, 0.42, loftStrength);
  const waypointX = mix(midpointX, overviewCamera.x, waypointCenterBias);
  const waypointY = mix(midpointY, overviewCamera.y, waypointCenterBias);
  const endpointScale = Math.max(fromScale, targetScale, 1e-3);
  const loftedScale = endpointScale * mix(0.62, 0.4, loftStrength);
  const overviewLimitedScale = overviewCamera.scale * mix(1.85, 1.55, loftStrength);
  const waypointScale = clamp2(
    Math.max(loftedScale, overviewLimitedScale),
    CAMERA_SCALE_MIN,
    Math.max(endpointScale * 0.92, CAMERA_SCALE_MIN)
  );
  const overviewDistanceFromStart = Math.hypot(waypointX - fromX, waypointY - fromY) * Math.max(fromScale, waypointScale, 1e-3);
  const overviewDistanceToTarget = Math.hypot(targetX - waypointX, targetY - waypointY) * Math.max(targetScale, waypointScale, 1e-3);
  const totalLegDistance = overviewDistanceFromStart + overviewDistanceToTarget;
  const waypointPhase = totalLegDistance <= 0 ? 0.36 : clamp2(overviewDistanceFromStart / totalLegDistance, 0.26, 0.44);
  const duration = clamp2(
    0.26 + screenDistance / (minViewportSide * 5.5) * 0.28,
    0.3,
    0.68
  );
  return {
    waypointX,
    waypointY,
    waypointScale,
    waypointPhase,
    duration
  };
};
var resolveArticleOverviewCamera = (article, viewport) => {
  if (article.blocks.length === 0) {
    const fitScale2 = Math.min(
      viewport.width / Math.max(article.width, 1),
      viewport.height / Math.max(article.height, 1)
    );
    return {
      x: article.width * 0.5,
      y: article.height * 0.5,
      scale: clamp2(fitScale2 * 0.92, CAMERA_SCALE_MIN, 0.72)
    };
  }
  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;
  for (const block of article.blocks) {
    minX = Math.min(minX, block.x);
    minY = Math.min(minY, block.y);
    maxX = Math.max(maxX, block.x + block.width);
    maxY = Math.max(maxY, block.y + block.height);
  }
  const paddingX = clamp2(viewport.width * 0.2, 120, 280);
  const paddingY = clamp2(viewport.height * 0.2, 96, 220);
  const framedWidth = Math.max(maxX - minX + paddingX * 2, 1);
  const framedHeight = Math.max(maxY - minY + paddingY * 2, 1);
  const fitScale = Math.min(
    viewport.width / framedWidth,
    viewport.height / framedHeight
  );
  return {
    x: (minX + maxX) * 0.5,
    y: (minY + maxY) * 0.5,
    scale: clamp2(fitScale, CAMERA_SCALE_MIN, 0.72)
  };
};
var resolveFocusBlock = (article, currentLineIndex, currentTimeValue) => {
  if (currentLineIndex >= 0) {
    const active = article.blockBySourceLineIndex.get(currentLineIndex) ?? null;
    if (active) {
      return active;
    }
  }
  const chronologicalLastBlock = article.chronologicalBlocks[article.chronologicalBlocks.length - 1] ?? null;
  if (chronologicalLastBlock && currentTimeValue >= article.lastChronologicalRenderEndTime) {
    return chronologicalLastBlock;
  }
  for (let index = article.chronologicalBlocks.length - 1; index >= 0; index -= 1) {
    const block = article.chronologicalBlocks[index];
    const printedCount = resolvePrintedGraphemeCount(
      block.line,
      block.wordRanges,
      block.graphemes.length,
      currentTimeValue
    );
    if (printedCount > 0) {
      return block;
    }
  }
  return article.chronologicalBlocks[0] ?? null;
};
var VisualizerFume = (props) => {
  const {
    currentTime,
    currentLineIndex,
    lines,
    theme,
    subtitleTheme,
    audioPower,
    audioBands,
    showText = true,
    seed,
    staticMode = false,
    lyricsFontScale = 1,
    subtitleFontScale = 1,
    fumeTuning,
    subtitleOverlayOpacity,
    subtitleOverlayBackground,
    isPlayerChromeHidden = false,
    hideTranslationSubtitle = false,
    showSubtitleTranslation = true,
    subtitleContentMode,
    paused = false
  } = props;
  const viewportRef = (0, import_react.useRef)(null);
  const canvasRef = (0, import_react.useRef)(null);
  const topInset = getLumeEmbeddedTopInset();
  const currentLineIndexRef = (0, import_react.useRef)(currentLineIndex);
  const cameraInitializedRef = (0, import_react.useRef)(false);
  const cameraRetargetRef = (0, import_react.useRef)({
    sourceLineIndex: -1,
    startedAt: 0,
    duration: 0.18,
    fromX: 0,
    fromY: 0,
    fromScale: 1,
    bridgeMode: "none",
    bridgeWaypointX: 0,
    bridgeWaypointY: 0,
    bridgeWaypointScale: 1,
    bridgeWaypointPhase: 0.36
  });
  const cameraRef = (0, import_react.useRef)({
    x: 0,
    y: 0,
    velocityX: 0,
    velocityY: 0,
    focusX: 0,
    focusY: 0,
    scale: 1,
    velocityScale: 0,
    focusScale: 1
  });
  const staticBlockSnapshotCacheRef = (0, import_react.useRef)(/* @__PURE__ */ new Map());
  const layoutBuildVersionRef = (0, import_react.useRef)(0);
  const hasResolvedArticleRef = (0, import_react.useRef)(false);
  const [viewport, setViewport] = (0, import_react.useState)({ width: 0, height: 0 });
  const renderViewport = viewport;
  const [article, setArticle] = (0, import_react.useState)(null);
  const [isLayoutPending, setIsLayoutPending] = (0, import_react.useState)(false);
  const [hasPrintedContent, setHasPrintedContent] = (0, import_react.useState)(false);
  const hasPrintedContentRef = (0, import_react.useRef)(false);
  (0, import_react.useEffect)(() => {
    currentLineIndexRef.current = currentLineIndex;
  }, [currentLineIndex]);
  (0, import_react.useEffect)(() => {
    const element = viewportRef.current;
    if (!element) {
      return;
    }
    const observer = new ResizeObserver((entries) => {
      const entry = entries[0];
      if (!entry) return;
      const nextWidth = entry.contentRect.width;
      const nextHeight = entry.contentRect.height;
      setViewport((previous) => previous.width === nextWidth && previous.height === nextHeight ? previous : { width: nextWidth, height: nextHeight });
    });
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  const runtime = (0, import_react.useMemo)(() => {
    const activeLine = lines[currentLineIndex] ?? null;
    const timeNow = currentTime.get();
    return {
      activeLine,
      recentCompletedLine: getRecentCompletedLine({
        lines,
        currentLineIndex,
        currentTime: timeNow,
        getLineEndTime: getLineRenderEndTime
      }),
      nextLines: getUpcomingLines(lines, currentLineIndex, 2)
    };
  }, [currentLineIndex, lines]);
  const resolvedFumeTuning = (0, import_react.useMemo)(() => ({
    hidePrintSymbols: fumeTuning?.hidePrintSymbols ?? DEFAULT_FUME_TUNING.hidePrintSymbols,
    disableGeometricBackground: fumeTuning?.disableGeometricBackground ?? DEFAULT_FUME_TUNING.disableGeometricBackground,
    backgroundObjectOpacity: clamp2(
      fumeTuning?.backgroundObjectOpacity ?? DEFAULT_FUME_TUNING.backgroundObjectOpacity,
      0,
      1
    ),
    textHoldRatio: clamp2(fumeTuning?.textHoldRatio ?? DEFAULT_FUME_TUNING.textHoldRatio, 0, 1),
    cameraTrackingMode: fumeTuning?.cameraTrackingMode === "stepped" || fumeTuning?.cameraTrackingMode === "smooth" ? fumeTuning.cameraTrackingMode : DEFAULT_FUME_TUNING.cameraTrackingMode,
    cameraSpeed: clamp2(fumeTuning?.cameraSpeed ?? DEFAULT_FUME_TUNING.cameraSpeed, 0.55, 1.85),
    glowIntensity: clamp2(fumeTuning?.glowIntensity ?? DEFAULT_FUME_TUNING.glowIntensity, 0, 1.8),
    heroScale: clamp2(fumeTuning?.heroScale ?? DEFAULT_FUME_TUNING.heroScale, 0.82, 1.32)
  }), [fumeTuning]);
  const layoutTheme = (0, import_react.useMemo)(
    () => ({
      name: theme.name,
      fontStyle: theme.fontStyle,
      fontFamily: theme.fontFamily,
      fontFamilyStack: theme.fontFamilyStack,
      fontWeight: theme.fontWeight
    }),
    [theme.fontFamily, theme.fontFamilyStack, theme.fontStyle, theme.fontWeight, theme.name]
  );
  const layoutFumeTuning = (0, import_react.useMemo)(() => ({
    ...DEFAULT_FUME_TUNING,
    heroScale: resolvedFumeTuning.heroScale
  }), [resolvedFumeTuning.heroScale]);
  (0, import_react.useEffect)(() => {
    const requestVersion = layoutBuildVersionRef.current + 1;
    layoutBuildVersionRef.current = requestVersion;
    if (renderViewport.width <= 0 || renderViewport.height <= 0 || lines.length === 0) {
      hasResolvedArticleRef.current = false;
      setArticle(null);
      setIsLayoutPending(false);
      return;
    }
    setIsLayoutPending(true);
    let rafId = 0;
    let timeoutId = 0;
    const delay = hasResolvedArticleRef.current ? LAYOUT_REBUILD_DEBOUNCE_MS : 0;
    rafId = window.requestAnimationFrame(() => {
      timeoutId = window.setTimeout(() => {
        if (layoutBuildVersionRef.current !== requestVersion) {
          return;
        }
        const layoutCacheKey = buildLayoutCacheKey(lines, renderViewport, layoutTheme, lyricsFontScale, layoutFumeTuning);
        const nextArticle = lastFumeLayoutCache?.key === layoutCacheKey ? lastFumeLayoutCache.article : buildArticleLayout(lines, renderViewport, layoutTheme, lyricsFontScale, layoutFumeTuning);
        if (layoutBuildVersionRef.current !== requestVersion) {
          return;
        }
        lastFumeLayoutCache = {
          key: layoutCacheKey,
          article: nextArticle
        };
        hasResolvedArticleRef.current = nextArticle !== null;
        setArticle(nextArticle);
        setIsLayoutPending(false);
      }, delay);
    });
    return () => {
      window.cancelAnimationFrame(rafId);
      window.clearTimeout(timeoutId);
    };
  }, [layoutFumeTuning, layoutTheme, lines, lyricsFontScale, renderViewport]);
  const lastRenderableLine = (0, import_react.useMemo)(() => {
    for (let index = lines.length - 1; index >= 0; index -= 1) {
      const line = lines[index];
      if (line?.fullText.trim().length) {
        return line;
      }
    }
    return null;
  }, [lines]);
  const overviewStartTime = (0, import_react.useMemo)(() => {
    if (!lastRenderableLine) {
      return Number.POSITIVE_INFINITY;
    }
    const lineStartTime = lastRenderableLine.startTime;
    const lineRenderEndTime = getLineRenderEndTime(lastRenderableLine);
    return lineStartTime + Math.max(lineRenderEndTime - lineStartTime, 0) * 0.5;
  }, [lastRenderableLine]);
  const backgroundScene = (0, import_react.useMemo)(
    () => buildFumeBackgroundScene({
      viewport: renderViewport,
      world: {
        width: article?.width ?? Math.max(renderViewport.width * 1.8, renderViewport.width),
        height: article?.height ?? Math.max(renderViewport.height * 1.8, renderViewport.height)
      },
      paperBounds: article?.paperBounds,
      seed: `${seed ?? "fume"}:${theme.name}`
    }),
    [article?.height, article?.paperBounds, article?.width, renderViewport, seed, theme.name]
  );
  const overviewCamera = (0, import_react.useMemo)(
    () => article ? resolveArticleOverviewCamera(article, renderViewport) : null,
    [article, renderViewport]
  );
  const cameraSpeed = resolvedFumeTuning.cameraSpeed;
  const glowIntensity = resolvedFumeTuning.glowIntensity;
  const backgroundObjectOpacity = resolvedFumeTuning.backgroundObjectOpacity;
  const textHoldRatio = resolvedFumeTuning.textHoldRatio;
  const passedFadeDuration = (0, import_react.useMemo)(
    () => resolveFumePassedFadeDuration(lines, textHoldRatio),
    [lines, textHoldRatio]
  );
  const translationFontSize = `clamp(${(1.05 * lyricsFontScale).toFixed(3)}rem, ${(2.2 * lyricsFontScale).toFixed(3)}vw, ${(1.2 * lyricsFontScale).toFixed(3)}rem)`;
  const upcomingFontSize = `clamp(${(0.875 * lyricsFontScale).toFixed(3)}rem, ${(1.8 * lyricsFontScale).toFixed(3)}vw, ${(1 * lyricsFontScale).toFixed(3)}rem)`;
  (0, import_react.useEffect)(() => {
    staticBlockSnapshotCacheRef.current.clear();
  }, [
    article,
    theme.name,
    theme.primaryColor,
    theme.secondaryColor,
    theme.accentColor,
    theme.fontStyle,
    theme.fontFamily,
    theme.fontFamilyStack
  ]);
  (0, import_react.useEffect)(() => {
    hasPrintedContentRef.current = false;
    setHasPrintedContent(false);
  }, [article]);
  (0, import_react.useEffect)(() => {
    const canvas = canvasRef.current;
    if (!canvas) {
      return;
    }
    const context = canvas.getContext("2d");
    if (!context) {
      return;
    }
    const topVisualCanvas = document.createElement("canvas");
    topVisualCanvas.dataset.lumenicTopVisualSnapshot = "fume";
    topVisualCanvas.style.position = "absolute";
    topVisualCanvas.style.left = "0";
    topVisualCanvas.style.top = "0";
    topVisualCanvas.style.visibility = "hidden";
    topVisualCanvas.style.pointerEvents = "none";
    canvas.parentElement?.appendChild(topVisualCanvas);
    const topVisualLayer = registerLumeTopVisualLayer({
      canvas: topVisualCanvas,
      order: 20,
      role: "lyrics"
    });
    const width = Math.max(Math.floor(renderViewport.width), 1);
    const height = Math.max(Math.floor(renderViewport.height), 1);
    const dpr = window.devicePixelRatio || 1;
    if (canvas.width !== Math.floor(width * dpr) || canvas.height !== Math.floor(height * dpr)) {
      canvas.width = Math.floor(width * dpr);
      canvas.height = Math.floor(height * dpr);
      canvas.style.width = `${width}px`;
      canvas.style.height = `${height}px`;
    }
    context.setTransform(dpr, 0, 0, dpr, 0, 0);
    context.clearRect(0, 0, width, height);
    if (article && !cameraInitializedRef.current) {
      cameraRef.current = {
        x: article.width * 0.5,
        y: article.height * 0.5,
        velocityX: 0,
        velocityY: 0,
        focusX: article.width * 0.5,
        focusY: article.height * 0.5,
        scale: 1.18,
        velocityScale: 0,
        focusScale: 1.18
      };
      cameraInitializedRef.current = true;
    } else if (article) {
      cameraRef.current.x = clamp2(cameraRef.current.x, 0, article.width);
      cameraRef.current.y = clamp2(cameraRef.current.y, 0, article.height);
      cameraRef.current.focusX = clamp2(cameraRef.current.focusX, 0, article.width);
      cameraRef.current.focusY = clamp2(cameraRef.current.focusY, 0, article.height);
      cameraRef.current.scale = clamp2(cameraRef.current.scale, CAMERA_SCALE_MIN, CAMERA_SCALE_MAX);
      cameraRef.current.focusScale = clamp2(cameraRef.current.focusScale, CAMERA_SCALE_MIN, CAMERA_SCALE_MAX);
    } else {
      cameraInitializedRef.current = false;
    }
    let frameId = 0;
    let lastFrameAt = null;
    const draw = () => {
      const now = performance.now();
      const dt = lastFrameAt === null ? 1 / 60 : clamp2((now - lastFrameAt) / 1e3, 1 / 240, 0.05);
      lastFrameAt = now;
      const currentWidth = Math.max(Math.floor(renderViewport.width), 1);
      const currentHeight = Math.max(Math.floor(renderViewport.height), 1);
      const currentDpr = window.devicePixelRatio || 1;
      if (canvas.width !== Math.floor(currentWidth * currentDpr) || canvas.height !== Math.floor(currentHeight * currentDpr)) {
        canvas.width = Math.floor(currentWidth * currentDpr);
        canvas.height = Math.floor(currentHeight * currentDpr);
        canvas.style.width = `${currentWidth}px`;
        canvas.style.height = `${currentHeight}px`;
      }
      context.setTransform(currentDpr, 0, 0, currentDpr, 0, 0);
      context.clearRect(0, 0, currentWidth, currentHeight);
      const time = currentTime.get();
      const viewportCenterX = renderViewport.width * 0.5;
      const viewportCenterY = renderViewport.height * 0.5;
      const fumeBackgroundAudioLevels = {
        power: audioPower.get(),
        bass: audioBands.bass.get(),
        lowMid: audioBands.lowMid.get(),
        mid: audioBands.mid.get(),
        vocal: audioBands.vocal.get(),
        treble: audioBands.treble.get()
      };
      if (!article) {
        if (!staticMode) {
          context.save();
          context.translate(viewportCenterX, viewportCenterY);
          context.translate(-backgroundScene.width * 0.5, -backgroundScene.height * 0.5);
          drawFumeBackground({
            context,
            scene: backgroundScene,
            theme,
            time: time + now * 18e-5,
            audioLevels: fumeBackgroundAudioLevels,
            objectOpacityMultiplier: backgroundObjectOpacity * 2
          });
          context.restore();
        }
        if (!paused) {
          frameId = window.requestAnimationFrame(draw);
        }
        return;
      }
      if (!hasPrintedContentRef.current && time >= article.firstRenderableStartTime) {
        hasPrintedContentRef.current = true;
        setHasPrintedContent(true);
      }
      const focusBlock = resolveFocusBlock(article, currentLineIndexRef.current, time);
      const shouldShowOverview = overviewCamera !== null && time >= overviewStartTime;
      let targetCameraX = article.width * 0.5;
      let targetCameraY = article.height * 0.5;
      let targetCameraScale = 1.18;
      let entryFocusPoint = null;
      let didRetargetThisFrame = false;
      if (shouldShowOverview && overviewCamera) {
        targetCameraX = overviewCamera.x;
        targetCameraY = overviewCamera.y;
        targetCameraScale = overviewCamera.scale;
        if (cameraRetargetRef.current.sourceLineIndex !== OVERVIEW_CAMERA_SOURCE) {
          cameraRetargetRef.current = {
            sourceLineIndex: OVERVIEW_CAMERA_SOURCE,
            startedAt: time,
            duration: clamp2(resolveOverviewRetargetDuration(renderViewport) / cameraSpeed, 0.12, 1.2),
            fromX: cameraRef.current.x,
            fromY: cameraRef.current.y,
            fromScale: cameraRef.current.scale,
            bridgeMode: "none",
            bridgeWaypointX: 0,
            bridgeWaypointY: 0,
            bridgeWaypointScale: 1,
            bridgeWaypointPhase: 0.36
          };
          didRetargetThisFrame = true;
        }
      } else if (focusBlock) {
        const focusPoint = resolvedFumeTuning.cameraTrackingMode === "stepped" ? resolveSteppedBlockFocusPoint(
          focusBlock,
          resolvePrintedGraphemeCount(
            focusBlock.line,
            focusBlock.wordRanges,
            focusBlock.graphemes.length,
            time
          )
        ) : resolveSmoothBlockFocusPoint(
          focusBlock,
          resolvePrintedGraphemeProgress(
            focusBlock.line,
            focusBlock.wordRanges,
            focusBlock.graphemes.length,
            time
          )
        );
        entryFocusPoint = resolveBlockEntryFocusPoint(focusBlock);
        targetCameraX = focusPoint.x;
        targetCameraY = focusPoint.y;
        targetCameraScale = resolveCameraScaleForBlock(focusBlock, renderViewport);
        if (cameraRetargetRef.current.sourceLineIndex !== focusBlock.sourceLineIndex) {
          cameraRetargetRef.current = {
            sourceLineIndex: focusBlock.sourceLineIndex,
            startedAt: time,
            duration: clamp2(resolveCameraRetargetDuration(focusBlock.line) / cameraSpeed, 0.03, 0.3),
            fromX: cameraRef.current.x,
            fromY: cameraRef.current.y,
            fromScale: cameraRef.current.scale,
            bridgeMode: "none",
            bridgeWaypointX: 0,
            bridgeWaypointY: 0,
            bridgeWaypointScale: 1,
            bridgeWaypointPhase: 0.36
          };
          didRetargetThisFrame = true;
        }
      } else if (cameraRetargetRef.current.sourceLineIndex !== -1) {
        cameraRetargetRef.current = {
          sourceLineIndex: -1,
          startedAt: time,
          duration: clamp2(0.18 / cameraSpeed, 0.05, 0.4),
          fromX: cameraRef.current.x,
          fromY: cameraRef.current.y,
          fromScale: cameraRef.current.scale,
          bridgeMode: "none",
          bridgeWaypointX: 0,
          bridgeWaypointY: 0,
          bridgeWaypointScale: 1,
          bridgeWaypointPhase: 0.36
        };
        didRetargetThisFrame = true;
      }
      const retargetElapsed = Math.max(time - cameraRetargetRef.current.startedAt, 0);
      const overviewTextRestoreProgress = shouldShowOverview && cameraRetargetRef.current.sourceLineIndex === OVERVIEW_CAMERA_SOURCE ? easeInOutCubic(clamp2(
        retargetElapsed / Math.max(cameraRetargetRef.current.duration, 1e-3),
        0,
        1
      )) : 0;
      const retargetPhase = clamp2(
        retargetElapsed / Math.max(cameraRetargetRef.current.duration, 1e-3),
        0,
        1
      );
      const retargetBoost = 1 - easeOutCubic(retargetPhase);
      const entryFocusBias = Math.pow(retargetBoost, 0.58);
      if (entryFocusPoint && !paused) {
        targetCameraX = mix(targetCameraX, entryFocusPoint.x, entryFocusBias);
        targetCameraY = mix(targetCameraY, entryFocusPoint.y, entryFocusBias);
      }
      if (!staticMode) {
        const floatConfig = theme.animationIntensity === "chaotic" ? { distance: 24, duration: 5.8, scaleAmplitude: 0.014 } : theme.animationIntensity === "calm" ? { distance: 14, duration: 8.5, scaleAmplitude: 8e-3 } : { distance: 18, duration: 7, scaleAmplitude: 0.011 };
        const floatPhase = now / 1e3 / floatConfig.duration * Math.PI * 2;
        const overviewAttenuation = shouldShowOverview ? 0.36 : 1;
        const screenFloatX = Math.sin(floatPhase * 0.74 + 0.8) * floatConfig.distance * 0.34;
        const screenFloatY = (Math.sin(floatPhase) * floatConfig.distance + Math.sin(floatPhase * 0.5 + 1.1) * floatConfig.distance * 0.22) * overviewAttenuation;
        const worldFloatDivisor = Math.max(targetCameraScale, 1e-3);
        targetCameraX -= screenFloatX / worldFloatDivisor;
        targetCameraY -= screenFloatY / worldFloatDivisor;
        targetCameraScale = clamp2(
          targetCameraScale * (1 + Math.sin(floatPhase + 0.9) * floatConfig.scaleAmplitude * overviewAttenuation),
          CAMERA_SCALE_MIN,
          CAMERA_SCALE_MAX
        );
      }
      if (didRetargetThisFrame) {
        const bridgeScale = Math.max(cameraRef.current.scale, targetCameraScale, 1e-3);
        const screenDeltaX = Math.abs(targetCameraX - cameraRetargetRef.current.fromX) * bridgeScale;
        const screenDeltaY = Math.abs(targetCameraY - cameraRetargetRef.current.fromY) * bridgeScale;
        const screenDistance = Math.hypot(screenDeltaX, screenDeltaY);
        cameraRetargetRef.current.bridgeMode = screenDistance >= Math.min(renderViewport.width, renderViewport.height) * 0.42 ? "direct" : "none";
        cameraRetargetRef.current.bridgeWaypointX = targetCameraX;
        cameraRetargetRef.current.bridgeWaypointY = targetCameraY;
        cameraRetargetRef.current.bridgeWaypointScale = targetCameraScale;
        cameraRetargetRef.current.bridgeWaypointPhase = 0.5;
        if (cameraRetargetRef.current.sourceLineIndex >= 0) {
          const overviewFlightBridge = resolveOverviewFlightBridge({
            fromX: cameraRetargetRef.current.fromX,
            fromY: cameraRetargetRef.current.fromY,
            fromScale: cameraRetargetRef.current.fromScale,
            targetX: targetCameraX,
            targetY: targetCameraY,
            targetScale: targetCameraScale,
            overviewCamera,
            viewport: renderViewport
          });
          if (overviewFlightBridge) {
            cameraRetargetRef.current.bridgeMode = "overview";
            cameraRetargetRef.current.bridgeWaypointX = overviewFlightBridge.waypointX;
            cameraRetargetRef.current.bridgeWaypointY = overviewFlightBridge.waypointY;
            cameraRetargetRef.current.bridgeWaypointScale = overviewFlightBridge.waypointScale;
            cameraRetargetRef.current.bridgeWaypointPhase = overviewFlightBridge.waypointPhase;
            cameraRetargetRef.current.duration = Math.max(
              cameraRetargetRef.current.duration,
              clamp2(overviewFlightBridge.duration / cameraSpeed, 0.16, 0.9)
            );
          }
        }
      }
      const cameraDistance = Math.hypot(
        targetCameraX - cameraRef.current.x,
        targetCameraY - cameraRef.current.y
      );
      const shouldUseBridge = cameraRetargetRef.current.bridgeMode !== "none" && retargetPhase < 1;
      if (shouldUseBridge) {
        let bridgedCameraX = targetCameraX;
        let bridgedCameraY = targetCameraY;
        let bridgedCameraScale = targetCameraScale;
        if (cameraRetargetRef.current.bridgeMode === "overview") {
          const bridgePhase = easeOutCubic(retargetPhase);
          bridgedCameraX = quadraticBezier(
            cameraRetargetRef.current.fromX,
            cameraRetargetRef.current.bridgeWaypointX,
            targetCameraX,
            bridgePhase
          );
          bridgedCameraY = quadraticBezier(
            cameraRetargetRef.current.fromY,
            cameraRetargetRef.current.bridgeWaypointY,
            targetCameraY,
            bridgePhase
          );
          bridgedCameraScale = quadraticBezier(
            cameraRetargetRef.current.fromScale,
            cameraRetargetRef.current.bridgeWaypointScale,
            targetCameraScale,
            bridgePhase
          );
        } else {
          const bridgePhase = easeInOutCubic(retargetPhase);
          bridgedCameraX = mix(cameraRetargetRef.current.fromX, targetCameraX, bridgePhase);
          bridgedCameraY = mix(cameraRetargetRef.current.fromY, targetCameraY, bridgePhase);
          bridgedCameraScale = mix(cameraRetargetRef.current.fromScale, targetCameraScale, bridgePhase);
        }
        const bridgeCatchUp = 1 - Math.exp(-dt * (cameraRetargetRef.current.bridgeMode === "overview" ? mix(12.5, 22, 1 - retargetPhase) : mix(10.5, 17.5, 1 - retargetPhase)));
        cameraRef.current.focusX = bridgedCameraX;
        cameraRef.current.focusY = bridgedCameraY;
        cameraRef.current.focusScale = bridgedCameraScale;
        cameraRef.current.x += (bridgedCameraX - cameraRef.current.x) * bridgeCatchUp;
        cameraRef.current.y += (bridgedCameraY - cameraRef.current.y) * bridgeCatchUp;
        cameraRef.current.scale += (bridgedCameraScale - cameraRef.current.scale) * bridgeCatchUp;
        cameraRef.current.scale = clamp2(cameraRef.current.scale, CAMERA_SCALE_MIN, CAMERA_SCALE_MAX);
        cameraRef.current.velocityX *= 0.72;
        cameraRef.current.velocityY *= 0.72;
        cameraRef.current.velocityScale *= 0.68;
      } else {
        const boostedCatchUpRate = clamp2(
          4.8 / Math.max(cameraRetargetRef.current.duration, 0.05),
          20,
          54
        );
        const targetCatchUp = 1 - Math.exp(-dt * mix(11.2, boostedCatchUpRate, retargetBoost));
        cameraRef.current.focusX += (targetCameraX - cameraRef.current.focusX) * targetCatchUp;
        cameraRef.current.focusY += (targetCameraY - cameraRef.current.focusY) * targetCatchUp;
        cameraRef.current.focusScale += (targetCameraScale - cameraRef.current.focusScale) * (1 - Math.exp(-dt * mix(5.4, 12.8, retargetBoost)));
        const springStrength = mix(
          208,
          clamp2(15.8 / Math.max(cameraRetargetRef.current.duration * cameraRetargetRef.current.duration, 64e-4), 260, 780),
          retargetBoost
        );
        const damping = mix(
          24,
          clamp2(Math.sqrt(springStrength) * 1.36, 24, 40),
          retargetBoost
        );
        const accelX = (cameraRef.current.focusX - cameraRef.current.x) * springStrength - cameraRef.current.velocityX * damping;
        const accelY = (cameraRef.current.focusY - cameraRef.current.y) * springStrength - cameraRef.current.velocityY * damping;
        cameraRef.current.velocityX += accelX * dt;
        cameraRef.current.velocityY += accelY * dt;
        const maxVelocity = mix(
          1320,
          clamp2(cameraDistance / Math.max(cameraRetargetRef.current.duration * 0.28, 0.028), 2600, 8800),
          retargetBoost
        );
        cameraRef.current.velocityX = clamp2(cameraRef.current.velocityX, -maxVelocity, maxVelocity);
        cameraRef.current.velocityY = clamp2(cameraRef.current.velocityY, -maxVelocity, maxVelocity);
        cameraRef.current.x += cameraRef.current.velocityX * dt;
        cameraRef.current.y += cameraRef.current.velocityY * dt;
        const scaleSpringStrength = mix(54, 108, retargetBoost);
        const scaleDamping = mix(13.5, 21, retargetBoost);
        const accelScale = (cameraRef.current.focusScale - cameraRef.current.scale) * scaleSpringStrength - cameraRef.current.velocityScale * scaleDamping;
        cameraRef.current.velocityScale += accelScale * dt;
        cameraRef.current.velocityScale = clamp2(cameraRef.current.velocityScale, -1.6, 1.6);
        cameraRef.current.scale += cameraRef.current.velocityScale * dt;
        cameraRef.current.scale = clamp2(cameraRef.current.scale, CAMERA_SCALE_MIN, CAMERA_SCALE_MAX);
      }
      if (paused) {
        Object.assign(cameraRef.current, {
          x: targetCameraX,
          y: targetCameraY,
          scale: targetCameraScale,
          focusX: targetCameraX,
          focusY: targetCameraY,
          focusScale: targetCameraScale,
          velocityX: 0,
          velocityY: 0,
          velocityScale: 0
        });
      }
      const screenScale = cameraRef.current.scale;
      if (!staticMode) {
        const backgroundCenterX = backgroundScene.width * 0.5;
        const backgroundCenterY = backgroundScene.height * 0.5;
        const backgroundVerticalOffset = clamp2(
          renderViewport.height * FUME_BACKGROUND_VERTICAL_OFFSET_RATIO / Math.max(screenScale, 1e-3),
          48,
          180
        );
        const backgroundCameraX = mix(
          backgroundCenterX,
          cameraRef.current.x,
          FUME_BACKGROUND_PARALLAX_X
        );
        const backgroundCameraY = mix(
          backgroundCenterY,
          cameraRef.current.y,
          FUME_BACKGROUND_PARALLAX_Y
        ) - backgroundVerticalOffset;
        const backgroundScale = clamp2(
          screenScale * FUME_BACKGROUND_SCALE_FACTOR,
          CAMERA_SCALE_MIN,
          CAMERA_SCALE_MAX
        );
        context.save();
        context.translate(viewportCenterX, viewportCenterY);
        context.scale(backgroundScale, backgroundScale);
        context.translate(-backgroundCameraX, -backgroundCameraY);
        drawFumeBackground({
          context,
          scene: backgroundScene,
          theme,
          time,
          audioLevels: fumeBackgroundAudioLevels,
          objectOpacityMultiplier: backgroundObjectOpacity * 2,
          parallax: {
            cameraX: backgroundCameraX,
            cameraY: backgroundCameraY,
            originX: backgroundCenterX,
            originY: backgroundCenterY,
            strength: 0.72
          }
        });
        context.restore();
      }
      context.save();
      context.translate(viewportCenterX, viewportCenterY);
      context.scale(screenScale, screenScale);
      context.translate(-cameraRef.current.x, -cameraRef.current.y);
      const activeGlowBoost = (theme.animationIntensity === "chaotic" ? 1.15 : theme.animationIntensity === "calm" ? 0.72 : 0.92) * glowIntensity;
      const passedGlowBase = (theme.animationIntensity === "chaotic" ? 0.95 : theme.animationIntensity === "calm" ? 0.35 : 0.62) * glowIntensity;
      if (showText) {
        for (const block of article.blocks) {
          const screenLeft = viewportCenterX + (block.x - cameraRef.current.x) * screenScale;
          const screenTop = viewportCenterY + (block.y - cameraRef.current.y) * screenScale;
          const screenRight = screenLeft + block.width * screenScale;
          const screenBottom = screenTop + block.height * screenScale;
          const overscan = 180;
          if (screenRight < -overscan || screenLeft > renderViewport.width + overscan || screenBottom < -overscan || screenTop > renderViewport.height + overscan) {
            continue;
          }
          const waitingOpacity = block.variant === "hero" ? 0.06 : 0.035;
          const activeOpacity = block.variant === "hero" ? 0.985 : 0.92;
          const effectiveTextHoldStyle = textHoldRatio >= 1 ? "standard" : "dimmed";
          const passedStyle = resolvePassedTextStyle(block.variant, effectiveTextHoldStyle);
          const passedOpacity = passedStyle.opacity;
          const passedFillColor = theme.karaokeFillColor ?? theme.primaryColor;
          const transitionPassedStyle = resolvePassedTextStyle(block.variant, "standard");
          const baselineOffset = block.lineHeight * (isCJK(block.line.fullText) ? 0.52 : 0.5);
          const lineEndTime = getLineRenderEndTime(block.line);
          const nextLineStartTime = lines[block.sourceLineIndex + 1]?.startTime ?? null;
          const linePassCutoffTime = resolveLinePassCutoffTime(block.line, nextLineStartTime);
          const revealCompleteTime = block.line.endTime;
          const hasRevealCompleted = time >= revealCompleteTime;
          const hasPassCutoffReached = time >= linePassCutoffTime;
          const lineDuration = Math.max(lineEndTime - block.line.startTime, 0.18);
          const colorTrailDuration = clamp2(
            lineDuration * (block.variant === "hero" ? 0.42 : 0.52),
            0.45,
            1.45
          );
          const staticState = time < block.line.startTime ? "waiting" : time >= lineEndTime + colorTrailDuration ? "passed" : null;
          if (staticState) {
            const snapshotScale = clamp2(window.devicePixelRatio || 1, 1, 2);
            const cacheStyleKey = staticState === "passed" ? effectiveTextHoldStyle : "base";
            const cacheKey = `${block.id}:${staticState}:${cacheStyleKey}:${snapshotScale}`;
            let snapshot = staticBlockSnapshotCacheRef.current.get(cacheKey);
            if (!snapshot) {
              snapshot = createStaticBlockSnapshot(
                block,
                theme,
                staticState === "waiting" ? colorWithAlpha(theme.primaryColor, waitingOpacity) : colorWithAlpha(passedFillColor, passedOpacity),
                staticState === "waiting" ? 0 : (2 + block.fontPx * 0.1) * 0.65 * passedGlowBase * passedStyle.glowMultiplier,
                staticState === "waiting" ? "transparent" : colorWithAlpha(theme.primaryColor, passedStyle.shadowAlphaBase)
              ) ?? void 0;
              if (snapshot) {
                staticBlockSnapshotCacheRef.current.set(cacheKey, snapshot);
              }
            }
            if (snapshot) {
              if (staticState === "passed" && effectiveTextHoldStyle === "dimmed") {
                const passedAt = lineEndTime + colorTrailDuration;
                const baseDimAmount = resolvePassedDimAmount(time, passedAt, passedFadeDuration);
                const dimAmount = baseDimAmount * (1 - overviewTextRestoreProgress);
                const standardStyle = resolvePassedTextStyle(block.variant, "standard");
                const standardCacheKey = `${block.id}:passed:standard:${snapshotScale}`;
                let standardSnapshot = staticBlockSnapshotCacheRef.current.get(standardCacheKey);
                if (!standardSnapshot) {
                  standardSnapshot = createStaticBlockSnapshot(
                    block,
                    theme,
                    colorWithAlpha(theme.primaryColor, standardStyle.opacity),
                    (2 + block.fontPx * 0.1) * 0.65 * passedGlowBase * standardStyle.glowMultiplier,
                    colorWithAlpha(theme.primaryColor, standardStyle.shadowAlphaBase)
                  ) ?? void 0;
                  if (standardSnapshot) {
                    staticBlockSnapshotCacheRef.current.set(standardCacheKey, standardSnapshot);
                  }
                }
                if (standardSnapshot) {
                  if (dimAmount <= 0) {
                    context.drawImage(
                      standardSnapshot.canvas,
                      block.x - standardSnapshot.padding,
                      block.y - standardSnapshot.padding,
                      block.width + standardSnapshot.padding * 2,
                      block.height + standardSnapshot.padding * 2
                    );
                    continue;
                  }
                  if (dimAmount < 1) {
                    const previousAlpha = context.globalAlpha;
                    context.globalAlpha = previousAlpha * (1 - dimAmount);
                    context.drawImage(
                      standardSnapshot.canvas,
                      block.x - standardSnapshot.padding,
                      block.y - standardSnapshot.padding,
                      block.width + standardSnapshot.padding * 2,
                      block.height + standardSnapshot.padding * 2
                    );
                    context.globalAlpha = previousAlpha * dimAmount;
                    context.drawImage(
                      snapshot.canvas,
                      block.x - snapshot.padding,
                      block.y - snapshot.padding,
                      block.width + snapshot.padding * 2,
                      block.height + snapshot.padding * 2
                    );
                    context.globalAlpha = previousAlpha;
                    continue;
                  }
                }
              }
              context.drawImage(
                snapshot.canvas,
                block.x - snapshot.padding,
                block.y - snapshot.padding,
                block.width + snapshot.padding * 2,
                block.height + snapshot.padding * 2
              );
              continue;
            }
          }
          const printedCount = resolvePrintedGraphemeCount(
            block.line,
            block.wordRanges,
            block.graphemes.length,
            time
          );
          const totalGraphemeCount = block.graphemes.length;
          context.save();
          context.font = buildCanvasFont(block, theme);
          context.textAlign = "left";
          context.textBaseline = "middle";
          const isLineActive = time >= block.line.startTime && time <= linePassCutoffTime;
          if (isLineActive) {
            const lineProgress = resolveVisualProgressWithCutoff(
              block.line.startTime,
              lineDuration,
              time,
              linePassCutoffTime
            );
            const lineGlowEnvelope = resolveDelayedGlowEnvelope(lineProgress, 0.8);
            const lineGlowAlpha = ((block.variant === "hero" ? 0.16 : 0.12) + lineGlowEnvelope * (block.variant === "hero" ? 0.26 : 0.2)) * glowIntensity;
            const lineGlowBlur = ((block.variant === "hero" ? 12 : 8) + lineGlowEnvelope * (block.fontPx * (block.variant === "hero" ? 0.7 : 0.52))) * glowIntensity;
            const lineGlowColor = colorWithAlpha(theme.accentColor, lineGlowAlpha);
            context.save();
            context.fillStyle = lineGlowColor;
            context.shadowBlur = lineGlowBlur;
            context.shadowColor = colorWithAlpha(theme.accentColor, lineGlowAlpha * 1.35);
            for (const renderLine of block.renderLines) {
              const glowBaseX = block.x + renderLine.left;
              const glowBaseY = block.y + renderLine.top + baselineOffset;
              for (const segment of renderLine.segments) {
                if (segment.text.trim().length === 0) {
                  continue;
                }
                context.fillText(segment.text, glowBaseX + segment.x, glowBaseY);
              }
            }
            context.restore();
          }
          for (const renderLine of block.renderLines) {
            const baseX = block.x + renderLine.left;
            const baseY = block.y + renderLine.top + baselineOffset;
            for (const segment of renderLine.segments) {
              for (let globalOffset = segment.start; globalOffset < segment.end; globalOffset += 1) {
                const graphemeIndex = globalOffset - renderLine.start;
                const grapheme = renderLine.graphemes[graphemeIndex];
                const rangeIndex = block.wordRangeIndexByOffset[globalOffset] ?? -1;
                const range = rangeIndex >= 0 ? block.wordRanges[rangeIndex] : null;
                const colorRangeIndex = block.colorRangeIndexByOffset[globalOffset] ?? -1;
                const colorRange = colorRangeIndex >= 0 ? block.wordRanges[colorRangeIndex] : range;
                const isPrinted = hasRevealCompleted || globalOffset < printedCount;
                const isFrontier = printedCount > 0 && globalOffset === printedCount && printedCount < totalGraphemeCount && !hasRevealCompleted && !hasPassCutoffReached;
                let alpha = isPrinted ? activeOpacity : isFrontier ? 0.82 : waitingOpacity;
                let shadowBlur = 0;
                let shadowColor = "transparent";
                let fillStyle = colorWithAlpha(theme.primaryColor, alpha);
                let karaokeFillProgress = 0;
                let karaokeFillStyle = "";
                if (range) {
                  const wordDuration = Math.max(range.word.endTime - range.word.startTime, 0.08);
                  const wordProgress = clamp2((time - range.word.startTime) / wordDuration, 0, 1);
                  const glyphCount = Math.max(range.end - range.start, 1);
                  const glyphIndexInRange = globalOffset - range.start;
                  const glyphTiming = range.word.syllables?.length ? range.graphemeTimings[Math.min(glyphIndexInRange, Math.max(range.graphemeTimings.length - 1, 0))] : void 0;
                  const glyphStartTime = glyphTiming?.startTime ?? range.word.startTime + glyphIndexInRange / glyphCount * wordDuration;
                  const glyphEndTime = glyphTiming?.endTime ?? range.word.startTime + (glyphIndexInRange + 1) / glyphCount * wordDuration;
                  const glyphDuration = Math.max(glyphEndTime - glyphStartTime, 1e-3);
                  const glyphProgress = glyphTiming ? clamp2((time - glyphStartTime) / glyphDuration, 0, 1) : clamp2(wordProgress * glyphCount - glyphIndexInRange, 0, 1);
                  const easedGlyphProgress = easeInOutCubic(glyphProgress);
                  const activeColor = getActiveColor((colorRange ?? range).word.text, theme);
                  const karaokeFillColor = theme.karaokeFillColor ?? "#ffffff";
                  const colorTrailPhase = resolveVisualProgressWithCutoff(
                    glyphEndTime,
                    colorTrailDuration,
                    time,
                    linePassCutoffTime
                  );
                  const colorTrailProgress = Math.pow(colorTrailPhase, 1.35);
                  const karaokeGlowProgress = time < range.word.startTime ? 0 : time < glyphEndTime ? resolveDelayedGlowEnvelope(glyphProgress, 0.68) : 1 - easeInCubic(colorTrailProgress);
                  if (hasPassCutoffReached) {
                    alpha = mix(activeOpacity, transitionPassedStyle.opacity, colorTrailProgress);
                    fillStyle = colorWithAlpha(karaokeFillColor, alpha);
                    shadowBlur = (2 + block.fontPx * 0.1) * (1 - colorTrailProgress * 0.35) * passedGlowBase * transitionPassedStyle.glowMultiplier;
                    shadowColor = colorWithAlpha(
                      mixColors(activeColor, theme.primaryColor, 0.55 + colorTrailProgress * 0.45),
                      transitionPassedStyle.shadowAlphaBase + (1 - colorTrailProgress) * transitionPassedStyle.shadowAlphaTrail
                    );
                  } else if (time < range.word.startTime) {
                    alpha = waitingOpacity;
                    fillStyle = colorWithAlpha(theme.primaryColor, alpha);
                  } else if (time < glyphEndTime) {
                    alpha = mix(waitingOpacity, activeOpacity, easedGlyphProgress);
                    fillStyle = colorWithAlpha(theme.primaryColor, alpha);
                    karaokeFillStyle = colorWithAlpha(karaokeFillColor, alpha);
                    karaokeFillProgress = easedGlyphProgress;
                    shadowBlur = (6 + block.fontPx * 0.28) * karaokeGlowProgress * activeGlowBoost;
                    shadowColor = colorWithAlpha(karaokeFillColor, 0.32 + karaokeGlowProgress * 0.5);
                  } else {
                    alpha = mix(activeOpacity, transitionPassedStyle.opacity, colorTrailProgress);
                    fillStyle = colorWithAlpha(karaokeFillColor, alpha);
                    shadowBlur = (4 + block.fontPx * 0.18) * karaokeGlowProgress * passedGlowBase * transitionPassedStyle.glowMultiplier;
                    shadowColor = colorWithAlpha(karaokeFillColor, 0.18 + karaokeGlowProgress * 0.42);
                  }
                }
                if (alpha <= 2e-3) {
                  continue;
                }
                const localStart = globalOffset - renderLine.start;
                const localEnd = localStart + 1;
                context.fillStyle = fillStyle;
                context.shadowBlur = shadowBlur;
                context.shadowColor = shadowColor;
                drawRenderTextRun(context, renderLine, segment, localStart, localEnd, baseX, baseY);
                if (karaokeFillProgress > 0 && karaokeFillStyle) {
                  drawProgressiveGlyphFill(
                    context,
                    renderLine,
                    segment,
                    localStart,
                    localEnd,
                    baseX,
                    baseY,
                    karaokeFillStyle,
                    karaokeFillProgress
                  );
                }
                context.shadowBlur = 0;
                context.shadowColor = "transparent";
              }
            }
          }
          context.restore();
        }
      }
      context.restore();
      publishLiquidGlassWebGLSceneFrame(canvas);
      if (captureLumeTopVisualFrame(canvas, topVisualCanvas, topInset)) {
        topVisualLayer.requestFrame();
      }
      if (!paused) {
        frameId = window.requestAnimationFrame(draw);
      }
    };
    const stopPausedClock = currentTime.on("change", () => {
      if (paused) draw();
    });
    draw();
    return () => {
      stopPausedClock();
      window.cancelAnimationFrame(frameId);
      topVisualLayer.dispose();
      topVisualCanvas.remove();
      unregisterLiquidGlassWebGLSceneCanvas(canvas);
      lastFrameAt = null;
    };
  }, [
    article,
    audioBands,
    audioPower,
    backgroundScene,
    backgroundObjectOpacity,
    cameraSpeed,
    currentTime,
    glowIntensity,
    passedFadeDuration,
    showText,
    paused,
    staticMode,
    textHoldRatio,
    theme,
    topInset,
    renderViewport
  ]);
  return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(
    Shell_default,
    {
      theme,
      audioPower,
      audioBands,
      sharedProps: {
        ...props,
        background: {
          ...props.background,
          common: {
            ...props.background?.common,
            disableGeometricBackground: Boolean(props.background?.common?.disableGeometricBackground) || resolvedFumeTuning.disableGeometricBackground
          }
        }
      },
      children: [
        /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", { ref: viewportRef, className: "relative z-10 h-full w-full pointer-events-none", children: [
          (article || lines.length === 0) && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            motion.div,
            {
              initial: false,
              animate: {
                opacity: 1,
                scale: article && showText ? hasPrintedContent ? 1 : 0.985 : 1
              },
              transition: { duration: 0.45, ease: "easeOut" },
              className: "absolute inset-x-0 bottom-0",
              style: {
                ...getLumeFullBleedLayerStyle(topInset),
                width: renderViewport.width,
                height: renderViewport.height
              },
              children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                "canvas",
                {
                  ref: canvasRef,
                  "data-lumenic-liquid-glass-scene-lyrics": true,
                  className: "absolute inset-0 h-full w-full"
                }
              )
            }
          ),
          isLayoutPending && /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            motion.div,
            {
              initial: { opacity: 0 },
              animate: { opacity: 1 },
              exit: { opacity: 0 },
              className: "absolute inset-0 flex items-center justify-center",
              children: /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(
                "div",
                {
                  className: "flex min-w-40 flex-col items-center gap-4 rounded-3xl border px-6 py-5",
                  style: {
                    backgroundColor: theme.backgroundColor,
                    borderColor: colorWithAlpha(theme.secondaryColor, 0.24),
                    boxShadow: `0 18px 60px ${colorWithAlpha(theme.backgroundColor, 0.52)}`
                  },
                  children: [
                    /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                      Hourglass,
                      {
                        size: 24,
                        className: "animate-pulse",
                        style: { color: colorWithAlpha(theme.primaryColor, 0.78) }
                      }
                    ),
                    /* @__PURE__ */ (0, import_jsx_runtime.jsxs)("div", { className: "flex w-28 flex-col gap-2.5", children: [
                      /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                        "div",
                        {
                          className: "h-2 rounded-full animate-pulse",
                          style: { backgroundColor: colorWithAlpha(theme.primaryColor, 0.32) }
                        }
                      ),
                      /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                        "div",
                        {
                          className: "h-2 rounded-full animate-pulse",
                          style: {
                            width: "78%",
                            backgroundColor: colorWithAlpha(theme.primaryColor, 0.22)
                          }
                        }
                      ),
                      /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
                        "div",
                        {
                          className: "h-2 rounded-full animate-pulse",
                          style: {
                            width: "56%",
                            backgroundColor: colorWithAlpha(theme.secondaryColor, 0.2)
                          }
                        }
                      )
                    ] })
                  ]
                }
              )
            }
          )
        ] }),
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          VisualizerSubtitleOverlay_default,
          {
            showText,
            activeLine: runtime.activeLine,
            recentCompletedLine: runtime.recentCompletedLine,
            nextLines: runtime.nextLines,
            theme,
            subtitleTheme,
            translationFontSize,
            upcomingFontSize,
            subtitleOverlayOpacity,
            subtitleOverlayBackground,
            subtitleFontScale,
            isPlayerChromeHidden,
            hideTranslationSubtitle,
            showSubtitleTranslation,
            subtitleContentMode
          }
        )
      ]
    }
  );
};
var VisualizerFume_default = VisualizerFume;
export {
  buildWordRangesFromWords,
  VisualizerFume_default as default,
  resolveLinePassCutoffTime,
  resolvePrintedGraphemeCount,
  resolvePrintedGraphemeProgress,
  resolveVisualProgressWithCutoff
};
