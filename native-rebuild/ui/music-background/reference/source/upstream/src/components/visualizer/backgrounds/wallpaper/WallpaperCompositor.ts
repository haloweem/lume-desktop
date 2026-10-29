import { AnimatedWallpaperImage } from './AnimatedWallpaperImage';

type Media = { kind: 'video' | 'image'; url: string; mimeType?: string };
type Layer = {
    source: HTMLImageElement | HTMLVideoElement | HTMLCanvasElement;
    kind: Media['kind'] | 'snapshot'; color: string; url: string;
    removeListeners?: () => void;
    videoFrame?: number;
    animation?: AnimatedWallpaperImage;
    loadAbort?: AbortController;
};
type Dependencies = {
    createMedia: (kind: Media['kind']) => HTMLImageElement | HTMLVideoElement;
    createCanvas: () => HTMLCanvasElement;
    requestFrame: typeof requestAnimationFrame;
    cancelFrame: typeof cancelAnimationFrame;
    now: () => number;
};

// CSS ease-in / ease-out used by the existing 420ms shell transition.
function ease(t: number, incoming: boolean) {
    const x1 = incoming ? 0 : 0.42, x2 = incoming ? 0.58 : 1;
    let lo = 0, hi = 1;
    for (let i = 0; i < 16; i++) {
        const u = (lo + hi) / 2, v = 1 - u;
        const x = 3 * v * v * u * x1 + 3 * v * u * u * x2 + u * u * u;
        if (x < t) lo = u; else hi = u;
    }
    const u = (lo + hi) / 2;
    return 3 * (1 - u) * u * u + u * u * u;
}

/** Visible wallpaper and glass use this exact, already composited canvas. */
export class WallpaperCompositor {
    private readonly deps: Dependencies;
    private readonly context: CanvasRenderingContext2D;
    private current: Layer | null = null;
    private outgoing: Layer | null = null;
    private pending: { layer: Layer; finish: (ok: boolean) => void } | null = null;
    private frame: number | null = null;
    private visible = true;
    private disposed = false;
    private contextLost = false;
    private fadeStart = 0;
    private hiddenAt = 0;
    private reducedMotion = false;
    private frameVersion = 0;
    private fadeScratch: HTMLCanvasElement | null = null;

    constructor(private canvas: HTMLCanvasElement, dependencies: Partial<Dependencies> = {}) {
        this.deps = {
            createMedia: kind => kind === 'video' ? document.createElement('video') : new Image(),
            createCanvas: () => document.createElement('canvas'),
            requestFrame: callback => requestAnimationFrame(callback),
            cancelFrame: id => cancelAnimationFrame(id), now: () => performance.now(), ...dependencies,
        };
        const context = canvas.getContext('2d', { alpha: false });
        if (!context) throw new Error('WALLPAPER_CANVAS_UNAVAILABLE');
        this.context = context;
        canvas.addEventListener('contextlost', this.onContextLost);
        canvas.addEventListener('contextrestored', this.onContextRestored);
    }

    private onContextLost = (event: Event) => {
        event.preventDefault(); this.contextLost = true;
        if (this.frame !== null) this.deps.cancelFrame(this.frame);
        this.frame = null;
    };

    private onContextRestored = () => {
        this.contextLost = false; this.releaseScratch(); this.invalidate();
    };

