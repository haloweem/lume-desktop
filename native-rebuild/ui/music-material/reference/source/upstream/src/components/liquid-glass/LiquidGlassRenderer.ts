import {
    liquidGlassBackgroundShader,
    liquidGlassBlurShader,
    liquidGlassMainShader,
    liquidGlassVertexShader,
} from './liquidGlassShaders';
import { LIQUID_GLASS_STUDIO_PRESET } from './liquidGlassPreset';
import {
    resolveLiquidGlassReferenceFrame,
    resolveLiquidGlassTextureRegion,
    resolveLiquidGlassShapeRadiusFactor,
    resolveLiquidGlassTargetScale,
} from './liquidGlassModel';

export type LiquidGlassFrame = {
    width: number;
    height: number;
    dpr: number;
    viewportWidth: number;
    viewportHeight: number;
    viewportX: number;
    viewportY: number;
    expanded: boolean;
    cornerRadiusPx?: number;
    renderTargetScale?: number;
    reducedMotion: boolean;
    media: LiquidGlassMediaSource | null;
    mediaRatio: number;
    scene: HTMLCanvasElement | null;
    sceneBounds?: { left: number; top: number; right: number; bottom: number };
};

type LiquidGlassMediaSource = HTMLImageElement | HTMLVideoElement | HTMLCanvasElement;

type RenderTarget = {
    framebuffer: WebGLFramebuffer;
    texture: WebGLTexture;
    width: number;
    height: number;
    internalFormat: number;
    pixelType: number;
};

const computeGaussianKernelByRadius = (radius: number): Float32Array => {
    const clampedRadius = Math.max(0, Math.min(200, Math.round(radius)));
    if (clampedRadius === 0) return new Float32Array([1]);
    const sigma = clampedRadius / 3;
    const weights = new Float32Array(clampedRadius + 1);
    let sum = 0;
    for (let index = 0; index <= clampedRadius; index += 1) {
        const weight = Math.exp(-0.5 * (index * index) / (sigma * sigma));
        weights[index] = weight;
        sum += index === 0 ? weight : weight * 2;
    }
    for (let index = 0; index < weights.length; index += 1) {
        weights[index] /= sum;
    }
    return weights;
};

const BACKDROP_OVERSCAN_CSS_PX = 96;

const createShader = (
    gl: WebGL2RenderingContext,
    type: number,
    source: string,
): WebGLShader => {
    const shader = gl.createShader(type);
    if (!shader) {
        throw new Error('Unable to allocate liquid-glass shader');
    }
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        const message = gl.getShaderInfoLog(shader) || 'Unknown shader compile error';
        gl.deleteShader(shader);
        throw new Error(message);
    }
    return shader;
};

const createProgram = (
    gl: WebGL2RenderingContext,
    fragmentSource: string,
): WebGLProgram => {
    const vertexShader = createShader(gl, gl.VERTEX_SHADER, liquidGlassVertexShader);
    const fragmentShader = createShader(gl, gl.FRAGMENT_SHADER, fragmentSource);
    const program = gl.createProgram();
    if (!program) {
        gl.deleteShader(vertexShader);
        gl.deleteShader(fragmentShader);
        throw new Error('Unable to allocate liquid-glass program');
    }
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);
    gl.deleteShader(vertexShader);
    gl.deleteShader(fragmentShader);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        const message = gl.getProgramInfoLog(program) || 'Unknown program link error';
        gl.deleteProgram(program);
        throw new Error(message);
    }
    return program;
};

const createRenderTarget = (
    gl: WebGL2RenderingContext,
    width: number,
    height: number,
    highPrecision: boolean,
): RenderTarget => {
    const framebuffer = gl.createFramebuffer();
    const texture = gl.createTexture();
    if (!framebuffer || !texture) {
        if (framebuffer) gl.deleteFramebuffer(framebuffer);
        if (texture) gl.deleteTexture(texture);
        throw new Error('Unable to allocate liquid-glass render target');
    }
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    const internalFormat = highPrecision ? gl.RGBA16F : gl.RGBA8;
    const pixelType = highPrecision ? gl.FLOAT : gl.UNSIGNED_BYTE;
    gl.texImage2D(gl.TEXTURE_2D, 0, internalFormat, width, height, 0, gl.RGBA, pixelType, null);
    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    gl.framebufferTexture2D(
        gl.FRAMEBUFFER,
        gl.COLOR_ATTACHMENT0,
        gl.TEXTURE_2D,
        texture,
        0,
    );
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
        gl.deleteFramebuffer(framebuffer);
        gl.deleteTexture(texture);
        throw new Error('Liquid-glass framebuffer is incomplete');
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.bindTexture(gl.TEXTURE_2D, null);
    return { framebuffer, texture, width, height, internalFormat, pixelType };
};

