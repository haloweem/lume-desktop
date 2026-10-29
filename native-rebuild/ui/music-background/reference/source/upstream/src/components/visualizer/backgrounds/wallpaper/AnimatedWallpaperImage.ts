type Decoder = {
    tracks: { ready: Promise<void>; selectedTrack: { frameCount: number; repetitionCount: number } | null };
    decode: (options: { frameIndex: number }) => Promise<{ image: VideoFrame }>;
    close: () => void;
};
type DecoderClass = { new(options: { data: ArrayBuffer; type: string }): Decoder };

// drawImage(HTMLImageElement) exposes only an animation's first frame. Decode
// frames explicitly so the visible wallpaper and borrowed glass stay identical.
export class AnimatedWallpaperImage {
    readonly canvas = document.createElement('canvas');
    private context: CanvasRenderingContext2D;
    private visible = true;
    private closed = false;
    private timer: number | undefined;
    private index = 0;
    private loops = 0;
    private duration = 100;
    private decoding = false;
    private constructor(private decoder: Decoder, private count: number, private repeat: number, private onFrame: () => void) {
        const context = this.canvas.getContext('2d');
        if (!context) throw new Error('Animated wallpaper canvas unavailable');
        this.context = context;
    }

    static async load(url: string, mimeType: string | undefined, signal: AbortSignal, onFrame: () => void) {
        const type = mimeType || (/\.gif(?:[?#]|$)/i.test(url) ? 'image/gif' : /\.webp(?:[?#]|$)/i.test(url) ? 'image/webp' : '');
        if (type !== 'image/gif' && type !== 'image/webp' && type !== 'image/png') return null;
        const Constructor = (globalThis as typeof globalThis & { ImageDecoder?: DecoderClass }).ImageDecoder;
        if (!Constructor) return null;
        const response = await fetch(url, { signal });
        if (!response.ok) throw new Error('Animated wallpaper read failed');
        const data = await response.arrayBuffer(); signal.throwIfAborted();
        const decoder = new Constructor({ data, type });
        try {
            await decoder.tracks.ready; signal.throwIfAborted();
            const track = decoder.tracks.selectedTrack;
            if (!track || track.frameCount <= 1) { decoder.close(); return null; }
            const animation = new AnimatedWallpaperImage(decoder, track.frameCount, track.repetitionCount, onFrame);
            await animation.decode(); signal.throwIfAborted();
            return animation;
        } catch (error) { decoder.close(); throw error; }
    }

    private async decode() {
        this.decoding = true;
        try {
            const { image } = await this.decoder.decode({ frameIndex: this.index });
            try {
                if (this.closed) return;
                if (this.canvas.width !== image.displayWidth) this.canvas.width = image.displayWidth;
                if (this.canvas.height !== image.displayHeight) this.canvas.height = image.displayHeight;
                this.context.clearRect(0, 0, this.canvas.width, this.canvas.height);
                this.context.drawImage(image, 0, 0);
                this.duration = Math.max(20, (image.duration ?? 100000) / 1000);
                this.onFrame();
            } finally { image.close(); }
        } finally { this.decoding = false; }
    }

    private schedule() {
        if (this.closed || !this.visible || this.timer !== undefined || this.decoding) return;
        if (this.index === this.count - 1 && this.loops >= this.repeat) return;
        this.timer = window.setTimeout(() => {
            this.timer = undefined;
            if (this.closed || !this.visible) return;
            this.index++;
            if (this.index >= this.count) { this.index = 0; this.loops++; }
            void this.decode().then(() => this.schedule()).catch(() => this.close());
        }, this.duration);
    }

    setVisible(visible: boolean) {
        this.visible = visible;
        if (!visible && this.timer !== undefined) { clearTimeout(this.timer); this.timer = undefined; }
        if (visible) this.schedule();
    }

    close() {
        if (this.closed) return;
        this.closed = true;
        if (this.timer !== undefined) clearTimeout(this.timer);
        this.timer = undefined; this.decoder.close();
        this.canvas.width = this.canvas.height = 1;
    }
}
