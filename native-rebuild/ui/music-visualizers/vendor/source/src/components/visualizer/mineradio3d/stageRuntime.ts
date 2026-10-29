import * as THREE from 'three';
import type { Line, Theme } from '../../../types';
import { colorWithAlpha } from '../colorMix';
import { buildLineGraphemeTimeline, splitLyricGraphemes } from '../../../utils/lyrics/graphemeTiming';
import { StarRiverParticles } from '../effects/StarRiverParticles';
import type { VisualizerEffectMode } from '../effects/definition';

export interface Mineradio3DLineFrame {
    x: number;
    y: number;
    z: number;
    scale: number;
    opacity: number;
    rotationY: number;
    rotationZ: number;
}

export interface Mineradio3DTranslationFrame {
    y: number;
    scale: number;
    opacity: number;
}

export interface Mineradio3DStageState {
    lines: Line[];
    activeLineIndex: number;
    currentTime: number;
    theme: Theme;
    showTranslation: boolean;
    paused: boolean;
    staticMode?: boolean;
    visualizerEffectMode?: VisualizerEffectMode;
    coverUrl?: string | null;
}

export interface Mineradio3DOrbitState {
    theta: number;
    phi: number;
    radius: number;
}

export interface Mineradio3DStageSpinState {
    x: number;
    y: number;
    velocityX: number;
    velocityY: number;
}

export interface Mineradio3DStagePanState {
    x: number;
    y: number;
}

export type Mineradio3DPointerMode = 'rotate' | 'pan';

interface StageLyricMesh {
    signature: string;
    group: THREE.Group;
    text: THREE.Mesh<THREE.PlaneGeometry, THREE.MeshBasicMaterial>;
    backText: THREE.Mesh<THREE.PlaneGeometry, THREE.MeshBasicMaterial>;
    texture: THREE.CanvasTexture;
    backTexture: THREE.CanvasTexture;
    geometry: THREE.PlaneGeometry;
    material: THREE.MeshBasicMaterial;
    backMaterial: THREE.MeshBasicMaterial;
    glow: StageLyricGlowLayer;
    translation?: THREE.Mesh<THREE.PlaneGeometry, THREE.MeshBasicMaterial>;
    backTranslation?: THREE.Mesh<THREE.PlaneGeometry, THREE.MeshBasicMaterial>;
    translationTexture?: THREE.CanvasTexture;
    backTranslationTexture?: THREE.CanvasTexture;
    translationGeometry?: THREE.PlaneGeometry;
    translationMaterial?: THREE.MeshBasicMaterial;
    backTranslationMaterial?: THREE.MeshBasicMaterial;
    translationGlow?: StageLyricGlowLayer;
    textCanvas: HTMLCanvasElement;
    graphemeOffsets: number[];
    translationGraphemeOffsets: number[];
    lastKaraokeFillWidth: number;
    lastTranslationKaraokeFillWidth: number;
    lastKaraokePaintAt: number;
}

interface StageLyricGlowLayer {
    front: THREE.Mesh<THREE.PlaneGeometry, THREE.MeshBasicMaterial>;
    back: THREE.Mesh<THREE.PlaneGeometry, THREE.MeshBasicMaterial>;
    texture: THREE.CanvasTexture;
    backTexture: THREE.CanvasTexture;
    geometry: THREE.PlaneGeometry;
    material: THREE.MeshBasicMaterial;
    backMaterial: THREE.MeshBasicMaterial;
    dispose: () => void;
}

const MIN_CANVAS_WIDTH = 640;
const CANVAS_HEIGHT = 180;
const TRANSLATION_CANVAS_HEIGHT = 128;
const MINERADIO_GLOW_TEXTURE_SCALE = 0.5;
const MINERADIO_CINEMA_LINE_COUNT = 5;
const MINERADIO_CONTEXT_SPREAD = 1.96;
const MINERADIO_TRANSLATION_GAP = 0.92;
const MINERADIO_TRANSLATION_SCALE = 0.65;
const MINERADIO_PRIMARY_WORLD_WIDTH = 6.10;
const MINERADIO_MASK_WIDTH = 2048;
const MINERADIO_FONT_SIZE = 128;
const MINERADIO_CAMERA_RADIUS = 6.6;
const MINERADIO_CAMERA_FOV = 45;
const MINERADIO_LYRIC_GROUP_Z = 1.46;
const LUME_CAMERA_RADIUS = 16;
const LUME_CAMERA_FOV = 44;
const ACTIVE_LINE_Z = 0.055;
const MINERADIO_TRANSLATION_VISUAL_GAP = 0.98 + (MINERADIO_TRANSLATION_GAP - 0.28) * 0.36;
const MINERADIO_PRIMARY_TRANSLATED_SLOT_STEP = MINERADIO_TRANSLATION_VISUAL_GAP + 0.82 + MINERADIO_TRANSLATION_SCALE * 0.14;
const MINERADIO_PRIMARY_CONTEXT_SLOT_STEP = 1.04 + (MINERADIO_CONTEXT_SPREAD - 1) * 0.10;
const MINERADIO_TO_LUME_SCENE_SCALE = (
    (LUME_CAMERA_RADIUS - ACTIVE_LINE_Z) /
    (MINERADIO_CAMERA_RADIUS - MINERADIO_LYRIC_GROUP_Z - ACTIVE_LINE_Z)
) * (
    Math.tan(LUME_CAMERA_FOV * Math.PI / 360) /
    Math.tan(MINERADIO_CAMERA_FOV * Math.PI / 360)
);
const MINERADIO_BASE_LINE_STEP = MINERADIO_PRIMARY_WORLD_WIDTH * (MINERADIO_FONT_SIZE / MINERADIO_MASK_WIDTH);
const TRACK_LINE_STEP = MINERADIO_BASE_LINE_STEP * (
    1 + (MINERADIO_CONTEXT_SPREAD - 1) * 0.32
) * 1.06 * MINERADIO_TO_LUME_SCENE_SCALE;
const TRANSLATION_LINE_STEP = MINERADIO_BASE_LINE_STEP * 1.04 * MINERADIO_TO_LUME_SCENE_SCALE;
const TRACK_VISIBLE_RADIUS = MINERADIO_CINEMA_LINE_COUNT * 0.5 * MINERADIO_PRIMARY_TRANSLATED_SLOT_STEP;
const TRACK_CONTEXT_OPACITY = 0.54;
const TRACK_COMPLETED_OPACITY = 0.78;
const MIN_ORBIT_RADIUS = 8;
const MAX_ORBIT_RADIUS = 28;
// Lume's larger 16-unit baseline needs a proportionally larger step to match
// Mineradio's 0.6 movement from a 6.6-unit baseline per -120 wheel tick.
const ORBIT_WHEEL_RADIUS_STEP = 0.012;
const PARTICLE_POINTER_SPIN_X = 0.0032;
const PARTICLE_POINTER_SPIN_Y = 0.0034;
const PARTICLE_SPIN_MAX = 6.2;
const MINERADIO_ANIMATION_BUFFER = 3;
const MINERADIO_MESH_BUFFER = MINERADIO_ANIMATION_BUFFER + 2;
const PARTICLE_ROTATION_REBASE_LIMIT = Math.PI * 10;
const KARAOKE_TEXTURE_FRAME_INTERVAL_MS = 1000 / 30;
const KARAOKE_BASE_ALPHA = 1;
const MINERADIO_NARROW_STAGE_ASPECT = 0.62;
const MINERADIO_TEXT_TEXTURE_SCALE = 1.5;
const ACTIVE_LYRIC_GLOW_OPACITY = 0.30;
const CONTEXT_LYRIC_GLOW_OPACITY = 0.10;
const ACTIVE_TRANSLATION_GLOW_OPACITY = 0.17;
const CONTEXT_TRANSLATION_GLOW_OPACITY = 0.06;

