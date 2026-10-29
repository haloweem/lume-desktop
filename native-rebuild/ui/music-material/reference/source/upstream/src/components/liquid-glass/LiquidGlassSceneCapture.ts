import { getLiquidGlassWebGLSceneSnapshots, isLiquidGlassSceneCanvasRegistered } from './LiquidGlassWebGLSceneBridge';
import { LiquidGlassSceneComposition, type SceneLeaf } from './LiquidGlassSceneComposition';
import { getSceneLeafClip } from './sceneOverflow';

const SCENE_SELECTOR = '[data-lumenic-liquid-glass-scene]';
const CANVAS_SELECTOR = [
    'canvas[data-lumenic-liquid-glass-scene-effect]',
    'canvas[data-lumenic-liquid-glass-scene-lyrics]',
    'canvas[data-visualizer-canvas="mineradio-3d"]',
].join(', ');
const SCENE_PADDING_PX = 96;

type SceneFrame = {
    canvas: HTMLCanvasElement;
    hasContent: boolean;
    nextFrameDelay: number;
    bounds: SceneBounds;
    needsPolling: boolean;
};

type SceneBounds = {
    left: number;
    top: number;
    right: number;
    bottom: number;
};

const intersects = (rect: SceneBounds, bounds: SceneBounds) => (
    rect.right > bounds.left
    && rect.left < bounds.right
    && rect.bottom > bounds.top
    && rect.top < bounds.bottom
);

const hasDirectText = (element: HTMLElement) => Array.from(element.childNodes).some(node => (
    node.nodeType === Node.TEXT_NODE && Boolean(node.textContent?.trim())
));

const resolveTextAlign = (value: string): CanvasTextAlign => {
    if (value === 'center' || value === 'right') return value;
    if (value === 'end') return 'right';
    return 'left';
};

const drawText = (
    context: CanvasRenderingContext2D,
    element: HTMLElement,
    rect: DOMRect,
) => {
    const style = window.getComputedStyle(element);
    const text = Array.from(element.childNodes)
        .filter(node => node.nodeType === Node.TEXT_NODE)
        .map(node => node.textContent?.trim() ?? '')
        .filter(Boolean)
        .join(' ');
    if (!text || style.visibility === 'hidden' || style.display === 'none') return false;

    // DOM bounds still include offscreen scroll children. Respect the same ancestor
    // clips as the browser before putting those children into the glass texture.
    const visible = getSceneLeafClip(element, rect);
    if (visible.right <= visible.left || visible.bottom <= visible.top) return false;

    const fontSize = Number.parseFloat(style.fontSize);
    if (!Number.isFinite(fontSize) || fontSize <= 0) return false;
    const lineHeight = Number.parseFloat(style.lineHeight) || fontSize * 1.2;
    const textAlign = resolveTextAlign(style.textAlign);
    const textX = textAlign === 'center'
        ? rect.left + rect.width / 2
        : textAlign === 'right'
            ? rect.right
            : rect.left;

    context.save();
    context.beginPath();
    context.rect(visible.left, visible.top, visible.right - visible.left, visible.bottom - visible.top);
    context.clip();
    context.fillStyle = style.color;
    context.font = `${style.fontStyle} ${style.fontWeight} ${style.fontSize}/${style.lineHeight} ${style.fontFamily}`;
    context.textAlign = textAlign;
    context.textBaseline = 'middle';
    context.direction = style.direction as CanvasDirection;
    context.letterSpacing = style.letterSpacing;
    context.shadowColor = 'transparent';
    context.fillText(text, textX, rect.top + Math.min(rect.height, lineHeight) / 2, rect.width);
    context.restore();
    return true;
};

/**
 * Captures visible lyrics/effects in a local texture. Normally transparent;
 * blend modes first include the actual backdrop so their colors stay correct.
 */
