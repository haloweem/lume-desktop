import { SNOW_SHADER_SOURCE } from './shaders';

const replaceShaderSection = (source: string, pattern: RegExp, replacement: string) => {
    const overlay = source.replace(pattern, replacement);
    if (overlay === source) {
        throw new Error('Unable to create transparent visualizer effect shader.');
    }
    return overlay;
};

export const SNOW_OVERLAY_SHADER_SOURCE = replaceShaderSection(
    SNOW_SHADER_SOURCE,
    /    vec3 col =[\s\S]*?    fragColor = vec4\(col, 1\.0\);/,
    `    float alpha = clamp(snow * 0.9, 0.0, 0.92);
    fragColor = vec4(vec3(1.0), alpha);`,
);
