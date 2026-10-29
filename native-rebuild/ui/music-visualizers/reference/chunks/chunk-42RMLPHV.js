import {
  buildLineGraphemeTimeline,
  splitLyricGraphemes
} from "./chunk-LDALZVSS.js";
import {
  getLineRenderEndTime
} from "./chunk-LSTKNWUO.js";

// src/components/visualizer/sonnet/sonnetRandom.ts
var hashSonnetSeed = (value) => {
  let hash = 2166136261;
  for (let index = 0; index < value.length; index += 1) {
    hash ^= value.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  return hash >>> 0;
};

// src/components/visualizer/sonnet/types.ts
var SONNET_TRANSITION_KINDS = [
  "fast-blur",
  "mono-glitch",
  "camera-pull"
];

// src/components/visualizer/sonnet/sonnetSemantic.ts
var PUNCTUATION_ONLY = /^[\s\p{P}\p{S}]+$/u;
var getSegmenterParts = (text) => {
  const Segmenter = typeof Intl !== "undefined" ? Intl.Segmenter : void 0;
  if (Segmenter) {
    try {
      return Array.from(new Segmenter(void 0, { granularity: "word" }).segment(text), (part) => ({
        segment: part.segment,
        index: part.index,
        isWordLike: part.isWordLike
      }));
    } catch {
    }
  }
  let cursor = 0;
  return splitLyricGraphemes(text).map((segment) => {
    const part = { segment, index: cursor, isWordLike: !PUNCTUATION_ONLY.test(segment) };
    cursor += segment.length;
    return part;
  });
};
var getGraphemeRanges = (text) => {
  let cursor = 0;
  return splitLyricGraphemes(text).map((grapheme) => {
    const range = { start: cursor, end: cursor + grapheme.length };
    cursor = range.end;
    return range;
  });
};
var timingForRange = (line, startOffset, endOffset, timeline, ranges) => {
  const indices = ranges.flatMap((range, index) => range.end > startOffset && range.start < endOffset ? [index] : []);
  const graphemes = indices.map((index) => timeline[index]).filter(Boolean);
  const wordIndices = [...new Set(graphemes.flatMap((item) => typeof item.wordIndex === "number" ? [item.wordIndex] : []))];
  return {
    graphemes,
    wordIndices,
    startTime: graphemes[0]?.startTime ?? line.startTime,
    endTime: graphemes[graphemes.length - 1]?.endTime ?? line.endTime
  };
};
var buildSonnetSemanticSegments = (line) => {
  if (!line.fullText) return [];
  const timeline = buildLineGraphemeTimeline(line);
  const ranges = getGraphemeRanges(line.fullText);
  const parts = getSegmenterParts(line.fullText);
  const segments = parts.map((part, index) => {
    const startOffset = part.index;
    const endOffset = parts[index + 1]?.index ?? line.fullText.length;
    return {
      text: line.fullText.slice(startOffset, endOffset),
      startOffset,
      endOffset,
      ...timingForRange(line, startOffset, endOffset, timeline, ranges),
      isWordLike: part.isWordLike ?? !PUNCTUATION_ONLY.test(part.segment)
    };
  });
  const sticky = [];
  for (const segment of segments) {
    const previous = sticky[sticky.length - 1];
    if (previous && !segment.isWordLike && !/^\s+$/u.test(segment.text)) {
      previous.text += segment.text;
      previous.endOffset = segment.endOffset;
      previous.endTime = Math.max(previous.endTime, segment.endTime);
      previous.graphemes.push(...segment.graphemes);
      previous.wordIndices = [.../* @__PURE__ */ new Set([...previous.wordIndices, ...segment.wordIndices])];
    } else {
      sticky.push({ ...segment, graphemes: [...segment.graphemes], wordIndices: [...segment.wordIndices] });
    }
  }
  return sticky;
};

// src/components/visualizer/sonnet/sonnetProgram.ts
var SONNET_SHOT_KINDS = [
  "editorial-column",
  "type-impact",
  "fragment-collage",
  "tracking-ribbon",
  "mask-reveal",
  "poster-blocks",
  "quiet-tableau"
];
var SONNET_DEBUG_SHOT_KIND = null;
var resolveSonnetDebugShotKind = () => SONNET_DEBUG_SHOT_KIND;
var clamp = (value, min, max) => Math.min(max, Math.max(min, value));
var median = (values) => {
  if (values.length === 0) return 0.5;
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0 ? ((sorted[middle - 1] ?? sorted[middle]) + sorted[middle]) / 2 : sorted[middle];
};
var resolveSonnetParagraphGapThreshold = (lines) => {
  const gaps = lines.slice(1).map((line, index) => line.startTime - Math.min(getLineRenderEndTime(lines[index]), line.startTime)).filter((gap) => gap > 0);
  return clamp(median(gaps) * 2.5, 1.25, 3.5);
};
var metadataChanged = (previous, next) => previous.blockIndex !== void 0 && next.blockIndex !== void 0 && previous.blockIndex !== next.blockIndex || previous.songPart !== void 0 && next.songPart !== void 0 && previous.songPart !== next.songPart;
var splitOversizedDraft = (draft) => {
  const output = [];
  let remaining = draft.lines;
  let boundary = draft.boundary;
  let loopGuard = 0;
  while (remaining.length > 6 || remaining.length > 1 && remaining.at(-1).renderEndTime - remaining[0].line.startTime > 18) {
    if (loopGuard++ > 1e3) {
      console.error("splitOversizedDraft: Infinite loop detected, breaking");
      break;
    }
    const candidates = remaining.slice(2, -1).map((line, offset) => ({
      splitIndex: offset + 2,
      gap: line.line.startTime - remaining[offset + 1].renderEndTime
    }));
    const validCandidates = candidates.filter((c) => !Number.isNaN(c.gap));
    const rawSplitIndex = validCandidates.sort((a, b) => b.gap - a.gap)[0]?.splitIndex ?? Math.min(4, remaining.length - 1);
    const splitIndex = Math.max(1, rawSplitIndex);
    output.push({ lines: remaining.slice(0, splitIndex), boundary });
    remaining = remaining.slice(splitIndex);
    boundary = output.at(-1).lines.length >= 6 ? "line-cap" : "duration-cap";
  }
  output.push({ lines: remaining, boundary });
  return output;
};
var classifyParagraph = (lines, index, total) => {
  if (lines.some((item) => item.line.isChorus || /chorus|副歌/i.test(item.line.songPart ?? ""))) return "chorus";
  if (lines.some((item) => /bridge|break|間奏|ブリッジ/i.test(item.line.songPart ?? ""))) return "break";
  if (index === total - 1) return "outro";
  const duration = lines.at(-1).renderEndTime - lines[0].line.startTime;
  const segmentCount = lines.reduce((sum, line) => sum + line.segments.filter((segment) => segment.isWordLike).length, 0);
  const punctuationCount = lines.reduce((sum, line) => sum + (line.line.fullText.match(/[!?！？…]/g)?.length ?? 0), 0);
  if (duration <= 3.5 || segmentCount <= 3) return "breath";
  if (punctuationCount >= 2 || segmentCount / Math.max(duration, 1) > 2.5) return "lift";
  return "verse";
};
var chooseWithoutRepeat = (choices, seed, previous) => {
  const start = hashSonnetSeed(seed) % choices.length;
  for (let offset = 0; offset < choices.length; offset += 1) {
    const candidate = choices[(start + offset) % choices.length];
    if (candidate !== previous) return candidate;
  }
  return choices[start];
};
var buildCues = (lines) => {
  const segments = lines.flatMap((line) => line.segments).filter((segment) => segment.text.length > 0);
  return segments.map((segment, index) => ({
    at: segment.startTime,
    duration: Math.max(0.08, segment.endTime - segment.startTime),
    kind: index === segments.length - 1 ? "accent" : "enter",
    segmentStart: index,
    segmentEnd: index + 1
  }));
};
var groupShotLines = (lines) => {
  const groups = [];
  let currentGroup = [];
  let groupStartTime = 0;
  for (let index = 0; index < lines.length; index += 1) {
    const line = lines[index];
    if (currentGroup.length === 0) {
      currentGroup.push(line);
      groupStartTime = line.line.startTime;
    } else {
      const durationSoFar = line.renderEndTime - groupStartTime;
      if (currentGroup.length < 4 && durationSoFar <= 6) {
        currentGroup.push(line);
      } else {
        groups.push(currentGroup);
        currentGroup = [line];
        groupStartTime = line.line.startTime;
      }
    }
  }
  if (currentGroup.length > 0) groups.push(currentGroup);
  return groups;
};
var buildShots = (lines, kind, paragraphIndex, seed, previousKind) => {
  let lastKind = previousKind;
  return groupShotLines(lines).map((group, shotIndex) => {
    const signature = group.map((item) => item.line.fullText).join("|");
    const debugShotKind = resolveSonnetDebugShotKind();
    let shotKind = debugShotKind ?? chooseWithoutRepeat(SONNET_SHOT_KINDS, `${seed}:${paragraphIndex}:${shotIndex}:${signature}`, lastKind);
    const wordCount = group.reduce((sum, item) => sum + item.segments.filter((s) => s.isWordLike).length, 0);
    if (debugShotKind === null) {
      if (kind === "breath" && shotIndex === 0 && wordCount <= 2) shotKind = "quiet-tableau";
      if (kind === "chorus" && shotKind === "quiet-tableau") shotKind = "type-impact";
    }
    lastKind = shotKind;
    const random = hashSonnetSeed(`${seed}:${paragraphIndex}:${shotIndex}:camera`);
    return {
      id: `p${paragraphIndex}-s${shotIndex}`,
      kind: shotKind,
      startTime: group[0].line.startTime,
      endTime: group.at(-1).renderEndTime,
      lineIndices: group.map((item) => item.sourceIndex),
      cues: buildCues(group),
      camera: {
        x: ((random & 255) / 255 - 0.5) * 0.18,
        y: ((random >>> 8 & 255) / 255 - 0.5) * 0.14,
        zoom: 0.92 + (random >>> 16 & 255) / 255 * 0.2,
        rotation: ((random >>> 24 & 255) / 255 - 0.5) * 0.08
      }
    };
  });
};
var compileSonnetProgram = (lines, seed = "sonnet") => {
  const compiled = lines.map((line, sourceIndex) => ({
    sourceIndex,
    line,
    // The visual tail may extend beyond authored timing, but never into the next line.
    renderEndTime: Math.max(
      line.startTime,
      Math.min(getLineRenderEndTime(line), lines[sourceIndex + 1]?.startTime ?? Number.POSITIVE_INFINITY)
    ),
    segments: buildSonnetSemanticSegments(line)
  }));
  const paragraphGapThreshold = resolveSonnetParagraphGapThreshold(lines);
  const drafts = [];
  let current = { lines: [], boundary: "song-start" };
  compiled.forEach((line, index) => {
    const previous = compiled[index - 1];
    const gap = previous ? line.line.startTime - previous.renderEndTime : 0;
    const boundary = previous && metadataChanged(previous.line, line.line) ? "metadata" : previous && gap >= paragraphGapThreshold ? "time-gap" : null;
    if (boundary && current.lines.length > 0) {
      drafts.push(...splitOversizedDraft(current));
      current = { lines: [], boundary };
    }
    current.lines.push(line);
  });
  if (current.lines.length > 0) drafts.push(...splitOversizedDraft(current));
  const resolvedSeed = String(seed);
  let previousShot = null;
  let previousTransition = null;
  const paragraphs = drafts.map((draft, index) => {
    const kind = classifyParagraph(draft.lines, index, drafts.length);
    const shots = buildShots(draft.lines, kind, index, resolvedSeed, previousShot);
    previousShot = shots.at(-1)?.kind ?? previousShot;
    const next = drafts[index + 1];
    const endTime = draft.lines.at(-1).renderEndTime;
    const gap = next ? next.lines[0].line.startTime - endTime : 0;
    const availableTransitions = [...SONNET_TRANSITION_KINDS];
    const transitionKind = next ? chooseWithoutRepeat(availableTransitions, `${resolvedSeed}:${index}:transition`, previousTransition) : null;
    if (transitionKind) previousTransition = transitionKind;
    const transitionDuration = next ? Math.min(0.3, Math.max(0.16, gap > 0 ? gap * 0.5 : 0.2)) : 0;
    const transitionEndTime = next?.lines[0].line.startTime ?? endTime;
    return {
      id: `sonnet-p${index}`,
      kind,
      boundary: draft.boundary,
      startTime: draft.lines[0].line.startTime,
      endTime,
      lines: draft.lines,
      shots,
      transitionOut: transitionKind ? {
        kind: transitionKind,
        startTime: Math.max(draft.lines[0].line.startTime, transitionEndTime - transitionDuration),
        endTime: transitionEndTime
      } : null
    };
  });
  return { version: 1, seed: resolvedSeed, paragraphGapThreshold, paragraphs };
};
var findSonnetParagraphIndexAtTime = (program, time) => {
  for (let index = program.paragraphs.length - 1; index >= 0; index -= 1) {
    if (time >= program.paragraphs[index].startTime) return index;
  }
  return 0;
};

export {
  SONNET_TRANSITION_KINDS,
  hashSonnetSeed,
  compileSonnetProgram,
  findSonnetParagraphIndexAtTime
};