const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, value));

export const resolveMineradio3DResponsiveScale = (aspect: number) => (
    clamp(aspect / MINERADIO_NARROW_STAGE_ASPECT, 0.5, 1)
);

export const resolveMineradio3DBackfaceTextureTransform = () => ({ repeatX: -1, offsetX: 1 });

export const createMineradio3DOrbitState = (): Mineradio3DOrbitState => ({ theta: 0, phi: 0.06, radius: 16 });

export const applyMineradio3DOrbitWheel = (
    orbit: Mineradio3DOrbitState,
    deltaY: number,
    visualRadius = orbit.radius,
): Mineradio3DOrbitState => {
    const radiusStep = deltaY * ORBIT_WHEEL_RADIUS_STEP;
    const queuedDelta = orbit.radius - visualRadius;
    const reversesQueuedMotion = radiusStep !== 0 && queuedDelta !== 0 && Math.sign(radiusStep) !== Math.sign(queuedDelta);
    const radiusBase = reversesQueuedMotion ? visualRadius : orbit.radius;
    return {
        ...orbit,
        radius: clamp(radiusBase + radiusStep, MIN_ORBIT_RADIUS, MAX_ORBIT_RADIUS),
    };
};

export const easeMineradio3DOrbitCamera = (current: Mineradio3DOrbitState, target: Mineradio3DOrbitState): Mineradio3DOrbitState => ({
    theta: current.theta + (target.theta - current.theta) * 0.10,
    phi: current.phi + (target.phi - current.phi) * 0.10,
    radius: current.radius + (target.radius - current.radius) * 0.07,
});

export const resolveMineradio3DPointerMode = (button: number): Mineradio3DPointerMode | null => {
    if (button === 0) return 'rotate';
    if (button === 1) return 'pan';
    return null;
};

export const applyMineradio3DStagePan = (
    pan: Mineradio3DStagePanState,
    deltaX: number,
    deltaY: number,
    viewportHeight: number,
    radius: number,
    fov: number,
): Mineradio3DStagePanState => {
    const worldPerPixel = 2 * radius * Math.tan(fov * Math.PI / 360) / Math.max(1, viewportHeight);
    return {
        x: pan.x + deltaX * worldPerPixel,
        y: pan.y - deltaY * worldPerPixel,
    };
};

export const applyMineradio3DStageDrag = (spin: Mineradio3DStageSpinState, deltaX: number, deltaY: number, deltaSeconds: number): Mineradio3DStageSpinState => {
    const rotationX = deltaY * PARTICLE_POINTER_SPIN_X;
    const rotationY = deltaX * PARTICLE_POINTER_SPIN_Y;
    const safeDeltaSeconds = Math.max(1 / 120, deltaSeconds);
    const instantVelocityX = clamp(rotationX / safeDeltaSeconds * 0.46, -PARTICLE_SPIN_MAX, PARTICLE_SPIN_MAX);
    const instantVelocityY = clamp(rotationY / safeDeltaSeconds * 0.46, -PARTICLE_SPIN_MAX, PARTICLE_SPIN_MAX);
    return {
        x: spin.x + rotationX,
        y: spin.y + rotationY,
        velocityX: instantVelocityX,
        velocityY: instantVelocityY,
    };
};

export const advanceMineradio3DStageSpin = (spin: Mineradio3DStageSpinState, deltaSeconds: number): Mineradio3DStageSpinState => {
    const velocityX = spin.velocityX * Math.pow(0.90, deltaSeconds * 60);
    const velocityY = spin.velocityY * Math.pow(0.90, deltaSeconds * 60);
    return {
        x: spin.x + spin.velocityX * deltaSeconds,
        y: spin.y + spin.velocityY * deltaSeconds,
        velocityX: Math.abs(velocityX) < 0.01 ? 0 : velocityX,
        velocityY: Math.abs(velocityY) < 0.01 ? 0 : velocityY,
    };
};

export const rebaseMineradio3DStageSpin = (
    spin: Mineradio3DStageSpinState,
    limit = PARTICLE_ROTATION_REBASE_LIMIT,
) => {
    let offsetX = 0;
    let offsetY = 0;
    if (Math.abs(spin.x) >= limit) offsetX = Math.round(spin.x / (Math.PI * 2)) * Math.PI * 2;
    if (Math.abs(spin.y) >= limit) offsetY = Math.round(spin.y / (Math.PI * 2)) * Math.PI * 2;
    return {
        spin: {
            x: spin.x - offsetX,
            y: spin.y - offsetY,
            velocityX: spin.velocityX,
            velocityY: spin.velocityY,
        },
        offsetX,
        offsetY,
    };
};

export const resolveMineradio3DOrbitPosition = (orbit: Mineradio3DOrbitState, output = new THREE.Vector3()) => {
    const cosPhi = Math.cos(orbit.phi);
    return output.set(
        orbit.radius * cosPhi * Math.sin(orbit.theta),
        orbit.radius * Math.sin(orbit.phi),
        orbit.radius * cosPhi * Math.cos(orbit.theta),
    );
};

export const resolveMineradio3DLineWindow = (activeLineIndex: number, lineCount: number) => {
    if (lineCount <= 0) return [];
    const center = clamp(activeLineIndex, 0, lineCount - 1);
    const start = Math.max(0, center - MINERADIO_MESH_BUFFER);
    const end = Math.min(lineCount - 1, center + MINERADIO_MESH_BUFFER);
    // Preload beyond the animation corridor without retaining every sung line's textures.
    return Array.from({ length: end - start + 1 }, (_, index) => start + index);
};

const normalizeLyricText = (text: string | undefined) => (text ?? '').trim().replace(/\s+/g, ' ');

const resolveTranslationText = (line: { fullText?: string; translation?: string } | undefined) => {
    const translation = normalizeLyricText(line?.translation);
    if (!translation || translation === normalizeLyricText(line?.fullText)) return '';
    return translation;
};

export const resolveMineradio3DPrimaryVirtualIndices = (
    lines: ReadonlyArray<{ fullText?: string; translation?: string }>,
    showTranslation = true,
) => {
    const indices = new Array<number>(lines.length);
    if (lines.length === 0) return indices;
    indices[0] = 0;
    for (let index = 1; index < lines.length; index += 1) {
        const needsTranslationSlot = showTranslation && (
            Boolean(resolveTranslationText(lines[index - 1])) || Boolean(resolveTranslationText(lines[index]))
        );
        indices[index] = indices[index - 1] + (
            needsTranslationSlot ? MINERADIO_PRIMARY_TRANSLATED_SLOT_STEP : MINERADIO_PRIMARY_CONTEXT_SLOT_STEP
        );
    }
    return indices;
};

export const easeMineradio3DTrackOffset = (current: number, target: number, deltaSeconds: number) => {
    if (current === target) return target;
    const frameScale = clamp(deltaSeconds * 60, 0.25, 3);
    const ease = 1 - Math.pow(1 - 0.18, frameScale);
    const step = clamp((target - current) * ease, -0.68, 0.68);
    return Math.abs(target - current) < 0.0005 ? target : current + step;
};

export const resolveMineradio3DTrackMotionActive = (current: number, target: number): boolean => (
    Math.abs(current - target) > 0.0001
);

export const resolveMineradio3DStableActiveLineIndex = (
    requestedIndex: number,
    previousIndex: number,
    lineCount: number,
    trackChanged: boolean,
) => {
    if (lineCount <= 0) return -1;
    if (requestedIndex >= 0) return clamp(requestedIndex, 0, lineCount - 1);
    if (!trackChanged && previousIndex >= 0) return clamp(previousIndex, 0, lineCount - 1);
    return 0;
};

