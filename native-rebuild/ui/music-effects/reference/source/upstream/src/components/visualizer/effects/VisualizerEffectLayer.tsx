import React, { useEffect, useRef } from 'react';
import rainWindowDropAlphaUrl from '../../../assets/visualizer-effects/rain-window-drop-alpha.png';
import rainWindowDropColorUrl from '../../../assets/visualizer-effects/rain-window-drop-color.png';
import type { LumenicBackgroundMedia } from '../../../types';
import { drawMediaCover } from './backgroundMedia';
import { publishLiquidGlassWebGLSceneFrame, unregisterLiquidGlassWebGLSceneCanvas } from '../../liquid-glass/LiquidGlassWebGLSceneBridge';
import createRainWindowEffect from './rainWindowPhysics.js';
import { SNOW_OVERLAY_SHADER_SOURCE } from './overlayShaders';
import StarRiverCanvas from './StarRiverCanvas';
import { RAIN_SHADER_SOURCE, RIPPLES_SHADER_SOURCE } from './shaders';
import type { VisualizerEffectMode } from './definition';
import {
    getLumeEmbeddedTopInset,
    getLumeFullBleedLayerStyle,
    registerLumeTopVisualLayer,
} from '../lumeTopVisualCompositor';

interface VisualizerEffectLayerProps {
    mode?: VisualizerEffectMode;
    paused?: boolean;
    backgroundMedia?: LumenicBackgroundMedia | null;
    fallbackBackgroundUrl?: string | null;
}

const VERTEX_SHADER_SOURCE = `#version 300 es
in vec2 a_position;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
}`;

const compileShader = (gl: WebGL2RenderingContext, type: number, source: string) => {
    const shader = gl.createShader(type);
    if (!shader) return null;
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        console.warn('[VisualizerEffectLayer] Shader compile failed:', gl.getShaderInfoLog(shader));
        gl.deleteShader(shader);
        return null;
    }
    return shader;
};

interface ShaderCanvasProps {
    fragmentShader: string;
    paused?: boolean;
    backgroundMedia?: LumenicBackgroundMedia | null;
    fallbackBackgroundUrl?: string | null;
    requiresBackground?: boolean;
    usesMipmaps?: boolean;
    className?: string;
    effectOpacity?: number;
    maxPixelRatio?: number;
    targetFps?: number;
}

