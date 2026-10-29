import {
  require_jsx_runtime
} from "./chunk-AS2U3CHD.js";
import {
  require_react
} from "./chunk-RG3JEOUL.js";
import {
  resolveThemeFontStack,
  resolveThemeFontWeight
} from "./chunk-2CN2J6VF.js";
import {
  __toESM
} from "./chunk-XCBH6NLF.js";

// src/components/visualizer/runtime.ts
var import_react = __toESM(require_react(), 1);
var getRecentCompletedLine = ({
  lines,
  currentLineIndex,
  currentTime,
  getLineEndTime = (line) => line.endTime
}) => {
  if (currentLineIndex !== -1 || lines.length === 0) {
    return null;
  }
  for (let i = lines.length - 1; i >= 0; i--) {
    if (currentTime > getLineEndTime(lines[i])) {
      return lines[i];
    }
  }
  return null;
};
var getUpcomingLine = (lines, currentLineIndex, currentTime) => {
  const activeLine = lines[currentLineIndex];
  if (activeLine) {
    return lines[currentLineIndex + 1] ?? null;
  }
  for (const line of lines) {
    if (line.startTime > currentTime) {
      return line;
    }
  }
  return null;
};
var getUpcomingLines = (lines, currentLineIndex, count = 2) => {
  if (currentLineIndex < 0) {
    return [];
  }
  return lines.slice(currentLineIndex + 1, currentLineIndex + 1 + count);
};
var shouldPreheatLine = (line, currentTime, window) => {
  if (!line) {
    return false;
  }
  const leadTime = line.startTime - currentTime;
  return leadTime >= window.minLead && leadTime <= window.maxLead;
};
var prepareActiveAndUpcoming = ({
  activeLine,
  upcomingLine,
  prepareLine
}) => {
  if (!activeLine) {
    prepareLine(upcomingLine);
    return null;
  }
  const currentState = prepareLine(activeLine);
  prepareLine(upcomingLine);
  return currentState;
};
var useVisualizerRuntime = ({
  currentTime,
  currentLineIndex,
  lines,
  getLineEndTime
}) => {
  const currentTimeValue = currentTime.get();
  const activeLine = lines[currentLineIndex] ?? null;
  const recentCompletedLine = (0, import_react.useMemo)(() => getRecentCompletedLine({
    lines,
    currentLineIndex,
    currentTime: currentTimeValue,
    getLineEndTime
  }), [currentLineIndex, currentTimeValue, getLineEndTime, lines]);
  const upcomingLine = (0, import_react.useMemo)(
    () => getUpcomingLine(lines, currentLineIndex, currentTimeValue),
    [currentLineIndex, currentTimeValue, lines]
  );
  const nextLines = (0, import_react.useMemo)(
    () => getUpcomingLines(lines, currentLineIndex, 2),
    [currentLineIndex, lines]
  );
  return {
    currentTimeValue,
    activeLine,
    recentCompletedLine,
    upcomingLine,
    nextLines
  };
};

// ../../../../../workspace/lume1.0.7/native-rebuild/ui/music-visualizers/reference/src/Shell.tsx
var import_react2 = __toESM(require_react());
var import_jsx_runtime = __toESM(require_jsx_runtime());
var Shell_default = (0, import_react2.forwardRef)(function VisualizerShell({ theme, sharedProps, visualizerOpacity = 1, children, className = "" }, ref) {
  const fontClassName = theme.fontStyle === "mono" ? "font-mono" : theme.fontStyle === "serif" ? "font-serif" : "font-sans";
  return /* @__PURE__ */ (0, import_jsx_runtime.jsx)(
    "div",
    {
      ref,
      "data-lumenic-liquid-glass-scene": true,
      className: `w-full h-full flex flex-col items-center justify-center overflow-hidden relative ${fontClassName} transition-colors duration-1000 ${className}`.trim(),
      style: {
        backgroundColor: "transparent",
        fontFamily: resolveThemeFontStack(theme),
        fontWeight: resolveThemeFontWeight(theme, 400),
        opacity: sharedProps?.visualizerOpacity ?? visualizerOpacity
      },
      children
    }
  );
});