export const resolveMineradio3DRotationEase = (deltaSeconds: number) => (
    1 - Math.pow(1 - 0.055, clamp(deltaSeconds * 60, 0.25, 3))
);

const smoothstep = (value: number) => value * value * (3 - 2 * value);

// Mineradio's cinema layout is one vertical track; distance controls hierarchy and depth.
export const resolveMineradio3DLineFrame = (
    relativeIndex: number,
    karaokeProgress: number,
    output: Mineradio3DLineFrame = { x: 0, y: 0, z: 0, scale: 1, opacity: 1, rotationY: 0, rotationZ: 0 },
    primaryLineOffset = relativeIndex,
): Mineradio3DLineFrame => {
    const distance = Math.abs(relativeIndex);
    const isActive = distance < 0.001;
    const isWithinCinemaWindow = Math.abs(Math.round(primaryLineOffset)) <= Math.floor(MINERADIO_CINEMA_LINE_COUNT / 2);
    const authoredScale = isActive ? 1 : distance <= 1.001 ? 0.90 : 0.82;
    const depthScale = clamp(1 - Math.min(5.5, distance) * 0.026, 0.84, 1.02);
    const contextAlpha = distance <= 1.001 ? TRACK_CONTEXT_OPACITY : TRACK_CONTEXT_OPACITY * 0.64;
    const visibility = !isWithinCinemaWindow ? 0 : isActive ? 1 : smoothstep(clamp((TRACK_VISIBLE_RADIUS - distance) / 1.1, 0, 1));
    const depthFade = clamp(1 - distance * 0.055, 0.54, 1);
    const isCompleted = relativeIndex < -0.001 && karaokeProgress >= 0.999;
    output.x = 0;
    output.y = -relativeIndex * TRACK_LINE_STEP;
    output.z = ACTIVE_LINE_Z - Math.pow(Math.min(5.5, distance), 1.06) * 0.145;
    output.scale = authoredScale * depthScale;
    output.opacity = !isWithinCinemaWindow ? 0 : isActive ? 1 : isCompleted
        ? Math.max(TRACK_COMPLETED_OPACITY, contextAlpha * depthFade * visibility)
        : contextAlpha * depthFade * visibility;
    output.rotationY = 0;
    output.rotationZ = 0;
    return output;
};

export const resolveMineradio3DTranslationFrame = (
    isCurrent: boolean,
    parentOpacity: number,
    output: Mineradio3DTranslationFrame = { y: 0, scale: 1, opacity: 1 },
    isCompleted = false,
): Mineradio3DTranslationFrame => {
    output.y = -MINERADIO_TRANSLATION_VISUAL_GAP * TRANSLATION_LINE_STEP;
    output.scale = MINERADIO_TRANSLATION_SCALE * (isCurrent ? 1.08 : 0.92);
    output.opacity = isCurrent ? 0.94 : isCompleted
        ? clamp(parentOpacity * 0.90, 0.60, 0.76)
        : clamp(parentOpacity * 0.62, 0.24, 0.60);
    return output;
};

const lineKaraokeProgress = (line: Line, currentTime: number) => {
    const duration = Math.max(0.001, line.endTime - line.startTime);
    return clamp((currentTime - line.startTime) / duration, 0, 1);
};

export const resolveMineradio3DKaraokeFillWidth = (
    line: Line,
    currentTime: number,
    graphemeOffsets: number[],
) => {
    const fullWidth = graphemeOffsets[graphemeOffsets.length - 1] ?? 0;
    if (currentTime <= line.startTime || fullWidth <= 0) return 0;
    if (currentTime >= line.endTime) return fullWidth;

    const graphemeTimings = buildLineGraphemeTimeline(line);
    const timingCount = Math.min(graphemeTimings.length, graphemeOffsets.length - 1);
    for (let index = 0; index < timingCount; index += 1) {
        const timing = graphemeTimings[index];
        const timingStart = Math.max(line.startTime, timing.startTime);
        const timingEnd = Math.max(timingStart, timing.endTime);
        const startWidth = graphemeOffsets[index] ?? 0;
        const endWidth = graphemeOffsets[index + 1] ?? startWidth;
        if (currentTime < timingStart) return startWidth;
        if (currentTime <= timingEnd) {
            const progress = (currentTime - timingStart) / Math.max(0.001, timingEnd - timingStart);
            return startWidth + (endWidth - startWidth) * progress;
        }
    }
    return graphemeOffsets[timingCount] ?? fullWidth;
};

const configureTextContext = (
    context: CanvasRenderingContext2D,
    font: string,
) => {
    context.font = font;
    context.textAlign = 'center';
    context.textBaseline = 'middle';
};

const measureGraphemeOffsets = (
    context: CanvasRenderingContext2D,
    text: string,
) => {
    const graphemes = splitLyricGraphemes(text);
    const offsets = new Array<number>(graphemes.length + 1).fill(0);
    for (let index = 1; index <= graphemes.length; index += 1) {
        offsets[index] = context.measureText(graphemes.slice(0, index).join('')).width;
    }
    return offsets;
};

const paintKaraokeTextCanvas = (
    canvas: HTMLCanvasElement,
    text: string,
    theme: Theme,
    graphemeOffsets: number[],
    fillWidth: number,
    baseColor = theme.primaryColor,
) => {
    const context = canvas.getContext('2d');
    if (!context) return;
    const renderedText = text || '...';
    const fullWidth = graphemeOffsets[graphemeOffsets.length - 1] ?? 0;
    const textLeft = (canvas.width - fullWidth) / 2;
    const edgeSoftness = Math.max(Math.min(68 * 0.45, 16), 6);
    const fillColor = theme.karaokeFillColor ?? '#ffffff';

    context.clearRect(0, 0, canvas.width, canvas.height);
    context.fillStyle = colorWithAlpha(baseColor, KARAOKE_BASE_ALPHA);
    context.fillText(renderedText, canvas.width / 2, canvas.height / 2);
    if (fillWidth <= 0) return;

    // Clip the color pass to the sung graphemes. Without this clip, Canvas
    // keeps using the final gradient stop after the sweep and paints the rest
    // of the sentence before it has been sung.
    const fillEnd = textLeft + fillWidth;
    const sweepStart = Math.max(textLeft, fillEnd - edgeSoftness);
    const gradient = context.createLinearGradient(sweepStart, 0, Math.max(sweepStart + 1, fillEnd), 0);
    gradient.addColorStop(0, fillColor);
    gradient.addColorStop(1, baseColor);
    context.save();
    context.beginPath();
    context.rect(textLeft, 0, fillWidth, canvas.height);
    context.clip();
    // First expose the complete sung region, then soften only its moving edge.
    context.fillStyle = fillColor;
    context.fillText(renderedText, canvas.width / 2, canvas.height / 2);
    context.fillStyle = gradient;
    context.fillText(renderedText, canvas.width / 2, canvas.height / 2);
    context.restore();
};

const createTextCanvas = (
    text: string,
    theme: Theme,
    options: { fontSize?: number; fontWeight?: number; color?: string; height?: number } = {},
) => {
    const canvas = document.createElement('canvas');
    const context = canvas.getContext('2d');
    if (!context) throw new Error('Mineradio 3D lyrics requires a canvas 2D context.');

    const fontFamily = theme.fontFamilyStack?.join(', ') || theme.fontFamily || 'system-ui, sans-serif';
    const fontSize = options.fontSize ?? 68;
    const fontWeight = options.fontWeight ?? 750;
    context.font = `${fontWeight} ${fontSize}px ${fontFamily}`;
    const measuredWidth = Math.ceil(context.measureText(text || ' ').width);
    const baseCanvasWidth = Math.max(MIN_CANVAS_WIDTH, measuredWidth + 160);
    const baseCanvasHeight = options.height ?? CANVAS_HEIGHT;
    canvas.width = Math.ceil(baseCanvasWidth * MINERADIO_TEXT_TEXTURE_SCALE);
    canvas.height = Math.ceil(baseCanvasHeight * MINERADIO_TEXT_TEXTURE_SCALE);

    const drawContext = canvas.getContext('2d');
    if (!drawContext) throw new Error('Mineradio 3D lyrics requires a canvas 2D context.');
    configureTextContext(
        drawContext,
        `${fontWeight} ${fontSize * MINERADIO_TEXT_TEXTURE_SCALE}px ${fontFamily}`,
    );
    drawContext.fillStyle = options.color ?? theme.primaryColor;
    drawContext.fillText(text || '...', canvas.width / 2, canvas.height / 2);
    return canvas;
};

