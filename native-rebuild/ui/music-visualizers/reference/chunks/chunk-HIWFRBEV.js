import {
  require_jsx_runtime
} from "./chunk-XSUOVEKG.js";
import {
  require_react
} from "./chunk-CWOU647E.js";
import {
  resolveThemeFontStack,
  resolveThemeFontWeight
} from "./chunk-2CN2J6VF.js";
import {
  __toESM
} from "./chunk-NZLE2WMY.js";

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

// ../../src/Shell.tsx
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

export {
  getRecentCompletedLine,
  getUpcomingLines,
  shouldPreheatLine,
  prepareActiveAndUpcoming,
  useVisualizerRuntime,
  Shell_default
};
