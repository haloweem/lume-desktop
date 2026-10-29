import {
  layoutWithLines,
  prepareWithSegments
} from "./chunks/chunk-KECZQD7W.js";
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
  motionValue,
  require_jsx_runtime
} from "./chunks/chunk-AS2U3CHD.js";
import {
  require_react
} from "./chunks/chunk-RG3JEOUL.js";
import {
  getLineRenderEndTime
} from "./chunks/chunk-LSTKNWUO.js";
import {
  DEFAULT_TILT_TUNING
} from "./chunks/chunk-LH7Y2YK3.js";
import {
  resolveThemeFontStack,
  resolveThemeFontWeight
} from "./chunks/chunk-2CN2J6VF.js";
import {
  __toESM
} from "./chunks/chunk-XCBH6NLF.js";

// src/components/visualizer/tilt/VisualizerTilt.tsx
var import_react = __toESM(require_react(), 1);

// src/utils/lyrics/sentenceLayout.ts
var CJK_REGEX = /[\u4e00-\u9fa5\u3040-\u30ff\uac00-\ud7af]/;
var hasCjkText = (text) => CJK_REGEX.test(text);
function findRegexMatch(regex, text, fromIndex = 0) {
  regex.lastIndex = fromIndex;
  const match = regex.exec(text);
  return match ? { start: match.index, end: match.index + match[0].length } : null;
}
function findStringMatch(str, text, fromIndex = 0) {
  const idx = text.indexOf(str, fromIndex);
  return idx === -1 ? null : { start: idx, end: idx + str.length };
}
var SentenceLayout = class _SentenceLayout {
  constructor(text) {
    this.words = [];
    this.startTime = 0;
    this.endTime = 0;
    this.isSemantic = true;
    this.text = text;
  }
  static splitIntoSentences(text, targetCount, timeSeed) {
    if (targetCount <= 1 && targetCount >= 0) {
      return [new _SentenceLayout(text)];
    }
    const maxLevel = targetCount === -1 ? 1 : targetCount === -2 ? 2 : targetCount === -3 ? 3 : targetCount === -4 ? 4 : targetCount === -5 ? 5 : 5;
    let sentences = [text];
    for (let level = 1; level <= maxLevel; level++) {
      if (sentences.length >= Math.abs(targetCount) && targetCount > 0) {
        break;
      }
      const newSentences = [];
      for (const sentence of sentences) {
        newSentences.push(..._SentenceLayout.splitByLevel(sentence, level));
      }
      sentences = newSentences;
    }
    let result = sentences.map((s) => new _SentenceLayout(s));
    if (targetCount > 0 && result.length < targetCount) {
      result = _SentenceLayout.secondarySplit(result, targetCount, timeSeed);
    }
    if (result.length > 1 && result.length > targetCount && targetCount > 0) {
      result = _SentenceLayout.mergeSentences(result, targetCount);
    }
    return result;
  }
  static splitByLevel(text, level) {
    switch (level) {
      case 1:
        return _SentenceLayout.splitByPunctuation(text);
      case 2:
        return _SentenceLayout.splitByBracketsQuotes(text);
      case 3:
        return _SentenceLayout.splitByWesternWords(text);
      case 4:
        return _SentenceLayout.splitCJKBySpace(text);
      case 5:
        return _SentenceLayout.splitBySpecialChars(text);
      default:
        return [text];
    }
  }
  static splitByPunctuation(text) {
    const allPunctRegex = /[，。；！？、…·\.\,\;\!\?]+/g;
    const matches = [...text.matchAll(allPunctRegex)];
    if (matches.length === 0) {
      return [text];
    }
    const result = [];
    let lastIndex = 0;
    for (const match of matches) {
      const punctStart = match.index;
      const punctEnd = punctStart + match[0].length;
      let trailingSpaces = "";
      const afterPunct = text.slice(punctEnd);
      const spaceMatch = afterPunct.match(/^\s+/);
      if (spaceMatch) {
        trailingSpaces = spaceMatch[0];
      }
      const beforePunct = text.slice(lastIndex, punctStart);
      const punctWithSpaces = match[0] + trailingSpaces;
      if (beforePunct.length > 0) {
        result.push(beforePunct + punctWithSpaces);
      } else if (result.length > 0) {
        result[result.length - 1] += punctWithSpaces;
      } else {
        result.push(punctWithSpaces);
      }
      lastIndex = punctEnd + trailingSpaces.length;
    }
    if (lastIndex < text.length) {
      result.push(text.slice(lastIndex));
    }
    return result.filter((r) => r.length > 0);
  }
  static splitByWesternWords(text) {
    if (!hasCjkText(text)) {
      return [text];
    }
    const westernBlockRegex = /[a-zA-Z0-9]+(?:[a-zA-Z0-9'\-]*[a-zA-Z0-9]+)?(?:\s+[a-zA-Z0-9]+(?:[a-zA-Z0-9'\-]*[a-zA-Z0-9]+)?)+[.,;:!?。，；：！？]?\s*/g;
    _SentenceLayout.charCountCache.clear();
    let hasMultipleWordBlock = false;
    let match;
    while ((match = westernBlockRegex.exec(text)) !== null) {
      const wordCount = match[0].match(/[a-zA-Z0-9]+/g)?.length || 0;
      if (wordCount > 1) {
        hasMultipleWordBlock = true;
        break;
      }
    }
    if (!hasMultipleWordBlock) {
      return [text];
    }
    westernBlockRegex.lastIndex = 0;
    const parts = [];
    let lastIndex = 0;
    while ((match = westernBlockRegex.exec(text)) !== null) {
      const blockStart = match.index;
      const blockEnd = blockStart + match[0].length;
      const beforeBlock = text.slice(lastIndex, blockStart);
      if (_SentenceLayout.shouldSkipBoundary(text, beforeBlock, blockStart)) {
        parts.push(beforeBlock + match[0]);
      } else if (blockStart > lastIndex) {
        parts.push(beforeBlock);
        parts.push(match[0]);
      } else {
        parts.push(match[0]);
      }
      lastIndex = blockEnd;
    }
    if (lastIndex < text.length) {
      parts.push(text.slice(lastIndex));
    }
    return parts.length > 1 ? parts : [text];
  }
  static {
    this.charCountCache = /* @__PURE__ */ new Map();
  }
  static getGlobalCount(text, char) {
    const key = `${text.length}:${char}`;
    if (_SentenceLayout.charCountCache.has(key)) {
      return _SentenceLayout.charCountCache.get(key);
    }
    const escaped = char.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    const count = (text.match(new RegExp(escaped, "g")) || []).length;
    _SentenceLayout.charCountCache.set(key, count);
    return count;
  }
  static shouldSkipBoundary(fullText, beforeBlock, _boundaryPos) {
    if (beforeBlock.length === 0) return false;
    const sepChar = beforeBlock[beforeBlock.length - 1];
    if (sepChar === "-") return true;
    if (sepChar === ":" || sepChar === "\uFF1A" || sepChar === "/" || sepChar === "\uFF0F" || sepChar === "|" || sepChar === "\uFF5C") {
      return _SentenceLayout.getGlobalCount(fullText, sepChar) >= 2;
    }
    return false;
  }
  static splitWesternPhrase(text) {
    if (!hasCjkText(text) && text.length > 0) {
      const splitRegex = /(?<=\w[.,;:!?。，；：！？])(?=\s+\w)/;
      const parts = text.split(splitRegex);
      if (parts.length > 1) {
        return parts.filter((p) => p.length > 0);
      }
    }
    return [text];
  }
  static splitByBracketsQuotes(text) {
    const allPairedSymbols = [
      { open: /「/, close: /」/ },
      { open: /『/, close: /』/ },
      { open: /《/, close: /》/ },
      { open: /【/, close: /】/ },
      { open: /｛/, close: /｝/ },
      { open: /［/, close: /］/ },
      { open: /\[/, close: /\]/ },
      { open: /（/, close: /）/ },
      { open: /\(/, close: /\)/ },
      { openStr: '"', closeStr: '"' },
      { openStr: "'", closeStr: "'" }
    ];
    const result = _SentenceLayout.extractOutermostPairs(text, allPairedSymbols);
    return result.length > 1 ? result : [text];
  }
  static extractOutermostPairs(text, defs) {
    const best = _SentenceLayout.findOutermostPair(text, defs);
    if (!best) {
      return [text];
    }
    const before = text.slice(0, best.openStart);
    const pairedContent = text.slice(best.openStart, best.closeEnd);
    const remainder = text.slice(best.closeEnd);
    const result = [];
    if (before.length > 0) {
      result.push(..._SentenceLayout.extractOutermostPairs(before, defs));
    }
    result.push(pairedContent);
    if (remainder.length > 0) {
      result.push(..._SentenceLayout.extractOutermostPairs(remainder, defs));
    }
    return result.filter((p) => p.length > 0);
  }
  static findOutermostPair(text, defs) {
    let best = null;
    for (const def of defs) {
      const openMatch = def.openStr !== void 0 ? findStringMatch(def.openStr, text) : findRegexMatch(def.open, text);
      if (!openMatch) continue;
      const searchFrom = openMatch.end;
      const closeMatch = def.closeStr !== void 0 ? findStringMatch(def.closeStr, text, searchFrom) : findRegexMatch(def.close, text, searchFrom);
      if (!closeMatch) continue;
      const candidate = {
        openStart: openMatch.start,
        openEnd: openMatch.end,
        closeStart: closeMatch.start,
        closeEnd: closeMatch.end
      };
      if (!best || candidate.openStart < best.openStart) {
        best = candidate;
      } else if (candidate.openStart === best.openStart && candidate.closeEnd > best.closeEnd) {
        best = candidate;
      }
    }
    return best;
  }
  static splitCJKBySpace(text) {
    if (!hasCjkText(text)) {
      return [text];
    }
    const segments = [];
    let currentSegment = "";
    let inCjkBlock = false;
    for (let i = 0; i < text.length; i++) {
      const char = text[i];
      const isCJK = CJK_REGEX.test(char);
      const isWestern = /[a-zA-Z0-9]/.test(char);
      const isSpace = /\s/.test(char);
      if (isCJK) {
        currentSegment += char;
        inCjkBlock = true;
      } else if (isWestern) {
        currentSegment += char;
      } else if (isSpace) {
        const isFullWidthSpace = char === "\u3000";
        if (isFullWidthSpace && currentSegment.length > 0) {
          currentSegment += char;
          segments.push(currentSegment);
          currentSegment = "";
          inCjkBlock = false;
        } else if (inCjkBlock && currentSegment.length > 0) {
          const lastChar = currentSegment[currentSegment.length - 1];
          if (CJK_REGEX.test(lastChar)) {
            currentSegment += char;
            segments.push(currentSegment);
            currentSegment = "";
            inCjkBlock = false;
          } else {
            currentSegment += char;
          }
        } else if (currentSegment.length === 0 && segments.length > 0) {
          segments[segments.length - 1] += char;
        } else {
          currentSegment += char;
        }
      } else {
        if (currentSegment.length === 0 && segments.length > 0) {
          segments[segments.length - 1] += char;
        } else {
          currentSegment += char;
        }
      }
    }
    if (currentSegment) {
      if (segments.length > 0 && /^\s+$/.test(currentSegment)) {
        segments[segments.length - 1] += currentSegment;
      } else {
        segments.push(currentSegment);
      }
    }
    return segments.length > 1 ? segments : [text];
  }
  static splitBySpecialChars(text) {
    const specialCharRegex = /[：:\/／\\|｜~～]+/;
    const parts = text.split(specialCharRegex);
    if (parts.filter((part) => part.length > 0).length <= 1) {
      return [text];
    }
    const result = [];
    let lastIndex = 0;
    for (const part of parts) {
      const index = text.indexOf(part, lastIndex);
      if (index > lastIndex) {
        const specialChar = text.slice(lastIndex, index);
        if (result.length > 0) {
          result[result.length - 1] += specialChar;
        } else {
          result.push(specialChar);
        }
      }
      if (part) {
        result.push(part);
        lastIndex = index + part.length;
      } else if (index !== -1) {
        lastIndex = index;
      }
    }
    if (lastIndex < text.length) {
      const tail = text.slice(lastIndex);
      if (tail && result.length > 0) {
        result[result.length - 1] += tail;
      } else if (tail) {
        result.push(tail);
      }
    }
    return result.filter((r) => r.length > 0);
  }
  static secondarySplit(sentences, targetCount, timeSeed) {
    const Segmenter = Intl?.Segmenter;
    const segmenter = Segmenter ? new Segmenter(void 0, { granularity: "word" }) : null;
    while (sentences.length < targetCount) {
      const candidates = sentences.filter((s) => s.text.length > 2);
      if (candidates.length === 0) break;
      if (segmenter) {
        const semanticCandidates = [];
        for (const s of candidates) {
          try {
            const segments = Array.from(
              segmenter.segment(s.text)
            );
            const wordPositions = [];
            let offset = 0;
            for (const seg of segments) {
              if (seg.isWordLike) {
                wordPositions.push({ start: offset, end: offset + seg.segment.length });
              }
              offset += seg.segment.length;
            }
            if (wordPositions.length >= 2) {
              const midChar = s.text.length / 2;
              let bestGapIdx = 1;
              let bestDist = Infinity;
              for (let g = 1; g < wordPositions.length; g++) {
                const dist = Math.abs(wordPositions[g].start - midChar);
                if (dist < bestDist) {
                  bestDist = dist;
                  bestGapIdx = g;
                }
              }
              const splitPos = wordPositions[bestGapIdx].start;
              const balanceScore = 1 - Math.abs(splitPos - midChar) / midChar;
              semanticCandidates.push({
                sentence: s,
                index: sentences.indexOf(s),
                splitPos,
                score: balanceScore * 10 + wordPositions.length
              });
            }
          } catch {
          }
        }
        if (semanticCandidates.length > 0) {
          semanticCandidates.sort((a, b) => b.score - a.score);
          const best = semanticCandidates[0];
          const firstHalf2 = best.sentence.text.slice(0, best.splitPos);
          const secondHalf2 = best.sentence.text.slice(best.splitPos);
          sentences.splice(
            best.index,
            1,
            new _SentenceLayout(firstHalf2),
            new _SentenceLayout(secondHalf2)
          );
          continue;
        }
      }
      const pseudoRandom = (seed2) => {
        const x = Math.sin(seed2) * 1e4;
        return x - Math.floor(x);
      };
      const textHash = sentences.reduce((acc, s) => acc + s.text.split("").reduce((sum, c) => sum + c.charCodeAt(0), 0), 0);
      const seed = textHash + sentences.length + (timeSeed ?? 0);
      const randomIndex = Math.floor(pseudoRandom(seed) * candidates.length);
      const selectedCandidate = candidates[randomIndex];
      const candidateIndex = sentences.indexOf(selectedCandidate);
      const midPoint = Math.floor(selectedCandidate.text.length / 2);
      const firstHalf = selectedCandidate.text.slice(0, midPoint);
      const secondHalf = selectedCandidate.text.slice(midPoint);
      sentences.splice(
        candidateIndex,
        1,
        new _SentenceLayout(firstHalf),
        new _SentenceLayout(secondHalf)
      );
    }
    return sentences;
  }
  static mergeSentences(sentences, targetCount) {
    while (sentences.length > targetCount) {
      let bestMergeIndex = 0;
      let shortestCombinedLength = Infinity;
      for (let i = 0; i < sentences.length - 1; i++) {
        const combinedLength = sentences[i].text.length + sentences[i + 1].text.length;
        if (combinedLength < shortestCombinedLength) {
          shortestCombinedLength = combinedLength;
          bestMergeIndex = i;
        }
      }
      const merged = new _SentenceLayout(
        sentences[bestMergeIndex].text + sentences[bestMergeIndex + 1].text
      );
      sentences.splice(bestMergeIndex, 2, merged);
    }
    return sentences;
  }
};

