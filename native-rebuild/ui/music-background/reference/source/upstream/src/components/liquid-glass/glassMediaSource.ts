export type GlassMediaElement = HTMLImageElement | HTMLVideoElement | HTMLCanvasElement;
export const isGlassCanvas = (source: GlassMediaElement | null): source is HTMLCanvasElement => Boolean(source && 'getContext' in source);
type MediaDescriptor = { kind: 'image' | 'video'; url: string };

/** Visible media owns playback. Glass consumers borrow pixels, never transport. */
export class GlassMediaRegistry {
    private sources = new Map<symbol, { url: string; source: GlassMediaElement }>();
    private listeners = new Set<() => void>();
    register(url: string, source: GlassMediaElement) {
        const token = Symbol();
        this.sources.set(token, { url, source });
        this.notify();
        // A React tree may detach the visible source just before disposing its
        // glass consumers. Wait until that commit ends before opening fallback.
        return () => { if (this.sources.delete(token)) queueMicrotask(() => this.notify()); };
    }
    get(url: string): GlassMediaElement | undefined {
        return [...this.sources.values()].reverse().find(entry => entry.url === url)?.source;
    }
    subscribe(listener: () => void) {
        this.listeners.add(listener);
        return () => { this.listeners.delete(listener); };
    }
    private notify() { for (const listener of [...this.listeners]) listener(); }
}

export const glassMediaRegistry = new GlassMediaRegistry();

export function bindGlassMedia(descriptor: MediaDescriptor, options: {
    hidden: boolean;
    onFrame: (source: GlassMediaElement | null, borrowed: boolean) => void;
    registry?: GlassMediaRegistry;
    create?: (kind: MediaDescriptor['kind']) => HTMLImageElement | HTMLVideoElement;
}) {
    const registry = options.registry ?? glassMediaRegistry;
    const create = options.create ?? (kind => kind === 'video' ? document.createElement('video') : new Image());
    let source: GlassMediaElement | null = null;
    let borrowed = false;
    let hidden = options.hidden;
    let disposed = false;
    const events = ['load', 'loadeddata', 'resize', 'seeked', 'playing', 'pause', 'error', 'lume-glass-media-frame'];
    const video = () => descriptor.kind === 'video' && !isGlassCanvas(source) ? source as HTMLVideoElement | null : null;
    const emit = () => {
        if (disposed || !source) return;
        const ready = isGlassCanvas(source) ? Boolean(source.dataset.glassFrameVersion) : descriptor.kind === 'video'
            ? (source as HTMLVideoElement).readyState >= 2
            : (source as HTMLImageElement).naturalWidth > 0;
        options.onFrame(ready ? source : null, borrowed);
    };
    const startOwned = () => {
        const media = video();
        if (!borrowed && !hidden && media?.paused) void media.play().catch(() => undefined);
    };
    const onMediaEvent = (event: Event) => {
        if (event.type === 'error') { options.onFrame(null, borrowed); return; }
        if (event.type === 'loadeddata') startOwned();
        emit();
    };
    const release = () => {
        if (!source) return;
        events.forEach(event => source?.removeEventListener(event, onMediaEvent));
        if (!borrowed && !isGlassCanvas(source)) {
            const media = video();
            media?.pause();
            source.removeAttribute('src');
            media?.load();
        }
        source = null;
    };
    const update = () => {
        if (disposed) return;
        const shared = registry.get(descriptor.url);
        if ((shared && shared === source) || (!shared && source && !borrowed)) return;
        release();
        borrowed = Boolean(shared);
        source = shared ?? create(descriptor.kind);
        events.forEach(event => source?.addEventListener(event, onMediaEvent));
        if (!borrowed && !isGlassCanvas(source)) {
            if (/^https?:\/\//i.test(descriptor.url)) source.crossOrigin = 'anonymous';
            const media = video();
            if (media) {
                media.muted = true; media.loop = true; media.playsInline = true; media.preload = 'auto';
            }
            source.src = descriptor.url;
            media?.load();
            startOwned();
        }
        emit();
    };
    const unsubscribe = registry.subscribe(update);
    update();
    return {
        setHidden(value: boolean) {
            if (disposed || hidden === value) return;
            hidden = value;
            if (hidden && !borrowed) video()?.pause();
            else if (!hidden) { startOwned(); emit(); }
        },
        dispose() {
            if (disposed) return;
            disposed = true;
            unsubscribe();
            release();
        },
    };
}