const createLyricGlowAlphaMask = (textCanvas: HTMLCanvasElement) => {
    const alphaMaskCanvas = document.createElement('canvas');
    alphaMaskCanvas.width = Math.max(1, Math.ceil(textCanvas.width * MINERADIO_GLOW_TEXTURE_SCALE));
    alphaMaskCanvas.height = Math.max(1, Math.ceil(textCanvas.height * MINERADIO_GLOW_TEXTURE_SCALE));
    const alphaContext = alphaMaskCanvas.getContext('2d');
    if (!alphaContext) throw new Error('Mineradio 3D lyrics requires a canvas 2D context.');

    alphaContext.fillStyle = '#fff';
    alphaContext.fillRect(0, 0, alphaMaskCanvas.width, alphaMaskCanvas.height);
    alphaContext.globalCompositeOperation = 'destination-in';
    alphaContext.drawImage(textCanvas, 0, 0, alphaMaskCanvas.width, alphaMaskCanvas.height);
    return alphaMaskCanvas;
};

const createLyricGlowCanvas = (textCanvas: HTMLCanvasElement) => {
    const alphaMaskCanvas = createLyricGlowAlphaMask(textCanvas);
    const canvas = document.createElement('canvas');
    canvas.width = alphaMaskCanvas.width;
    canvas.height = alphaMaskCanvas.height;
    const context = canvas.getContext('2d');
    if (!context) throw new Error('Mineradio 3D lyrics requires a canvas 2D context.');
    const scaleX = canvas.width / textCanvas.width;
    const scaleY = canvas.height / textCanvas.height;

    // Keep the screen-space blur and glow offsets stable at the smaller texture size.
    const drawBlurPass = (filter: string, alpha: number) => {
        context.save();
        context.filter = filter;
        context.globalAlpha = alpha;
        context.drawImage(alphaMaskCanvas, 0, 0, canvas.width, canvas.height);
        context.restore();
    };

    drawBlurPass(`blur(${14 * scaleX}px)`, 0.46);
    drawBlurPass(`blur(${34 * scaleX}px)`, 0.34);
    drawBlurPass(`blur(${78 * scaleX}px)`, 0.22);
    drawBlurPass(`blur(${116 * scaleX}px)`, 0.13);

    context.save();
    context.globalCompositeOperation = 'lighter';
    context.filter = `blur(${8 * scaleX}px)`;
    context.globalAlpha = 0.26;
    for (let index = 0; index < 8; index += 1) {
        const angle = index / 8 * Math.PI * 2;
        context.drawImage(
            alphaMaskCanvas,
            Math.cos(angle) * 7 * scaleX,
            Math.sin(angle) * 4 * scaleY,
            canvas.width,
            canvas.height,
        );
    }
    context.restore();

    context.save();
    context.globalCompositeOperation = 'destination-in';
    const horizontalMask = context.createLinearGradient(0, 0, canvas.width, 0);
    horizontalMask.addColorStop(0, 'rgba(255,255,255,0)');
    horizontalMask.addColorStop(0.10, 'rgba(255,255,255,1)');
    horizontalMask.addColorStop(0.90, 'rgba(255,255,255,1)');
    horizontalMask.addColorStop(1, 'rgba(255,255,255,0)');
    context.fillStyle = horizontalMask;
    context.fillRect(0, 0, canvas.width, canvas.height);
    const verticalMask = context.createLinearGradient(0, 0, 0, canvas.height);
    verticalMask.addColorStop(0, 'rgba(255,255,255,0)');
    verticalMask.addColorStop(0.16, 'rgba(255,255,255,1)');
    verticalMask.addColorStop(0.84, 'rgba(255,255,255,1)');
    verticalMask.addColorStop(1, 'rgba(255,255,255,0)');
    context.fillStyle = verticalMask;
    context.fillRect(0, 0, canvas.width, canvas.height);
    context.restore();
    return canvas;
};

const createBackfaceTexture = (texture: THREE.CanvasTexture) => {
    const backfaceTexture = texture.clone();
    const transform = resolveMineradio3DBackfaceTextureTransform();
    backfaceTexture.repeat.x = transform.repeatX;
    backfaceTexture.offset.x = transform.offsetX;
    backfaceTexture.needsUpdate = true;
    return backfaceTexture;
};

const createLyricGlowLayer = (
    textCanvas: HTMLCanvasElement,
    geometry: THREE.PlaneGeometry,
    glowColor: string,
    z: number,
): StageLyricGlowLayer => {
    const glowTexture = new THREE.CanvasTexture(createLyricGlowCanvas(textCanvas));
    glowTexture.minFilter = THREE.LinearFilter;
    glowTexture.magFilter = THREE.LinearFilter;
    glowTexture.generateMipmaps = false;
    const backGlowTexture = createBackfaceTexture(glowTexture);
    const material = new THREE.MeshBasicMaterial({
        map: glowTexture,
        color: glowColor,
        transparent: true,
        depthWrite: false,
        depthTest: false,
        blending: THREE.AdditiveBlending,
        side: THREE.FrontSide,
    });
    const backMaterial = new THREE.MeshBasicMaterial({
        map: backGlowTexture,
        color: glowColor,
        transparent: true,
        depthWrite: false,
        depthTest: false,
        blending: THREE.AdditiveBlending,
        side: THREE.BackSide,
    });
    const front = new THREE.Mesh(geometry, material);
    const back = new THREE.Mesh(geometry, backMaterial);
    front.position.z = z;
    back.position.z = -z;
    return {
        front,
        back,
        texture: glowTexture,
        backTexture: backGlowTexture,
        geometry,
        material,
        backMaterial,
        dispose: () => {
            glowTexture.dispose();
            backGlowTexture.dispose();
            material.dispose();
            backMaterial.dispose();
        },
    };
};

const resolveLyricGlowOpacity = (
    lineOpacity: number,
    isCurrent: boolean,
    translation: boolean,
) => {
    const activeOpacity = translation ? ACTIVE_TRANSLATION_GLOW_OPACITY : ACTIVE_LYRIC_GLOW_OPACITY;
    const contextOpacity = translation ? CONTEXT_TRANSLATION_GLOW_OPACITY : CONTEXT_LYRIC_GLOW_OPACITY;
    return lineOpacity * (isCurrent ? activeOpacity : contextOpacity);
};

const disposeStageMesh = (mesh: StageLyricMesh) => {
    mesh.group.parent?.remove(mesh.group);
    mesh.geometry.dispose();
    mesh.texture.dispose();
    mesh.backTexture.dispose();
    mesh.material.dispose();
    mesh.backMaterial.dispose();
    mesh.glow.dispose();
    mesh.translationGeometry?.dispose();
    mesh.translationTexture?.dispose();
    mesh.backTranslationTexture?.dispose();
    mesh.translationMaterial?.dispose();
    mesh.backTranslationMaterial?.dispose();
    mesh.translationGlow?.dispose();
    // Release pixel stores even if a renderer's previous render list still references the mesh.
    const canvases = [mesh.textCanvas, mesh.glow.texture.image];
    if (mesh.translationTexture) canvases.push(mesh.translationTexture.image);
    if (mesh.translationGlow) canvases.push(mesh.translationGlow.texture.image);
    for (const canvas of canvases) {
        canvas.width = 1;
        canvas.height = 1;
    }
};