// src/components/visualizer/colorMix.ts
var clamp = (value, min, max) => Math.max(min, Math.min(max, value));
var mix = (from, to, amount) => from + (to - from) * amount;
var FALLBACK_RGB = { r: 255, g: 255, b: 255 };
var isFiniteChannel = (value) => Number.isFinite(value);
var formatRgba = (channels, alpha) => `rgba(${Math.round(clamp(channels.r, 0, 255))}, ${Math.round(clamp(channels.g, 0, 255))}, ${Math.round(clamp(channels.b, 0, 255))}, ${alpha})`;
var colorWithAlpha = (color, alpha) => {
  const normalizedAlpha = clamp(alpha, 0, 1);
  const normalizedColor = typeof color === "string" ? color.trim() : "";
  if (!normalizedColor) {
    return formatRgba(FALLBACK_RGB, normalizedAlpha);
  }
  if (normalizedColor.startsWith("#")) {
    const hex = normalizedColor.slice(1);
    const parse = (value) => Number.parseInt(value, 16);
    if (/^[0-9a-fA-F]{3}$/.test(hex)) {
      const r = parse(hex[0] + hex[0]);
      const g = parse(hex[1] + hex[1]);
      const b = parse(hex[2] + hex[2]);
      return formatRgba({ r, g, b }, normalizedAlpha);
    }
    if (/^[0-9a-fA-F]{6}$/.test(hex)) {
      const r = parse(hex.slice(0, 2));
      const g = parse(hex.slice(2, 4));
      const b = parse(hex.slice(4, 6));
      return formatRgba({ r, g, b }, normalizedAlpha);
    }
    return formatRgba(FALLBACK_RGB, normalizedAlpha);
  }
  const rgbMatch = normalizedColor.match(/^rgba?\(([^)]+)\)$/);
  if (rgbMatch) {
    const [r, g, b] = rgbMatch[1].split(",").slice(0, 3).map((part) => Number.parseFloat(part.trim()));
    if ([r, g, b].every(isFiniteChannel)) {
      return formatRgba({ r, g, b }, normalizedAlpha);
    }
    return formatRgba(FALLBACK_RGB, normalizedAlpha);
  }
  return normalizedColor;
};
var parseColorChannels = (color) => {
  const normalizedColor = typeof color === "string" ? color.trim() : "";
  if (!normalizedColor) {
    return null;
  }
  if (normalizedColor.startsWith("#")) {
    const hex = normalizedColor.slice(1);
    const parse = (value) => Number.parseInt(value, 16);
    if (/^[0-9a-fA-F]{3}$/.test(hex)) {
      return {
        r: parse(hex[0] + hex[0]),
        g: parse(hex[1] + hex[1]),
        b: parse(hex[2] + hex[2])
      };
    }
    if (/^[0-9a-fA-F]{6}$/.test(hex)) {
      return {
        r: parse(hex.slice(0, 2)),
        g: parse(hex.slice(2, 4)),
        b: parse(hex.slice(4, 6))
      };
    }
  }
  const rgbMatch = normalizedColor.match(/^rgba?\(([^)]+)\)$/);
  if (rgbMatch) {
    const [r, g, b] = rgbMatch[1].split(",").slice(0, 3).map((part) => Number.parseFloat(part.trim()));
    if ([r, g, b].every(isFiniteChannel)) {
      return { r, g, b };
    }
  }
  return null;
};
var mixColors = (from, to, amount, alpha = 1) => {
  const normalizedAmount = clamp(amount, 0, 1);
  const fromChannels = parseColorChannels(from);
  const toChannels = parseColorChannels(to);
  if (!fromChannels || !toChannels) {
    return colorWithAlpha(normalizedAmount >= 0.5 ? to : from, alpha);
  }
  return `rgba(${Math.round(mix(fromChannels.r, toChannels.r, normalizedAmount))}, ${Math.round(mix(fromChannels.g, toChannels.g, normalizedAmount))}, ${Math.round(mix(fromChannels.b, toChannels.b, normalizedAmount))}, ${clamp(alpha, 0, 1)})`;
};

export {
  getRecentCompletedLine,
  getUpcomingLines,
  shouldPreheatLine,
  prepareActiveAndUpcoming,
  useVisualizerRuntime,
  Shell_default,
  colorWithAlpha,
  mixColors
};
