// CSS propagates root overflow (or body overflow when root is visible) to the
// viewport. Fixed app shells often leave body's layout box at zero height.
export const getSceneOverflowBounds = (element: HTMLElement) => {
    const root = document.documentElement;
    const rootStyle = root && window.getComputedStyle(root);
    if (element === root || (element === document.body
        && rootStyle?.overflowX === 'visible' && rootStyle.overflowY === 'visible')) {
        return { left: 0, top: 0, right: window.innerWidth, bottom: window.innerHeight };
    }
    return element.getBoundingClientRect();
};

const establishesFixedContainer = (style: CSSStyleDeclaration) => (
    ['transform', 'perspective', 'filter', 'backdropFilter'].some(key => {
        const value = style[key as keyof CSSStyleDeclaration];
        return value && value !== 'none';
    }) || /transform|perspective|filter/.test(style.willChange)
    || /paint|layout|strict|content/.test(style.contain)
);

// Positioned descendants escape overflow clips between themselves and their
// containing block. Apply clips per leaf, not indiscriminately to a whole tree.
export const getSceneLeafClip = (element: HTMLElement, bounds: { left: number; top: number; right: number; bottom: number }) => {
    const clip = { ...bounds };
    let position = window.getComputedStyle(element).position;
    let seekingContainer = position === 'fixed' || position === 'absolute';
    for (let node: HTMLElement | null = element; node; node = node.parentElement) {
        const style = window.getComputedStyle(node);
        if (node !== element && seekingContainer) {
            const viewport = node === document.documentElement || node === document.body;
            if (!viewport && !establishesFixedContainer(style)
                && !(position === 'absolute' && style.position && style.position !== 'static')) continue;
            seekingContainer = false;
        }
        const clipsX = /^(hidden|clip|scroll|auto)$/.test(style.overflowX);
        const clipsY = /^(hidden|clip|scroll|auto)$/.test(style.overflowY);
        if (clipsX || clipsY) {
            const rect = getSceneOverflowBounds(node);
            if (clipsX) { clip.left = Math.max(clip.left, rect.left); clip.right = Math.min(clip.right, rect.right); }
            if (clipsY) { clip.top = Math.max(clip.top, rect.top); clip.bottom = Math.min(clip.bottom, rect.bottom); }
        }
        if (node !== element && (style.position === 'fixed' || style.position === 'absolute')) {
            position = style.position;
            seekingContainer = true;
        }
    }
    return clip;
};