export class Mineradio3DStageRuntime {
    private readonly scene = new THREE.Scene();
    private readonly stageRoot = new THREE.Group();
    private readonly camera = new THREE.PerspectiveCamera(44, 1, 0.1, 300);
    private starRiver: StarRiverParticles | null = null;
    private readonly renderer: THREE.WebGLRenderer;
    private readonly meshes = new Map<number, StageLyricMesh>();
    private readonly resizeObserver: ResizeObserver;
    private readonly frameScratch: Mineradio3DLineFrame = { x: 0, y: 0, z: 0, scale: 1, opacity: 1, rotationY: 0, rotationZ: 0 };
    private readonly translationFrameScratch: Mineradio3DTranslationFrame = { y: 0, scale: 1, opacity: 1 };
    private readonly orbitPositionTarget = new THREE.Vector3();
    private readonly groupPositionTarget = new THREE.Vector3();
    private readonly groupScaleTarget = new THREE.Vector3();
    private readonly translationScaleTarget = new THREE.Vector3();
    private readonly orbitBaseline = createMineradio3DOrbitState();
    private orbit = createMineradio3DOrbitState();
    private orbitTarget = createMineradio3DOrbitState();
    private stageSpin: Mineradio3DStageSpinState = { x: 0, y: 0, velocityX: 0, velocityY: 0 };
    private modelPan: Mineradio3DStagePanState = { x: 0, y: 0 };
    private trackVirtualIndices: number[] = [];
    private trackScrollOffset = 0;
    private trackTargetOffset = 0;
    private trackLines: Line[] | null = null;
    private meshThemeSignature = '';
    private state: Mineradio3DStageState;
    private animationFrameId: number | null = null;
    private activePointerId: number | null = null;
    private activePointerMode: Mineradio3DPointerMode | null = null;
    private pointerX = 0;
    private pointerY = 0;
    private lastPointerMoveAt = performance.now();
    private lastStageFrameAt = performance.now();
    private destroyed = false;

    private readonly onPointerDown = (event: PointerEvent) => {
        const mode = resolveMineradio3DPointerMode(event.button);
        if (!mode) return;
        event.preventDefault();
        this.renderer.domElement.focus();
        this.activePointerId = event.pointerId;
        this.activePointerMode = mode;
        if (mode === 'rotate') {
            this.stageSpin.velocityX = 0;
            this.stageSpin.velocityY = 0;
        }
        this.pointerX = event.clientX;
        this.pointerY = event.clientY;
        this.lastPointerMoveAt = performance.now();
        this.renderer.domElement.setPointerCapture?.(event.pointerId);
        this.renderer.domElement.style.cursor = 'grabbing';
    };

    private readonly onPointerMove = (event: PointerEvent) => {
        if (!this.activePointerMode || event.pointerId !== this.activePointerId) return;
        const deltaX = event.clientX - this.pointerX;
        const deltaY = event.clientY - this.pointerY;
        this.pointerX = event.clientX;
        this.pointerY = event.clientY;
        const now = performance.now();
        const deltaSeconds = clamp((now - this.lastPointerMoveAt) / 1000, 1 / 120, 0.08);
        this.lastPointerMoveAt = now;
        if (this.activePointerMode === 'rotate') {
            this.stageSpin = applyMineradio3DStageDrag(this.stageSpin, deltaX, deltaY, deltaSeconds);
        } else {
            this.modelPan = applyMineradio3DStagePan(
                this.modelPan,
                deltaX,
                deltaY,
                this.host.clientHeight,
                this.orbit.radius,
                this.camera.fov,
            );
        }
        this.scheduleRender();
    };

    private readonly onPointerUp = (event: PointerEvent) => {
        if (event.pointerId !== this.activePointerId) return;
        this.renderer.domElement.releasePointerCapture?.(event.pointerId);
        this.activePointerId = null;
        this.activePointerMode = null;
        this.renderer.domElement.style.cursor = 'grab';
        this.scheduleRender();
    };

    private readonly onWheel = (event: WheelEvent) => {
        event.preventDefault();
        this.orbitTarget = applyMineradio3DOrbitWheel(this.orbitTarget, event.deltaY, this.orbit.radius);
        this.scheduleRender();
    };

    private readonly onDoubleClick = () => {
        this.orbitTarget = { ...this.orbitBaseline };
        this.stageSpin = { x: 0, y: 0, velocityX: 0, velocityY: 0 };
        this.modelPan = { x: 0, y: 0 };
        this.stageRoot.position.set(0, 0, 0);
        this.scheduleRender();
    };

    constructor(
        private readonly host: HTMLElement,
        state: Mineradio3DStageState,
        private readonly onVisualFrame?: (canvas: HTMLCanvasElement) => void,
    ) {
        this.state = state;
        this.scene.add(this.stageRoot);
        this.renderer = new THREE.WebGLRenderer({ alpha: true, antialias: true, powerPreference: 'high-performance' });
        this.renderer.setClearColor(0x000000, 0);
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
        this.renderer.domElement.dataset.visualizerCanvas = 'mineradio-3d';
        this.renderer.domElement.tabIndex = 0;
        this.renderer.domElement.style.width = '100%';
        this.renderer.domElement.style.height = '100%';
        this.renderer.domElement.style.display = 'block';
        this.renderer.domElement.style.cursor = 'grab';
        this.renderer.domElement.style.touchAction = 'none';
        this.renderer.domElement.addEventListener('pointerdown', this.onPointerDown);
        this.renderer.domElement.addEventListener('pointermove', this.onPointerMove);
        this.renderer.domElement.addEventListener('pointerup', this.onPointerUp);
        this.renderer.domElement.addEventListener('pointercancel', this.onPointerUp);
        this.renderer.domElement.addEventListener('wheel', this.onWheel, { passive: false });
        this.renderer.domElement.addEventListener('dblclick', this.onDoubleClick);
        this.host.appendChild(this.renderer.domElement);
        this.resizeObserver = new ResizeObserver(() => this.resize());
        this.resizeObserver.observe(host);
        this.resize();
        this.update(state);
    }

    getCanvas() {
        return this.renderer.domElement;
    }

    refreshSceneFrame() {
        if (this.destroyed) return;
        // Republish the current pose without advancing animation or playback.
        this.renderer.render(this.scene, this.camera);
        this.onVisualFrame?.(this.renderer.domElement);
    }

    update(nextState: Mineradio3DStageState) {
        if (this.destroyed) return;
        const trackChanged = this.trackLines !== nextState.lines || this.state.showTranslation !== nextState.showTranslation;
        const stableActiveLineIndex = resolveMineradio3DStableActiveLineIndex(
            nextState.activeLineIndex,
            this.state.activeLineIndex,
            nextState.lines.length,
            trackChanged,
        );
        const activeLineChanged = stableActiveLineIndex !== this.state.activeLineIndex;
        const nextThemeSignature = `${nextState.theme.primaryColor}|${nextState.theme.secondaryColor}|${nextState.theme.accentColor}|${nextState.theme.karaokeFillColor ?? ''}|${nextState.theme.fontFamily ?? ''}|${nextState.theme.fontFamilyStack?.join(',') ?? ''}|${nextState.showTranslation}`;
        this.state = { ...nextState, activeLineIndex: stableActiveLineIndex };
        if (nextState.visualizerEffectMode === 'star-river') {
            if (!this.starRiver) {
                this.starRiver = new StarRiverParticles(() => this.scheduleRender());
                this.scene.add(this.starRiver);
            }
            this.starRiver.setCoverUrl(nextState.coverUrl);
        } else if (this.starRiver) {
            this.starRiver.dispose();
            this.starRiver = null;
        }
        this.trackLines = nextState.lines;
        if (trackChanged) this.trackVirtualIndices = resolveMineradio3DPrimaryVirtualIndices(nextState.lines, nextState.showTranslation);
        const activeLineIndex = stableActiveLineIndex;
        this.trackTargetOffset = this.trackVirtualIndices[activeLineIndex] ?? 0;
        if (trackChanged) this.trackScrollOffset = this.trackTargetOffset;
        if (trackChanged || activeLineChanged || this.meshThemeSignature !== nextThemeSignature) {
            this.meshThemeSignature = nextThemeSignature;
            this.syncMeshes();
        }
        if (nextState.paused) {
            this.stop();
            this.scheduleRender();
        } else {
            this.start();
        }
    }