    setMedia(media: Media, color: string): Promise<boolean> {
        if (this.disposed) return Promise.resolve(false);
        if (this.pending) { this.release(this.pending.layer); this.pending.finish(false); this.pending = null; }
        if (this.current?.url === media.url && this.current.kind === media.kind) {
            this.current.color = color; this.invalidate(); return Promise.resolve(true);
        }
        return new Promise(resolve => {
            const source = this.deps.createMedia(media.kind);
            const layer: Layer = { source, kind: media.kind, url: media.url, color };
            let settled = false;
            const finish = (ok: boolean) => { if (!settled) { settled = true; resolve(ok); } };
            const ready = async () => {
                if (this.disposed || this.pending?.layer !== layer) return;
                if (media.kind === 'image') {
                    const controller = new AbortController(); layer.loadAbort = controller;
                    try { layer.animation = await AnimatedWallpaperImage.load(media.url, media.mimeType, controller.signal, this.invalidate) ?? undefined; }
                    catch { /* Keep a decoded still when animated decoding is unavailable. */ }
                    if (this.disposed || this.pending?.layer !== layer) { layer.animation?.close(); return; }
                }
                if (this.outgoing) {
                    // A second switch interrupts the fade at its visible pixels.
                    const frozen = this.deps.createCanvas();
                    frozen.width = this.canvas.width; frozen.height = this.canvas.height;
                    frozen.getContext('2d')?.drawImage(this.canvas, 0, 0);
                    this.release(this.outgoing); this.release(this.current);
                    this.current = { source: frozen, kind: 'snapshot', url: '', color };
                }
                this.outgoing = this.current;
                this.current = layer; this.pending = null;
                this.fadeStart = this.deps.now();
                if (!this.visible) this.hiddenAt = this.fadeStart;
                this.syncVideo(layer); layer.animation?.setVisible(this.visible); this.invalidate(); finish(true);
            };
            const failed = () => {
                if (this.pending?.layer !== layer) return;
                this.pending = null; this.release(layer); finish(false);
            };
            const refresh = () => this.invalidate();
            const readyEvent = media.kind === 'video' ? 'loadeddata' : 'load';
            source.addEventListener(readyEvent, ready, { once: true });
            source.addEventListener('error', failed, { once: true });
            source.addEventListener('seeked', refresh);
            layer.removeListeners = () => {
                source.removeEventListener(readyEvent, ready);
                source.removeEventListener('error', failed); source.removeEventListener('seeked', refresh);
            };
            this.pending = { layer, finish };
            if (/^https?:\/\//i.test(media.url)) source.crossOrigin = 'anonymous';
            if (media.kind === 'video') {
                const video = source as HTMLVideoElement;
                video.muted = true; video.loop = true; video.playsInline = true; video.preload = 'auto';
            }
            source.src = media.url;
        });
    }

    setVisible(visible: boolean) {
        if (this.disposed || visible === this.visible) return;
        this.visible = visible;
        if (!visible) {
            this.hiddenAt = this.deps.now();
            if (this.frame !== null) this.deps.cancelFrame(this.frame);
            this.frame = null;
        } else this.fadeStart += this.deps.now() - this.hiddenAt;
        [this.current, this.outgoing].forEach(layer => { if (layer) { this.syncVideo(layer); layer.animation?.setVisible(visible); } });
        this.invalidate();
    }

    setReducedMotion(value: boolean) { this.reducedMotion = value; this.invalidate(); }

    resize(width: number, height: number) {
        if (this.disposed || !Number.isFinite(width) || !Number.isFinite(height)) return;
        const w = Math.max(1, Math.round(width)), h = Math.max(1, Math.round(height));
        if (w === this.canvas.width && h === this.canvas.height) return;
        this.canvas.width = w; this.canvas.height = h; this.invalidate();
    }

    private syncVideo(layer: Layer) {
        if (layer.kind !== 'video') return;
        const video = layer.source as HTMLVideoElement;
        if (this.visible) {
            if (video.paused) void video.play().catch(() => {});
            this.armVideoFrame(layer);
        } else {
            video.pause();
            if (layer.videoFrame !== undefined) video.cancelVideoFrameCallback?.(layer.videoFrame);
            layer.videoFrame = undefined;
        }
    }

    private armVideoFrame(layer: Layer) {
        const video = layer.source as HTMLVideoElement;
        if (video.requestVideoFrameCallback && layer.videoFrame === undefined) {
            layer.videoFrame = video.requestVideoFrameCallback(() => {
                layer.videoFrame = undefined;
                if (this.disposed || !this.visible) return;
                this.invalidate(); this.armVideoFrame(layer);
            });
        }
    }

    private release(layer: Layer | null) {
        if (!layer) return;
        layer.loadAbort?.abort(); layer.animation?.close();
        layer.removeListeners?.();
        if (layer.kind === 'video') {
            const video = layer.source as HTMLVideoElement;
            if (layer.videoFrame !== undefined) video.cancelVideoFrameCallback?.(layer.videoFrame);
            video.pause(); video.removeAttribute('src'); video.load();
        } else if (layer.kind === 'snapshot') {
            (layer.source as HTMLCanvasElement).width = 1;
            (layer.source as HTMLCanvasElement).height = 1;
        } else layer.source.removeAttribute('src');
    }

    private drawLayer(layer: Layer, opacity: number) {
        const source = layer.animation?.canvas ?? layer.source;
        const kind = layer.animation ? 'snapshot' : layer.kind;
        const width = kind === 'video' ? (source as HTMLVideoElement).videoWidth
            : kind === 'image' ? (source as HTMLImageElement).naturalWidth : (source as HTMLCanvasElement).width;
        const height = kind === 'video' ? (source as HTMLVideoElement).videoHeight
            : kind === 'image' ? (source as HTMLImageElement).naturalHeight : (source as HTMLCanvasElement).height;
        // CSS layer opacity applies after the media and its base color have
        // composed. Applying opacity to each draw would darken the fade twice.
        let target = this.context;
        if (opacity < 1) {
            this.fadeScratch ??= this.deps.createCanvas();
            if (this.fadeScratch.width !== this.canvas.width) this.fadeScratch.width = this.canvas.width;
            if (this.fadeScratch.height !== this.canvas.height) this.fadeScratch.height = this.canvas.height;
            const context = this.fadeScratch.getContext('2d', { alpha: false });
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

    private invalidate = () => {
        if (this.disposed || this.contextLost || !this.visible || this.frame !== null) return;
        this.frame = this.deps.requestFrame(this.draw);
    };

    private draw = () => {
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
            this.release(this.outgoing); this.outgoing = null;
            this.releaseScratch();
            this.drawLayer(this.current, 1);
        }
        this.context.globalAlpha = 1;
        this.canvas.dataset.glassFrameVersion = String(++this.frameVersion);
        this.canvas.dispatchEvent(new Event('lume-glass-media-frame'));
        const fallbackVideoClock = [this.current, this.outgoing].some(layer => layer?.kind === 'video'
            && !(layer.source as HTMLVideoElement).requestVideoFrameCallback && !(layer.source as HTMLVideoElement).paused);
        if (this.outgoing || fallbackVideoClock) this.invalidate();
    };

    dispose() {
        if (this.disposed) return;
        this.disposed = true;
        this.canvas.removeEventListener('contextlost', this.onContextLost);
        this.canvas.removeEventListener('contextrestored', this.onContextRestored);
        if (this.frame !== null) this.deps.cancelFrame(this.frame);
        this.frame = null;
        if (this.pending) { this.release(this.pending.layer); this.pending.finish(false); this.pending = null; }
        this.release(this.current); this.release(this.outgoing); this.current = this.outgoing = null;
        this.releaseScratch();
        this.canvas.width = this.canvas.height = 1;
    }

    private releaseScratch() {
        if (!this.fadeScratch) return;
        this.fadeScratch.width = this.fadeScratch.height = 1; this.fadeScratch = null;
    }
}
