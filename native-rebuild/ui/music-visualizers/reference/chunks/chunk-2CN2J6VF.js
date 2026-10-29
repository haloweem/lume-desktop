// src/utils/fontStacks.ts
var MIN_FONT_WEIGHT = 100;
var MAX_FONT_WEIGHT = 900;
var FONT_WEIGHT_STEP = 10;
var normalizeFontWeight = (fontWeight) => {
  if (typeof fontWeight !== "number" || !Number.isFinite(fontWeight)) return null;
  const clamped = Math.min(MAX_FONT_WEIGHT, Math.max(MIN_FONT_WEIGHT, fontWeight));
  return Math.round(clamped / FONT_WEIGHT_STEP) * FONT_WEIGHT_STEP;
};
var resolveThemeFontWeight = (theme, fallback) => normalizeFontWeight(theme?.fontWeight) ?? fallback;
var SUGAR_SERIF_FAMILY = '"\u7345\u5C3E\u56DB\u5B63\u6625\u52A0\u7CD6SC"';
var BUILTIN_FONT_STACKS = {
  sans: '"Inter", "Noto Sans CJK SC", "Noto Sans JP", "Source Han Sans SC", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", "Helvetica Neue", Arial, sans-serif',
  serif: `${SUGAR_SERIF_FAMILY}, "Iowan Old Style", "Noto Serif CJK SC", "Noto Serif JP", "Source Han Serif SC", "Songti SC", "STSong", "Georgia", serif`,
  mono: '"IBM Plex Mono", "Sarasa Mono SC", "Noto Sans Mono CJK SC", "Noto Sans Mono", "SFMono-Regular", Consolas, monospace'
};
var TRANSLATION_FONT_STACKS = {
  sans: 'system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", "Inter", "Helvetica Neue", Arial, "Noto Sans CJK SC", "Source Han Sans SC", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", "Noto Sans JP", "Source Han Sans JP", "Hiragino Sans", "Yu Gothic", Meiryo, sans-serif',
  serif: `${SUGAR_SERIF_FAMILY}, "Folia Noto Serif SC", "Iowan Old Style", Georgia, "Times New Roman", "Noto Serif CJK SC", "Source Han Serif SC", "Songti SC", "STSong", "SimSun", "Noto Serif JP", "Source Han Serif JP", "Yu Mincho", "MS PMincho", serif`,
  mono: 'Consolas, "IBM Plex Mono", "SFMono-Regular", Menlo, Monaco, "Sarasa Mono SC", "Noto Sans Mono CJK SC", "SimHei", "DengXian", "Microsoft YaHei UI", "Microsoft YaHei", "Noto Sans Mono CJK JP", "MS Gothic", monospace'
};
var CSS_GENERIC_FONT_FAMILIES = /* @__PURE__ */ new Set([
  "serif",
  "sans-serif",
  "monospace",
  "cursive",
  "fantasy",
  "system-ui",
  "ui-serif",
  "ui-sans-serif",
  "ui-monospace",
  "ui-rounded",
  "emoji",
  "math",
  "fangsong"
]);
var quoteFontFamily = (fontFamily) => `"${fontFamily.replace(/["\\]/g, "\\$&")}"`;
var normalizeFontFamilyName = (fontFamily) => fontFamily.trim().replace(/^['"]|['"]$/g, "").trim();
var formatFontFamily = (fontFamily) => {
  const normalized = normalizeFontFamilyName(fontFamily);
  if (!normalized) return null;
  return CSS_GENERIC_FONT_FAMILIES.has(normalized.toLowerCase()) ? normalized : quoteFontFamily(normalized);
};
var normalizeFontFamilyStack = (fontFamilies) => {
  const seen = /* @__PURE__ */ new Set();
  const stack = [];
  fontFamilies?.forEach((fontFamily) => {
    const normalized = normalizeFontFamilyName(fontFamily ?? "");
    if (!normalized) return;
    const key = normalized.toLocaleLowerCase();
    if (seen.has(key)) return;
    seen.add(key);
    stack.push(normalized);
  });
  return stack;
};
var buildCustomFontFamilyStack = (theme) => {
  const familiesToNormalize = [
    ...theme.fontFamily ? [theme.fontFamily] : [],
    ...theme.fontFamilyStack ?? []
  ];
  if (familiesToNormalize.length === 0) {
    return [];
  }
  return normalizeFontFamilyStack(familiesToNormalize).map(formatFontFamily).filter((fontFamily) => Boolean(fontFamily));
};
var getBuiltinThemeFontStack = (fontStyle) => {
  return BUILTIN_FONT_STACKS[fontStyle] ?? BUILTIN_FONT_STACKS.sans;
};
var resolveThemeFontStack = (theme) => {
  const fallbackStack = getBuiltinThemeFontStack(theme.fontStyle);
  const customFontStack = buildCustomFontFamilyStack(theme);
  if (customFontStack.length === 0) {
    return fallbackStack;
  }
  return `${customFontStack.join(", ")}, ${fallbackStack}`;
};
var resolveThemeTranslationFontStack = (theme) => {
  const fallbackStack = TRANSLATION_FONT_STACKS[theme.fontStyle] ?? TRANSLATION_FONT_STACKS.sans;
  const customFontStack = buildCustomFontFamilyStack(theme);
  if (customFontStack.length === 0) {
    return fallbackStack;
  }
  return `${customFontStack.join(", ")}, ${fallbackStack}`;
};

export {
  resolveThemeFontWeight,
  resolveThemeFontStack,
  resolveThemeTranslationFontStack
};