    private resize() {
        if (this.destroyed) return;
        const width = Math.max(1, this.host.clientWidth);
        const height = Math.max(1, this.host.clientHeight);
        this.camera.aspect = width / height;
        this.stageRoot.scale.setScalar(resolveMineradio3DResponsiveScale(this.camera.aspect));
        this.camera.updateProjectionMatrix();
        this.renderer.setSize(width, height, false);
        this.scheduleRender();
    }

    private createMesh(line: Line, index: number) {
        const canvas = createTextCanvas(line.fullText, this.state.theme);
        const textContext = canvas.getContext('2d');
        if (!textContext) throw new Error('Mineradio 3D lyrics requires a canvas 2D context.');
        const graphemeOffsets = measureGraphemeOffsets(textContext, line.fullText || '...');
        const initialKaraokeFillWidth = index < this.state.activeLineIndex
            ? graphemeOffsets[graphemeOffsets.length - 1] ?? 0
            : index === this.state.activeLineIndex
                ? resolveMineradio3DKaraokeFillWidth(line, this.state.currentTime, graphemeOffsets)
                : 0;
        paintKaraokeTextCanvas(canvas, line.fullText, this.state.theme, graphemeOffsets, initialKaraokeFillWidth);
        const texture = new THREE.CanvasTexture(canvas);
        texture.minFilter = THREE.LinearFilter;
        texture.magFilter = THREE.LinearFilter;
        const aspect = canvas.width / canvas.height;
        const geometry = new THREE.PlaneGeometry(1.72 * aspect, 1.72);
        const backTexture = createBackfaceTexture(texture);
        const material = new THREE.MeshBasicMaterial({
            map: texture,
            transparent: true,
            depthWrite: false,
            side: THREE.FrontSide,
        });
        const backMaterial = new THREE.MeshBasicMaterial({
            map: backTexture,
            transparent: true,
            depthWrite: false,
            side: THREE.BackSide,
        });
        const text = new THREE.Mesh(geometry, material);
        const backText = new THREE.Mesh(geometry, backMaterial);
        const glow = createLyricGlowLayer(canvas, geometry, this.state.theme.primaryColor, -0.012);
        const group = new THREE.Group();
        group.add(glow.back, backText, glow.front, text);
        let translation: StageLyricMesh['translation'];
        let backTranslation: StageLyricMesh['backTranslation'];
        let translationTexture: StageLyricMesh['translationTexture'];
        let backTranslationTexture: StageLyricMesh['backTranslationTexture'];
        let translationGeometry: StageLyricMesh['translationGeometry'];
        let translationMaterial: StageLyricMesh['translationMaterial'];
        let backTranslationMaterial: StageLyricMesh['backTranslationMaterial'];
        let translationGlow: StageLyricMesh['translationGlow'];
        let translationGraphemeOffsets: number[] = [];
        let initialTranslationKaraokeFillWidth = 0;
        const translationText = this.state.showTranslation ? resolveTranslationText(line) : '';
        if (translationText) {
            const translationCanvas = createTextCanvas(translationText, this.state.theme, {
                fontSize: 42,
                fontWeight: 650,
                color: this.state.theme.secondaryColor,
                height: TRANSLATION_CANVAS_HEIGHT,
            });
            const translationContext = translationCanvas.getContext('2d');
            if (!translationContext) throw new Error('Mineradio 3D lyrics requires a canvas 2D context.');
            translationGraphemeOffsets = measureGraphemeOffsets(translationContext, translationText);
            const translationFullWidth = translationGraphemeOffsets[translationGraphemeOffsets.length - 1] ?? 0;
            const karaokeProgress = (graphemeOffsets[graphemeOffsets.length - 1] ?? 0) > 0
                ? initialKaraokeFillWidth / (graphemeOffsets[graphemeOffsets.length - 1] ?? 1)
                : 0;
            initialTranslationKaraokeFillWidth = index < this.state.activeLineIndex
                ? translationFullWidth
                : index === this.state.activeLineIndex ? translationFullWidth * karaokeProgress : 0;
            paintKaraokeTextCanvas(
                translationCanvas,
                translationText,
                this.state.theme,
                translationGraphemeOffsets,
                initialTranslationKaraokeFillWidth,
                this.state.theme.secondaryColor,
            );
            translationTexture = new THREE.CanvasTexture(translationCanvas);
            translationTexture.minFilter = THREE.LinearFilter;
            translationTexture.magFilter = THREE.LinearFilter;
            const translationAspect = translationCanvas.width / translationCanvas.height;
            translationGeometry = new THREE.PlaneGeometry(1.18 * translationAspect, 1.18);
            backTranslationTexture = createBackfaceTexture(translationTexture);
            translationMaterial = new THREE.MeshBasicMaterial({
                map: translationTexture,
                transparent: true,
                depthWrite: false,
                side: THREE.FrontSide,
            });
            backTranslationMaterial = new THREE.MeshBasicMaterial({
                map: backTranslationTexture,
                transparent: true,
                depthWrite: false,
                side: THREE.BackSide,
            });
            translation = new THREE.Mesh(translationGeometry, translationMaterial);
            backTranslation = new THREE.Mesh(translationGeometry, backTranslationMaterial);
            translationGlow = createLyricGlowLayer(translationCanvas, translationGeometry, this.state.theme.secondaryColor, 0.003);
            const translationFrame = resolveMineradio3DTranslationFrame(
                index === this.state.activeLineIndex,
                1,
                undefined,
                index < this.state.activeLineIndex,
            );
            translation.position.set(0, translationFrame.y, 0.015);
            backTranslation.position.copy(translation.position);
            translationGlow.front.position.y = translation.position.y;
            translationGlow.back.position.y = translation.position.y;
            translation.scale.setScalar(translationFrame.scale);
            backTranslation.scale.setScalar(translationFrame.scale);
            translationGlow.front.scale.copy(translation.scale);
            translationGlow.back.scale.copy(translation.scale);
            translationMaterial.opacity = translationFrame.opacity;
            backTranslationMaterial.opacity = translationFrame.opacity;
            const translationGlowOpacity = resolveLyricGlowOpacity(
                translationFrame.opacity,
                index === this.state.activeLineIndex,
                true,
            );
            translationGlow.material.opacity = translationGlowOpacity;
            translationGlow.backMaterial.opacity = translationGlowOpacity;
            group.add(translationGlow.back, backTranslation, translationGlow.front, translation);
        }
        const initialFrame = resolveMineradio3DLineFrame(
            (this.trackVirtualIndices[index] ?? index) - this.trackScrollOffset,
            lineKaraokeProgress(line, this.state.currentTime),
            undefined,
            index - this.state.activeLineIndex,
        );
        group.position.set(initialFrame.x, initialFrame.y, initialFrame.z);
        group.scale.setScalar(initialFrame.scale);
        material.opacity = initialFrame.opacity;
        backMaterial.opacity = initialFrame.opacity;
        const initialGlowOpacity = resolveLyricGlowOpacity(
            initialFrame.opacity,
            index === this.state.activeLineIndex,
            false,
        );
        glow.material.opacity = initialGlowOpacity;
        glow.backMaterial.opacity = initialGlowOpacity;
        group.visible = initialFrame.opacity > 0.002;
        this.stageRoot.add(group);
        return {
            signature: `${line.fullText}|${line.translation ?? ''}|${this.state.theme.primaryColor}|${this.state.theme.secondaryColor}|${this.state.theme.accentColor}|${this.state.theme.karaokeFillColor ?? ''}|${this.state.theme.fontFamily ?? ''}|${this.state.theme.fontFamilyStack?.join(',') ?? ''}|${this.state.showTranslation}`,
            group,
            text,
            backText,
            texture,
            backTexture,
            geometry,
            material,
            backMaterial,
            glow,
            translation,
            backTranslation,
            translationTexture,
            backTranslationTexture,
            translationGeometry,
            translationMaterial,
            backTranslationMaterial,
            translationGlow,
            textCanvas: canvas,
            graphemeOffsets,
            translationGraphemeOffsets,
            lastKaraokeFillWidth: initialKaraokeFillWidth,
            lastTranslationKaraokeFillWidth: initialTranslationKaraokeFillWidth,
            lastKaraokePaintAt: 0,
        };
    }

