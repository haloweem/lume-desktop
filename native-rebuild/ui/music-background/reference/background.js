// ../../../history/lume1.0.1/third_party/folia-major/upstream/src/components/visualizer/backgrounds/wallpaper/AnimatedWallpaperImage.ts
var MAX_ANIMATED_BYTES = 50 * 1024 * 1024;
var MAX_ANIMATED_PIXELS = 64 * 1024 * 1024;
var MAX_ANIMATED_SIDE = 16384;
async function readBounded(response, signal) {
  const length = Number(response.headers.get("content-length"));
  if (Number.isFinite(length) && length > MAX_ANIMATED_BYTES) throw new Error("Animated wallpaper is too large");
  if (!response.body) {
    const data2 = await response.arrayBuffer();
    if (data2.byteLength > MAX_ANIMATED_BYTES) throw new Error("Animated wallpaper is too large");
    return data2;
  }
  const reader = response.body.getReader(), chunks = [];
  let total = 0;
  try {
    while (true) {
      const part = await reader.read();
      if (part.done) break;
      total += part.value.byteLength;
      if (total > MAX_ANIMATED_BYTES) {
        await reader.cancel();
        throw new Error("Animated wallpaper is too large");
      }
      chunks.push(part.value);
      signal.throwIfAborted();
    }
  } finally {
    reader.releaseLock();
  }
  const data = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    data.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return data.buffer;
}
var AnimatedWallpaperImage = class _AnimatedWallpaperImage {
  constructor(decoder, count, repeat, onFrame) {
    this.decoder = decoder;
    this.count = count;
    this.repeat = repeat;
    this.onFrame = onFrame;
    this.canvas = document.createElement("canvas");
    this.visible = true;
    this.closed = false;
    this.index = 0;
    this.loops = 0;
    this.duration = 100;
    this.decoding = false;
    const context = this.canvas.getContext("2d");
    if (!context) throw new Error("Animated wallpaper canvas unavailable");
    this.context = context;
  }
  static async load(url, mimeType, signal, onFrame) {
    const type = mimeType || (/\.gif(?:[?#]|$)/i.test(url) ? "image/gif" : /\.webp(?:[?#]|$)/i.test(url) ? "image/webp" : "");
    if (type !== "image/gif" && type !== "image/webp" && type !== "image/png") return null;
    const Constructor = globalThis.ImageDecoder;
    if (!Constructor) return null;
    const response = await fetch(url, { signal });
    if (!response.ok) throw new Error("Animated wallpaper read failed");
    const data = await readBounded(response, signal);
    signal.throwIfAborted();
    const decoder = new Constructor({ data, type });
    try {
      await decoder.tracks.ready;
      signal.throwIfAborted();
      const track = decoder.tracks.selectedTrack;
      if (!track || track.frameCount <= 1) {
        decoder.close();
        return null;
      }
      const animation = new _AnimatedWallpaperImage(decoder, track.frameCount, track.repetitionCount, onFrame);
      await animation.decode();
      signal.throwIfAborted();
      return animation;
    } catch (error) {
      decoder.close();
      throw error;
    }
  }
  async decode() {
    this.decoding = true;
    try {
      const { image } = await this.decoder.decode({ frameIndex: this.index });
      try {
        if (this.closed) return;
        const width = image.displayWidth, height = image.displayHeight;
        if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || width > MAX_ANIMATED_SIDE || height > MAX_ANIMATED_SIDE || width * height > MAX_ANIMATED_PIXELS)
          throw new Error("Animated wallpaper dimensions are too large");
        if (this.canvas.width !== width) this.canvas.width = width;
        if (this.canvas.height !== height) this.canvas.height = height;
        this.context.clearRect(0, 0, this.canvas.width, this.canvas.height);
        this.context.drawImage(image, 0, 0);
        this.duration = Math.max(20, (image.duration ?? 1e5) / 1e3);
        this.onFrame();
      } finally {
        image.close();
      }
    } finally {
      this.decoding = false;
    }
  }
  schedule() {
    if (this.closed || !this.visible || this.timer !== void 0 || this.decoding) return;
    if (this.index === this.count - 1 && this.loops >= this.repeat) return;
    this.timer = window.setTimeout(() => {
      this.timer = void 0;
      if (this.closed || !this.visible) return;
      this.index++;
      if (this.index >= this.count) {
        this.index = 0;
        this.loops++;
      }
      void this.decode().then(() => this.schedule()).catch(() => this.close());
    }, this.duration);
  }
  setVisible(visible) {
    this.visible = visible;
    if (!visible && this.timer !== void 0) {
      clearTimeout(this.timer);
      this.timer = void 0;
    }
    if (visible) this.schedule();
  }
  close() {
    if (this.closed) return;
    this.closed = true;
    if (this.timer !== void 0) clearTimeout(this.timer);
    this.timer = void 0;
    this.decoder.close();
    this.canvas.width = this.canvas.height = 1;
  }
};

// ../../../history/lume1.0.1/third_party/folia-major/upstream/src/components/visualizer/backgrounds/wallpaper/WallpaperCompositor.ts
function ease(t, incoming) {
  const x1 = incoming ? 0 : 0.42, x2 = incoming ? 0.58 : 1;
  let lo = 0, hi = 1;
  for (let i = 0; i < 16; i++) {
    const u2 = (lo + hi) / 2, v = 1 - u2;
    const x = 3 * v * v * u2 * x1 + 3 * v * u2 * u2 * x2 + u2 * u2 * u2;
    if (x < t) lo = u2;
    else hi = u2;
  }
  const u = (lo + hi) / 2;
  return 3 * (1 - u) * u * u + u * u * u;
}
var WallpaperCompositor = class {
  constructor(canvas, dependencies = {}) {
    this.canvas = canvas;
    this.current = null;
    this.outgoing = null;
    this.pending = null;
    this.frame = null;
    this.visible = true;
    this.disposed = false;
    this.contextLost = false;
    this.fadeStart = 0;
    this.hiddenAt = 0;
    this.reducedMotion = false;
    this.frameVersion = 0;
    this.fadeScratch = null;
    this.onContextLost = (event) => {
      event.preventDefault();
      this.contextLost = true;
      if (this.frame !== null) this.deps.cancelFrame(this.frame);
      this.frame = null;
    };
    this.onContextRestored = () => {
      this.contextLost = false;
      this.releaseScratch();
      this.invalidate();
    };
    this.invalidate = () => {
      if (this.disposed || this.contextLost || !this.visible || this.frame !== null) return;
      this.frame = this.deps.requestFrame(this.draw);
    };
    this.draw = () => {
      this.frame = null;
      if (this.disposed || this.contextLost || !this.visible || !this.current) return;
      const progress = this.reducedMotion ? 1 : Math.min(1, Math.max(0, (this.deps.now() - this.fadeStart) / 420));
      this.context.globalAlpha = 1;
      this.context.fillStyle = this.current.color;
      this.context.fillRect(0, 0, this.canvas.width, this.canvas.height);
      if (this.outgoing && progress < 1) {
        this.drawLayer(this.outgoing, 1 - ease(progress, false));
        this.drawLayer(this.current, ease(progress, true));
      } else {
        this.release(this.outgoing);
        this.outgoing = null;
        this.releaseScratch();
        this.drawLayer(this.current, 1);
      }
      this.context.globalAlpha = 1;
      this.canvas.dataset.glassFrameVersion = String(++this.frameVersion);
      this.canvas.dispatchEvent(new Event("lume-glass-media-frame"));
      const fallbackVideoClock = [this.current, this.outgoing].some((layer) => layer?.kind === "video" && !layer.source.requestVideoFrameCallback && !layer.source.paused);
      if (this.outgoing || fallbackVideoClock) this.invalidate();
    };
    this.deps = {
      createMedia: (kind) => kind === "video" ? document.createElement("video") : new Image(),
      createCanvas: () => document.createElement("canvas"),
      requestFrame: (callback) => requestAnimationFrame(callback),
      cancelFrame: (id) => cancelAnimationFrame(id),
      now: () => performance.now(),
      ...dependencies
    };
    const context = canvas.getContext("2d", { alpha: false });
    if (!context) throw new Error("WALLPAPER_CANVAS_UNAVAILABLE");
    this.context = context;
    canvas.addEventListener("contextlost", this.onContextLost);
    canvas.addEventListener("contextrestored", this.onContextRestored);
  }
  setMedia(media, color) {
    if (this.disposed) return Promise.resolve(false);
    this.cancelPending();
    if (this.current?.url === media.url && this.current.kind === media.kind) {
      this.current.color = color;
      this.invalidate();
      return Promise.resolve(true);
    }
    return new Promise((resolve) => {
      const source = this.deps.createMedia(media.kind);
      const layer = { source, kind: media.kind, url: media.url, color };
      let settled = false;
      const finish = (ok) => {
        if (!settled) {
          settled = true;
          resolve(ok);
        }
      };
      const ready = async () => {
        if (this.disposed || this.pending?.layer !== layer) return;
        if (media.kind === "image") {
          const controller = new AbortController();
          layer.loadAbort = controller;
          try {
            if (!media.skipAnimation) layer.animation = await AnimatedWallpaperImage.load(media.url, media.mimeType, controller.signal, this.invalidate) ?? void 0;
          } catch {
          }
          if (this.disposed || this.pending?.layer !== layer) {
            layer.animation?.close();
            return;
          }
        }
        if (this.outgoing) {
          const frozen = this.deps.createCanvas();
          frozen.width = this.canvas.width;
          frozen.height = this.canvas.height;
          frozen.getContext("2d")?.drawImage(this.canvas, 0, 0);
          this.release(this.outgoing);
          this.release(this.current);
          this.current = { source: frozen, kind: "snapshot", url: "", color };
        }
        this.outgoing = this.current;
        this.current = layer;
        this.pending = null;
        this.fadeStart = this.deps.now();
        if (!this.visible) this.hiddenAt = this.fadeStart;
        this.syncVideo(layer);
        layer.animation?.setVisible(this.visible);
        this.invalidate();
        finish(true);
      };
      const failed = () => {
        if (this.pending?.layer !== layer) return;
        this.pending = null;
        this.release(layer);
        finish(false);
      };
      const refresh = () => this.invalidate();
      const readyEvent = media.kind === "video" ? "loadeddata" : "load";
      source.addEventListener(readyEvent, ready, { once: true });
      source.addEventListener("error", failed, { once: true });
      source.addEventListener("seeked", refresh);
      layer.removeListeners = () => {
        source.removeEventListener(readyEvent, ready);
        source.removeEventListener("error", failed);
        source.removeEventListener("seeked", refresh);
      };
      this.pending = { layer, finish };
      if (/^https?:\/\//i.test(media.url)) source.crossOrigin = "anonymous";
      if (media.kind === "video") {
        const video = source;
        video.muted = true;
        video.loop = true;
        video.playsInline = true;
        video.preload = "auto";
      }
      source.src = media.url;
    });
  }
  cancelPending() {
    if (!this.pending) return;
    this.release(this.pending.layer);
    this.pending.finish(false);
    this.pending = null;
  }
  setVisible(visible) {
    if (this.disposed || visible === this.visible) return;
    this.visible = visible;
    if (!visible) {
      this.hiddenAt = this.deps.now();
      if (this.frame !== null) this.deps.cancelFrame(this.frame);
      this.frame = null;
    } else this.fadeStart += this.deps.now() - this.hiddenAt;
    [this.current, this.outgoing].forEach((layer) => {
      if (layer) {
        this.syncVideo(layer);
        layer.animation?.setVisible(visible);
      }
    });
    this.invalidate();
  }
  setReducedMotion(value) {
    this.reducedMotion = value;
    this.invalidate();
  }
  resize(width, height) {
    if (this.disposed || !Number.isFinite(width) || !Number.isFinite(height)) return;
    const w = Math.max(1, Math.round(width)), h = Math.max(1, Math.round(height));
    if (w === this.canvas.width && h === this.canvas.height) return;
    this.canvas.width = w;
    this.canvas.height = h;
    this.invalidate();
  }
  syncVideo(layer) {
    if (layer.kind !== "video") return;
    const video = layer.source;
    if (this.visible) {
      if (video.paused) void video.play().catch(() => {
      });
      this.armVideoFrame(layer);
    } else {
      video.pause();
      if (layer.videoFrame !== void 0) video.cancelVideoFrameCallback?.(layer.videoFrame);
      layer.videoFrame = void 0;
    }
  }
  armVideoFrame(layer) {
    const video = layer.source;
    if (video.requestVideoFrameCallback && layer.videoFrame === void 0) {
      layer.videoFrame = video.requestVideoFrameCallback(() => {
        layer.videoFrame = void 0;
        if (this.disposed || !this.visible) return;
        this.invalidate();
        this.armVideoFrame(layer);
      });
    }
  }
  release(layer) {
    if (!layer) return;
    layer.loadAbort?.abort();
    layer.animation?.close();
    layer.removeListeners?.();
    if (layer.kind === "video") {
      const video = layer.source;
      if (layer.videoFrame !== void 0) video.cancelVideoFrameCallback?.(layer.videoFrame);
      video.pause();
      video.removeAttribute("src");
      video.load();
    } else if (layer.kind === "snapshot") {
      layer.source.width = 1;
      layer.source.height = 1;
    } else layer.source.removeAttribute("src");
  }
  drawLayer(layer, opacity) {
    const source = layer.animation?.canvas ?? layer.source;
    const kind = layer.animation ? "snapshot" : layer.kind;
    const width = kind === "video" ? source.videoWidth : kind === "image" ? source.naturalWidth : source.width;
    const height = kind === "video" ? source.videoHeight : kind === "image" ? source.naturalHeight : source.height;
    let target = this.context;
    if (opacity < 1) {
      this.fadeScratch ??= this.deps.createCanvas();
      if (this.fadeScratch.width !== this.canvas.width) this.fadeScratch.width = this.canvas.width;
      if (this.fadeScratch.height !== this.canvas.height) this.fadeScratch.height = this.canvas.height;
      const context = this.fadeScratch.getContext("2d", { alpha: false });
      if (context) target = context;
    }
    target.globalAlpha = 1;
    target.fillStyle = layer.color;
    target.fillRect(0, 0, this.canvas.width, this.canvas.height);
    if (!width || !height) return;
    const scale = Math.max(this.canvas.width / width, this.canvas.height / height);
    const w = width * scale, h = height * scale;
    target.drawImage(source, (this.canvas.width - w) / 2, (this.canvas.height - h) / 2, w, h);
    if (target !== this.context && this.fadeScratch) {
      this.context.globalAlpha = opacity;
      this.context.drawImage(this.fadeScratch, 0, 0);
    }
  }
  dispose() {
    if (this.disposed) return;
    this.disposed = true;
    this.canvas.removeEventListener("contextlost", this.onContextLost);
    this.canvas.removeEventListener("contextrestored", this.onContextRestored);
    if (this.frame !== null) this.deps.cancelFrame(this.frame);
    this.frame = null;
    if (this.pending) {
      this.release(this.pending.layer);
      this.pending.finish(false);
      this.pending = null;
    }
    this.release(this.current);
    this.release(this.outgoing);
    this.current = this.outgoing = null;
    this.releaseScratch();
    this.canvas.width = this.canvas.height = 1;
  }
  releaseScratch() {
    if (!this.fadeScratch) return;
    this.fadeScratch.width = this.fadeScratch.height = 1;
    this.fadeScratch = null;
  }
};

// ../../../history/lume1.0.1/third_party/folia-major/upstream/src/components/liquid-glass/glassMediaSource.ts
var isGlassCanvas = (source) => Boolean(source && "getContext" in source);
var GlassMediaRegistry = class {
  constructor() {
    this.sources = /* @__PURE__ */ new Map();
    this.listeners = /* @__PURE__ */ new Set();
  }
  register(url, source) {
    const token = Symbol();
    this.sources.set(token, { url, source });
    this.notify();
    return () => {
      if (this.sources.delete(token)) queueMicrotask(() => this.notify());
    };
  }
  get(url) {
    return [...this.sources.values()].reverse().find((entry) => entry.url === url)?.source;
  }
  subscribe(listener) {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }
  notify() {
    for (const listener of [...this.listeners]) listener();
  }
};
var glassMediaRegistry = new GlassMediaRegistry();
export {
  AnimatedWallpaperImage,
  GlassMediaRegistry,
  WallpaperCompositor,
  isGlassCanvas
};