const ShaderCanvas: React.FC<ShaderCanvasProps> = ({
    fragmentShader,
    paused = false,
    backgroundMedia,
    fallbackBackgroundUrl,
    requiresBackground = false,
    usesMipmaps = false,
    className = '',
    effectOpacity = 1,
    maxPixelRatio = 1.5,
    targetFps = 30,
}) => {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const topInset = getLumeEmbeddedTopInset();
    const resolvedEffectOpacity = Math.max(0, Math.min(1, effectOpacity));

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;
        const gl = canvas.getContext('webgl2', {
            alpha: true,
            premultipliedAlpha: false,
            preserveDrawingBuffer: true,
        });
        if (!gl) return;
        const vertexShader = compileShader(gl, gl.VERTEX_SHADER, VERTEX_SHADER_SOURCE);
        const pixelShader = compileShader(gl, gl.FRAGMENT_SHADER, fragmentShader);
        if (!vertexShader || !pixelShader) return;

        const program = gl.createProgram();
        if (!program) return;
        gl.attachShader(program, vertexShader);
        gl.attachShader(program, pixelShader);
        gl.linkProgram(program);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            console.warn('[VisualizerEffectLayer] Program link failed:', gl.getProgramInfoLog(program));
            gl.deleteProgram(program);
            gl.deleteShader(vertexShader);
            gl.deleteShader(pixelShader);
            return;
        }
        const topVisualLayer = registerLumeTopVisualLayer({
            canvas,
            order: 10,
            opacity: resolvedEffectOpacity,
            blendMode: className.includes('mix-blend-screen') ? 'screen' : 'source-over',
            role: 'effect',
        });

        const positionBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
            -1, -1, 1, -1, -1, 1,
            -1, 1, 1, -1, 1, 1,
        ]), gl.STATIC_DRAW);
        const positionLocation = gl.getAttribLocation(program, 'a_position');
        gl.enableVertexAttribArray(positionLocation);
        gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);

        const backgroundTexture = gl.createTexture();
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, backgroundTexture);
        gl.texParameteri(
            gl.TEXTURE_2D,
            gl.TEXTURE_MIN_FILTER,
            usesMipmaps ? gl.LINEAR_MIPMAP_LINEAR : gl.LINEAR,
        );
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texImage2D(
            gl.TEXTURE_2D,
            0,
            gl.RGBA,
            1,
            1,
            0,
            gl.RGBA,
            gl.UNSIGNED_BYTE,
            new Uint8Array([0, 0, 0, 0]),
        );
        if (usesMipmaps) gl.generateMipmap(gl.TEXTURE_2D);

        const channelLocation = gl.getUniformLocation(program, 'iChannel0');
        gl.useProgram(program);
        if (channelLocation !== null) gl.uniform1i(channelLocation, 0);

        const uploadCanvas = document.createElement('canvas');
        const uploadContext = uploadCanvas.getContext('2d');
        const sourceUrl = backgroundMedia?.url || fallbackBackgroundUrl || null;
        const sourceKind = backgroundMedia?.url ? backgroundMedia.kind : 'image';
        let sourceElement: HTMLImageElement | HTMLVideoElement | null = null;
        let sourceReady = false;
        let textureDirty = true;

        if (sourceUrl) {
            if (sourceKind === 'video') {
                const video = document.createElement('video');
                video.crossOrigin = 'anonymous';
                video.loop = true;
                video.muted = true;
                video.playsInline = true;
                video.preload = 'auto';
                video.onloadeddata = () => {
                    sourceReady = true;
                    textureDirty = true;
                    void video.play().catch(() => undefined);
                };
                video.src = sourceUrl;
                sourceElement = video;
            } else {
                const image = new Image();
                image.crossOrigin = 'anonymous';
                image.decoding = 'async';
                image.onload = () => {
                    sourceReady = true;
                    textureDirty = true;
                };
                image.src = sourceUrl;
                sourceElement = image;
            }
        }

        let disposed = false;
        const resolutionLocation = gl.getUniformLocation(program, 'iResolution');
        const timeLocation = gl.getUniformLocation(program, 'iTime');
        const mouseLocation = gl.getUniformLocation(program, 'iMouse');
        const intensityLocation = gl.getUniformLocation(program, 'iIntensity');
        const thunderLocation = gl.getUniformLocation(program, 'iThunderVisual');
        const rippleDensityLocation = gl.getUniformLocation(program, 'iRippleDensity');
        const rippleSpeedLocation = gl.getUniformLocation(program, 'iRippleSpeed');
        const startTime = performance.now();
        const frameInterval = 1000 / Math.max(1, targetFps);
        let lastFrame = -frameInterval;
        let animationFrame = 0;
        let didRender = false;

        const resize = () => {
            const width = Math.max(1, Math.floor(canvas.clientWidth));
            const height = Math.max(1, Math.floor(canvas.clientHeight));
            const pixelRatio = Math.min(window.devicePixelRatio || 1, maxPixelRatio);
            const nextWidth = Math.max(1, Math.floor(width * pixelRatio));
            const nextHeight = Math.max(1, Math.floor(height * pixelRatio));
            if (canvas.width !== nextWidth || canvas.height !== nextHeight) {
                canvas.width = nextWidth;
                canvas.height = nextHeight;
                gl.viewport(0, 0, nextWidth, nextHeight);
                textureDirty = true;
            }
        };

        const uploadBackgroundTexture = () => {
            if (!sourceReady || !sourceElement || !uploadContext) return false;
            const isVideo = sourceElement instanceof HTMLVideoElement;
            if (!isVideo && !textureDirty) return true;

            const sourceWidth = isVideo
                ? (sourceElement as HTMLVideoElement).videoWidth
                : (sourceElement as HTMLImageElement).naturalWidth;
            const sourceHeight = isVideo
                ? (sourceElement as HTMLVideoElement).videoHeight
                : (sourceElement as HTMLImageElement).naturalHeight;
            if (!sourceWidth || !sourceHeight) return false;

            if (uploadCanvas.width !== canvas.width || uploadCanvas.height !== canvas.height) {
                uploadCanvas.width = canvas.width;
                uploadCanvas.height = canvas.height;
            }
            drawMediaCover(
                uploadContext,
                sourceElement,
                sourceWidth,
                sourceHeight,
                uploadCanvas.width,
                uploadCanvas.height,
            );
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, backgroundTexture);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, uploadCanvas);
            if (usesMipmaps) gl.generateMipmap(gl.TEXTURE_2D);
            textureDirty = false;
            return true;
        };

        const render = (now: number) => {
            if (disposed) return;
            if (!document.hidden && now - lastFrame >= frameInterval) {
                lastFrame = now - ((now - lastFrame) % frameInterval);
                resize();
                const hasBackgroundTexture = uploadBackgroundTexture();
                gl.useProgram(program);
                gl.uniform3f(resolutionLocation, canvas.width, canvas.height, 1);
                gl.uniform1f(timeLocation, (now - startTime) / 1000);
                gl.uniform4f(mouseLocation, 0, canvas.height * 0.5, 0, 0);
                gl.uniform1f(intensityLocation, 0.5);
                gl.uniform1f(thunderLocation, 0);
                gl.uniform1f(rippleDensityLocation, 0.16);
                gl.uniform1f(rippleSpeedLocation, 0.25);
                gl.clearColor(0, 0, 0, 0);
                gl.clear(gl.COLOR_BUFFER_BIT);
                if (!requiresBackground || hasBackgroundTexture) {
                    gl.drawArrays(gl.TRIANGLES, 0, 6);
                    publishLiquidGlassWebGLSceneFrame(canvas, now);
                    topVisualLayer.requestFrame();
                }
                didRender = true;
            }
            if (!paused || !didRender) animationFrame = requestAnimationFrame(render);
        };

        animationFrame = requestAnimationFrame(render);
        return () => {
            disposed = true;
            cancelAnimationFrame(animationFrame);
            unregisterLiquidGlassWebGLSceneCanvas(canvas);
            topVisualLayer.dispose();
            if (positionBuffer) gl.deleteBuffer(positionBuffer);
            if (backgroundTexture) gl.deleteTexture(backgroundTexture);
            if (sourceElement instanceof HTMLVideoElement) {
                sourceElement.pause();
                sourceElement.removeAttribute('src');
                sourceElement.load();
            } else if (sourceElement) {
                sourceElement.src = '';
            }
            gl.deleteProgram(program);
            gl.deleteShader(vertexShader);
            gl.deleteShader(pixelShader);
        };
    }, [
        backgroundMedia,
        fallbackBackgroundUrl,
        fragmentShader,
        maxPixelRatio,
        paused,
        requiresBackground,
        resolvedEffectOpacity,
        targetFps,
        topInset,
        usesMipmaps,
    ]);

    return (
        <canvas
            ref={canvasRef}
            data-lumenic-liquid-glass-scene-effect
            className={`absolute inset-x-0 bottom-0 w-full pointer-events-none ${className}`.trim()}
            style={{
                ...getLumeFullBleedLayerStyle(topInset),
                opacity: resolvedEffectOpacity,
            }}
        />
    );
};