    private syncMeshes() {
        const visibleIndices = new Set(resolveMineradio3DLineWindow(this.state.activeLineIndex, this.state.lines.length));
        this.meshes.forEach((mesh, index) => {
            if (visibleIndices.has(index)) return;
            disposeStageMesh(mesh);
            this.meshes.delete(index);
        });
        visibleIndices.forEach(index => {
            const line = this.state.lines[index];
            const signature = `${line.fullText}|${line.translation ?? ''}|${this.state.theme.primaryColor}|${this.state.theme.secondaryColor}|${this.state.theme.accentColor}|${this.state.theme.karaokeFillColor ?? ''}|${this.state.theme.fontFamily ?? ''}|${this.state.theme.fontFamilyStack?.join(',') ?? ''}|${this.state.showTranslation}`;
            const existing = this.meshes.get(index);
            if (existing?.signature === signature) return;
            if (existing) disposeStageMesh(existing);
            this.meshes.set(index, this.createMesh(line, index));
        });
    }

    private render = () => {
        if (this.destroyed) return;
        this.updateCamera();
        const now = performance.now();
        const deltaSeconds = clamp((now - this.lastStageFrameAt) / 1000, 1 / 240, 0.08);
        this.lastStageFrameAt = now;
        this.trackScrollOffset = easeMineradio3DTrackOffset(this.trackScrollOffset, this.trackTargetOffset, deltaSeconds);
        if (!this.activePointerMode) this.stageSpin = advanceMineradio3DStageSpin(this.stageSpin, deltaSeconds);
        if (
            Math.abs(this.stageSpin.x) >= PARTICLE_ROTATION_REBASE_LIMIT ||
            Math.abs(this.stageSpin.y) >= PARTICLE_ROTATION_REBASE_LIMIT
        ) {
            const rebasedSpin = rebaseMineradio3DStageSpin(this.stageSpin);
            this.stageRoot.rotation.x -= rebasedSpin.offsetX;
            this.stageRoot.rotation.y -= rebasedSpin.offsetY;
            this.stageSpin = rebasedSpin.spin;
        }
        const rotationEase = resolveMineradio3DRotationEase(deltaSeconds);
        this.stageRoot.rotation.x += (this.stageSpin.x - this.stageRoot.rotation.x) * rotationEase;
        this.stageRoot.rotation.y += (this.stageSpin.y - this.stageRoot.rotation.y) * rotationEase;
        const motionEase = 1 - Math.pow(1 - 0.16, deltaSeconds * 60);
        this.stageRoot.position.x += (this.modelPan.x - this.stageRoot.position.x) * motionEase;
        this.stageRoot.position.y += (this.modelPan.y - this.stageRoot.position.y) * motionEase;
        const animationStart = Math.max(0, this.state.activeLineIndex - MINERADIO_ANIMATION_BUFFER);
        const animationEnd = Math.min(this.state.lines.length - 1, this.state.activeLineIndex + MINERADIO_ANIMATION_BUFFER);
        this.meshes.forEach((mesh, index) => {
            if (index < animationStart || index > animationEnd) {
                mesh.group.visible = false;
                mesh.group.userData.mineradio3DInCorridor = false;
                return;
            }
            const line = this.state.lines[index];
            const progress = lineKaraokeProgress(line, this.state.currentTime);
            const frame = resolveMineradio3DLineFrame(
                (this.trackVirtualIndices[index] ?? index) - this.trackScrollOffset,
                progress,
                this.frameScratch,
                index - this.state.activeLineIndex,
            );
            const isCurrent = index === this.state.activeLineIndex;
            const fullTextWidth = mesh.graphemeOffsets[mesh.graphemeOffsets.length - 1] ?? 0;
            const karaokeFillWidth = isCurrent
                ? resolveMineradio3DKaraokeFillWidth(line, this.state.currentTime, mesh.graphemeOffsets)
                : index < this.state.activeLineIndex ? fullTextWidth : 0;
            const karaokeChanged = Math.abs(karaokeFillWidth - mesh.lastKaraokeFillWidth) > 0.1;
            const canPaintKaraoke = !isCurrent || now - mesh.lastKaraokePaintAt >= KARAOKE_TEXTURE_FRAME_INTERVAL_MS;
            if (karaokeChanged && canPaintKaraoke) {
                paintKaraokeTextCanvas(mesh.textCanvas, line.fullText, this.state.theme, mesh.graphemeOffsets, karaokeFillWidth);
                mesh.texture.needsUpdate = true;
                mesh.backTexture.needsUpdate = true;
                mesh.lastKaraokeFillWidth = karaokeFillWidth;
                mesh.lastKaraokePaintAt = now;
            }
            if (mesh.translation && mesh.translationTexture && mesh.translationGraphemeOffsets.length > 1) {
                const translationFullWidth = mesh.translationGraphemeOffsets[mesh.translationGraphemeOffsets.length - 1] ?? 0;
                const karaokeProgress = fullTextWidth > 0 ? karaokeFillWidth / fullTextWidth : 0;
                const translationFillWidth = isCurrent
                    ? translationFullWidth * karaokeProgress
                    : index < this.state.activeLineIndex ? translationFullWidth : 0;
                const translationChanged = Math.abs(translationFillWidth - mesh.lastTranslationKaraokeFillWidth) > 0.1;
                if (translationChanged && canPaintKaraoke) {
                    paintKaraokeTextCanvas(
                        mesh.translationTexture.image,
                        line.translation ?? '',
                        this.state.theme,
                        mesh.translationGraphemeOffsets,
                        translationFillWidth,
                        this.state.theme.secondaryColor,
                    );
                    mesh.translationTexture.needsUpdate = true;
                    if (mesh.backTranslationTexture) mesh.backTranslationTexture.needsUpdate = true;
                    mesh.lastTranslationKaraokeFillWidth = translationFillWidth;
                }
            }
            if (isCurrent) {
                frame.scale = 1;
                frame.opacity = 1;
            }
            const glowOpacity = resolveLyricGlowOpacity(frame.opacity, isCurrent, false);
            const wasInCorridor = mesh.group.userData.mineradio3DInCorridor === true;
            mesh.group.userData.mineradio3DInCorridor = true;
            mesh.group.visible = frame.opacity > 0.002;
            if (wasInCorridor) {
                mesh.group.position.lerp(this.groupPositionTarget.set(frame.x, frame.y, frame.z), motionEase);
                mesh.group.scale.lerp(this.groupScaleTarget.set(frame.scale, frame.scale, frame.scale), motionEase);
                mesh.group.rotation.x += (0 - mesh.group.rotation.x) * motionEase;
                mesh.group.rotation.y += (frame.rotationY - mesh.group.rotation.y) * motionEase;
                mesh.group.rotation.z += (frame.rotationZ - mesh.group.rotation.z) * motionEase;
                mesh.material.opacity += (frame.opacity - mesh.material.opacity) * motionEase;
                mesh.backMaterial.opacity += (frame.opacity - mesh.backMaterial.opacity) * motionEase;
                mesh.glow.material.opacity += (glowOpacity - mesh.glow.material.opacity) * motionEase;
                mesh.glow.backMaterial.opacity += (glowOpacity - mesh.glow.backMaterial.opacity) * motionEase;
            } else {
                mesh.group.position.set(frame.x, frame.y, frame.z);
                mesh.group.scale.setScalar(frame.scale);
                mesh.group.rotation.set(0, frame.rotationY, frame.rotationZ);
                mesh.material.opacity = frame.opacity;
                mesh.backMaterial.opacity = frame.opacity;
                mesh.glow.material.opacity = glowOpacity;
                mesh.glow.backMaterial.opacity = glowOpacity;
            }
            if (mesh.translation && mesh.backTranslation && mesh.translationMaterial && mesh.backTranslationMaterial) {
                const translationFrame = resolveMineradio3DTranslationFrame(
                    isCurrent,
                    frame.opacity,
                    this.translationFrameScratch,
                    index < this.state.activeLineIndex,
                );
                const translationGlowOpacity = resolveLyricGlowOpacity(translationFrame.opacity, isCurrent, true);
                if (wasInCorridor) {
                    mesh.translation.position.y += (translationFrame.y - mesh.translation.position.y) * motionEase;
                } else {
                    mesh.translation.position.y = translationFrame.y;
                }
                mesh.backTranslation.position.y = mesh.translation.position.y;
                if (mesh.translationGlow) {
                    mesh.translationGlow.front.position.y = mesh.translation.position.y;
                    mesh.translationGlow.back.position.y = mesh.translation.position.y;
                }
                if (wasInCorridor) {
                    mesh.translation.scale.lerp(this.translationScaleTarget.set(translationFrame.scale, translationFrame.scale, translationFrame.scale), motionEase);
                } else {
                    mesh.translation.scale.setScalar(translationFrame.scale);
                }
                mesh.backTranslation.scale.copy(mesh.translation.scale);
                if (mesh.translationGlow) {
                    mesh.translationGlow.front.scale.copy(mesh.translation.scale);
                    mesh.translationGlow.back.scale.copy(mesh.translation.scale);
                }
                if (wasInCorridor) {
                    mesh.translationMaterial.opacity += (translationFrame.opacity - mesh.translationMaterial.opacity) * motionEase;
                    mesh.backTranslationMaterial.opacity += (translationFrame.opacity - mesh.backTranslationMaterial.opacity) * motionEase;
                    mesh.translationGlow?.material && (mesh.translationGlow.material.opacity += (translationGlowOpacity - mesh.translationGlow.material.opacity) * motionEase);
                    mesh.translationGlow?.backMaterial && (mesh.translationGlow.backMaterial.opacity += (translationGlowOpacity - mesh.translationGlow.backMaterial.opacity) * motionEase);
                } else {
                    mesh.translationMaterial.opacity = translationFrame.opacity;
                    mesh.backTranslationMaterial.opacity = translationFrame.opacity;
                    if (mesh.translationGlow) {
                        mesh.translationGlow.material.opacity = translationGlowOpacity;
                        mesh.translationGlow.backMaterial.opacity = translationGlowOpacity;
                    }
                }
            }
        });
        if (this.starRiver) {
            // Use the same visual rotation, pan, camera and frame as the lyrics.
            // Keep the star field full-bleed when narrow layouts shrink the text.
            this.starRiver.rotation.copy(this.stageRoot.rotation);
            this.starRiver.position.copy(this.stageRoot.position);
            this.starRiver.update(
                deltaSeconds,
                this.renderer.getPixelRatio(),
                this.state.staticMode ?? false,
            );
        }
        this.renderer.render(this.scene, this.camera);
        this.onVisualFrame?.(this.renderer.domElement);
    };

