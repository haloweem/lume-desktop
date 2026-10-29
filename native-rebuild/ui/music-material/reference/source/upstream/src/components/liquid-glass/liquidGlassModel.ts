export type LiquidGlassMedia = {
    kind: 'image' | 'video';
    url: string;
} | null;

export type LiquidGlassBackdrop = {
    kind: 'void' | 'image' | 'video';
    url: string | null;
};

export const resolveLiquidGlassRenderScale = (devicePixelRatio: number): number => {
    const normalizedRatio = Number.isFinite(devicePixelRatio) && devicePixelRatio > 0
        ? devicePixelRatio
        : 1;
    return Math.min(1.5, Math.max(1, normalizedRatio));
};

export const resolveLiquidGlassTargetScale = (targetScale?: number): number => {
    if (!Number.isFinite(targetScale)) return 1;
    return Math.min(1, Math.max(0.1, targetScale ?? 1));
};

export const resolveLiquidGlassShapeRadiusFactor = (
    surfaceHeight: number,
    cornerRadiusPx?: number,
    fallbackFactor = 1,
): number => {
    if (!Number.isFinite(cornerRadiusPx)) {
        return Math.min(1, Math.max(0, fallbackFactor));
    }

    const height = Number.isFinite(surfaceHeight) && surfaceHeight > 0 ? surfaceHeight : 1;
    const maxCornerRadius = height / 2;
    return Math.min(1, Math.max(0, (cornerRadiusPx ?? 0) / maxCornerRadius));
};

// Liquid Glass Studio sizes its optical canvas from the viewport before applying
// the user-editable shape values. Keeping that reference size preserves the
// exported preset's edge depth and normal strength when Lumenic renders only a
// 600x90 capsule.
export const resolveLiquidGlassOpticalCanvasSize = (
    viewportWidth: number,
    viewportHeight: number,
): number => {
    const width = Number.isFinite(viewportWidth) && viewportWidth > 0 ? viewportWidth : 0;
    const height = Number.isFinite(viewportHeight) && viewportHeight > 0 ? viewportHeight : 0;
    return Math.max(Math.min(width, height) - 150, 600);
};

export type LiquidGlassReferenceFrame = {
    opticalSizeCss: number;
    targetSizeCss: number;
    surfaceCenterCss: { x: number; y: number };
    surfaceOffsetCss: { x: number; y: number };
};

// Crop storage, not the optical reference. Integer texel boundaries preserve
// bilinear sampling phase, including fractional DPR and reduced target scales.
export const resolveLiquidGlassTextureRegion = (
    reference: LiquidGlassReferenceFrame,
    surfaceWidth: number,
    surfaceHeight: number,
    dpr: number,
    targetScale = 1,
    padding = 96,
) => {
    const full = resolveLiquidGlassReferenceTargetSize(
        reference.opticalSizeCss, dpr, padding, targetScale, reference.targetSizeCss,
    );
    const cssPerTexel = reference.targetSizeCss / full.width;
    const safePadding = Number.isFinite(padding) ? Math.max(0, padding) : 96;
    const crop = (size: number) => {
        const extent = (Number.isFinite(size) ? Math.max(1, size) : 1) + 2 * safePadding;
        const start = Math.max(0, Math.floor((reference.targetSizeCss - extent) / (2 * cssPerTexel)));
        return { start, pixels: Math.max(1, full.width - 2 * start) };
    };
    const x = crop(surfaceWidth);
    const y = crop(surfaceHeight);
    return {
        width: x.pixels,
        height: y.pixels,
        sizeCss: { width: x.pixels * cssPerTexel, height: y.pixels * cssPerTexel },
        offsetCss: {
            x: reference.surfaceOffsetCss.x + x.start * cssPerTexel,
            y: reference.surfaceOffsetCss.y + y.start * cssPerTexel,
        },
    };
};

export const resolveLiquidGlassReferenceTargetSize = (
    opticalSizeCss: number,
    devicePixelRatio: number,
    overscanCssPx: number,
    targetScale?: number,
    targetSizeCss?: number,
): { width: number; height: number } => {
    const dpr = resolveLiquidGlassRenderScale(devicePixelRatio);
    const opticalSize = Number.isFinite(opticalSizeCss) && opticalSizeCss > 0 ? opticalSizeCss : 600;
    const overscan = Number.isFinite(overscanCssPx) && overscanCssPx >= 0 ? overscanCssPx : 0;
    const referenceSize = Number.isFinite(targetSizeCss) && (targetSizeCss ?? 0) > 0
        ? targetSizeCss as number
        : opticalSize + overscan * 2;
    const fullResolutionSize = Math.round(referenceSize * dpr);
    const physicalSize = Math.round(fullResolutionSize * resolveLiquidGlassTargetScale(targetScale));
    return { width: Math.max(1, physicalSize), height: Math.max(1, physicalSize) };
};

// Studio renders the background on its complete square canvas. The player capsule
// samples the centered strip of that canvas, so the reference origin must be
// derived from the capsule center rather than from its top-left corner.
export const resolveLiquidGlassReferenceFrame = ({
    viewportWidth,
    viewportHeight,
    surfaceX,
    surfaceY,
    surfaceWidth,
    surfaceHeight,
    devicePixelRatio,
    overscanCssPx = 96,
}: {
    viewportWidth: number;
    viewportHeight: number;
    surfaceX: number;
    surfaceY: number;
    surfaceWidth: number;
    surfaceHeight: number;
    devicePixelRatio: number;
    overscanCssPx?: number;
}): LiquidGlassReferenceFrame => {
    const opticalSizeCss = resolveLiquidGlassOpticalCanvasSize(viewportWidth, viewportHeight);
    const overscan = Math.max(0, overscanCssPx);
    // The texture must cover the entire surface. A wide player can exceed the
    // square Studio reference, and sampling past its edges stretches clamped pixels.
    const normalizedSurfaceWidth = Number.isFinite(surfaceWidth) && surfaceWidth > 0 ? surfaceWidth : 0;
    const normalizedSurfaceHeight = Number.isFinite(surfaceHeight) && surfaceHeight > 0 ? surfaceHeight : 0;
    const targetSizeCss = Math.max(opticalSizeCss, normalizedSurfaceWidth, normalizedSurfaceHeight) + overscan * 2;
    const surfaceCenterCss = {
        x: surfaceX + surfaceWidth / 2,
        y: surfaceY + surfaceHeight / 2,
    };
    const surfaceOffsetCss = {
        x: surfaceCenterCss.x - targetSizeCss / 2,
        y: surfaceCenterCss.y - targetSizeCss / 2,
    };
    // Normalize invalid DPR values at the same boundary as the renderer.
    resolveLiquidGlassRenderScale(devicePixelRatio);
    return { opticalSizeCss, targetSizeCss, surfaceCenterCss, surfaceOffsetCss };
};

export const resolveLiquidGlassBackdrop = (
    mode: string | null | undefined,
    media: LiquidGlassMedia,
): LiquidGlassBackdrop => (
    mode === 'wallpaper' && media
        ? { kind: media.kind, url: media.url }
        : { kind: 'void', url: null }
);

export const shouldContinuouslyRenderLiquidGlass = ({
    visible,
    mediaKind,
    mediaReady,
}: {
    visible: boolean;
    mediaKind: LiquidGlassBackdrop['kind'];
    reducedMotion: boolean;
    mediaReady: boolean;
}): boolean => visible && mediaKind === 'video' && mediaReady;