const resizeRenderTarget = (
    gl: WebGL2RenderingContext,
    target: RenderTarget,
    width: number,
    height: number,
) => {
    if (target.width === width && target.height === height) return;
    target.width = width;
    target.height = height;
    gl.bindTexture(gl.TEXTURE_2D, target.texture);
    gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        target.internalFormat,
        width,
        height,
        0,
        gl.RGBA,
        target.pixelType,
        null,
    );
    gl.bindTexture(gl.TEXTURE_2D, null);
};

const uniform = (
    gl: WebGL2RenderingContext,
    program: WebGLProgram,
    name: string,
) => gl.getUniformLocation(program, name);

export class LiquidGlassRenderer {
    private readonly gl: WebGL2RenderingContext;
    private readonly backgroundProgram: WebGLProgram;
    private readonly blurProgram: WebGLProgram;
    private readonly mainProgram: WebGLProgram;
    private readonly vao: WebGLVertexArrayObject;
    private readonly vertexBuffer: WebGLBuffer;
    private readonly mediaTexture: WebGLTexture;
    private readonly sceneTexture: WebGLTexture;
    private readonly targets: [RenderTarget, RenderTarget, RenderTarget];
    private readonly blurWeights = computeGaussianKernelByRadius(LIQUID_GLASS_STUDIO_PRESET.blurRadius);
    private lastUploadedMedia: LiquidGlassMediaSource | null = null;
    private lastUploadedCanvasVersion: string | undefined;
    private disposed = false;

    static create(canvas: HTMLCanvasElement): LiquidGlassRenderer | null {
        const gl = canvas.getContext('webgl2', {
            alpha: true,
            antialias: true,
            depth: false,
            premultipliedAlpha: false,
            powerPreference: 'high-performance',
            preserveDrawingBuffer: true,
        });
        if (!gl) return null;
        try {
            return new LiquidGlassRenderer(gl);
        } catch (error) {
            if (import.meta.env.DEV) {
                console.warn('[LumenicLiquidGlass] renderer initialization failed', error);
            }
            return null;
        }
    }

    private constructor(gl: WebGL2RenderingContext) {
        this.gl = gl;
        this.backgroundProgram = createProgram(gl, liquidGlassBackgroundShader);
        this.blurProgram = createProgram(gl, liquidGlassBlurShader);
        this.mainProgram = createProgram(gl, liquidGlassMainShader);

        const vao = gl.createVertexArray();
        const vertexBuffer = gl.createBuffer();
        const mediaTexture = gl.createTexture();
        const sceneTexture = gl.createTexture();
        if (!vao || !vertexBuffer || !mediaTexture || !sceneTexture) {
            throw new Error('Unable to allocate liquid-glass geometry');
        }
        this.vao = vao;
        this.vertexBuffer = vertexBuffer;
        this.mediaTexture = mediaTexture;
        this.sceneTexture = sceneTexture;

        gl.bindVertexArray(vao);
        gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);
        gl.bufferData(
            gl.ARRAY_BUFFER,
            new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
            gl.STATIC_DRAW,
        );
        const location = gl.getAttribLocation(this.backgroundProgram, 'a_position');
        gl.enableVertexAttribArray(location);
        gl.vertexAttribPointer(location, 2, gl.FLOAT, false, 0, 0);
        gl.bindVertexArray(null);

        gl.bindTexture(gl.TEXTURE_2D, mediaTexture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
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
            new Uint8Array([0, 0, 0, 255]),
        );
        gl.bindTexture(gl.TEXTURE_2D, sceneTexture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
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
        gl.bindTexture(gl.TEXTURE_2D, null);

        const highPrecisionTargets = Boolean(gl.getExtension('EXT_color_buffer_float'));
        this.targets = [
            createRenderTarget(gl, 1, 1, highPrecisionTargets),
            createRenderTarget(gl, 1, 1, highPrecisionTargets),
            createRenderTarget(gl, 1, 1, highPrecisionTargets),
        ];
    }

