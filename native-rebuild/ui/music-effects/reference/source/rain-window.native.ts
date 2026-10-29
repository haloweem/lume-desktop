import { resolveRainWindowTextureSize } from './rainWindowModel';

// Extracted from the installed Lume 1.2.7 Rain Window theme.
// Only viewport reads are adapted to the host canvas so the settings preview can size correctly.
const createRainWindowEffect = function(canvas, resources, settings) { return (function() {
  'use strict';

  // ═══════════════════════════════════════════════
  //  Shaders (WebGL 2.0 ES 300)
  // ═══════════════════════════════════════════════

  var VERT_SRC = '#version 300 es\n' +
    'in vec2 a_position;\n' +
    'void main() {\n' +
    '  gl_Position = vec4(a_position, 0.0, 1.0);\n' +
    '}\n';

  var FRAG_SRC = '#version 300 es\n' +
    'precision highp float;\n' +
    'out vec4 fragColor;\n' +
    'uniform sampler2D u_waterMap;\n' +
    'uniform sampler2D u_textureShine;\n' +
    'uniform sampler2D u_textureFg;\n' +
    'uniform sampler2D u_textureBg;\n' +
    'uniform vec2 u_resolution;\n' +
    'uniform vec2 u_parallax;\n' +
    'uniform float u_parallaxFg;\n' +
    'uniform float u_parallaxBg;\n' +
    'uniform float u_textureRatio;\n' +
    'uniform bool u_renderShine;\n' +
    'uniform bool u_renderShadow;\n' +
    'uniform float u_minRefraction;\n' +
    'uniform float u_refractionDelta;\n' +
    'uniform float u_brightness;\n' +
    'uniform float u_alphaMultiply;\n' +
    'uniform float u_alphaSubtract;\n' +
    'vec4 blend(vec4 bg, vec4 fg) {\n' +
    '  vec3 bgm = bg.rgb * bg.a;\n' +
    '  vec3 fgm = fg.rgb * fg.a;\n' +
    '  float ia = 1.0 - fg.a;\n' +
    '  float a = (fg.a + bg.a * ia);\n' +
    '  vec3 rgb;\n' +
    '  if (a != 0.0) { rgb = (fgm + bgm * ia) / a; }\n' +
    '  else { rgb = vec3(0.0); }\n' +
    '  return vec4(rgb, a);\n' +
    '}\n' +
    'vec2 pixel() { return vec2(1.0) / u_resolution; }\n' +
    'vec2 parallaxOffset(float v) { return u_parallax * pixel() * v; }\n' +
    'vec2 texCoord() { return vec2(gl_FragCoord.x, u_resolution.y - gl_FragCoord.y) / u_resolution; }\n' +
    'vec2 scaledTexCoord() {\n' +
    '  // drawImageCover already matches the texture to the viewport; applying\n' +
    '  // a second aspect correction here crops the wallpaper a little more.\n' +
    '  return texCoord();\n' +
    '}\n' +
    'vec4 fgColor(float x, float y) {\n' +
    '  float p2 = u_parallaxFg * 2.0;\n' +
    '  vec2 s = vec2((u_resolution.x + p2) / u_resolution.x, (u_resolution.y + p2) / u_resolution.y);\n' +
    '  vec2 stc = texCoord() / s;\n' +
    '  vec2 off = vec2((1.0 - (1.0 / s.x)) / 2.0, (1.0 - (1.0 / s.y)) / 2.0);\n' +
    '  return texture(u_waterMap, (stc + off) + (pixel() * vec2(x, y)) + parallaxOffset(u_parallaxFg));\n' +
    '}\n' +
    'void main() {\n' +
    '  vec4 bg = texture(u_textureBg, scaledTexCoord() + parallaxOffset(u_parallaxBg));\n' +
    '  vec4 cur = fgColor(0.0, 0.0);\n' +
    '  float d = cur.b;\n' +
    '  float x = cur.g;\n' +
    '  float y = cur.r;\n' +
    '  float a = clamp(cur.a * u_alphaMultiply - u_alphaSubtract - d * 0.35, 0.0, 1.0);\n' +
    '  vec2 refraction = (vec2(x, y) - 0.5) * 2.0;\n' +
    '  vec2 rParallax = parallaxOffset(u_parallaxBg - u_parallaxFg);\n' +
    '  vec2 refractionPos = scaledTexCoord() + (pixel() * refraction * (u_minRefraction + (d * u_refractionDelta))) + rParallax;\n' +
    '  vec4 tex = texture(u_textureFg, refractionPos);\n' +
    '  if (u_renderShine) {\n' +
    '    float maxShine = 490.0;\n' +
    '    float minShine = maxShine * 0.18;\n' +
    '    vec2 shinePos = vec2(0.5) + ((1.0 / 512.0) * refraction) * -(minShine + ((maxShine - minShine) * d));\n' +
    '    vec4 shine = texture(u_textureShine, shinePos);\n' +
    '    tex = blend(tex, shine);\n' +
    '  }\n' +
    '  vec4 fg = vec4(tex.rgb * u_brightness, a);\n' +
    '  if (u_renderShadow) {\n' +
    '    float borderAlpha = fgColor(0.0, -(d * 6.0)).a;\n' +
    '    borderAlpha = borderAlpha * u_alphaMultiply - (u_alphaSubtract + 0.5);\n' +
    '    borderAlpha = clamp(borderAlpha, 0.0, 1.0) * 0.2;\n' +
    '    fg = blend(vec4(0.0, 0.0, 0.0, borderAlpha), fg);\n' +
    '  }\n' +
    '  fragColor = blend(bg, fg);\n' +
    '}\n';

  // ═══════════════════════════════════════════════
  //  Utility
  // ═══════════════════════════════════════════════

  function rand(a, b) { return a + Math.random() * (b - a); }
  function chance(p) { return Math.random() < p; }

  /** Biased random: favors lower values using Math.pow(n, power) */
  function randBias(a, b, power) {
    if (!power) power = 3;
    var n = Math.random();
    return a + Math.pow(n, power) * (b - a);
  }

  function createCanvas(w, h) {
    var c = document.createElement('canvas');
    c.width = w; c.height = h;
    return c;
  }

  // ═══════════════════════════════════════════════
  //  Drop sprite rendering
  // ═══════════════════════════════════════════════

  var DROP_SIZE = 64;
  var dropsGfx = [];
  var clearDropletsGfx = null;

  function renderDropsGfx(dropColor, dropAlpha) {
    dropsGfx = [];
    var dropBuffer = createCanvas(DROP_SIZE, DROP_SIZE);
    var dropBufferCtx = dropBuffer.getContext('2d');

    for (var i = 0; i < 255; i++) {
      var drop = createCanvas(DROP_SIZE, DROP_SIZE);
      var dropCtx = drop.getContext('2d');

      dropBufferCtx.clearRect(0, 0, DROP_SIZE, DROP_SIZE);
      dropBufferCtx.globalCompositeOperation = 'source-over';
      dropBufferCtx.drawImage(dropColor, 0, 0, DROP_SIZE, DROP_SIZE);
      dropBufferCtx.globalCompositeOperation = 'screen';
      dropBufferCtx.fillStyle = 'rgba(0,0,' + i + ',1)';
      dropBufferCtx.fillRect(0, 0, DROP_SIZE, DROP_SIZE);

      dropCtx.globalCompositeOperation = 'source-over';
      dropCtx.drawImage(dropAlpha, 0, 0, DROP_SIZE, DROP_SIZE);
      dropCtx.globalCompositeOperation = 'source-in';
      dropCtx.drawImage(dropBuffer, 0, 0, DROP_SIZE, DROP_SIZE);

      dropsGfx.push(drop);
    }

    clearDropletsGfx = createCanvas(128, 128);
    var clearCtx = clearDropletsGfx.getContext('2d');
    clearCtx.fillStyle = '#000';
    clearCtx.beginPath();
    clearCtx.arc(64, 64, 64, 0, Math.PI * 2);
    clearCtx.fill();
  }

  function drawDrop(ctx, drop, scale, minR, deltaR) {
    if (dropsGfx.length === 0) return;
    var x = drop.x, y = drop.y, r = drop.r;
    var spreadX = drop.spreadX, spreadY = drop.spreadY;
    var scaleX = 1, scaleY = 1.5;
    var d = Math.max(0, Math.min(1, ((r - minR) / deltaR) * 0.9));
    d *= 1 / (((spreadX + spreadY) * 0.5) + 1);
    d = Math.floor(d * (dropsGfx.length - 1));
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = 'source-over';
    ctx.drawImage(
      dropsGfx[d],
      (x - (r * scaleX * (spreadX + 1))) * scale,
      (y - (r * scaleY * (spreadY + 1))) * scale,
      (r * 2 * scaleX * (spreadX + 1)) * scale,
      (r * 2 * scaleY * (spreadY + 1)) * scale
    );
  }

  function clearDroplets(ctx, x, y, r, scale) {
    ctx.globalCompositeOperation = 'destination-out';
    ctx.drawImage(
      clearDropletsGfx,
      (x - r) * scale,
      (y - r) * scale,
      (r * 2) * scale,
      (r * 2) * scale * 1.5
    );
  }

  // ═══════════════════════════════════════════════
  //  WebGL helpers
  // ═══════════════════════════════════════════════

  function compileShader(gl, src, type) {
    var shader = gl.createShader(type);
    gl.shaderSource(shader, src);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      console.error('[RainWindow] Shader compile error:', gl.getShaderInfoLog(shader));
      gl.deleteShader(shader);
      return null;
    }
    return shader;
  }

  function createProgram(gl, vertSrc, fragSrc) {
    var vs = compileShader(gl, vertSrc, gl.VERTEX_SHADER);
    var fs = compileShader(gl, fragSrc, gl.FRAGMENT_SHADER);
    if (!vs || !fs) return null;
    var prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      console.error('[RainWindow] Program link error:', gl.getProgramInfoLog(prog));
      return null;
    }
    var posLoc = gl.getAttribLocation(prog, 'a_position');
    var buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(posLoc);
    gl.vertexAttribPointer(posLoc, 2, gl.FLOAT, false, 0, 0);
    return prog;
  }

  function createTextureFromImage(gl, img, unit) {
    var tex = gl.createTexture();
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return tex;
  }

  function updateTextureFromCanvas(gl, canvas, unit) {
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, canvas);
  }

  // ═══════════════════════════════════════════════
  //  Factory
  // ═══════════════════════════════════════════════

  return function(canvas, resources, settings) {
    if (!resources.background || !resources.dropColor || !resources.dropAlpha) {
      console.warn('[RainWindow] Missing resources, cannot start');
      return null;
    }

    var frame = 0;
    try {
    renderDropsGfx(resources.dropColor, resources.dropAlpha);

    // ─── DPI & dimensions ───
    var dpi = window.devicePixelRatio || 1;
    var W_css = Math.max(1, Math.floor(canvas.clientWidth || window.innerWidth));
    var H_css = Math.max(1, Math.floor(canvas.clientHeight || window.innerHeight));
    var resizePending = true;

    // Canvas is DPI-sized for crisp output
    function resizeCanvas() {
      dpi = window.devicePixelRatio || 1;
      W_css = Math.max(1, Math.floor(canvas.clientWidth || window.innerWidth));
      H_css = Math.max(1, Math.floor(canvas.clientHeight || window.innerHeight));
      canvas.width = Math.max(1, Math.floor(W_css * dpi));
      canvas.height = Math.max(1, Math.floor(H_css * dpi));
      resizePending = false;
    }
    resizeCanvas();

    var resizeObserver = typeof ResizeObserver === 'function'
      ? new ResizeObserver(function() { resizePending = true; })
      : null;
    if (resizeObserver) resizeObserver.observe(canvas);

    // Physics operates in CSS space; drawing multiplies by dpi
    var W = W_css, H = H_css;  // CSS-space dimensions

    // ─── Settings ───
    function getSetting(key, def) {
      return settings[key] != null ? +settings[key] : def;
    }

    // ─── Offscreen canvases (DPI-sized, used as WebGL textures) ───
    var liquidCanvas = createCanvas(canvas.width, canvas.height);
    var liquidCtx = liquidCanvas.getContext('2d');

    var dropletsCanvas = createCanvas(canvas.width, canvas.height);
    var dropletsCtx = dropletsCanvas.getContext('2d');

    // ─── Texture canvases (screen-size adaptive, drawn from background) ───
    function computeTexSizes(ww, hh) {
      var background = resolveRainWindowTextureSize(ww, hh, {
        baseScale: 0.4,
        minWidth: 768,
        minHeight: 512,
        maxWidth: 2048,
        maxHeight: 1365,
      });
      var foreground = resolveRainWindowTextureSize(ww, hh, {
        baseScale: 0.3,
        minWidth: 512,
        minHeight: 340,
        maxWidth: 1536,
        maxHeight: 1024,
      });
      return {
        bgW: background.width,
        bgH: background.height,
        fgW: foreground.width,
        fgH: foreground.height,
      };
    }
    var texSizes = computeTexSizes(W_css, H_css);
    var FG_W = texSizes.fgW, FG_H = texSizes.fgH;
    var BG_W = texSizes.bgW, BG_H = texSizes.bgH;
    var texFgCanvas = createCanvas(FG_W, FG_H);
    var texBgCanvas = createCanvas(BG_W, BG_H);
    var texFgCtx = texFgCanvas.getContext('2d');
    var texBgCtx = texBgCanvas.getContext('2d');

    function drawImageCover(ctx, img, cw, ch) {
      var imageWidth = img.naturalWidth || img.videoWidth || img.width;
      var imageHeight = img.naturalHeight || img.videoHeight || img.height;
      var ir = imageWidth / imageHeight;
      var cr = cw / ch;
      var sx, sy, sw, sh;
      if (ir > cr) { sw = imageHeight * cr; sh = imageHeight; sx = (imageWidth - sw) / 2; sy = 0; }
      else { sw = imageWidth; sh = imageWidth / cr; sx = 0; sy = (imageHeight - sh) / 2; }
      ctx.drawImage(img, sx, sy, sw, sh, 0, 0, cw, ch);
    }

    function blurCanvas(canvas, radius) {
    if (radius <= 0) return;
    var ctx = canvas.getContext('2d');
    var w = canvas.width, h = canvas.height;
    // Downscale then upscale to create a blur effect (works in all browsers)
    var scale = Math.max(1, Math.round(radius / 4));
    var sw = Math.max(1, Math.round(w / scale));
    var sh = Math.max(1, Math.round(h / scale));
    var temp = createCanvas(sw, sh);
    var tempCtx = temp.getContext('2d');
    // Use imageSmoothingEnabled for better downscale quality
    tempCtx.imageSmoothingEnabled = true;
    tempCtx.imageSmoothingQuality = 'high';
    tempCtx.drawImage(canvas, 0, 0, sw, sh);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(temp, 0, 0, sw, sh, 0, 0, w, h);
  }
    drawImageCover(texFgCtx, resources.background, FG_W, FG_H);
    drawImageCover(texBgCtx, resources.background, BG_W, BG_H);
    // Blur background texture only for frosted-glass effect
    // Foreground (what you see through drops) stays sharp
    // Use downscale-upscale approach (compatible with all browser engines)
    var screenScale = Math.max(W / BG_W, H / BG_H);
    blurCanvas(texBgCanvas, Math.round(7 * screenScale));

    // ─── WebGL setup ───
    var gl = canvas.getContext('webgl2', {
      alpha: true,
      premultipliedAlpha: false,
      preserveDrawingBuffer: true,
    });
    if (!gl) throw new Error('Rain Window WebGL2 unavailable');

    var prog = createProgram(gl, VERT_SRC, FRAG_SRC);
    if (!prog) throw new Error('Rain Window shader program failed');
    gl.useProgram(prog);

    function loc(name) { return gl.getUniformLocation(prog, name); }
    var uLoc = {
      resolution: loc('u_resolution'), parallax: loc('u_parallax'),
      parallaxFg: loc('u_parallaxFg'), parallaxBg: loc('u_parallaxBg'),
      textureRatio: loc('u_textureRatio'),
      renderShine: loc('u_renderShine'), renderShadow: loc('u_renderShadow'),
      minRefraction: loc('u_minRefraction'), refractionDelta: loc('u_refractionDelta'),
      brightness: loc('u_brightness'), alphaMultiply: loc('u_alphaMultiply'), alphaSubtract: loc('u_alphaSubtract'),
    };

    // Unit 0: water map
    var texWater = gl.createTexture();
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texWater);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    var waterLoc = loc('u_waterMap');
    if (waterLoc) gl.uniform1i(waterLoc, 0);

    // Unit 1: shine placeholder
    var shineCanvas = createCanvas(2, 2);
    createTextureFromImage(gl, shineCanvas, 1);
    if (loc('u_textureShine')) gl.uniform1i(loc('u_textureShine'), 1);

    // Unit 2: fg texture (small, foreground layer — refracted background)
    createTextureFromImage(gl, texFgCanvas, 2);
    if (loc('u_textureFg')) gl.uniform1i(loc('u_textureFg'), 2);

    // Unit 3: bg texture (large, background layer — direct view)
    createTextureFromImage(gl, texBgCanvas, 3);
    if (loc('u_textureBg')) gl.uniform1i(loc('u_textureBg'), 3);

    // ─── Physics state (CSS-space coordinates) ───
    var drops = [];
    var MAX_DROPS = 1200;

    // Scale drop sizes to keep visual proportion across resolutions
    var DROP_SIZE_SCALE = Math.max(0.8, Math.min(Math.sqrt((W * H) / (1920 * 1080)), 2.0));
    // Linear area factor for droplet density (keeps density consistent across resolutions)
    var AREA_FACTOR = (W * H) / (1920 * 1080);
    var MIN_R = 20 * DROP_SIZE_SCALE, MAX_R = 50 * DROP_SIZE_SCALE;
    var deltaR = MAX_R - MIN_R;
    var RAIN_CHANCE = 0.35;
    var RAIN_LIMIT = 6;
    var DROPLETS_RATE = 15;
    var DROPLETS_SIZE_MIN = 3 * DROP_SIZE_SCALE, DROPLETS_SIZE_MAX = 5.5 * DROP_SIZE_SCALE;
    var TRAIL_RATE = 1;
    var TRAIL_SCALE_RANGE = [0.25, 0.35];
    var COLLISION_RADIUS = 0.45;
    var COLLISION_RADIUS_INCREASE = 0.0002;
    var DROPLETS_CLEANING_RADIUS = 0.28;

    // Droplets (beads on glass) — persistent offscreen canvas, never fully cleared
    var dropletsCounter = 0;

    function createDropData(opts) {
      return {
        x: opts.x || 0, y: opts.y || 0, r: opts.r || MIN_R,
        spreadX: opts.spreadX || 0, spreadY: opts.spreadY || 0,
        momentum: opts.momentum || 0, momentumX: opts.momentumX || 0,
        lastSpawn: opts.lastSpawn || 0, nextSpawn: opts.nextSpawn || 0,
        parent: opts.parent || null, isNew: true, killed: false, shrink: 0,
      };
    }

    // ─── Update physics ───
    function updatePhysics(timeScale) {
      var intensity = getSetting('rain_intensity', 0.6);
      var wind = getSetting('wind', 0.5);
      var speedMul = getSetting('speed', 0.5) * 1.4 + 0.3;  // 0.3 ~ 1.7
      var density = getSetting('density', 0.5);

      var windForce = (wind - 0.5) * 2.0;  // -1.0 ~ 1.0

      // Dynamic drop sizes from user settings (applied on top of resolution scaling)
      var rawRainSize = getSetting('rain_drop_size', 0.5);
      var rainSizeMul = 0.6 + rawRainSize * 0.8;  // 0.6 ~ 1.4
      MIN_R = 20 * DROP_SIZE_SCALE * rainSizeMul;
      MAX_R = 50 * DROP_SIZE_SCALE * rainSizeMul;
      deltaR = MAX_R - MIN_R;

      // Keep droplet (accumulated beads) size at resolution-scaled base
      DROPLETS_SIZE_MIN = 3 * DROP_SIZE_SCALE;
      DROPLETS_SIZE_MAX = 5.5 * DROP_SIZE_SCALE;

      var areaMul = Math.sqrt((W * H) / (1024 * 768));
      var ts = timeScale * speedMul;

      // ─── Clear liquid canvas ───
      liquidCtx.clearRect(0, 0, liquidCanvas.width, liquidCanvas.height);

      // ─── Generate new rain drops (CSS-space coords) ───
      if (intensity > 0) {
        var rainChance = RAIN_CHANCE * intensity;
        var rainLimit = Math.ceil(RAIN_LIMIT * intensity);
        var count = 0;
        var limit = Math.ceil(rainLimit * ts * areaMul);
        while (chance(rainChance * ts * areaMul) && count < limit && drops.length < MAX_DROPS) {
          count++;
          var r = randBias(MIN_R, MAX_R, 3);
          drops.push(createDropData({
            x: rand(0, W),
            y: rand(-0.1 * H, 0.95 * H),
            r: r,
            momentum: 1 + ((r - MIN_R) * 0.1) + rand(0, 2),
            spreadX: 1.5,
            spreadY: 1.5,
          }));
        }
      }

      // ─── Accumulated droplets (beads on glass) ───
      // Persistent canvas: never cleared with clearRect. New beads added each frame.
      // Removed by clearDroplets (destination-out) when drops move through.
      // Composited onto liquid canvas BEFORE drops (original order).
      if (intensity > 0) {
        var dropletAmountMul = 0.2 + getSetting('droplet_amount', 0.5) * 1.8;  // 0.2 ~ 2.0
        dropletsCounter += Math.ceil(DROPLETS_RATE * ts * AREA_FACTOR * dropletAmountMul);
        while (dropletsCounter > 0) {
          dropletsCounter--;
          var br = randBias(DROPLETS_SIZE_MIN, DROPLETS_SIZE_MAX, 2);
          drawDrop(dropletsCtx, {
            x: rand(0, W),
            y: rand(0, H),
            r: br,
            spreadX: 0, spreadY: 0,
          }, dpi, 1, DROPLETS_SIZE_MAX - DROPLETS_SIZE_MIN);
        }
      }

      // Composite droplets onto liquid canvas FIRST (behind drops)
      liquidCtx.drawImage(dropletsCanvas, 0, 0, liquidCanvas.width, liquidCanvas.height);

      // ─── Sort drops by Y position (original behavior) ───
      drops.sort(function(a, b) {
        var va = (a.y * W) + a.x;
        var vb = (b.y * W) + b.x;
        return va > vb ? 1 : va === vb ? 0 : -1;
      });

      // ─── Update all drops ───
      var newDrops = [];

      for (var i = 0; i < drops.length; i++) {
        var drop = drops[i];
        if (drop.killed) continue;

        // Gravity (probabilistic — original model)
        if (chance((drop.r - MIN_R) * (0.1 / deltaR) * ts)) {
          drop.momentum += rand(0, (drop.r / MAX_R) * 4);
        }

        // Clean small drops (autoShrink)
        if (drop.r <= MIN_R && chance(0.05 * ts)) {
          drop.shrink += 0.01;
        }
        drop.r -= drop.shrink * ts;
        if (drop.r <= 0) { drop.killed = true; continue; }

        // Trails
        if (intensity > 0) {
          drop.lastSpawn += drop.momentum * ts * TRAIL_RATE;
          if (drop.lastSpawn > drop.nextSpawn && drops.length + newDrops.length < MAX_DROPS) {
            var trailR = drop.r * rand(TRAIL_SCALE_RANGE[0], TRAIL_SCALE_RANGE[1]);
            newDrops.push(createDropData({
              x: drop.x + rand(-drop.r, drop.r) * 0.1,
              y: drop.y - (drop.r * 0.01),
              r: trailR,
              spreadY: drop.momentum * 0.1,
              parent: drop,
            }));
            drop.r *= Math.pow(0.97, ts);
            drop.lastSpawn = 0;
            drop.nextSpawn = rand(MIN_R, MAX_R) - (drop.momentum * 2 * TRAIL_RATE) + (MAX_R - drop.r);
            if (drop.nextSpawn < 8) drop.nextSpawn = 8;
          }
        }

        // Spread decay
        drop.spreadX *= Math.pow(0.4, ts);
        drop.spreadY *= Math.pow(0.7, ts);

        // Position update
        var moved = drop.momentum > 0;
        if (moved && !drop.killed) {
          drop.y += drop.momentum * ts;
          drop.x += (drop.momentumX + windForce * 2.0) * ts;
          if (drop.y > H + drop.r || drop.x < -drop.r || drop.x > W + drop.r) drop.killed = true;
        }

        // Collision
        if ((moved || drop.isNew) && !drop.killed) {
          drop.isNew = false;
          for (var j = i + 1; j < drops.length && j < i + 70; j++) {
            var drop2 = drops[j];
            if (drop === drop2 || drop.r <= drop2.r || drop.parent === drop2 || drop2.parent === drop || drop2.killed) continue;
            var dx = drop2.x - drop.x;
            var dy = drop2.y - drop.y;
            var dist = Math.sqrt(dx * dx + dy * dy);
            if (dist < (drop.r + drop2.r) * (COLLISION_RADIUS + (drop.momentum * COLLISION_RADIUS_INCREASE * ts))) {
              var targetR = Math.sqrt((Math.PI * drop.r * drop.r + Math.PI * drop2.r * drop2.r * 0.8) / Math.PI);
              if (targetR > MAX_R) targetR = MAX_R;
              drop.r = targetR;
              drop.momentumX += dx * 0.1;
              drop.spreadX = 0; drop.spreadY = 0;
              drop2.killed = true;
              drop.momentum = Math.max(drop2.momentum, Math.min(40, drop.momentum + (targetR * 0.05) + 1));
            }
          }
        }

        // Momentum decay
        drop.momentum -= Math.max(1, (MIN_R * 0.5) - drop.momentum) * 0.1 * ts;
        if (drop.momentum < 0) drop.momentum = 0;
        drop.momentumX *= Math.pow(0.7, ts);

        // Clear accumulated droplets where drop passes (CSS-space coords, scaled by dpi inside function)
        if (moved) {
          clearDroplets(dropletsCtx, drop.x, drop.y, drop.r * DROPLETS_CLEANING_RADIUS, dpi);
        }

        if (!drop.killed) {
          newDrops.push(drop);
          drawDrop(liquidCtx, drop, dpi, MIN_R, deltaR);
        }
      }

      drops = newDrops;
    }

    // ─── Render loop ───
    var running = true;
    var lastTime = 0;
    var cleanup = false;
    var _lastBlurSetting = -1;
    var _videoFrameCounter = 0;
    var lastBackgroundVersion = resources.background.dataset?.glassFrameVersion;
    var backgroundDirty = false, backgroundDirtyBlur = false;
    var lastBackgroundBlur = 0;
    var onBackgroundFrame = function() { backgroundDirty = true; backgroundDirtyBlur = true; };
    resources.background.addEventListener?.('lume-glass-media-frame', onBackgroundFrame);

    function render(now) {
      if (!running || cleanup) { running = false; return; }
      try {

      // Resize check
      var newW = Math.floor((canvas.clientWidth || window.innerWidth));
      var newH = Math.floor((canvas.clientHeight || window.innerHeight));
      var newDpi = window.devicePixelRatio || 1;
      if (resizePending || newW !== W_css || newH !== H_css || newDpi !== dpi) {
        resizeCanvas();
        W = W_css; H = H_css;
        liquidCanvas.width = canvas.width;
        liquidCanvas.height = canvas.height;
        dropletsCanvas.width = canvas.width;
        dropletsCanvas.height = canvas.height;
        dropletsCtx.clearRect(0, 0, dropletsCanvas.width, dropletsCanvas.height);
        dropletsCounter = 0;
        drops.length = 0;
        DROP_SIZE_SCALE = Math.max(0.8, Math.min(Math.sqrt((W * H) / (1920 * 1080)), 2.0));
        AREA_FACTOR = (W * H) / (1920 * 1080);
        // Recompute texture sizes (grow/shrink with screen)
        var ns = computeTexSizes(W_css, H_css);
        if (ns.fgW !== FG_W || ns.fgH !== FG_H) {
          FG_W = ns.fgW; FG_H = ns.fgH;
          texFgCanvas.width = FG_W; texFgCanvas.height = FG_H;
        }
        if (ns.bgW !== BG_W || ns.bgH !== BG_H) {
          BG_W = ns.bgW; BG_H = ns.bgH;
          texBgCanvas.width = BG_W; texBgCanvas.height = BG_H;
        }
        drawImageCover(texFgCtx, resources.background, FG_W, FG_H);
        drawImageCover(texBgCtx, resources.background, BG_W, BG_H);
        var _screenScale = Math.max(W / BG_W, H / BG_H);
        blurCanvas(texBgCanvas, Math.round(7 * _screenScale));
        updateTextureFromCanvas(gl, texFgCanvas, 2);
        updateTextureFromCanvas(gl, texBgCanvas, 3);
        // Recalculate drop sizes for new resolution
        DROP_SIZE_SCALE = Math.max(0.8, Math.min(Math.sqrt((W * H) / (1920 * 1080)), 2.0));
        AREA_FACTOR = (W * H) / (1920 * 1080);
        MIN_R = 20 * DROP_SIZE_SCALE; MAX_R = 50 * DROP_SIZE_SCALE;
        deltaR = MAX_R - MIN_R;
        // Reset drops to avoid inconsistent sizes
        drops = [];
      }

      // Blur intensity setting change — re-blur background on the fly
      var newBlur = getSetting('blur_intensity', 0.5);
      if (newBlur !== _lastBlurSetting) {
        _lastBlurSetting = newBlur;
        drawImageCover(texBgCtx, resources.background, BG_W, BG_H);
        var blurRadius = Math.max(1, Math.round(7 * Math.max(W / BG_W, H / BG_H) * (0.1 + newBlur * 1.8)));
        blurCanvas(texBgCanvas, blurRadius);
        updateTextureFromCanvas(gl, texBgCanvas, 3);
      }

      var backgroundVersion = resources.background.dataset?.glassFrameVersion;
      var backgroundChanged = backgroundDirty || backgroundVersion !== lastBackgroundVersion;
      if ((resources.background instanceof HTMLVideoElement && resources.background.readyState >= 2)
          || (resources.background instanceof HTMLCanvasElement && backgroundChanged && resources.background.width > 0 && resources.background.height > 0)) {
        backgroundDirty = false;
        backgroundDirtyBlur = true;
        lastBackgroundVersion = backgroundVersion;
        drawImageCover(texFgCtx, resources.background, FG_W, FG_H);
        updateTextureFromCanvas(gl, texFgCanvas, 2);
        _videoFrameCounter++;
        if (now - lastBackgroundBlur >= 50) {
          lastBackgroundBlur = now;
          backgroundDirtyBlur = false;
          drawImageCover(texBgCtx, resources.background, BG_W, BG_H);
          var videoBlurRadius = Math.max(1, Math.round(7 * Math.max(W / BG_W, H / BG_H) * (0.1 + newBlur * 1.8)));
          blurCanvas(texBgCanvas, videoBlurRadius);
          updateTextureFromCanvas(gl, texBgCanvas, 3);
        }
      }

      // Flush the final changed background even when a video pauses between blur updates.
      if (backgroundDirtyBlur && now - lastBackgroundBlur >= 50) {
        backgroundDirtyBlur = false;
        lastBackgroundBlur = now;
        drawImageCover(texBgCtx, resources.background, BG_W, BG_H);
        var finalBlurRadius = Math.max(1, Math.round(7 * Math.max(W / BG_W, H / BG_H) * (0.1 + newBlur * 1.8)));
        blurCanvas(texBgCanvas, finalBlurRadius);
        updateTextureFromCanvas(gl, texBgCanvas, 3);
      }
      if (!lastTime) lastTime = now;
      var dt = (now - lastTime) / 1000.0;
      if (dt > 0.05) dt = 0.05;
      lastTime = now;
      var timeScale = dt * 60;

      updatePhysics(timeScale);

      // Upload water map
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, texWater);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, liquidCanvas);

      // Set uniforms
      var brightness = getSetting('brightness', 1.0);
      var refraction = getSetting('refraction', 0.5);
      var densitySetting = getSetting('density', 0.5);

      gl.uniform2f(uLoc.resolution, canvas.width, canvas.height);
      gl.uniform1f(uLoc.textureRatio, BG_W / BG_H);
      gl.uniform1i(uLoc.renderShine, 0);
      gl.uniform1i(uLoc.renderShadow, 0);
      gl.uniform2f(uLoc.parallax, 0, 0);
      gl.uniform1f(uLoc.parallaxFg, 20.0);
      gl.uniform1f(uLoc.parallaxBg, 5.0);

      // Refraction: use original-like values (min 50, range 50~500, controlled by slider)
      var ref = 50 + refraction * 450;
      gl.uniform1f(uLoc.minRefraction, ref);
      gl.uniform1f(uLoc.refractionDelta, ref);

      // Brightness: 0.9~1.2
      gl.uniform1f(uLoc.brightness, 0.9 + brightness * 0.3);

      // Alpha: lower multiply + higher subtract = clearer, more transparent drops
      var alphaMul = 2 + densitySetting * 4;    // 2~6
      var alphaSub = 2 + (1 - densitySetting) * 2;  // 4~2
      gl.uniform1f(uLoc.alphaMultiply, alphaMul);
      gl.uniform1f(uLoc.alphaSubtract, alphaSub);

      gl.viewport(0, 0, canvas.width, canvas.height);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
      gl.drawArrays(gl.TRIANGLES, 0, 6);
      if (typeof resources.onFrame === 'function') resources.onFrame(canvas);

      frame = requestAnimationFrame(render);
      } catch (error) { release(); resources.onError?.(error); }
    }

    frame = requestAnimationFrame(render);

    // ─── Resize ───
    var onResize = function() {
      resizePending = true;
    };
    window.addEventListener('resize', onResize);

    // ─── Cleanup ───
    return release;
    } catch (error) { release(); throw error; }
    function release() {
      if (cleanup) return;
      running = false;
      cleanup = true;
      cancelAnimationFrame(frame);
      resources.background.removeEventListener?.('lume-glass-media-frame', onBackgroundFrame);
      if (resizeObserver) resizeObserver.disconnect();
      if (onResize) window.removeEventListener('resize', onResize);
      if (gl) gl.getExtension('WEBGL_lose_context')?.loseContext();
      [liquidCanvas, dropletsCanvas, texFgCanvas, texBgCanvas, shineCanvas, clearDropletsGfx, ...dropsGfx].forEach(function(item) {
        if (item) item.width = item.height = 1;
      });
      dropsGfx = [];
      clearDropletsGfx = null;
      drops = [];
      canvas.width = canvas.height = 1;
    }
  };
})()(canvas, resources, settings);
};
export default createRainWindowEffect;

export const RAIN_WINDOW_SETTINGS = Object.freeze({rain_intensity: 0.6,
                rain_drop_size: 0.5,
                speed: 0.5,
                wind: 0.5,
                droplet_amount: 0.5,
                density: 0.7,
                refraction: 0.5,
                brightness: 0.5,
                blur_intensity: 0.5,});