export class LiquidGlassSceneCapture {
    readonly canvas = document.createElement('canvas');
    private readonly context = this.canvas.getContext('2d', { alpha: true });
    private readonly composition = new LiquidGlassSceneComposition();
    capture(surfaceRect: DOMRect, _now = performance.now(), backdrop?: { source: CanvasImageSource; ratio: number }): SceneFrame {
        const bounds: SceneBounds = {
            left: Math.max(0, surfaceRect.left - SCENE_PADDING_PX),
            top: Math.max(0, surfaceRect.top - SCENE_PADDING_PX),
            right: Math.min(window.innerWidth, surfaceRect.right + SCENE_PADDING_PX),
            bottom: Math.min(window.innerHeight, surfaceRect.bottom + SCENE_PADDING_PX),
        };
        const scale = Math.max(1, window.devicePixelRatio || 1);
        const width = Math.max(1, Math.round((bounds.right - bounds.left) * scale));
        const height = Math.max(1, Math.round((bounds.bottom - bounds.top) * scale));
        if (this.canvas.width !== width || this.canvas.height !== height) {
            this.canvas.width = width;
            this.canvas.height = height;
        }
        const context = this.context;
        if (!context) return { canvas: this.canvas, hasContent: false, nextFrameDelay: 0, bounds, needsPolling: false };
        context.clearRect(0, 0, width, height);
        context.save();
        context.scale(width / Math.max(1, bounds.right - bounds.left), height / Math.max(1, bounds.bottom - bounds.top));
        context.translate(-bounds.left, -bounds.top);
        context.beginPath();
        context.rect(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top);
        context.clip();

        let needsPolling = false;
        const leaves: SceneLeaf[] = [];
        const snapshots = new Map(getLiquidGlassWebGLSceneSnapshots().map(snapshot => [snapshot.source, snapshot]));
        const sceneCanvases = [...new Set([
            ...document.querySelectorAll<HTMLCanvasElement>(CANVAS_SELECTOR), ...snapshots.keys(),
        ])];
        sceneCanvases.forEach(canvas => {
            const rect = canvas.getBoundingClientRect();
            if (!intersects(rect, bounds) || canvas.width < 1 || canvas.height < 1) return;
            const snapshot = snapshots.get(canvas);
            leaves.push({ element: canvas, paint: context => {
              if (!isLiquidGlassSceneCanvasRegistered(canvas)) needsPolling = true;
              try {
                if (snapshot) {
                    context.drawImage(snapshot.canvas, snapshot.bounds.left, snapshot.bounds.top,
                        snapshot.bounds.right - snapshot.bounds.left, snapshot.bounds.bottom - snapshot.bounds.top);
                } else {
                    context.drawImage(canvas, rect.left, rect.top, rect.width, rect.height);
                }
                return true;
              } catch { return false; }
            } });
        });

        const sceneRoot = document.querySelector<HTMLElement>(SCENE_SELECTOR);
        if (sceneRoot) {
            sceneRoot.querySelectorAll<HTMLElement>('*').forEach(element => {
                if (
                    !hasDirectText(element)
                    || element.closest('[data-lumenic-liquid-glass-controls]')
                    || element.closest('[data-lumenic-liquid-glass-scene-exclude]')
                ) return;
                const rect = element.getBoundingClientRect();
                if (!intersects(rect, bounds)) return;
                leaves.push({ element, paint: context => {
                    needsPolling = true;
                    return drawText(context, element, rect);
                } });
            });
        }
        const hasContent = this.composition.paint(context, leaves, bounds, backdrop ? () => {
            const ratio = Math.max(0.0001, backdrop.ratio);
            const drawWidth = Math.max(window.innerWidth, window.innerHeight * ratio);
            const drawHeight = drawWidth / ratio;
            context.drawImage(backdrop.source, (window.innerWidth - drawWidth) / 2,
                (window.innerHeight - drawHeight) / 2, drawWidth, drawHeight);
        } : undefined);
        context.restore();
        return { canvas: this.canvas, hasContent, nextFrameDelay: 0, bounds, needsPolling };
    }
}