// src/components/visualizer/tilt/VisualizerTilt.tsx
var import_jsx_runtime = __toESM(require_jsx_runtime(), 1);
var CHAR_REF_LENGTH = 20;
var LOG_OFFSET = 4;
var LINE_THRESHOLDS = [0.45, 1.05, 1.7];
var determineLineCount = (charCount, seed, splitProbability) => {
  const normalized = Math.log(charCount + LOG_OFFSET) / Math.log(CHAR_REF_LENGTH + LOG_OFFSET);
  const jitter = seededRandom(seed, 1) * 0.6 + 0.7;
  const score = normalized * jitter * splitProbability;
  if (score < LINE_THRESHOLDS[0]) return 1;
  if (score < LINE_THRESHOLDS[1]) return 2;
  if (score < LINE_THRESHOLDS[2]) return 3;
  return 4;
};
var REM_PX = 16;
var RESPLIT_THRESHOLD = 1.6;
var SCALE_FLOOR_NORMAL = 0.55;
var SCALE_FLOOR_TILT = 0.5;
var getAvailableWidth = () => {
  if (typeof window === "undefined") return 1200;
  return Math.max(320, window.innerWidth) * 0.85;
};
var GRAPHEME_SEGMENTER = new Intl.Segmenter(void 0, { granularity: "grapheme" });
var findSegmentWordRange = (charOffset, segmentText, fullText, words) => {
  const segmentStart = charOffset;
  if (segmentStart < 0 || segmentStart >= fullText.length) return { startWordIndex: 0, endWordIndex: words.length };
  const segmentEnd = segmentStart + segmentText.length;
  let fullTextPos = 0;
  let startWordIndex = 0;
  let endWordIndex = words.length;
  for (let wi = 0; wi < words.length; wi++) {
    const wordText = words[wi].text;
    const wordFullStart = fullText.indexOf(wordText, fullTextPos);
    if (wordFullStart === -1) {
      fullTextPos += wordText.length;
      continue;
    }
    const wordFullEnd = wordFullStart + wordText.length;
    if (wordFullStart <= segmentStart && wordFullEnd > segmentStart) {
      startWordIndex = wi;
    }
    if (wordFullStart < segmentEnd && wordFullEnd >= segmentEnd) {
      endWordIndex = wi + 1;
      break;
    }
    fullTextPos = wordFullEnd;
  }
  return { startWordIndex, endWordIndex };
};
var buildCharTimings = (charOffset, segmentText, segmentStartTime, segmentEndTime, activeLine) => {
  const graphemes = [...GRAPHEME_SEGMENTER.segment(segmentText)];
  if (!activeLine || graphemes.length === 0) return [];
  const nonSpaceGraphemes = graphemes.filter((g) => !/^\s+$/.test(g.segment));
  if (nonSpaceGraphemes.length === 0) return [];
  const totalDuration = Math.max(segmentEndTime - segmentStartTime, 0.3);
  const { startWordIndex, endWordIndex } = findSegmentWordRange(charOffset, segmentText, activeLine.fullText, activeLine.words);
  const segmentWords = activeLine.words.slice(startWordIndex, endWordIndex);
  let currentCharIndex = 0;
  const timings = [];
  if (segmentWords.length > 0) {
    for (const word of segmentWords) {
      if (word.syllables?.length) {
        const wordTimings = buildWordGraphemeTimings(word).filter((timing) => !/^\s+$/.test(timing.char));
        for (const timing of wordTimings) {
          if (currentCharIndex >= nonSpaceGraphemes.length) break;
          timings.push({
            charIndex: currentCharIndex,
            startTime: timing.startTime,
            endTime: timing.endTime
          });
          currentCharIndex++;
        }
        continue;
      }
      const wordGraphemes = [...GRAPHEME_SEGMENTER.segment(word.text)];
      const wordNonSpaceCount = wordGraphemes.filter((g) => !/^\s+$/.test(g.segment)).length;
      if (wordNonSpaceCount === 0) continue;
      const wordDuration = Math.max(word.endTime - word.startTime, 0.05);
      const charDuration = wordDuration / wordNonSpaceCount;
      let nonSpaceCi = 0;
      for (const grapheme of wordGraphemes) {
        if (currentCharIndex >= nonSpaceGraphemes.length) break;
        if (/^\s+$/.test(grapheme.segment)) continue;
        timings.push({
          charIndex: currentCharIndex,
          startTime: word.startTime + nonSpaceCi * charDuration,
          endTime: word.startTime + (nonSpaceCi + 1) * charDuration
        });
        currentCharIndex++;
        nonSpaceCi++;
      }
    }
  }
  if (timings.length === 0 || timings.length !== nonSpaceGraphemes.length) {
    const avgCharDuration = totalDuration / nonSpaceGraphemes.length;
    timings.length = 0;
    currentCharIndex = 0;
    for (let i = 0; i < graphemes.length; i++) {
      const isSpace = /^\s+$/.test(graphemes[i].segment);
      if (isSpace) continue;
      timings.push({
        charIndex: currentCharIndex,
        startTime: segmentStartTime + currentCharIndex * avgCharDuration,
        endTime: segmentStartTime + (currentCharIndex + 1) * avgCharDuration
      });
      currentCharIndex++;
    }
  }
  return timings;
};
var getCharPulseIntensity = (currentTime, charTiming) => {
  const { startTime, endTime } = charTiming;
  const rawDuration = Math.max(endTime - startTime, 0.05);
  const duration = Math.min(Math.max(rawDuration, 0.2), 0.9);
  const elapsed = currentTime - startTime;
  if (elapsed < 0) return 0;
  if (elapsed <= duration) {
    const progress = elapsed / duration;
    return Math.sin(progress * Math.PI);
  }
  const afterElapsed = elapsed - duration;
  const afterglowRamp = duration * 1.2;
  if (afterElapsed >= afterglowRamp) return 0.25;
  return 0.25 * (afterElapsed / afterglowRamp);
};
var measureAtSize = (text, pxSize, fontSpec) => {
  const prepared = prepareWithSegments(text, fontSpec);
  const layout = layoutWithLines(prepared, 99999, pxSize * 1.4);
  return layout.lines[0]?.width ?? text.length * pxSize * 0.6;
};
var seededRandom = (seed, offset) => {
  const x = Math.sin(seed * 1e3 + offset) * 1e4;
  return x - Math.floor(x);
};
var buildTiltLayout = (fullText, lineSeed, tuning, theme, fontScale) => {
  const fontStack = resolveThemeFontStack(theme);
  const viewportWidth = typeof window !== "undefined" ? window.innerWidth : 1200;
  const measureMaxPx = Math.max(viewportWidth * 0.06875, 5.625 * REM_PX * fontScale);
  const normalBasePx = measureMaxPx;
  const tiltBasePx = measureMaxPx;
  const normalFontSpec = `${resolveThemeFontWeight(theme, 400)} ${normalBasePx}px ${fontStack}`;
  const tiltFontSpec = `${resolveThemeFontWeight(theme, 300)} ${tiltBasePx}px ${fontStack} italic`;
  const availableWidth = getAvailableWidth();
  const charCount = fullText.trim().length;
  const isEllipsisOnly = /^[\s.…·。]+$/.test(fullText.trim());
  let mergedSegments;
  if (isEllipsisOnly) {
    mergedSegments = [fullText];
  } else {
    const numLines = determineLineCount(charCount, lineSeed, tuning.splitProbability);
    const layoutUnits = SentenceLayout.splitIntoSentences(fullText, numLines, lineSeed);
    mergedSegments = layoutUnits.map((u) => u.text);
  }
  const candidates = [];
  mergedSegments.forEach((_, i) => {
    const lineRoll = seededRandom(lineSeed, 100 + i);
    if (lineRoll < tuning.tiltStyleProbability) candidates.push(i);
  });
  let finalTiltIndex = -1;
  if (candidates.length > 0) {
    finalTiltIndex = candidates[Math.floor(seededRandom(lineSeed, 200) * candidates.length)];
  }
  let offsetAccum = 0;
  const segments = mergedSegments.map((text, i) => {
    const trimmed = text.trimStart().trimEnd();
    const leadingSpaces = text.length - text.trimStart().length;
    const seg = {
      text: trimmed,
      isTilt: i === finalTiltIndex,
      isShortLastLine: false,
      charOffset: offsetAccum + leadingSpaces
    };
    offsetAccum += text.length;
    return seg;
  });
  let scaleMultiplier = 1;
  const tiltWithWidth = segments.filter((s) => s.isTilt).map((s) => ({ text: s.text, width: measureAtSize(s.text, tiltBasePx, tiltFontSpec) }));
  const normalWithWidth = segments.filter((s) => !s.isTilt).map((s) => ({ text: s.text, width: measureAtSize(s.text, normalBasePx, normalFontSpec) }));
  const widestTiltEntry = tiltWithWidth.sort((a, b) => b.width - a.width)[0];
  const widestNormalEntry = normalWithWidth.sort((a, b) => b.width - a.width)[0];
  const tiltWidth = widestTiltEntry?.width ?? 0;
  const normalWidth = widestNormalEntry?.width ?? 0;
  const tiltOverflow = tiltWidth > 0 ? tiltWidth / availableWidth : 0;
  const normalOverflow = normalWidth > 0 ? normalWidth / availableWidth : 0;
  const maxOverflow = Math.max(tiltOverflow, normalOverflow);
  const markShortLastLine = (segs) => {
    if (segs.length < 2) return segs;
    const last = segs[segs.length - 1];
    if (last.isTilt || last.text.trim().length > 2) return segs;
    const prev = segs[segs.length - 2];
    if (last.text.trim().length * 2 <= prev.text.length) {
      segs[segs.length - 1] = { ...last, isShortLastLine: true };
    }
    return segs;
  };
  if (maxOverflow > 1) {
    if (maxOverflow >= RESPLIT_THRESHOLD) {
      const targetWidth = availableWidth * (RESPLIT_THRESHOLD - 0.15);
      const totalEstWidth = measureAtSize(fullText, normalBasePx, normalFontSpec);
      const extraSplitsNeeded = Math.min(4, Math.max(segments.length + 1, Math.ceil(totalEstWidth / targetWidth)));
      if (extraSplitsNeeded > segments.length) {
        const reSplitUnits = SentenceLayout.splitIntoSentences(fullText, extraSplitsNeeded, lineSeed);
        let reSplitOffset = 0;
        const newSegments = reSplitUnits.map((u) => {
          const trimmed = u.text.trimStart().trimEnd();
          const leadingSpaces = u.text.length - u.text.trimStart().length;
          const seg = { text: trimmed, isTilt: false, isShortLastLine: false, charOffset: reSplitOffset + leadingSpaces };
          reSplitOffset += u.text.length;
          return seg;
        });
        if (newSegments.length > 0) {
          const resplitTiltRoll = seededRandom(lineSeed, 300);
          if (resplitTiltRoll < tuning.tiltStyleProbability) {
            const picked = Math.floor(seededRandom(lineSeed, 301) * newSegments.length);
            newSegments[picked].isTilt = true;
          }
        }
        const postWidestTilt = newSegments.filter((s) => s.isTilt)[0];
        const postNormalWithWidth = newSegments.filter((s) => !s.isTilt).map((s) => ({ text: s.text, width: measureAtSize(s.text, normalBasePx, normalFontSpec) }));
        const postWidestNormal = postNormalWithWidth.sort((a, b) => b.width - a.width)[0];
        let postScale = 1;
        if (postWidestTilt) {
          const w = measureAtSize(postWidestTilt.text, tiltBasePx, tiltFontSpec);
          if (w > availableWidth) postScale = Math.max(SCALE_FLOOR_TILT, availableWidth / w);
        } else if (postWidestNormal) {
          const w = measureAtSize(postWidestNormal.text, normalBasePx, normalFontSpec);
          if (w > availableWidth) postScale = Math.max(SCALE_FLOOR_NORMAL, availableWidth / w);
        }
        return { segments: markShortLastLine(newSegments), scaleMultiplier: postScale };
      }
    }
    if (tiltOverflow >= normalOverflow && widestTiltEntry) {
      scaleMultiplier = Math.max(SCALE_FLOOR_TILT, availableWidth / tiltWidth);
    } else if (widestNormalEntry) {
      scaleMultiplier = Math.max(SCALE_FLOOR_NORMAL, availableWidth / normalWidth);
    }
  }
  return { segments: markShortLastLine(segments), scaleMultiplier };
};
var TiltLine = ({ segment, theme, fontScale, scaleMultiplier, visible, colorScheme = "default", currentTime, segmentStartTime = 0, segmentEndTime = 0, activeLine = null }) => {
  const baseFontScale = fontScale * scaleMultiplier;
  const shortLastBoost = segment.isShortLastLine ? 1.18 : 1;
  const viewportWidth = typeof window !== "undefined" ? window.innerWidth : 1200;
  const tiltFontPx = Math.min(viewportWidth * 0.06875 * baseFontScale, 5.625 * REM_PX * baseFontScale);
  const yOffset = tiltFontPx / 6;
  const normalFontSize = `clamp(${(3.125 * baseFontScale * shortLastBoost).toFixed(3)}rem, ${(6.875 * baseFontScale * shortLastBoost).toFixed(3)}vw, ${(5.625 * baseFontScale * shortLastBoost).toFixed(3)}rem)`;
  const tiltFontSize = `clamp(${(3.125 * baseFontScale).toFixed(3)}rem, ${(6.875 * baseFontScale).toFixed(3)}vw, ${(5.625 * baseFontScale).toFixed(3)}rem)`;
  const getColors = () => {
    switch (colorScheme) {
      case "swap":
        return { normal: theme.accentColor || theme.primaryColor, tilt: theme.primaryColor };
      case "accentAll":
        return { normal: theme.accentColor || theme.primaryColor, tilt: theme.accentColor || theme.primaryColor };
      case "primaryAll":
        return { normal: theme.primaryColor, tilt: theme.primaryColor };
      default:
        return { normal: theme.primaryColor, tilt: theme.accentColor || theme.primaryColor };
    }
  };
  const colors = getColors();
  const graphemes = (0, import_react.useMemo)(
    () => [...GRAPHEME_SEGMENTER.segment(segment.text)],
    [segment.text]
  );
  let visualIndex = 0;
  const charTimings = (0, import_react.useMemo)(() => {
    if (!activeLine) return [];
    return buildCharTimings(segment.charOffset, segment.text, segmentStartTime, segmentEndTime, activeLine);
  }, [activeLine, segment.charOffset, segment.text, segmentStartTime, segmentEndTime]);
  const charScaleMvs = (0, import_react.useRef)([]);
  if (charScaleMvs.current.length !== graphemes.length) {
    charScaleMvs.current = graphemes.map(() => motionValue(1));
  }
  const charIndexMap = (0, import_react.useMemo)(() => {
    let idx = 0;
    return graphemes.map((seg) => {
      const isSpace = /^\s+$/.test(seg.segment);
      if (!isSpace) idx++;
      return idx - 1;
    });
  }, [graphemes]);
  (0, import_react.useInsertionEffect)(() => {
    if (!currentTime) return;
    const handler = (latest) => {
      if (!visible) return;
      const mvs = charScaleMvs.current;
      if (mvs.length !== graphemes.length) return;
      if (!charTimings || charTimings.length === 0) {
        for (let i = 0; i < mvs.length; i++) mvs[i].set(1);
        return;
      }
      for (let ti = 0; ti < graphemes.length; ti++) {
        const seg = graphemes[ti];
        if (/^\s+$/.test(seg.segment)) {
          mvs[ti].set(1);
          continue;
        }
        const ci = charIndexMap[ti];
        const charTiming = charTimings[ci];
        if (!charTiming) {
          mvs[ti].set(1);
          continue;
        }
        const intensity = getCharPulseIntensity(latest, charTiming);
        mvs[ti].set(1 + intensity * (segment.isTilt ? 0.18 : 0.15));
      }
    };
    const unsubscribe = currentTime.on("change", handler);
    handler(currentTime.get());
    return unsubscribe;
  }, [currentTime, graphemes, charTimings, segment, charIndexMap, visible]);
  if (!segment.isTilt) {
    return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
      motion.div,
      {
        initial: { opacity: 0, y: 20 },
        animate: visible ? { opacity: 1, y: 0 } : { opacity: 0, y: 20 },
        exit: { opacity: 0, y: -12 },
        transition: { duration: 0.55, ease: [0.25, 0.46, 0.45, 0.94] },
        className: "whitespace-nowrap",
        style: {
          fontSize: normalFontSize,
          color: colors.normal,
          fontFamily: "inherit",
          lineHeight: 1.35,
          fontWeight: resolveThemeFontWeight(theme, 400),
          letterSpacing: "0.08em"
        },
        children: graphemes.map((seg, ti) => {
          const isSpace = /^\s+$/.test(seg.segment);
          const ci = visualIndex;
          if (!isSpace) visualIndex += 1;
          return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
            motion.span,
            {
              initial: { opacity: 0 },
              animate: visible ? {
                opacity: 1
              } : {
                opacity: 0
              },
              transition: {
                duration: 0.5,
                delay: visible && !isSpace ? ci * 0.04 : 0,
                ease: [0.25, 0.46, 0.45, 0.94]
              },
              className: "inline-block",
              style: {
                scale: charScaleMvs.current[ti],
                transition: "transform 0.06s ease-out",
                ...isSpace ? { minWidth: "0.25em" } : {}
              },
              children: isSpace ? "\xA0" : seg.segment
            },
            ti
          );
        })
      }
    );
  }
  return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
    motion.div,
    {
      initial: { opacity: 0, y: 24, scale: 0.92 },
      animate: visible ? { opacity: 1, y: 0, scale: 1 } : { opacity: 0, y: 24, scale: 0.92 },
      exit: { opacity: 0, y: -16, scale: 0.95 },
      transition: { duration: 0.6, ease: [0.25, 0.46, 0.45, 0.94] },
      className: "whitespace-nowrap",
      style: {
        fontSize: tiltFontSize,
        color: colors.tilt,
        fontFamily: "inherit",
        fontStyle: "italic",
        lineHeight: 1.25,
        fontWeight: resolveThemeFontWeight(theme, 300),
        letterSpacing: "0.15em"
      },
      children: graphemes.map((seg, ti) => {
        const isSpace = /^\s+$/.test(seg.segment);
        const isEven = visualIndex % 2 === 0;
        const yStagger = isEven ? -1 : 1;
        const ci = visualIndex;
        if (!isSpace) visualIndex += 1;
        return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          motion.span,
          {
            initial: {
              opacity: 0,
              y: isSpace ? 0 : yStagger * yOffset * 2
            },
            animate: visible ? {
              opacity: 1,
              y: isSpace ? 0 : yStagger * yOffset
            } : {
              opacity: 0,
              y: isSpace ? 0 : yStagger * yOffset * 2
            },
            transition: {
              duration: 0.5,
              delay: visible && !isSpace ? ci * 0.05 : 0,
              ease: [0.25, 0.46, 0.45, 0.94]
            },
            className: "inline-block",
            style: {
              scale: charScaleMvs.current[ti],
              transition: "transform 0.06s ease-out",
              ...isSpace ? { minWidth: "0.35em" } : {}
            },
            children: isSpace ? "\xA0" : seg.segment
          },
          ti
        );
      })
    }
  );
};
var VisualizerTilt = (props) => {
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
    tiltTuning = DEFAULT_TILT_TUNING
  } = props;
  const { t } = useTranslation();
  const [visibleSegmentIndex, setVisibleSegmentIndex] = (0, import_react.useState)(-1);
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
  const layout = (0, import_react.useMemo)(() => {
    if (!activeLine?.fullText) return null;
    return buildTiltLayout(activeLine.fullText, activeLine.startTime, tiltTuning, theme, lyricsFontScale);
  }, [activeLine?.fullText, activeLine?.startTime, tiltTuning, theme, lyricsFontScale]);
  const segmentTimings = (0, import_react.useMemo)(() => {
    if (!activeLine || !layout) return null;
    return layout.segments.map((seg) => {
      const { startWordIndex, endWordIndex } = findSegmentWordRange(
        seg.charOffset,
        seg.text,
        activeLine.fullText,
        activeLine.words
      );
      const segWords = activeLine.words.slice(startWordIndex, endWordIndex);
      if (segWords.length > 0) {
        return {
          start: segWords[0].startTime,
          end: segWords[segWords.length - 1].endTime
        };
      }
      return {
        start: activeLine.startTime,
        end: getLineRenderEndTime(activeLine)
      };
    });
  }, [activeLine, layout]);
  (0, import_react.useLayoutEffect)(() => {
    const handler = (latest) => {
      if (!segmentTimings || !activeLine) {
        setVisibleSegmentIndex(-1);
        return;
      }
      let targetIndex = -1;
      for (let i = 0; i < segmentTimings.length; i++) {
        if (latest >= segmentTimings[i].start - 0.25) {
          targetIndex = i;
        }
      }
      if (latest < activeLine.startTime - 0.1 || latest > getLineRenderEndTime(activeLine)) {
        targetIndex = -1;
      }
      setVisibleSegmentIndex((prev) => prev !== targetIndex ? targetIndex : prev);
    };
    const unsubscribe = currentTime.on("change", handler);
    handler(currentTime.get());
    return unsubscribe;
  }, [currentTime, segmentTimings, activeLine]);
  const translationFontSize = `clamp(${(1.125 * lyricsFontScale).toFixed(3)}rem, ${(2.6 * lyricsFontScale).toFixed(3)}vw, ${(1.25 * lyricsFontScale).toFixed(3)}rem)`;
  const upcomingFontSize = `clamp(${(0.875 * lyricsFontScale).toFixed(3)}rem, ${(2 * lyricsFontScale).toFixed(3)}vw, ${(1 * lyricsFontScale).toFixed(3)}rem)`;
  return /* @__PURE__ */ (0, import_jsx_runtime.jsxs)(
    Shell_default,
    {
      theme,
      audioPower,
      audioBands,
      sharedProps: props,
      children: [
        /* @__PURE__ */ (0, import_jsx_runtime.jsx)("div", { className: "relative z-10 w-full h-[70vh] flex items-center justify-center p-8 pointer-events-none", children: /* @__PURE__ */ (0, import_jsx_runtime.jsx)(AnimatePresence, { mode: "popLayout", children: showText && activeLine && layout ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          motion.div,
          {
            initial: { opacity: 0 },
            animate: { opacity: 1 },
            exit: { opacity: 0, transition: { duration: 0.45, ease: "easeInOut" } },
            className: "flex flex-col items-center justify-center gap-y-3 sm:gap-y-4",
            children: layout.segments.map((segment, si) => /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
              TiltLine,
              {
                segment,
                theme,
                fontScale: lyricsFontScale,
                scaleMultiplier: layout.scaleMultiplier,
                visible: si <= visibleSegmentIndex,
                colorScheme: tiltTuning?.colorScheme,
                currentTime,
                segmentStartTime: segmentTimings?.[si]?.start ?? 0,
                segmentEndTime: segmentTimings?.[si]?.end ?? 0,
                activeLine
              },
              `seg-${si}-${segment.text}`
            ))
          },
          `tilt-${activeLine.startTime}`
        ) : showText && !activeLine ? /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
          motion.div,
          {
            initial: { opacity: 0 },
            animate: { opacity: 1 },
            exit: { opacity: 0 },
            className: "text-2xl opacity-50 absolute",
            style: { color: theme.secondaryColor },
            children: t("ui.waitingForMusic")
          },
          "tilt-empty"
        ) : null }) }),
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
var VisualizerTilt_default = VisualizerTilt;
export {
  VisualizerTilt_default as default
};
