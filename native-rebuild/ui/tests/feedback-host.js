// Browser-only fixture. This directory is excluded from the production package.
const intervals = new Set();
const setIntervalOriginal = window.setInterval.bind(window);
const clearIntervalOriginal = window.clearInterval.bind(window);
document.body.dataset.previewIntervals = "0";
document.body.dataset.previewTicks = "0";
window.setInterval = (callback, delay) => {
  const handle = setIntervalOriginal(() => {
    if (delay === 1000)
      document.body.dataset.previewTicks = String(
        Number(document.body.dataset.previewTicks) + 1,
      );
    callback();
  }, delay);
  if (delay === 1000) intervals.add(handle);
  document.body.dataset.previewIntervals = String(intervals.size);
  return handle;
};
window.clearInterval = (handle) => {
  intervals.delete(handle);
  document.body.dataset.previewIntervals = String(intervals.size);
  clearIntervalOriginal(handle);
};

const mode = new URLSearchParams(location.search).get("mode");
if (mode !== "disconnected") {
  const listeners = new Set();
  const settings = { enabled: false, style: "welfare" };
  const reply = (saved) =>
    queueMicrotask(() => {
      listeners.forEach((callback) =>
        callback({
          data: { version: 1, type: "settings", clock: { ...settings }, saved },
        }),
      );
    });
  Object.defineProperty(window.chrome, "webview", {
    configurable: true,
    value: {
      addEventListener: (_, callback) => listeners.add(callback),
      removeEventListener: (_, callback) => listeners.delete(callback),
      postMessage(message) {
        if (message === "v1|ready") reply(true);
        else if (message.startsWith("v1|clock|")) {
          const [, , key, value] = message.split("|");
          settings[key] = key === "enabled" ? value === "true" : value;
          reply(mode === "success");
        }
      },
    },
  });
}
await import("../app.js");