    private resizeForFrame(
        frame: LiquidGlassFrame,
        targetSize: { width: number; height: number },
    ): void {
        if (this.disposed) return;
        const dpr = frame.dpr;
        const pixelWidth = Math.max(1, Math.round(frame.width * dpr));
        const pixelHeight = Math.max(1, Math.round(frame.height * dpr));
        const canvas = this.gl.canvas as HTMLCanvasElement;
        if (canvas.width !== pixelWidth) canvas.width = pixelWidth;
        if (canvas.height !== pixelHeight) canvas.height = pixelHeight;

        for (const target of this.targets) {
            resizeRenderTarget(this.gl, target, targetSize.width, targetSize.height);
        }
    }

    private uploadMedia(media: LiquidGlassMediaSource | null): boolean {
        if (!media) return false;
        const isVideo = typeof HTMLVideoElement !== 'undefined' && media instanceof HTMLVideoElement;
        const hasRetainedFrame = media === this.lastUploadedMedia;
        const isCanvas = typeof HTMLCanvasElement !== 'undefined' && media instanceof HTMLCanvasElement;
        if (!isVideo && !isCanvas && hasRetainedFrame) return true;
        const canvasVersion = isCanvas ? media.dataset.glassFrameVersion : undefined;
        if (isCanvas && hasRetainedFrame && canvasVersion !== undefined
            && canvasVersion === this.lastUploadedCanvasVersion) return true;
        if (isVideo && media.readyState < media.HAVE_CURRENT_DATA) return hasRetainedFrame;
        const gl = this.gl;
        try {
            gl.bindTexture(gl.TEXTURE_2D, this.mediaTexture);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, media);
            gl.bindTexture(gl.TEXTURE_2D, null);
            this.lastUploadedMedia = media;
            this.lastUploadedCanvasVersion = canvasVersion;
            return true;
        } catch {
            gl.bindTexture(gl.TEXTURE_2D, null);
            return hasRetainedFrame;
        }
    }

    private uploadScene(scene: HTMLCanvasElement | null): boolean {
        if (!scene || scene.width < 1 || scene.height < 1) return false;
        const gl = this.gl;
        try {
            gl.bindTexture(gl.TEXTURE_2D, this.sceneTexture);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, scene);
            gl.bindTexture(gl.TEXTURE_2D, null);
            return true;
        } catch {
            gl.bindTexture(gl.TEXTURE_2D, null);
            return false;
        }
    }

    private bindTarget(target: RenderTarget | null) {
        const gl = this.gl;
        gl.bindFramebuffer(gl.FRAMEBUFFER, target?.framebuffer ?? null);
        const width = target?.width ?? gl.drawingBufferWidth;
        const height = target?.height ?? gl.drawingBufferHeight;
        gl.viewport(0, 0, width, height);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
    }

    private bindTexture(program: WebGLProgram, name: string, texture: WebGLTexture, unit: number) {
        const gl = this.gl;
        gl.activeTexture(gl.TEXTURE0 + unit);
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.uniform1i(uniform(gl, program, name), unit);
    }

    private draw(program: WebGLProgram) {
        const gl = this.gl;
        gl.useProgram(program);
        gl.bindVertexArray(this.vao);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
        gl.bindVertexArray(null);
    }

    render(frame: LiquidGlassFrame): void {
        if (this.disposed) return;
        const gl = this.gl;
        const referenceFrame = resolveLiquidGlassReferenceFrame({
            viewportWidth: frame.viewportWidth,
            viewportHeight: frame.viewportHeight,
            surfaceX: frame.viewportX,
            surfaceY: frame.viewportY,
            surfaceWidth: frame.width,
            surfaceHeight: frame.height,
            devicePixelRatio: frame.dpr,
            overscanCssPx: BACKDROP_OVERSCAN_CSS_PX,
        });
        const targetScale = resolveLiquidGlassTargetScale(frame.renderTargetScale);
        const region = resolveLiquidGlassTextureRegion(
            referenceFrame, frame.width, frame.height, frame.dpr,
            targetScale, BACKDROP_OVERSCAN_CSS_PX,
        );
        this.resizeForFrame(frame, region);
        const uploaded = this.uploadMedia(frame.media);
        const uploadedScene = this.uploadScene(frame.scene);
        const shapeRadiusFactor = resolveLiquidGlassShapeRadiusFactor(
            frame.height,
            frame.cornerRadiusPx,
            LIQUID_GLASS_STUDIO_PRESET.shapeRadius,
        );

        this.bindTarget(this.targets[0]);
        gl.useProgram(this.backgroundProgram);
        this.bindTexture(this.backgroundProgram, 'u_background', this.mediaTexture, 0);
        this.bindTexture(this.backgroundProgram, 'u_scene', this.sceneTexture, 1);
        gl.uniform1i(uniform(gl, this.backgroundProgram, 'u_hasBackground'), uploaded ? 1 : 0);
        gl.uniform1i(uniform(gl, this.backgroundProgram, 'u_hasScene'), uploadedScene ? 1 : 0);
        const sceneBounds = frame.sceneBounds ?? { left: 0, top: 0, right: frame.viewportWidth, bottom: frame.viewportHeight };
        gl.uniform4f(uniform(gl, this.backgroundProgram, 'u_sceneBounds'), sceneBounds.left, sceneBounds.top,
            Math.max(1, sceneBounds.right - sceneBounds.left), Math.max(1, sceneBounds.bottom - sceneBounds.top));
        gl.uniform1f(uniform(gl, this.backgroundProgram, 'u_mediaAspect'), frame.mediaRatio || 1);
        gl.uniform2f(
            uniform(gl, this.backgroundProgram, 'u_viewportSize'),
            frame.viewportWidth,
            frame.viewportHeight,
        );
        gl.uniform2f(
            uniform(gl, this.backgroundProgram, 'u_surfaceOffset'),
            region.offsetCss.x,
            region.offsetCss.y,
        );
        gl.uniform2f(
            uniform(gl, this.backgroundProgram, 'u_surfaceSize'),
            region.sizeCss.width,
            region.sizeCss.height,
        );
        gl.uniform3f(uniform(gl, this.backgroundProgram, 'u_voidColor'), 0.005, 0.005, 0.006);
        gl.uniform2f(
            uniform(gl, this.backgroundProgram, 'u_capsuleSize'),
            frame.width,
            frame.height,
        );
        gl.uniform1f(
            uniform(gl, this.backgroundProgram, 'u_surfacePadding'),
            BACKDROP_OVERSCAN_CSS_PX,
        );
        gl.uniform2f(
            uniform(gl, this.backgroundProgram, 'u_capsuleOffset'),
            referenceFrame.surfaceCenterCss.x - region.offsetCss.x - frame.width / 2,
            referenceFrame.surfaceCenterCss.y - region.offsetCss.y - frame.height / 2,
        );
        gl.uniform1f(
            uniform(gl, this.backgroundProgram, 'u_shapeRadius'),
            shapeRadiusFactor,
        );
        gl.uniform1f(
            uniform(gl, this.backgroundProgram, 'u_shapeRoundness'),
            LIQUID_GLASS_STUDIO_PRESET.shapeRoundness,
        );
        gl.uniform1f(
            uniform(gl, this.backgroundProgram, 'u_shadowExpand'),
            LIQUID_GLASS_STUDIO_PRESET.shadowExpand,
        );
        gl.uniform1f(
            uniform(gl, this.backgroundProgram, 'u_shadowFactor'),
            LIQUID_GLASS_STUDIO_PRESET.shadowFactor,
        );
        gl.uniform2f(
            uniform(gl, this.backgroundProgram, 'u_shadowPosition'),
            -LIQUID_GLASS_STUDIO_PRESET.shadowPosition[0],
            -LIQUID_GLASS_STUDIO_PRESET.shadowPosition[1],
        );
        this.draw(this.backgroundProgram);

        this.bindTarget(this.targets[1]);
        gl.useProgram(this.blurProgram);
        this.bindTexture(this.blurProgram, 'u_source', this.targets[0].texture, 0);
        gl.uniform2f(
            uniform(gl, this.blurProgram, 'u_blurDirection'),
            1 / this.targets[0].width,
            0,
        );
        gl.uniform1i(
            uniform(gl, this.blurProgram, 'u_blurRadius'),
            LIQUID_GLASS_STUDIO_PRESET.blurRadius,
        );
        gl.uniform1fv(uniform(gl, this.blurProgram, 'u_blurWeights[0]'), this.blurWeights);
        this.draw(this.blurProgram);

        this.bindTarget(this.targets[2]);
        gl.useProgram(this.blurProgram);
        this.bindTexture(this.blurProgram, 'u_source', this.targets[1].texture, 0);
        gl.uniform2f(
            uniform(gl, this.blurProgram, 'u_blurDirection'),
            0,
            1 / this.targets[1].height,
        );
        gl.uniform1i(
            uniform(gl, this.blurProgram, 'u_blurRadius'),
            LIQUID_GLASS_STUDIO_PRESET.blurRadius,
        );
        gl.uniform1fv(uniform(gl, this.blurProgram, 'u_blurWeights[0]'), this.blurWeights);
        this.draw(this.blurProgram);

        this.bindTarget(null);
        gl.disable(gl.BLEND);
        gl.useProgram(this.mainProgram);
        this.bindTexture(this.mainProgram, 'u_background', this.targets[0].texture, 0);
        this.bindTexture(this.mainProgram, 'u_blurredBackground', this.targets[2].texture, 1);
        gl.uniform2f(
            uniform(gl, this.mainProgram, 'u_resolution'),
            gl.drawingBufferWidth,
            gl.drawingBufferHeight,
        );
        const preset = LIQUID_GLASS_STUDIO_PRESET;
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_dpr'), frame.dpr);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_refThickness'), preset.refThickness);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_refFactor'), preset.refFactor);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_refDispersion'), preset.refDispersion);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_refFresnelRange'), preset.refFresnelRange);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_refFresnelHardness'), preset.refFresnelHardness);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_refFresnelFactor'), preset.refFresnelFactor);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_glareRange'), preset.glareRange);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_glareHardness'), preset.glareHardness);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_glareFactor'), preset.glareFactor);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_glareConvergence'), preset.glareConvergence);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_glareOppositeFactor'), preset.glareOppositeFactor);
        gl.uniform1f(
            uniform(gl, this.mainProgram, 'u_glareAngle'),
            preset.glareAngle * Math.PI / 180,
        );
        gl.uniform1i(uniform(gl, this.mainProgram, 'u_blurEdge'), preset.blurEdge ? 1 : 0);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_shapeRadius'), shapeRadiusFactor);
        gl.uniform1f(uniform(gl, this.mainProgram, 'u_shapeRoundness'), preset.shapeRoundness);
        const opticalCanvasSize = referenceFrame.opticalSizeCss * frame.dpr;
        gl.uniform2f(
            uniform(gl, this.mainProgram, 'u_opticalViewportSize'),
            opticalCanvasSize,
            opticalCanvasSize,
        );
        gl.uniform2f(
            uniform(gl, this.mainProgram, 'u_backdropScale'),
            gl.drawingBufferWidth * targetScale / this.targets[0].width,
            gl.drawingBufferHeight * targetScale / this.targets[0].height,
        );
        gl.uniform2f(
            uniform(gl, this.mainProgram, 'u_refractionUvScale'),
            opticalCanvasSize * targetScale / this.targets[0].width,
            opticalCanvasSize * targetScale / this.targets[0].height,
        );
        gl.uniform4f(
            uniform(gl, this.mainProgram, 'u_tint'),
            preset.tint[0],
            preset.tint[1],
            preset.tint[2],
            preset.tint[3],
        );
        this.draw(this.mainProgram);
    }

    dispose(): void {
        if (this.disposed) return;
        this.disposed = true;
        const gl = this.gl;
        for (const target of this.targets) {
            gl.deleteFramebuffer(target.framebuffer);
            gl.deleteTexture(target.texture);
        }
        gl.deleteTexture(this.mediaTexture);
        gl.deleteTexture(this.sceneTexture);
        gl.deleteBuffer(this.vertexBuffer);
        gl.deleteVertexArray(this.vao);
        gl.deleteProgram(this.backgroundProgram);
        gl.deleteProgram(this.blurProgram);
        gl.deleteProgram(this.mainProgram);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.bindTexture(gl.TEXTURE_2D, null);
    }
}
