export interface RainWindowTextureSizeOptions {
    baseScale: number;
    minWidth: number;
    minHeight: number;
    maxWidth: number;
    maxHeight: number;
}

// Keeps both texture axes on one scale so Rain Window matches the full-bleed wallpaper crop.
export const resolveRainWindowTextureSize = (
    viewportWidth: number,
    viewportHeight: number,
    options: RainWindowTextureSizeOptions,
) => {
    const width = Math.max(1, viewportWidth);
    const height = Math.max(1, viewportHeight);
    const minimumScale = Math.max(
        options.baseScale,
        options.minWidth / width,
        options.minHeight / height,
    );
    const maximumScale = Math.min(
        options.maxWidth / width,
        options.maxHeight / height,
    );
    const scale = Math.max(0, Math.min(minimumScale, maximumScale));

    return {
        width: Math.max(1, Math.round(width * scale)),
        height: Math.max(1, Math.round(height * scale)),
    };
};