const StarsCanvas: React.FC<{ paused?: boolean; }> = ({ paused = false }) => {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const topInset = getLumeEmbeddedTopInset();

    useEffect(() => {
        const canvas = canvasRef.current;
        const context = canvas?.getContext('2d');
        if (!canvas || !context) return;
        const ctx = context;
        const topVisualLayer = registerLumeTopVisualLayer({
            canvas,
            order: 10,
            opacity: 0.8,
            blendMode: 'screen',
            role: 'effect',
        });

        let width = 1;
        let height = 1;
        const resize = () => {
            width = Math.max(1, Math.floor(canvas.clientWidth));
            height = Math.max(1, Math.floor(canvas.clientHeight));
            canvas.width = width;
            canvas.height = height;
        };
        resize();
        const observer = new ResizeObserver(resize);
        observer.observe(canvas);

        class Star {
            x = Math.random() * width;
            y = Math.random() * height;
            radius = Math.random() * 1.5 + 0.1;
            opacity = Math.random();
            twinkleSpeed = (Math.random() * 0.02 + 0.005) * (Math.random() > 0.5 ? 1 : -1);

            update() {
                this.opacity += this.twinkleSpeed;
                if (this.opacity >= 1 || this.opacity <= 0.1) {
                    this.opacity = Math.min(1, Math.max(0.1, this.opacity));
                    this.twinkleSpeed *= -1;
                }
            }

            draw() {
                ctx.beginPath();
                ctx.arc(this.x, this.y, this.radius, 0, Math.PI * 2);
                ctx.fillStyle = `rgba(255, 255, 255, ${this.opacity})`;
                ctx.fill();
            }
        }

        class ShootingStar {
            x = 0;
            y = 0;
            length = 0;
            speed = 0;
            angle = 0;
            opacity = 0;
            active = false;
            delay = 0;

            constructor() { this.reset(); }
            reset() {
                this.x = Math.random() * width;
                this.y = Math.random() * height * 0.5 - height * 0.2;
                this.length = Math.random() * 80 + 40;
                this.speed = Math.random() * 10 + 15;
                this.angle = Math.PI / 4 + (Math.random() * 0.2 - 0.1);
                this.opacity = 0;
                this.active = false;
                this.delay = Math.random() * 200 + 50;
            }
            update() {
                if (!this.active) {
                    this.delay--;
                    if (this.delay <= 0) { this.active = true; this.opacity = 1; }
                    return;
                }
                this.x += Math.cos(this.angle) * this.speed;
                this.y += Math.sin(this.angle) * this.speed;
                this.opacity -= 0.015;
                if (this.opacity <= 0 || this.x > width || this.y > height) this.reset();
            }
            draw() {
                if (!this.active) return;
                const tailX = this.x - Math.cos(this.angle) * this.length;
                const tailY = this.y - Math.sin(this.angle) * this.length;
                const gradient = ctx.createLinearGradient(this.x, this.y, tailX, tailY);
                gradient.addColorStop(0, `rgba(255, 255, 255, ${this.opacity})`);
                gradient.addColorStop(1, 'rgba(255, 255, 255, 0)');
                ctx.beginPath();
                ctx.moveTo(this.x, this.y);
                ctx.lineTo(tailX, tailY);
                ctx.strokeStyle = gradient;
                ctx.lineWidth = 2;
                ctx.stroke();
            }
        }

        const stars = Array.from({ length: 400 }, () => new Star());
        const shootingStars = Array.from({ length: 4 }, () => new ShootingStar());
        let frame = 0;
        let lastFrame = 0;
        const render = (now: number) => {
            if (!document.hidden && now - lastFrame >= 1000 / 30) {
                lastFrame = now;
                ctx.clearRect(0, 0, width, height);
                stars.forEach(star => { star.update(); star.draw(); });
                ctx.lineCap = 'round';
                shootingStars.forEach(star => { star.update(); star.draw(); });
                publishLiquidGlassWebGLSceneFrame(canvas, now);
                topVisualLayer.requestFrame();
            }
            if (!paused) frame = requestAnimationFrame(render);
        };
        frame = requestAnimationFrame(render);

        return () => {
            observer.disconnect();
            cancelAnimationFrame(frame);
            unregisterLiquidGlassWebGLSceneCanvas(canvas);
            topVisualLayer.dispose();
        };
    }, [paused, topInset]);

    return (
        <canvas
            ref={canvasRef}
            data-lumenic-liquid-glass-scene-effect
            className="absolute inset-x-0 bottom-0 w-full mix-blend-screen opacity-80 pointer-events-none"
            style={getLumeFullBleedLayerStyle(topInset)}
        />
    );
};

