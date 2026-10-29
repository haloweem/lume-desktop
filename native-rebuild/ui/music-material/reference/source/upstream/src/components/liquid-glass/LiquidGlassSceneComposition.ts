import { getSceneLeafClip } from './sceneOverflow';

// Composes selected scene leaves using their DOM opacity groups and paint order.
// This is not a general DOM rasterizer: leaves own their text/canvas painting.
export type SceneClip = { left: number; top: number; right: number; bottom: number };
export type SceneLeaf = { element: HTMLElement; paint: (context: CanvasRenderingContext2D) => boolean };
type SceneNode = { element: HTMLElement; children: SceneNode[]; paint?: SceneLeaf['paint']; style: CSSStyleDeclaration };

const opacityOf = (style: CSSStyleDeclaration) => {
    const value = Number.parseFloat(style.opacity);
    return Number.isFinite(value) ? Math.max(0, Math.min(1, value)) : 1;
};
const blendOf = (style: CSSStyleDeclaration): GlobalCompositeOperation => (
    style.mixBlendMode && style.mixBlendMode !== 'normal'
        ? style.mixBlendMode as GlobalCompositeOperation : 'source-over'
);
const hasZIndex = (element: HTMLElement, style: CSSStyleDeclaration) => (
    (style.position && style.position !== 'static')
    || (element.parentElement && /flex|grid/.test(window.getComputedStyle(element.parentElement).display))
);
const stackingContext = (element: HTMLElement, style: CSSStyleDeclaration) => (
    !element.parentElement || opacityOf(style) < 1 || blendOf(style) !== 'source-over'
    || style.isolation === 'isolate' || style.position === 'fixed' || style.position === 'sticky'
    || (hasZIndex(element, style) && style.zIndex && style.zIndex !== 'auto')
    || ['transform', 'perspective', 'filter', 'backdropFilter', 'clipPath', 'maskImage'].some(key => {
        const value = style[key as keyof CSSStyleDeclaration];
        return value && value !== 'none';
    }) || /transform|opacity|filter/.test(style.willChange) || /paint|layout|strict|content/.test(style.contain)
);

export class LiquidGlassSceneComposition {
    private scratch: HTMLCanvasElement[] = [];

    paint(context: CanvasRenderingContext2D, leaves: SceneLeaf[], bounds: SceneClip, backdrop?: () => void) {
        const nodes = new Map<HTMLElement, SceneNode>();
        const roots: SceneNode[] = [];
        const selected = new Set(leaves.map(leaf => leaf.element));
        const ensureNode = (element: HTMLElement): SceneNode => {
            const found = nodes.get(element);
            if (found) return found;
            const node: SceneNode = { element, children: [], style: window.getComputedStyle(element) };
            nodes.set(element, node);
            let parent = element.parentElement;
            while (parent && !selected.has(parent) && !stackingContext(parent, window.getComputedStyle(parent))) parent = parent.parentElement;
            if (parent) ensureNode(parent).children.push(node);
            else roots.push(node);
            return node;
        };
        for (const leaf of leaves) ensureNode(leaf.element).paint = leaf.paint;
        // Blend modes must see the wallpaper, not a transparent foreground later
        // source-overlaid onto the wallpaper by the glass shader.
        if ([...nodes.values()].some(node => blendOf(node.style) !== 'source-over')) backdrop?.();
        const width = context.canvas.width;
        const height = context.canvas.height;
        const transform = context.getTransform();
        let scratchUsed = 0;
        let hasContent = false;
        const zIndex = (node: SceneNode) => hasZIndex(node.element, node.style) ? Number.parseInt(node.style.zIndex, 10) || 0 : 0;
        const paintNode = (node: SceneNode, target: CanvasRenderingContext2D, inheritedClip: SceneClip, depth: number) => {
            const { style, element } = node;
            const opacity = opacityOf(style);
            if (style.display === 'none' || style.visibility === 'hidden' || opacity <= 0.001) return;
            // Non-stacking ancestors still control visibility and leaf clipping.
            for (let parent = element.parentElement; parent; parent = parent.parentElement) {
                const parentStyle = window.getComputedStyle(parent);
                if (parentStyle.display === 'none' || parentStyle.visibility === 'hidden') return;
            }
            const blend = blendOf(style);
            const isolated = node.children.length > 0 && (opacity < 1 || blend !== 'source-over' || style.isolation === 'isolate');
            let output = target;
            if (isolated) {
                const canvas = this.scratch[depth] ?? (this.scratch[depth] = document.createElement('canvas'));
                if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
                const scratchContext = canvas.getContext('2d');
                if (!scratchContext) return;
                scratchContext.setTransform(1, 0, 0, 1, 0, 0);
                scratchContext.clearRect(0, 0, width, height);
                scratchContext.setTransform(transform);
                output = scratchContext;
                scratchUsed = Math.max(scratchUsed, depth + 1);
            }
            output.save();
            output.globalAlpha = isolated ? 1 : opacity;
            output.globalCompositeOperation = isolated ? 'source-over' : blend;
            if (node.paint) {
                const clip = getSceneLeafClip(element, inheritedClip);
                if (clip.right > clip.left && clip.bottom > clip.top) {
                    output.save();
                    output.beginPath();
                    output.rect(clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top);
                    output.clip();
                    hasContent = node.paint(output) || hasContent;
                    output.restore();
                }
            }
            // Stable document order breaks ties; z-index orders sibling layers.
            node.children.sort((a, b) => zIndex(a) - zIndex(b)
                || (a.element.compareDocumentPosition(b.element) & Node.DOCUMENT_POSITION_PRECEDING ? 1 : -1));
            for (const child of node.children) paintNode(child, output, inheritedClip, depth + Number(isolated));
            output.restore();
            if (isolated) {
                target.save();
                target.setTransform(1, 0, 0, 1, 0, 0);
                target.globalAlpha = opacity;
                target.globalCompositeOperation = blend;
                target.drawImage(output.canvas, 0, 0);
                target.restore();
            }
        };
        for (const root of roots) paintNode(root, context, bounds, 0);
        // Retain only buffers needed by the current group depth; scene switches
        // must not leave the largest previous tree's canvas storage alive.
        this.scratch.length = scratchUsed;
        return hasContent;
    }
}