    private updateCamera() {
        this.orbit = easeMineradio3DOrbitCamera(this.orbit, this.orbitTarget);
        this.camera.position.copy(resolveMineradio3DOrbitPosition(this.orbit, this.orbitPositionTarget));
        this.camera.lookAt(0, 0, 0);
        this.camera.updateProjectionMatrix();
    }

    private scheduleRender() {
        if (this.destroyed || this.animationFrameId !== null) return;
        this.animationFrameId = requestAnimationFrame(this.tick);
    }

    private hasInteractiveMotion() {
        const orbitMoving = Math.abs(this.orbit.theta - this.orbitTarget.theta) > 0.0001 ||
            Math.abs(this.orbit.phi - this.orbitTarget.phi) > 0.0001 ||
            Math.abs(this.orbit.radius - this.orbitTarget.radius) > 0.002;
        const rotationMoving = this.activePointerMode === 'rotate' ||
            Math.abs(this.stageSpin.velocityX) > 0 ||
            Math.abs(this.stageSpin.velocityY) > 0 ||
            Math.abs(this.stageRoot.rotation.x - this.stageSpin.x) > 0.0001 ||
            Math.abs(this.stageRoot.rotation.y - this.stageSpin.y) > 0.0001;
        const panMoving = this.activePointerMode === 'pan' ||
            Math.abs(this.stageRoot.position.x - this.modelPan.x) > 0.0001 ||
            Math.abs(this.stageRoot.position.y - this.modelPan.y) > 0.0001;
        const trackMoving = resolveMineradio3DTrackMotionActive(this.trackScrollOffset, this.trackTargetOffset);
        return orbitMoving || rotationMoving || panMoving || trackMoving;
    }

    private tick = () => {
        this.animationFrameId = null;
        if (this.destroyed) return;
        this.render();
        // MR's ambient particle clock keeps running independently of song playback.
        const effectAnimating = this.starRiver !== null && !this.state.staticMode;
        if (!this.state.paused || effectAnimating || this.hasInteractiveMotion()) this.scheduleRender();
    };

    private start() {
        if (this.animationFrameId !== null || this.destroyed || this.state.paused) return;
        this.animationFrameId = requestAnimationFrame(this.tick);
    }

    private stop() {
        if (this.animationFrameId === null) return;
        cancelAnimationFrame(this.animationFrameId);
        this.animationFrameId = null;
    }

    destroy() {
        if (this.destroyed) return;
        this.destroyed = true;
        this.stop();
        this.resizeObserver.disconnect();
        this.renderer.domElement.removeEventListener('pointerdown', this.onPointerDown);
        this.renderer.domElement.removeEventListener('pointermove', this.onPointerMove);
        this.renderer.domElement.removeEventListener('pointerup', this.onPointerUp);
        this.renderer.domElement.removeEventListener('pointercancel', this.onPointerUp);
        this.renderer.domElement.removeEventListener('wheel', this.onWheel);
        this.renderer.domElement.removeEventListener('dblclick', this.onDoubleClick);
        this.meshes.forEach(disposeStageMesh);
        this.meshes.clear();
        this.starRiver?.dispose();
        this.starRiver = null;
        this.scene.remove(this.stageRoot);
        this.renderer.dispose();
        this.renderer.domElement.remove();
    }
}
