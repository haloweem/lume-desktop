import {SNOW_OVERLAY_SHADER_SOURCE} from './overlayShaders';
const VERTEX_SHADER_SOURCE = `#version 300 es
in vec2 a_position;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
}`;
const compileShader = (gl: WebGL2RenderingContext, type: number, source: string) => {
    const shader = gl.createShader(type);
    if (!shader) throw new Error('Snow shader allocation failed');
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        const message = gl.getShaderInfoLog(shader);
        gl.deleteShader(shader);
        throw new Error('Snow shader compile failed: ' + message);
    }
    return shader;
};
export function mountSnowCanvas(canvas, {onError} = {}) {
  const fragmentShader = SNOW_OVERLAY_SHADER_SOURCE, paused = false, usesMipmaps = false;
  const maxPixelRatio = 1.5, targetFps = 30;
  let disposed = false, animationFrame = 0;
  let vertexShader = null, pixelShader = null, program = null, positionBuffer = null, backgroundTexture = null;
        const gl = canvas.getContext('webgl2', {
            alpha: true,
            premultipliedAlpha: false,
            preserveDrawingBuffer: true,
        });
        if (!gl) throw new Error('Snow WebGL2 unavailable');
        try {
        vertexShader = compileShader(gl, gl.VERTEX_SHADER, VERTEX_SHADER_SOURCE);
        pixelShader = compileShader(gl, gl.FRAGMENT_SHADER, fragmentShader);
        if (!vertexShader || !pixelShader) return;

        program = gl.createProgram();
        if (!program) throw new Error('Snow program allocation failed');
        gl.attachShader(program, vertexShader);
        gl.attachShader(program, pixelShader);
        gl.linkProgram(program);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            throw new Error('Snow program link failed: ' + gl.getProgramInfoLog(program));
        }


        positionBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
            -1, -1, 1, -1, -1, 1,
            -1, 1, 1, -1, 1, 1,
        ]), gl.STATIC_DRAW);
        const positionLocation = gl.getAttribLocation(program, 'a_position');
        gl.enableVertexAttribArray(positionLocation);
        gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);

        backgroundTexture = gl.createTexture();
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

            }
        };

        const render = (now: number) => {
            if (disposed) return;
            try {
            if (!document.hidden && now - lastFrame >= frameInterval) {
                lastFrame = now - ((now - lastFrame) % frameInterval);
                resize();

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
                {
                    gl.drawArrays(gl.TRIANGLES, 0, 6);

                }
                didRender = true;
            }
            if (!paused || !didRender) animationFrame = requestAnimationFrame(render);
            } catch (error) { dispose(); onError?.(error); }
        };

        animationFrame = requestAnimationFrame(render);

        return { update() {}, dispose };
        } catch (error) { dispose(); throw error; }
        function dispose() {
            if (disposed) return;
            disposed = true;
            cancelAnimationFrame(animationFrame);
            if (positionBuffer) gl.deleteBuffer(positionBuffer);
            if (backgroundTexture) gl.deleteTexture(backgroundTexture);
            if (program) gl.deleteProgram(program);
            if (vertexShader) gl.deleteShader(vertexShader);
            if (pixelShader) gl.deleteShader(pixelShader);
            gl.getExtension('WEBGL_lose_context')?.loseContext();
            canvas.width = canvas.height = 1;
        }
}