const loadImage = (source: string) => new Promise<HTMLImageElement>((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = reject;
    image.src = source;
});

const loadBackgroundSource = (
    source: string,
    kind: LumenicBackgroundMedia['kind'],
) => new Promise<HTMLImageElement | HTMLVideoElement>((resolve, reject) => {
    if (kind === 'video') {
        const video = document.createElement('video');
        video.crossOrigin = 'anonymous';
        video.loop = true;
        video.muted = true;
        video.playsInline = true;
        video.preload = 'auto';
        video.onloadeddata = () => {
            void video.play().catch(() => undefined);
            resolve(video);
        };
        video.onerror = reject;
        video.src = source;
        return;
    }

    const image = new Image();
    image.crossOrigin = 'anonymous';
    image.onload = () => resolve(image);
    image.onerror = reject;
    image.src = source;
});

interface RainWindowCanvasProps {
    backgroundMedia?: LumenicBackgroundMedia | null;
    fallbackBackgroundUrl?: string | null;
}

export const RainWindowCanvas: React.FC<RainWindowCanvasProps> = ({
    backgroundMedia,
    fallbackBackgroundUrl,
}) => {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const topInset = getLumeEmbeddedTopInset();

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;
        const backgroundUrl = backgroundMedia?.url || fallbackBackgroundUrl;
        if (!backgroundUrl) return;
        const topVisualLayer = registerLumeTopVisualLayer({
            canvas,
            order: 10,
            role: 'effect',
            deferFrameEncoding: true,
        });
        const backgroundKind = backgroundMedia?.url ? backgroundMedia.kind : 'image';
        let disposed = false;
        let cleanup: (() => void) | null = null;
        let background: HTMLImageElement | HTMLVideoElement | null = null;
        Promise.all([
            loadImage(rainWindowDropColorUrl),
            loadImage(rainWindowDropAlphaUrl),
            loadBackgroundSource(backgroundUrl, backgroundKind),
        ]).then(([dropColor, dropAlpha, loadedBackground]) => {
            if (disposed) return;
            background = loadedBackground;
            cleanup = createRainWindowEffect(canvas, {
                background,
                dropColor,
                dropAlpha,
                onFrame: () => {
                    publishLiquidGlassWebGLSceneFrame(canvas);
                    topVisualLayer.requestFrame();
                },
            }, {
                rain_intensity: 0.6,
                rain_drop_size: 0.5,
                speed: 0.5,
                wind: 0.5,
                droplet_amount: 0.5,
                density: 0.7,
                refraction: 0.5,
                brightness: 0.5,
                blur_intensity: 0.5,
            });
        }).catch(error => console.warn('[VisualizerEffectLayer] Rain Window assets failed:', error));

        return () => {
            disposed = true;
            cleanup?.();
            unregisterLiquidGlassWebGLSceneCanvas(canvas);
            topVisualLayer.dispose();
            if (background instanceof HTMLVideoElement) {
                background.pause();
                background.removeAttribute('src');
                background.load();
            } else if (background) {
                background.src = '';
            }
        };
    }, [backgroundMedia, fallbackBackgroundUrl, topInset]);

    return (
        <canvas
            ref={canvasRef}
            data-lumenic-liquid-glass-scene-effect
            className="absolute inset-x-0 bottom-0 w-full pointer-events-none"
            style={getLumeFullBleedLayerStyle(topInset)}
        />
    );
};

