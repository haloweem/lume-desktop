// src/utils/lyrics/cjkSemanticLayout.ts
var CJK_REGEX = /[\u4e00-\u9fa5\u3040-\u30ff\uac00-\ud7af]/;
var WHITESPACE_REGEX = /^\s+$/;
var APOSTROPHE_ONLY_REGEX = /^['’]\s*$/;
var CONTRACTION_SUFFIX_REGEX = /^(s|t|m|d|ll|re|ve|em)\s*$/i;
var DIRECT_CONTRACTION_REGEX = /^['’](s|t|m|d|ll|re|ve|em)\s*$/i;
var TRAILING_APOSTROPHE_REGEX = /['’]\s*$/;
var TRAILING_WORD_CHAR_REGEX = /[\p{L}\p{N}]$/u;
var INLINE_CONTRACTION_REGEX = /[\p{L}\p{N}]+['’](s|t|m|d|ll|re|ve|em)/iu;
var STICKY_TRAILING_PUNCTUATION_REGEX = /^[,.;:!?，。！？、：；）】》」』〉〕］)}\]"'’”’]+$/u;
var hasCjkText = (text) => CJK_REGEX.test(text);
var createSingleWordLayoutUnits = (words) => words.map((word) => ({
  text: word.text,
  words: [word],
  startTime: word.startTime,
  endTime: word.endTime,
  isSemantic: false
}));
var getWordSegments = (text) => {
  const Segmenter = Intl?.Segmenter;
  if (!Segmenter) {
    return null;
  }
  try {
    return Array.from(new Segmenter(void 0, { granularity: "word" }).segment(text), (segment) => ({
      segment: segment.segment,
      isWordLike: segment.isWordLike
    }));
  } catch {
    return null;
  }
};
var appendWordsToUnit = (unit, text, words) => {
  unit.text += text;
  unit.words.push(...words);
  unit.endTime = words[words.length - 1]?.endTime ?? unit.endTime;
};
var cloneUnit = (unit) => ({
  ...unit,
  words: [...unit.words]
});
var appendUnitToStickyUnit = (target, unit) => {
  target.text += unit.text;
  target.words.push(...unit.words);
  target.endTime = unit.endTime;
  target.isSticky = true;
};
var canAttachToPrevious = (text) => TRAILING_WORD_CHAR_REGEX.test(text.trimEnd());
var endsWithApostrophe = (text) => TRAILING_APOSTROPHE_REGEX.test(text.trimEnd());
var isApostropheOnlyUnit = (unit) => APOSTROPHE_ONLY_REGEX.test(unit.text.trim());
var isContractionSuffixUnit = (unit) => CONTRACTION_SUFFIX_REGEX.test(unit.text.trim());
var isDirectContractionUnit = (unit) => DIRECT_CONTRACTION_REGEX.test(unit.text.trim());
var isStickyTrailingPunctuationUnit = (unit) => STICKY_TRAILING_PUNCTUATION_REGEX.test(unit.text.trim());
var hasAttachedTrailingPunctuation = (unit) => {
  if (unit.words.length <= 1) {
    return false;
  }
  const lastWord = unit.words[unit.words.length - 1];
  return Boolean(lastWord && isStickyTrailingPunctuationUnit({
    text: lastWord.text,
    words: [lastWord],
    startTime: lastWord.startTime,
    endTime: lastWord.endTime,
    isSemantic: false
  }));
};
var hasInlineContraction = (unit) => unit.words.length > 1 && !unit.isSemantic && INLINE_CONTRACTION_REGEX.test(unit.text);
var mapSegmentsToWords = (segments, words) => {
  const units = [];
  let wordIndex = 0;
  for (const segment of segments) {
    const segmentText = segment.segment;
    if (!segmentText || WHITESPACE_REGEX.test(segmentText)) {
      continue;
    }
    const startWordIndex = wordIndex;
    let collectedText = "";
    while (wordIndex < words.length && collectedText.length < segmentText.length) {
      collectedText += words[wordIndex].text;
      wordIndex += 1;
      if (!segmentText.startsWith(collectedText)) {
        return null;
      }
    }
    if (collectedText !== segmentText) {
      return null;
    }
    const segmentWords = words.slice(startWordIndex, wordIndex);
    const firstWord = segmentWords[0];
    const lastWord = segmentWords[segmentWords.length - 1];
    if (!firstWord || !lastWord) {
      return null;
    }
    if (!segment.isWordLike && units.length > 0) {
      appendWordsToUnit(units[units.length - 1], segmentText, segmentWords);
      continue;
    }
    units.push({
      text: segmentText,
      words: segmentWords,
      startTime: firstWord.startTime,
      endTime: lastWord.endTime,
      isSemantic: Boolean(segment.isWordLike && hasCjkText(segmentText) && segmentWords.length > 1)
    });
  }
  if (wordIndex !== words.length || units.length === 0) {
    return null;
  }
  return units;
};
var buildCjkSemanticLayoutUnits = (line) => {
  if (line.words.length === 0) {
    return [];
  }
  const fallbackUnits = createSingleWordLayoutUnits(line.words);
  if (!hasCjkText(line.fullText)) {
    return fallbackUnits;
  }
  const segments = getWordSegments(line.fullText);
  if (!segments) {
    return fallbackUnits;
  }
  return mapSegmentsToWords(segments, line.words) ?? fallbackUnits;
};
var applyStickyPunctuationLayoutUnits = (units) => {
  const merged = [];
  for (let index = 0; index < units.length; index += 1) {
    const current = units[index];
    const previous = merged[merged.length - 1];
    if (!previous) {
      merged.push(cloneUnit(current));
      continue;
    }
    const next = units[index + 1];
    if (isApostropheOnlyUnit(current) && next && canAttachToPrevious(previous.text) && isContractionSuffixUnit(next)) {
      appendUnitToStickyUnit(previous, current);
      appendUnitToStickyUnit(previous, next);
      index += 1;
      continue;
    }
    if (isDirectContractionUnit(current) && canAttachToPrevious(previous.text)) {
      appendUnitToStickyUnit(previous, current);
      continue;
    }
    if (isContractionSuffixUnit(current) && endsWithApostrophe(previous.text)) {
      appendUnitToStickyUnit(previous, current);
      continue;
    }
    if (isStickyTrailingPunctuationUnit(current) && canAttachToPrevious(previous.text)) {
      appendUnitToStickyUnit(previous, current);
      continue;
    }
    merged.push(cloneUnit(current));
  }
  return merged.map((unit) => hasAttachedTrailingPunctuation(unit) || hasInlineContraction(unit) ? { ...unit, isSticky: true } : unit);
};
var buildPostLyricLayoutUnits = (line, options = {}) => {
  const rawUnits = options.semantic ? buildCjkSemanticLayoutUnits(line) : createSingleWordLayoutUnits(line.words);
  return options.sticky ? applyStickyPunctuationLayoutUnits(rawUnits) : rawUnits;
};
var buildDisplayWordsFromLayoutUnits = (units) => units.flatMap((unit) => {
  if (!unit.isSticky || unit.isSemantic) {
    return unit.words;
  }
  return [{
    text: unit.text,
    startTime: unit.startTime,
    endTime: unit.endTime
  }];
});

export {
  buildPostLyricLayoutUnits,
  buildDisplayWordsFromLayoutUnits
};
