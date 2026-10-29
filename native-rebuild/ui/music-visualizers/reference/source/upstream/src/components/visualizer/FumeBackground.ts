import type { Theme } from '../../types';

export interface FumeBackgroundAudioLevels {
    power?: number;
    bass?: number;
    lowMid?: number;
    mid?: number;
    vocal?: number;
    treble?: number;
}

export interface FumeBackgroundScene {
    width: number;
    height: number;
    shapes: FumeBackgroundShape[];
}

type ShapeKind = 'ring' | 'square' | 'cross' | 'spark';
type AudioBand = 'bass' | 'lowMid' | 'mid' | 'vocal' | 'treble';
interface FumeBackgroundShape {
    kind: ShapeKind;
    x: number;
    y: number;
    size: number;
    rotation: number;
    rotationSpeed: number;
    opacity: number;
    strokeWidth: number;
    color: 'secondary' | 'accent';
    depth: number;
    audioBand: AudioBand;
}

export interface FumeBackgroundParallax {
    cameraX: number;
    cameraY: number;
    originX: number;
    originY: number;
    strength: number;
}

const hash = (input: string) => {
    let value = 2166136261;
    for (let index = 0; index < input.length; index += 1) {
        value ^= input.charCodeAt(index);
        value = Math.imul(value, 16777619);
    }
    return value >>> 0;
};
const random = (seed: string, index: number) => ((hash(`${seed}:${index}`) % 10000) / 10000);
const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, value));

export const buildFumeBackgroundScene = ({ viewport, world, seed = 'fume' }: {
    viewport: { width: number; height: number };
    world: { width: number; height: number };
    paperBounds?: unknown;
    seed?: string;
}): FumeBackgroundScene => {
    const width = Math.max(viewport.width, world.width);
    const height = Math.max(viewport.height, world.height);
    const bands: AudioBand[] = ['bass', 'lowMid', 'mid', 'vocal', 'treble'];
    const kinds: ShapeKind[] = ['ring', 'square', 'cross', 'spark'];
    const shapes: FumeBackgroundShape[] = [];
    const count = clamp(Math.round((width * height) / 175000), 8, 28);
    for (let index = 0; index < count; index += 1) {
        shapes.push({
            kind: kinds[index % kinds.length]!,
            x: width * (0.08 + random(seed, index * 7) * 0.84),
            y: height * (0.08 + random(seed, index * 7 + 1) * 0.84),
            size: 28 + random(seed, index * 7 + 2) * 140,
            rotation: random(seed, index * 7 + 3) * Math.PI * 2,
            rotationSpeed: (random(seed, index * 7 + 4) - 0.5) * 0.00035,
            opacity: 0.32 + random(seed, index * 7 + 5) * 0.4,
            strokeWidth: 1 + random(seed, index * 7 + 6) * 2,
            color: index % 3 === 0 ? 'accent' : 'secondary',
            depth: 0.45 + random(seed, index * 11) * 0.55,
            audioBand: bands[index % bands.length]!,
        });
    }
    return { width, height, shapes };
};

// Lume wallpapers and rain-window effects are owned by VisualizerShell.
export const drawFumeBackground = ({ context, scene, theme, time = 0, audioLevels, parallax, objectOpacityMultiplier = 1 }: {
    context: CanvasRenderingContext2D;
    scene: FumeBackgroundScene;
    theme: Theme;
    time?: number;
    audioLevels?: FumeBackgroundAudioLevels;
    parallax?: FumeBackgroundParallax;
    objectOpacityMultiplier?: number;
}) => {
    const levels = audioLevels ?? {};
    const originX = parallax?.originX ?? scene.width * 0.5;
    const originY = parallax?.originY ?? scene.height * 0.5;
    for (const shape of scene.shapes) {
        const level = clamp(levels[shape.audioBand] ?? levels.power ?? 0, 0, 1);
        const scale = 1 + level * 0.12;
        const x = shape.x + ((parallax?.cameraX ?? originX) - originX) * shape.depth * 0.08;
        const y = shape.y + ((parallax?.cameraY ?? originY) - originY) * shape.depth * 0.06;
        const color = shape.color === 'accent' ? theme.accentColor : theme.secondaryColor;
        const alpha = clamp(shape.opacity * (0.75 + level * 0.55) * objectOpacityMultiplier, 0, 0.42);
        if (alpha <= 0.002) continue;
        context.save();
        context.translate(x, y);
        context.rotate(shape.rotation + time * shape.rotationSpeed);
        context.scale(scale, scale);
        context.strokeStyle = color;
        context.fillStyle = color;
        context.globalAlpha = alpha;
        context.lineWidth = shape.strokeWidth;
        context.shadowColor = color;
        context.shadowBlur = 8 + level * 12;
        context.beginPath();
        const half = shape.size * 0.5;
        if (shape.kind === 'ring') context.arc(0, 0, half, 0.2, Math.PI * 1.86);
        else if (shape.kind === 'square') context.rect(-half, -half, shape.size, shape.size);
        else if (shape.kind === 'cross') {
            const arm = half * 0.35;
            context.moveTo(-arm, -half); context.lineTo(arm, -half); context.lineTo(arm, -arm); context.lineTo(half, -arm);
            context.lineTo(half, arm); context.lineTo(arm, arm); context.lineTo(arm, half); context.lineTo(-arm, half);
            context.lineTo(-arm, arm); context.lineTo(-half, arm); context.lineTo(-half, -arm); context.lineTo(-arm, -arm); context.closePath();
        } else {
            context.moveTo(0, -half); context.lineTo(half * 0.22, -half * 0.22); context.lineTo(half, 0);
            context.lineTo(half * 0.22, half * 0.22); context.lineTo(0, half); context.lineTo(-half * 0.22, half * 0.22);
            context.lineTo(-half, 0); context.lineTo(-half * 0.22, -half * 0.22); context.closePath();
        }
        context.stroke();
        context.restore();
    }
};
