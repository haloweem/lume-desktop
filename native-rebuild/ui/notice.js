export function createNoticeController(element, {
  timers = globalThis,
  reducedMotion = () => globalThis.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches === true,
} = {}) {
  let timer = null, generation = 0, disposed = false;
  function dismiss() {
    ++generation;
    if (timer !== null) timers.clearTimeout(timer);
    timer = null;
    element.hidden = true;
    element.classList.remove("is-hiding");
    element.textContent = "";
  }
  return {
    show(text, { autoDismiss = false } = {}) {
      if (disposed) return;
      dismiss();
      if (!text) return;
      element.textContent = text;
      element.hidden = false;
      if (!autoDismiss) return;
      const current = generation;
      timer = timers.setTimeout(() => {
        if (disposed || current !== generation) return;
        timer = null;
        if (reducedMotion()) { dismiss(); return; }
        element.classList.add("is-hiding");
        timer = timers.setTimeout(() => {
          if (!disposed && current === generation) dismiss();
        }, 180);
      }, 2500);
    },
    dismiss,
    dispose() { if (!disposed) { dismiss(); disposed = true; } },
  };
}