const VisualizerEffectLayer: React.FC<VisualizerEffectLayerProps> = ({
    mode = 'none',
    paused = false,
    backgroundMedia,
    fallbackBackgroundUrl,
}) => {
    if (mode === 'none') return null;
    if (mode === 'rain-window') {
        return (
            <RainWindowCanvas
                backgroundMedia={backgroundMedia}
                fallbackBackgroundUrl={fallbackBackgroundUrl}
            />
        );
    }
    if (mode === 'rain') {
        return (
            <ShaderCanvas
                fragmentShader={RAIN_SHADER_SOURCE}
                paused={paused}
                backgroundMedia={backgroundMedia}
                fallbackBackgroundUrl={fallbackBackgroundUrl}
                requiresBackground
                usesMipmaps
            />
        );
    }
    if (mode === 'ripples') {
        return (
            <ShaderCanvas
                fragmentShader={RIPPLES_SHADER_SOURCE}
                paused={paused}
                backgroundMedia={backgroundMedia}
                fallbackBackgroundUrl={fallbackBackgroundUrl}
                requiresBackground
                effectOpacity={0.3}
                maxPixelRatio={1}
                targetFps={24}
            />
        );
    }
    if (mode === 'snow') {
        return <ShaderCanvas fragmentShader={SNOW_OVERLAY_SHADER_SOURCE} paused={paused} className="mix-blend-screen" />;
    }
    if (mode === 'star-river') {
        return (
            <StarRiverCanvas
                paused={paused}
                coverUrl={fallbackBackgroundUrl}
            />
        );
    }
    return <StarsCanvas paused={paused} />;
};

export default VisualizerEffectLayer;
