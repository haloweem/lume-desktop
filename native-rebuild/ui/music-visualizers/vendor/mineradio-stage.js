// Generated from Folia Major / Mineradio. See ../SOURCES.md and licenses/.

// src/components/visualizer/mineradio3d/stageRuntime.ts
import * as THREE2 from "./three.module.js";

// src/components/visualizer/colorMix.ts
var clamp = (value, min, max) => Math.max(min, Math.min(max, value));
var FALLBACK_RGB = { r: 255, g: 255, b: 255 };
var isFiniteChannel = (value) => Number.isFinite(value);
var formatRgba = (channels, alpha) => `rgba(${Math.round(clamp(channels.r, 0, 255))}, ${Math.round(clamp(channels.g, 0, 255))}, ${Math.round(clamp(channels.b, 0, 255))}, ${alpha})`;
var colorWithAlpha = (color, alpha) => {
  const normalizedAlpha = clamp(alpha, 0, 1);
  const normalizedColor = typeof color === "string" ? color.trim() : "";
  if (!normalizedColor) {
    return formatRgba(FALLBACK_RGB, normalizedAlpha);
  }
  if (normalizedColor.startsWith("#")) {
    const hex = normalizedColor.slice(1);
    const parse = (value) => Number.parseInt(value, 16);
    if (/^[0-9a-fA-F]{3}$/.test(hex)) {
      const r = parse(hex[0] + hex[0]);
      const g = parse(hex[1] + hex[1]);
      const b = parse(hex[2] + hex[2]);
      return formatRgba({ r, g, b }, normalizedAlpha);
    }
    if (/^[0-9a-fA-F]{6}$/.test(hex)) {
      const r = parse(hex.slice(0, 2));
      const g = parse(hex.slice(2, 4));
      const b = parse(hex.slice(4, 6));
      return formatRgba({ r, g, b }, normalizedAlpha);
    }
    return formatRgba(FALLBACK_RGB, normalizedAlpha);
  }
  const rgbMatch = normalizedColor.match(/^rgba?\(([^)]+)\)$/);
  if (rgbMatch) {
    const [r, g, b] = rgbMatch[1].split(",").slice(0, 3).map((part) => Number.parseFloat(part.trim()));
    if ([r, g, b].every(isFiniteChannel)) {
      return formatRgba({ r, g, b }, normalizedAlpha);
    }
    return formatRgba(FALLBACK_RGB, normalizedAlpha);
  }
  return normalizedColor;
};

// src/utils/lyrics/graphemeTiming.ts
var graphemeSegmenter = typeof Intl !== "undefined" && Intl.Segmenter ? new Intl.Segmenter(void 0, { granularity: "grapheme" }) : null;
var splitLyricGraphemes = (text) => {
  if (!text) {
    return [];
  }
  if (graphemeSegmenter) {
    return Array.from(graphemeSegmenter.segment(text), ({ segment }) => segment);
  }
  return Array.from(text);
};
var buildEvenGraphemeTimings = (text, startTime, endTime, wordIndex) => {
  const graphemes = splitLyricGraphemes(text);
  if (graphemes.length === 0) {
    return [];
  }
  const duration = Math.max(endTime - startTime, 0);
  const unitDuration = duration / graphemes.length;
  return graphemes.map((char, index) => ({
    char,
    startTime: startTime + unitDuration * index,
    endTime: index === graphemes.length - 1 ? endTime : startTime + unitDuration * (index + 1),
    ...typeof wordIndex === "number" ? { wordIndex } : {}
  }));
};
var buildWordGraphemeTimings = (word, wordIndex) => {
  if (!word.syllables?.length) {
    return buildEvenGraphemeTimings(word.text, word.startTime, word.endTime, wordIndex);
  }
  return word.syllables.flatMap(
    (syllable) => buildEvenGraphemeTimings(syllable.text, syllable.startTime, syllable.endTime, wordIndex)
  );
};
var findGraphemeSequence = (source, target, fromIndex) => {
  if (target.length === 0) {
    return fromIndex;
  }
  for (let index = fromIndex; index <= source.length - target.length; index += 1) {
    let matched = true;
    for (let targetIndex = 0; targetIndex < target.length; targetIndex += 1) {
      if (source[index + targetIndex] !== target[targetIndex]) {
        matched = false;
        break;
      }
    }
    if (matched) {
      return index;
    }
  }
  return -1;
};
var buildLineGraphemeTimeline = (line) => {
  const lineGraphemes = splitLyricGraphemes(line.fullText);
  if (lineGraphemes.length === 0) {
    return [];
  }
  if (line.words.length === 0) {
    return buildEvenGraphemeTimings(line.fullText, line.startTime, line.endTime);
  }
  const timeline = [];
  let cursor = 0;
  let lastResolvedTime = line.startTime;
  line.words.forEach((word, wordIndex) => {
    const wordGraphemes = splitLyricGraphemes(word.text);
    if (wordGraphemes.length === 0) {
      return;
    }
    const matchedStart = findGraphemeSequence(lineGraphemes, wordGraphemes, cursor);
    const start = matchedStart >= 0 ? matchedStart : cursor;
    const end = Math.min(start + wordGraphemes.length, lineGraphemes.length);
    for (let gapIndex = cursor; gapIndex < start; gapIndex += 1) {
      timeline[gapIndex] = {
        char: lineGraphemes[gapIndex],
        startTime: word.startTime,
        endTime: word.startTime
      };
    }
    const wordTimings = buildWordGraphemeTimings(word, wordIndex);
    for (let localIndex = 0; localIndex < end - start; localIndex += 1) {
      const timing = wordTimings[localIndex] ?? buildEvenGraphemeTimings(
        wordGraphemes[localIndex] ?? "",
        word.startTime,
        word.endTime,
        wordIndex
      )[0];
      if (!timing) {
        continue;
      }
      timeline[start + localIndex] = {
        ...timing,
        char: lineGraphemes[start + localIndex]
      };
      lastResolvedTime = Math.max(lastResolvedTime, timing.endTime);
    }
    cursor = Math.max(cursor, end);
  });
  for (let index = 0; index < lineGraphemes.length; index += 1) {
    if (timeline[index]) {
      continue;
    }
    timeline[index] = {
      char: lineGraphemes[index],
      startTime: lastResolvedTime,
      endTime: lastResolvedTime
    };
  }
  return timeline;
};

// src/components/visualizer/effects/StarRiverParticles.ts
import * as THREE from "./three.module.js";

// src/components/visualizer/effects/starRiverPointShaders.ts
var STAR_RIVER_VERTEX_SHADER = `precision highp float;
uniform float uTime, uBass, uMid, uTreble, uBeat, uEnergy, uPixel, uBloomSize;
uniform sampler2D uCoverTex;
uniform float uSceneScale;
attribute vec2 aUv;
attribute float aRand;
varying vec3 vColor;
varying float vBright, vRipple, vEdgeBoost, vAlpha, vSourceLum;
#define PI 3.14159265359
vec3 mod289(vec3 x){return x-floor(x*(1.0/289.0))*289.0;}
vec4 mod289v(vec4 x){return x-floor(x*(1.0/289.0))*289.0;}
vec4 perm(vec4 x){return mod289v(((x*34.0)+1.0)*x);}
float snoise(vec3 v){
  const vec2 C=vec2(1.0/6.0,1.0/3.0);
  const vec4 D=vec4(0.0,0.5,1.0,2.0);
  vec3 i=floor(v+dot(v,C.yyy));
  vec3 x0=v-i+dot(i,C.xxx);
  vec3 g=step(x0.yzx,x0.xyz); vec3 l=1.0-g;
  vec3 i1=min(g.xyz,l.zxy); vec3 i2=max(g.xyz,l.zxy);
  vec3 x1=x0-i1+C.xxx;
  vec3 x2=x0-i2+C.yyy;
  vec3 x3=x0-D.yyy;
  i=mod289(i);
  vec4 p=perm(perm(perm(i.z+vec4(0.0,i1.z,i2.z,1.0))+i.y+vec4(0.0,i1.y,i2.y,1.0))+i.x+vec4(0.0,i1.x,i2.x,1.0));
  float n_=0.142857142857;
  vec3 ns=n_*D.wyz-D.xzx;
  vec4 j=p-49.0*floor(p*ns.z*ns.z);
  vec4 x_=floor(j*ns.z); vec4 y_=floor(j-7.0*x_);
  vec4 x=x_*ns.x+ns.yyyy; vec4 y=y_*ns.x+ns.yyyy;
  vec4 h=1.0-abs(x)-abs(y);
  vec4 b0=vec4(x.xy,y.xy); vec4 b1=vec4(x.zw,y.zw);
  vec4 s0=floor(b0)*2.0+1.0; vec4 s1=floor(b1)*2.0+1.0;
  vec4 sh=-step(h,vec4(0.0));
  vec4 a0=b0.xzyw+s0.xzyw*sh.xxyy; vec4 a1=b1.xzyw+s1.xzyw*sh.zzww;
  vec3 p0=vec3(a0.xy,h.x); vec3 p1=vec3(a0.zw,h.y); vec3 p2=vec3(a1.xy,h.z); vec3 p3=vec3(a1.zw,h.w);
  vec4 norm=inversesqrt(vec4(dot(p0,p0),dot(p1,p1),dot(p2,p2),dot(p3,p3)));
  p0*=norm.x; p1*=norm.y; p2*=norm.z; p3*=norm.w;
  vec4 m=max(0.6-vec4(dot(x0,x0),dot(x1,x1),dot(x2,x2),dot(x3,x3)),0.0);
  m=m*m;
  return 42.0*dot(m*m,vec4(dot(p0,x0),dot(p1,x1),dot(p2,x2),dot(p3,x3)));
}

float hash11(float p) {
  return fract(sin(p * 127.1) * 43758.5453123);
}


void main() {
float t = uTime;
float uBurstAmt = 0.0;
vec3 pos;
vec3 coverColor = texture2D(uCoverTex, clamp(aUv, vec2(0.0012), vec2(0.9988))).rgb;
float maxRippleAmp = 0.0;
float bassGlow = smoothstep(0.07, 0.78, uBass) * 0.34 + uBeat * 0.014;
float midGlow = smoothstep(0.07, 0.62, uMid) * 0.42;
float highGlow = smoothstep(0.04, 0.46, uTreble) * 0.46;
float lane = aUv.y;
float transition = clamp(uBurstAmt, 0.0, 1.0);

if (lane < 0.80) {
  float laneWarp = snoise(vec3(aUv.x * 0.42, lane * 1.7, t * 0.026)) * 0.11 + (hash11(aRand * 73.1) - 0.5) * 0.045;
  float warpedLane = clamp(lane + laneWarp, 0.0, 0.80);
  float bandCoord = warpedLane / 0.80 * 5.65 + snoise(vec3(aUv.x * 0.82, lane * 2.25, t * 0.032)) * 0.62;
  float band = floor(bandCoord);
  float local = fract(bandCoord + hash11(band * 9.13 + aRand * 2.4) * 0.18);
  float bandN = clamp((band + 0.5) / 5.65, 0.0, 1.0);
  float seed = hash11(band * 19.17 + aRand * 31.0);
  float flow = fract(aUv.x + t * (0.0034 + bandN * 0.0038 + seed * 0.0022) + seed * 0.53);
  float arc = (flow - 0.5) * PI * (1.35 + bandN * 0.72 + seed * 0.24);
  float armCurve = sin(arc + bandN * 2.2 + seed * 5.3);
  float spiralRadius = 9.2 + bandN * 11.8 + seed * 6.0 + local * 2.9;
  float x = cos(arc * 0.72 + bandN * 0.92 + seed * 1.3) * spiralRadius + (flow - 0.5) * (13.5 + bandN * 9.5);
  float ribbonPhase = flow * PI * 2.0 * (0.55 + bandN * 0.24 + seed * 0.10) + t * (0.010 + bandN * 0.007) + seed * 5.7;
  float broadWave = sin(ribbonPhase) * 0.92;
  float fineWave = sin(ribbonPhase * (1.36 + seed * 0.62) - t * 0.044 + seed * 5.0) * 0.045;
  float yBase = (bandN - 0.5) * 13.2 + armCurve * (2.3 + bandN * 1.6) + (seed - 0.5) * 1.85 + snoise(vec3(bandN * 2.0, flow * 0.62, seed)) * 0.92;
  float ridgeCenter = 0.43 + (seed - 0.5) * 0.18;
  float ridge = exp(-pow((local - ridgeCenter) / (0.25 + seed * 0.04), 2.0));
  float softMask = smoothstep(0.010, 0.12, lane) * (1.0 - smoothstep(0.72, 0.81, lane));
  float ribbonNoise = snoise(vec3(flow * 1.18 + seed, bandN * 2.0, t * 0.018)) * 0.74;
  float zLayer = mix(-23.5, 15.5, bandN) + (seed - 0.5) * 6.0;

  pos.x = x + ribbonNoise * 1.40 + sin(t * 0.012 + seed * 8.0) * 0.22;
  pos.y = yBase + broadWave + fineWave + (local - 0.5) * (0.58 + ridge * 0.14);
  pos.z = zLayer + broadWave * 1.35 + ribbonNoise * 1.85;

  float pulseLine = 0.5 + 0.5 * sin(ribbonPhase * (1.7 + seed * 0.9) - t * 0.32 + seed * 6.0);
  vec3 aurora = mix(vec3(0.52, 0.86, 1.0), vec3(0.70, 0.58, 1.0), bandN);
  aurora = mix(aurora, vec3(0.96, 0.98, 0.92), bassGlow * 0.05);
  vAlpha = (0.18 + ridge * 0.78 + pulseLine * highGlow * 0.035 + bassGlow * 0.025) * softMask * (0.96 + transition * 0.02);
  vColor = mix(coverColor, aurora, 0.62 + ridge * 0.22) * (0.76 + ridge * 0.86 + pulseLine * highGlow * 0.05 + bassGlow * 0.04);
  maxRippleAmp = max(maxRippleAmp, ridge * (0.12 + midGlow * 0.05) + pulseLine * highGlow * 0.045 + bassGlow * 0.030);
} else {
  float q = (lane - 0.80) / 0.20;
  float seed = hash11(aRand * 917.0 + floor(q * 130.0));
  float depth = mix(-32.0, 18.0, seed);
  float drift = fract(aUv.x + t * (0.0014 + seed * 0.0048) + seed * 0.63);
  float cluster = snoise(vec3(seed * 2.0, q * 3.2, t * 0.007));
  float x = (drift - 0.5) * (45.0 + seed * 22.0) + cluster * 3.4;
  float y = (hash11(aRand * 331.0 + seed * 5.0) - 0.5) * 22.0 + sin(t * (0.018 + seed * 0.028) + seed * 7.0) * 0.86;
  float z = depth + sin(t * (0.020 + seed * 0.032) + aRand * 8.0) * 1.05;
  float twinkle = pow(0.5 + 0.5 * sin(t * (0.24 + seed * 0.42) + aRand * 17.0), 5.0);
  float dust = smoothstep(0.22, 0.98, hash11(aRand * 661.0 + floor(q * 160.0)));

  pos = vec3(x, y, z);
  vAlpha = dust * (0.16 + twinkle * 0.46 + highGlow * 0.025 + bassGlow * 0.018) * (1.0 - q * 0.06);
  vColor = mix(coverColor, vec3(0.92, 0.97, 1.0), 0.62 + twinkle * 0.14) * (0.72 + twinkle * 0.62 + bassGlow * 0.025);
  maxRippleAmp = max(maxRippleAmp, twinkle * highGlow * 0.055 + dust * bassGlow * 0.030);
}

if (transition > 0.001) {
  float bloom = smoothstep(0.0, 1.0, transition);
  vec2 burstVec = pos.xy + vec2(hash11(aRand * 31.0) - 0.5, hash11(aRand * 47.0) - 0.5) * 0.75;
  vec2 burstDir = burstVec / max(length(burstVec), 0.001);
  pos.xy += burstDir * bloom * 0.026;
  pos.xy += vec2(snoise(vec3(aRand, t * 0.014, 1.0)), snoise(vec3(aRand, t * 0.014, 5.0))) * bloom * 0.06;
  pos.xy *= 1.0 + bloom * 0.014;
  pos.z += (hash11(aRand * 123.0) - 0.5) * bloom * 0.18;
  vAlpha *= 0.86 + bloom * 0.22;
  maxRippleAmp = max(maxRippleAmp, bloom * 0.10);
}
  
vSourceLum = dot(max(vColor, vec3(0.0)), vec3(0.299, 0.587, 0.114));
vEdgeBoost = 0.0;
vColor = pow(max(vColor, vec3(0.0)), vec3(1.0 / 1.10));
vBright = 0.94 + maxRippleAmp * 0.34 + uBass * 0.020 + uEnergy * 0.026;
vRipple = clamp(maxRippleAmp * 1.5, 0.0, 1.0);
vec4 mvPos = modelViewMatrix * vec4(pos, 1.0);
float depthSize = 36.0 / max(0.5, -mvPos.z / uSceneScale);
float flowDrive = uBass * 0.070 + uMid * 0.046 + uTreble * 0.060 + uBeat * 0.055;
float sz = clamp(depthSize * (1.05 + flowDrive), 1.00, 5.45);
gl_PointSize = sz * uPixel * uBloomSize;
gl_Position = projectionMatrix * mvPos;
}`;
var STAR_RIVER_FRAGMENT_SHADER = `
precision highp float;
uniform sampler2D uDotTex;
uniform float uAlpha, uPreset, uParticleDim, uBackdropAdapt;
varying vec3 vColor;
varying float vBright, vRipple, vEdgeBoost, vAlpha, vSourceLum;

void main(){
  vec4 tex = texture2D(uDotTex, gl_PointCoord);
  if (tex.a < 0.02) discard;
  vec3 col = vColor * vBright;
  col = mix(col, col * 1.3 + vec3(0.05), vEdgeBoost * 0.35);
  col = mix(col, col * 1.2, vRipple * 0.4);
  float keepBlack = 1.0 - smoothstep(0.025, 0.115, vSourceLum);
  float nonBlack = 1.0 - keepBlack;
  float dotDist = length(gl_PointCoord - vec2(0.5)) * 2.0;
  float readableRim = smoothstep(0.44, 0.94, dotDist) * (1.0 - smoothstep(0.94, 1.08, dotDist)) * tex.a;
  float outLum = dot(col, vec3(0.299, 0.587, 0.114));
  float lightParticle = smoothstep(0.50, 0.82, outLum) * nonBlack;
  float darkParticle = (1.0 - smoothstep(0.20, 0.50, outLum)) * nonBlack;
  float backdropAdapt = clamp(uBackdropAdapt, 0.0, 1.0);
  if (backdropAdapt > 0.001) {
    col = mix(col, vec3(0.0), readableRim * lightParticle * (0.38 + backdropAdapt * 0.30));
    col = mix(col, vec3(1.0), readableRim * darkParticle * (0.20 + backdropAdapt * 0.12));
  }
  col = clamp(col, vec3(0.0), vec3(1.6));
  gl_FragColor = vec4(col, tex.a * uAlpha * uParticleDim * vAlpha);
}
`;

// src/components/visualizer/effects/StarRiverParticles.ts
var STAR_RIVER_SCENE_SCALE = 16 / 6.6 * Math.tan(44 * Math.PI / 360) / Math.tan(45 * Math.PI / 360);
var GRID = 183;
function createDotTexture() {
  const size = 64;
  const data = new Uint8Array(size * size * 4);
  const stops = [[0, 0.96], [0.42, 0.78], [0.72, 0.22], [1, 0]];
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const radius = Math.min(1, Math.hypot(x + 0.5 - 32, y + 0.5 - 32) / 31);
      const index = Math.max(1, stops.findIndex((stop) => stop[0] >= radius));
      const [left, right] = [stops[index - 1], stops[index]];
      const alpha = left[1] + (right[1] - left[1]) * (radius - left[0]) / (right[0] - left[0]);
      const offset = (y * size + x) * 4;
      data.set([255, 255, 255, Math.round(alpha * 255)], offset);
    }
  }
  const texture = new THREE.DataTexture(data, size, size, THREE.RGBAFormat);
  texture.minFilter = texture.magFilter = THREE.LinearFilter;
  texture.needsUpdate = true;
  return texture;
}
function createGeometry() {
  const count = GRID * GRID;
  const geometry = new THREE.BufferGeometry();
  const positions = new Float32Array(count * 3);
  const uv = new Float32Array(count * 2);
  const seeds = new Float32Array(count);
  for (let index = 0; index < count; index++) {
    const x = index % GRID;
    const y = Math.floor(index / GRID);
    positions[index * 3] = (x / (GRID - 1) - 0.5) * 4.8;
    positions[index * 3 + 1] = (y / (GRID - 1) - 0.5) * 4.8;
    uv[index * 2] = (x + 0.5) / GRID;
    uv[index * 2 + 1] = (y + 0.5) / GRID;
    seeds[index] = Math.random();
  }
  geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  geometry.setAttribute("aUv", new THREE.BufferAttribute(uv, 2));
  geometry.setAttribute("aRand", new THREE.BufferAttribute(seeds, 1));
  return geometry;
}
var StarRiverParticles = class extends THREE.Group {
  constructor(onTextureReady) {
    super();
    this.onTextureReady = onTextureReady;
    this.geometry = createGeometry();
    this.dot = createDotTexture();
    this.fallback = new THREE.DataTexture(new Uint8Array([28, 28, 40, 255]), 1, 1, THREE.RGBAFormat);
    this.uniforms = {
      uTime: { value: 0 },
      // Keep MR's idle density and point sizes; music must not amplify the field.
      uBass: { value: 0 },
      uMid: { value: 0 },
      uTreble: { value: 0 },
      uBeat: { value: 0 },
      uEnergy: { value: 0 },
      uPixel: { value: 1 },
      uSceneScale: { value: STAR_RIVER_SCENE_SCALE },
      uAlpha: { value: 1 },
      uParticleDim: { value: 1 },
      uBackdropAdapt: { value: 0.72 },
      uPreset: { value: 5 },
      uDotTex: { value: this.dot },
      uCoverTex: { value: this.fallback }
    };
    this.coverUrl = null;
    this.coverTexture = null;
    this.pendingCover = null;
    this.cancelCover = null;
    this.disposed = false;
    this.name = "mineradio-star-river";
    this.scale.setScalar(STAR_RIVER_SCENE_SCALE);
    this.fallback.needsUpdate = true;
    this.material = new THREE.ShaderMaterial({
      uniforms: { ...this.uniforms, uBloomSize: { value: 1 } },
      vertexShader: STAR_RIVER_VERTEX_SHADER,
      fragmentShader: STAR_RIVER_FRAGMENT_SHADER,
      transparent: true,
      depthWrite: false,
      blending: THREE.NormalBlending
    });
    const points = new THREE.Points(this.geometry, this.material);
    points.frustumCulled = false;
    points.renderOrder = -1;
    this.add(points);
  }
  setCoverUrl(url) {
    const nextUrl = url || null;
    if (this.disposed || this.coverUrl === nextUrl) return;
    this.coverUrl = nextUrl;
    this.cancelCover?.();
    this.pendingCover?.dispose();
    this.pendingCover = null;
    this.coverTexture?.dispose();
    this.coverTexture = null;
    this.uniforms.uCoverTex.value = this.fallback;
    if (!nextUrl) return;
    const pending = new THREE.Texture();
    const image = document.createElementNS("http://www.w3.org/1999/xhtml", "img");
    let active = true;
    const detach = () => {
      image.removeEventListener("load", loaded);
      image.removeEventListener("error", failed);
      this.cancelCover = null;
    };
    const loaded = () => {
      if (!active) return;
      active = false;
      detach();
      pending.image = image;
      const isJPEG = nextUrl.search(/\.jpe?g($|\?)/i) > 0 || nextUrl.search(/^data\:image\/jpeg/) === 0;
      pending.format = isJPEG ? THREE.RGBFormat : THREE.RGBAFormat;
      pending.needsUpdate = true;
      this.pendingCover = null;
      this.coverTexture = pending;
      this.uniforms.uCoverTex.value = pending;
      this.onTextureReady?.();
    };
    const failed = () => {
      if (!active) return;
      active = false;
      detach();
      image.removeAttribute("src");
      pending.dispose();
      this.pendingCover = null;
    };
    this.pendingCover = pending;
    this.cancelCover = () => {
      if (!active) return;
      active = false;
      detach();
      image.removeAttribute("src");
    };
    image.addEventListener("load", loaded);
    image.addEventListener("error", failed);
    if (nextUrl.substr(0, 5) !== "data:") image.crossOrigin = "anonymous";
    image.src = nextUrl;
  }
  update(deltaSeconds, pixelRatio, staticMode) {
    if (this.disposed) return;
    if (!staticMode) this.uniforms.uTime.value += deltaSeconds;
    this.uniforms.uPixel.value = pixelRatio;
  }
  dispose() {
    if (this.disposed) return;
    this.disposed = true;
    this.parent?.remove(this);
    this.geometry.dispose();
    this.material.dispose();
    this.dot.dispose();
    this.fallback.dispose();
    this.cancelCover?.();
    this.pendingCover?.dispose();
    this.coverTexture?.dispose();
    this.pendingCover = this.coverTexture = null;
  }
};

// src/components/visualizer/mineradio3d/stageRuntime.ts
var MIN_CANVAS_WIDTH = 640;
var CANVAS_HEIGHT = 180;
var TRANSLATION_CANVAS_HEIGHT = 128;
var MINERADIO_GLOW_TEXTURE_SCALE = 0.5;
var MINERADIO_CINEMA_LINE_COUNT = 5;
var MINERADIO_CONTEXT_SPREAD = 1.96;
var MINERADIO_TRANSLATION_GAP = 0.92;
var MINERADIO_TRANSLATION_SCALE = 0.65;
var MINERADIO_PRIMARY_WORLD_WIDTH = 6.1;
var MINERADIO_MASK_WIDTH = 2048;
var MINERADIO_FONT_SIZE = 128;
var MINERADIO_CAMERA_RADIUS = 6.6;
var MINERADIO_CAMERA_FOV = 45;
var MINERADIO_LYRIC_GROUP_Z = 1.46;
var LUME_CAMERA_RADIUS = 16;
var LUME_CAMERA_FOV = 44;
var ACTIVE_LINE_Z = 0.055;
var MINERADIO_TRANSLATION_VISUAL_GAP = 0.98 + (MINERADIO_TRANSLATION_GAP - 0.28) * 0.36;
var MINERADIO_PRIMARY_TRANSLATED_SLOT_STEP = MINERADIO_TRANSLATION_VISUAL_GAP + 0.82 + MINERADIO_TRANSLATION_SCALE * 0.14;
var MINERADIO_PRIMARY_CONTEXT_SLOT_STEP = 1.04 + (MINERADIO_CONTEXT_SPREAD - 1) * 0.1;
var MINERADIO_TO_LUME_SCENE_SCALE = (LUME_CAMERA_RADIUS - ACTIVE_LINE_Z) / (MINERADIO_CAMERA_RADIUS - MINERADIO_LYRIC_GROUP_Z - ACTIVE_LINE_Z) * (Math.tan(LUME_CAMERA_FOV * Math.PI / 360) / Math.tan(MINERADIO_CAMERA_FOV * Math.PI / 360));
var MINERADIO_BASE_LINE_STEP = MINERADIO_PRIMARY_WORLD_WIDTH * (MINERADIO_FONT_SIZE / MINERADIO_MASK_WIDTH);
var TRACK_LINE_STEP = MINERADIO_BASE_LINE_STEP * (1 + (MINERADIO_CONTEXT_SPREAD - 1) * 0.32) * 1.06 * MINERADIO_TO_LUME_SCENE_SCALE;
var TRANSLATION_LINE_STEP = MINERADIO_BASE_LINE_STEP * 1.04 * MINERADIO_TO_LUME_SCENE_SCALE;
var TRACK_VISIBLE_RADIUS = MINERADIO_CINEMA_LINE_COUNT * 0.5 * MINERADIO_PRIMARY_TRANSLATED_SLOT_STEP;
var TRACK_CONTEXT_OPACITY = 0.54;
var TRACK_COMPLETED_OPACITY = 0.78;
var MIN_ORBIT_RADIUS = 8;
var MAX_ORBIT_RADIUS = 28;
var ORBIT_WHEEL_RADIUS_STEP = 0.012;
var PARTICLE_POINTER_SPIN_X = 32e-4;
var PARTICLE_POINTER_SPIN_Y = 34e-4;
var PARTICLE_SPIN_MAX = 6.2;
var MINERADIO_ANIMATION_BUFFER = 3;
var MINERADIO_MESH_BUFFER = MINERADIO_ANIMATION_BUFFER + 2;
var PARTICLE_ROTATION_REBASE_LIMIT = Math.PI * 10;
var KARAOKE_TEXTURE_FRAME_INTERVAL_MS = 1e3 / 30;
var KARAOKE_BASE_ALPHA = 1;
var MINERADIO_NARROW_STAGE_ASPECT = 0.62;
var MINERADIO_TEXT_TEXTURE_SCALE = 1.5;
var ACTIVE_LYRIC_GLOW_OPACITY = 0.3;
var CONTEXT_LYRIC_GLOW_OPACITY = 0.1;
var ACTIVE_TRANSLATION_GLOW_OPACITY = 0.17;
var CONTEXT_TRANSLATION_GLOW_OPACITY = 0.06;
var clamp2 = (value, min, max) => Math.max(min, Math.min(max, value));
var resolveMineradio3DResponsiveScale = (aspect) => clamp2(aspect / MINERADIO_NARROW_STAGE_ASPECT, 0.5, 1);
var resolveMineradio3DBackfaceTextureTransform = () => ({ repeatX: -1, offsetX: 1 });
var createMineradio3DOrbitState = () => ({ theta: 0, phi: 0.06, radius: 16 });
var applyMineradio3DOrbitWheel = (orbit, deltaY, visualRadius = orbit.radius) => {
  const radiusStep = deltaY * ORBIT_WHEEL_RADIUS_STEP;
  const queuedDelta = orbit.radius - visualRadius;
  const reversesQueuedMotion = radiusStep !== 0 && queuedDelta !== 0 && Math.sign(radiusStep) !== Math.sign(queuedDelta);
  const radiusBase = reversesQueuedMotion ? visualRadius : orbit.radius;
  return {
    ...orbit,
    radius: clamp2(radiusBase + radiusStep, MIN_ORBIT_RADIUS, MAX_ORBIT_RADIUS)
  };
};
var easeMineradio3DOrbitCamera = (current, target) => ({
  theta: current.theta + (target.theta - current.theta) * 0.1,
  phi: current.phi + (target.phi - current.phi) * 0.1,
  radius: current.radius + (target.radius - current.radius) * 0.07
});
var resolveMineradio3DPointerMode = (button) => {
  if (button === 0) return "rotate";
  if (button === 1) return "pan";
  return null;
};
var applyMineradio3DStagePan = (pan, deltaX, deltaY, viewportHeight, radius, fov) => {
  const worldPerPixel = 2 * radius * Math.tan(fov * Math.PI / 360) / Math.max(1, viewportHeight);
  return {
    x: pan.x + deltaX * worldPerPixel,
    y: pan.y - deltaY * worldPerPixel
  };
};
var applyMineradio3DStageDrag = (spin, deltaX, deltaY, deltaSeconds) => {
  const rotationX = deltaY * PARTICLE_POINTER_SPIN_X;
  const rotationY = deltaX * PARTICLE_POINTER_SPIN_Y;
  const safeDeltaSeconds = Math.max(1 / 120, deltaSeconds);
  const instantVelocityX = clamp2(rotationX / safeDeltaSeconds * 0.46, -PARTICLE_SPIN_MAX, PARTICLE_SPIN_MAX);
  const instantVelocityY = clamp2(rotationY / safeDeltaSeconds * 0.46, -PARTICLE_SPIN_MAX, PARTICLE_SPIN_MAX);
  return {
    x: spin.x + rotationX,
    y: spin.y + rotationY,
    velocityX: instantVelocityX,
    velocityY: instantVelocityY
  };
};
var advanceMineradio3DStageSpin = (spin, deltaSeconds) => {
  const velocityX = spin.velocityX * Math.pow(0.9, deltaSeconds * 60);
  const velocityY = spin.velocityY * Math.pow(0.9, deltaSeconds * 60);
  return {
    x: spin.x + spin.velocityX * deltaSeconds,
    y: spin.y + spin.velocityY * deltaSeconds,
    velocityX: Math.abs(velocityX) < 0.01 ? 0 : velocityX,
    velocityY: Math.abs(velocityY) < 0.01 ? 0 : velocityY
  };
};
var rebaseMineradio3DStageSpin = (spin, limit = PARTICLE_ROTATION_REBASE_LIMIT) => {
  let offsetX = 0;
  let offsetY = 0;
  if (Math.abs(spin.x) >= limit) offsetX = Math.round(spin.x / (Math.PI * 2)) * Math.PI * 2;
  if (Math.abs(spin.y) >= limit) offsetY = Math.round(spin.y / (Math.PI * 2)) * Math.PI * 2;
  return {
    spin: {
      x: spin.x - offsetX,
      y: spin.y - offsetY,
      velocityX: spin.velocityX,
      velocityY: spin.velocityY
    },
    offsetX,
    offsetY
  };
};
var resolveMineradio3DOrbitPosition = (orbit, output = new THREE2.Vector3()) => {
  const cosPhi = Math.cos(orbit.phi);
  return output.set(
    orbit.radius * cosPhi * Math.sin(orbit.theta),
    orbit.radius * Math.sin(orbit.phi),
    orbit.radius * cosPhi * Math.cos(orbit.theta)
  );
};
var resolveMineradio3DLineWindow = (activeLineIndex, lineCount) => {
  if (lineCount <= 0) return [];
  const center = clamp2(activeLineIndex, 0, lineCount - 1);
  const start = Math.max(0, center - MINERADIO_MESH_BUFFER);
  const end = Math.min(lineCount - 1, center + MINERADIO_MESH_BUFFER);
  return Array.from({ length: end - start + 1 }, (_, index) => start + index);
};
var normalizeLyricText = (text) => (text ?? "").trim().replace(/\s+/g, " ");
var resolveTranslationText = (line) => {
  const translation = normalizeLyricText(line?.translation);
  if (!translation || translation === normalizeLyricText(line?.fullText)) return "";
  return translation;
};
var resolveMineradio3DPrimaryVirtualIndices = (lines, showTranslation = true) => {
  const indices = new Array(lines.length);
  if (lines.length === 0) return indices;
  indices[0] = 0;
  for (let index = 1; index < lines.length; index += 1) {
    const needsTranslationSlot = showTranslation && (Boolean(resolveTranslationText(lines[index - 1])) || Boolean(resolveTranslationText(lines[index])));
    indices[index] = indices[index - 1] + (needsTranslationSlot ? MINERADIO_PRIMARY_TRANSLATED_SLOT_STEP : MINERADIO_PRIMARY_CONTEXT_SLOT_STEP);
  }
  return indices;
};
var easeMineradio3DTrackOffset = (current, target, deltaSeconds) => {
  if (current === target) return target;
  const frameScale = clamp2(deltaSeconds * 60, 0.25, 3);
  const ease = 1 - Math.pow(1 - 0.18, frameScale);
  const step = clamp2((target - current) * ease, -0.68, 0.68);
  return Math.abs(target - current) < 5e-4 ? target : current + step;
};
var resolveMineradio3DTrackMotionActive = (current, target) => Math.abs(current - target) > 1e-4;
var resolveMineradio3DStableActiveLineIndex = (requestedIndex, previousIndex, lineCount, trackChanged) => {
  if (lineCount <= 0) return -1;
  if (requestedIndex >= 0) return clamp2(requestedIndex, 0, lineCount - 1);
  if (!trackChanged && previousIndex >= 0) return clamp2(previousIndex, 0, lineCount - 1);
  return 0;
};
var resolveMineradio3DRotationEase = (deltaSeconds) => 1 - Math.pow(1 - 0.055, clamp2(deltaSeconds * 60, 0.25, 3));
var smoothstep = (value) => value * value * (3 - 2 * value);
var resolveMineradio3DLineFrame = (relativeIndex, karaokeProgress, output = { x: 0, y: 0, z: 0, scale: 1, opacity: 1, rotationY: 0, rotationZ: 0 }, primaryLineOffset = relativeIndex) => {
  const distance = Math.abs(relativeIndex);
  const isActive = distance < 1e-3;
  const isWithinCinemaWindow = Math.abs(Math.round(primaryLineOffset)) <= Math.floor(MINERADIO_CINEMA_LINE_COUNT / 2);
  const authoredScale = isActive ? 1 : distance <= 1.001 ? 0.9 : 0.82;
  const depthScale = clamp2(1 - Math.min(5.5, distance) * 0.026, 0.84, 1.02);
  const contextAlpha = distance <= 1.001 ? TRACK_CONTEXT_OPACITY : TRACK_CONTEXT_OPACITY * 0.64;
  const visibility = !isWithinCinemaWindow ? 0 : isActive ? 1 : smoothstep(clamp2((TRACK_VISIBLE_RADIUS - distance) / 1.1, 0, 1));
  const depthFade = clamp2(1 - distance * 0.055, 0.54, 1);
  const isCompleted = relativeIndex < -1e-3 && karaokeProgress >= 0.999;
  output.x = 0;
  output.y = -relativeIndex * TRACK_LINE_STEP;
  output.z = ACTIVE_LINE_Z - Math.pow(Math.min(5.5, distance), 1.06) * 0.145;
  output.scale = authoredScale * depthScale;
  output.opacity = !isWithinCinemaWindow ? 0 : isActive ? 1 : isCompleted ? Math.max(TRACK_COMPLETED_OPACITY, contextAlpha * depthFade * visibility) : contextAlpha * depthFade * visibility;
  output.rotationY = 0;
  output.rotationZ = 0;
  return output;
};
var resolveMineradio3DTranslationFrame = (isCurrent, parentOpacity, output = { y: 0, scale: 1, opacity: 1 }, isCompleted = false) => {
  output.y = -MINERADIO_TRANSLATION_VISUAL_GAP * TRANSLATION_LINE_STEP;
  output.scale = MINERADIO_TRANSLATION_SCALE * (isCurrent ? 1.08 : 0.92);
  output.opacity = isCurrent ? 0.94 : isCompleted ? clamp2(parentOpacity * 0.9, 0.6, 0.76) : clamp2(parentOpacity * 0.62, 0.24, 0.6);
  return output;
};
var lineKaraokeProgress = (line, currentTime) => {
  const duration = Math.max(1e-3, line.endTime - line.startTime);
  return clamp2((currentTime - line.startTime) / duration, 0, 1);
};
var resolveMineradio3DKaraokeFillWidth = (line, currentTime, graphemeOffsets) => {
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
      const progress = (currentTime - timingStart) / Math.max(1e-3, timingEnd - timingStart);
      return startWidth + (endWidth - startWidth) * progress;
    }
  }
  return graphemeOffsets[timingCount] ?? fullWidth;
};
var configureTextContext = (context, font) => {
  context.font = font;
  context.textAlign = "center";
  context.textBaseline = "middle";
};
var measureGraphemeOffsets = (context, text) => {
  const graphemes = splitLyricGraphemes(text);
  const offsets = new Array(graphemes.length + 1).fill(0);
  for (let index = 1; index <= graphemes.length; index += 1) {
    offsets[index] = context.measureText(graphemes.slice(0, index).join("")).width;
  }
  return offsets;
};
var paintKaraokeTextCanvas = (canvas, text, theme, graphemeOffsets, fillWidth, baseColor = theme.primaryColor) => {
  const context = canvas.getContext("2d");
  if (!context) return;
  const renderedText = text || "...";
  const fullWidth = graphemeOffsets[graphemeOffsets.length - 1] ?? 0;
  const textLeft = (canvas.width - fullWidth) / 2;
  const edgeSoftness = Math.max(Math.min(68 * 0.45, 16), 6);
  const fillColor = theme.karaokeFillColor ?? "#ffffff";
  context.clearRect(0, 0, canvas.width, canvas.height);
  context.fillStyle = colorWithAlpha(baseColor, KARAOKE_BASE_ALPHA);
  context.fillText(renderedText, canvas.width / 2, canvas.height / 2);
  if (fillWidth <= 0) return;
  const fillEnd = textLeft + fillWidth;
  const sweepStart = Math.max(textLeft, fillEnd - edgeSoftness);
  const gradient = context.createLinearGradient(sweepStart, 0, Math.max(sweepStart + 1, fillEnd), 0);
  gradient.addColorStop(0, fillColor);
  gradient.addColorStop(1, baseColor);
  context.save();
  context.beginPath();
  context.rect(textLeft, 0, fillWidth, canvas.height);
  context.clip();
  context.fillStyle = fillColor;
  context.fillText(renderedText, canvas.width / 2, canvas.height / 2);
  context.fillStyle = gradient;
  context.fillText(renderedText, canvas.width / 2, canvas.height / 2);
  context.restore();
};
var createTextCanvas = (text, theme, options = {}) => {
  const canvas = document.createElement("canvas");
  const context = canvas.getContext("2d");
  if (!context) throw new Error("Mineradio 3D lyrics requires a canvas 2D context.");
  const fontFamily = theme.fontFamilyStack?.join(", ") || theme.fontFamily || "system-ui, sans-serif";
  const fontSize = options.fontSize ?? 68;
  const fontWeight = options.fontWeight ?? 750;
  context.font = `${fontWeight} ${fontSize}px ${fontFamily}`;
  const measuredWidth = Math.ceil(context.measureText(text || " ").width);
  const baseCanvasWidth = Math.max(MIN_CANVAS_WIDTH, measuredWidth + 160);
  const baseCanvasHeight = options.height ?? CANVAS_HEIGHT;
  canvas.width = Math.ceil(baseCanvasWidth * MINERADIO_TEXT_TEXTURE_SCALE);
  canvas.height = Math.ceil(baseCanvasHeight * MINERADIO_TEXT_TEXTURE_SCALE);
  const drawContext = canvas.getContext("2d");
  if (!drawContext) throw new Error("Mineradio 3D lyrics requires a canvas 2D context.");
  configureTextContext(
    drawContext,
    `${fontWeight} ${fontSize * MINERADIO_TEXT_TEXTURE_SCALE}px ${fontFamily}`
  );
  drawContext.fillStyle = options.color ?? theme.primaryColor;
  drawContext.fillText(text || "...", canvas.width / 2, canvas.height / 2);
  return canvas;
};
var createLyricGlowAlphaMask = (textCanvas) => {
  const alphaMaskCanvas = document.createElement("canvas");
  alphaMaskCanvas.width = Math.max(1, Math.ceil(textCanvas.width * MINERADIO_GLOW_TEXTURE_SCALE));
  alphaMaskCanvas.height = Math.max(1, Math.ceil(textCanvas.height * MINERADIO_GLOW_TEXTURE_SCALE));
  const alphaContext = alphaMaskCanvas.getContext("2d");
  if (!alphaContext) throw new Error("Mineradio 3D lyrics requires a canvas 2D context.");
  alphaContext.fillStyle = "#fff";
  alphaContext.fillRect(0, 0, alphaMaskCanvas.width, alphaMaskCanvas.height);
  alphaContext.globalCompositeOperation = "destination-in";
  alphaContext.drawImage(textCanvas, 0, 0, alphaMaskCanvas.width, alphaMaskCanvas.height);
  return alphaMaskCanvas;
};
var createLyricGlowCanvas = (textCanvas) => {
  const alphaMaskCanvas = createLyricGlowAlphaMask(textCanvas);
  const canvas = document.createElement("canvas");
  canvas.width = alphaMaskCanvas.width;
  canvas.height = alphaMaskCanvas.height;
  const context = canvas.getContext("2d");
  if (!context) throw new Error("Mineradio 3D lyrics requires a canvas 2D context.");
  const scaleX = canvas.width / textCanvas.width;
  const scaleY = canvas.height / textCanvas.height;
  const drawBlurPass = (filter, alpha) => {
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
  context.globalCompositeOperation = "lighter";
  context.filter = `blur(${8 * scaleX}px)`;
  context.globalAlpha = 0.26;
  for (let index = 0; index < 8; index += 1) {
    const angle = index / 8 * Math.PI * 2;
    context.drawImage(
      alphaMaskCanvas,
      Math.cos(angle) * 7 * scaleX,
      Math.sin(angle) * 4 * scaleY,
      canvas.width,
      canvas.height
    );
  }
  context.restore();
  context.save();
  context.globalCompositeOperation = "destination-in";
  const horizontalMask = context.createLinearGradient(0, 0, canvas.width, 0);
  horizontalMask.addColorStop(0, "rgba(255,255,255,0)");
  horizontalMask.addColorStop(0.1, "rgba(255,255,255,1)");
  horizontalMask.addColorStop(0.9, "rgba(255,255,255,1)");
  horizontalMask.addColorStop(1, "rgba(255,255,255,0)");
  context.fillStyle = horizontalMask;
  context.fillRect(0, 0, canvas.width, canvas.height);
  const verticalMask = context.createLinearGradient(0, 0, 0, canvas.height);
  verticalMask.addColorStop(0, "rgba(255,255,255,0)");
  verticalMask.addColorStop(0.16, "rgba(255,255,255,1)");
  verticalMask.addColorStop(0.84, "rgba(255,255,255,1)");
  verticalMask.addColorStop(1, "rgba(255,255,255,0)");
  context.fillStyle = verticalMask;
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.restore();
  return canvas;
};
var createBackfaceTexture = (texture) => {
  const backfaceTexture = texture.clone();
  const transform = resolveMineradio3DBackfaceTextureTransform();
  backfaceTexture.repeat.x = transform.repeatX;
  backfaceTexture.offset.x = transform.offsetX;
  backfaceTexture.needsUpdate = true;
  return backfaceTexture;
};
var createLyricGlowLayer = (textCanvas, geometry, glowColor, z) => {
  const glowTexture = new THREE2.CanvasTexture(createLyricGlowCanvas(textCanvas));
  glowTexture.minFilter = THREE2.LinearFilter;
  glowTexture.magFilter = THREE2.LinearFilter;
  glowTexture.generateMipmaps = false;
  const backGlowTexture = createBackfaceTexture(glowTexture);
  const material = new THREE2.MeshBasicMaterial({
    map: glowTexture,
    color: glowColor,
    transparent: true,
    depthWrite: false,
    depthTest: false,
    blending: THREE2.AdditiveBlending,
    side: THREE2.FrontSide
  });
  const backMaterial = new THREE2.MeshBasicMaterial({
    map: backGlowTexture,
    color: glowColor,
    transparent: true,
    depthWrite: false,
    depthTest: false,
    blending: THREE2.AdditiveBlending,
    side: THREE2.BackSide
  });
  const front = new THREE2.Mesh(geometry, material);
  const back = new THREE2.Mesh(geometry, backMaterial);
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
    }
  };
};
var resolveLyricGlowOpacity = (lineOpacity, isCurrent, translation) => {
  const activeOpacity = translation ? ACTIVE_TRANSLATION_GLOW_OPACITY : ACTIVE_LYRIC_GLOW_OPACITY;
  const contextOpacity = translation ? CONTEXT_TRANSLATION_GLOW_OPACITY : CONTEXT_LYRIC_GLOW_OPACITY;
  return lineOpacity * (isCurrent ? activeOpacity : contextOpacity);
};
var disposeStageMesh = (mesh) => {
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
  const canvases = [mesh.textCanvas, mesh.glow.texture.image];
  if (mesh.translationTexture) canvases.push(mesh.translationTexture.image);
  if (mesh.translationGlow) canvases.push(mesh.translationGlow.texture.image);
  for (const canvas of canvases) {
    canvas.width = 1;
    canvas.height = 1;
  }
};
var Mineradio3DStageRuntime = class {
  constructor(host, state, onVisualFrame) {
    this.host = host;
    this.onVisualFrame = onVisualFrame;
    this.scene = new THREE2.Scene();
    this.stageRoot = new THREE2.Group();
    this.camera = new THREE2.PerspectiveCamera(44, 1, 0.1, 300);
    this.starRiver = null;
    this.meshes = /* @__PURE__ */ new Map();
    this.frameScratch = { x: 0, y: 0, z: 0, scale: 1, opacity: 1, rotationY: 0, rotationZ: 0 };
    this.translationFrameScratch = { y: 0, scale: 1, opacity: 1 };
    this.orbitPositionTarget = new THREE2.Vector3();
    this.groupPositionTarget = new THREE2.Vector3();
    this.groupScaleTarget = new THREE2.Vector3();
    this.translationScaleTarget = new THREE2.Vector3();
    this.orbitBaseline = createMineradio3DOrbitState();
    this.orbit = createMineradio3DOrbitState();
    this.orbitTarget = createMineradio3DOrbitState();
    this.stageSpin = { x: 0, y: 0, velocityX: 0, velocityY: 0 };
    this.modelPan = { x: 0, y: 0 };
    this.trackVirtualIndices = [];
    this.trackScrollOffset = 0;
    this.trackTargetOffset = 0;
    this.trackLines = null;
    this.meshThemeSignature = "";
    this.animationFrameId = null;
    this.activePointerId = null;
    this.activePointerMode = null;
    this.pointerX = 0;
    this.pointerY = 0;
    this.lastPointerMoveAt = performance.now();
    this.lastStageFrameAt = performance.now();
    this.destroyed = false;
    this.visible = true;
    this.karaokePaintPending = false;
    this.onPointerDown = (event) => {
      if (this.destroyed || !this.visible) return;
      const mode = resolveMineradio3DPointerMode(event.button);
      if (!mode) return;
      event.preventDefault();
      this.renderer.domElement.focus();
      this.activePointerId = event.pointerId;
      this.activePointerMode = mode;
      if (mode === "rotate") {
        this.stageSpin.velocityX = 0;
        this.stageSpin.velocityY = 0;
      }
      this.pointerX = event.clientX;
      this.pointerY = event.clientY;
      this.lastPointerMoveAt = performance.now();
      this.renderer.domElement.setPointerCapture?.(event.pointerId);
      this.renderer.domElement.style.cursor = "grabbing";
    };
    this.onPointerMove = (event) => {
      if (this.destroyed || !this.visible) return;
      if (!this.activePointerMode || event.pointerId !== this.activePointerId) return;
      const deltaX = event.clientX - this.pointerX;
      const deltaY = event.clientY - this.pointerY;
      this.pointerX = event.clientX;
      this.pointerY = event.clientY;
      const now = performance.now();
      const deltaSeconds = clamp2((now - this.lastPointerMoveAt) / 1e3, 1 / 120, 0.08);
      this.lastPointerMoveAt = now;
      if (this.activePointerMode === "rotate") {
        this.stageSpin = applyMineradio3DStageDrag(this.stageSpin, deltaX, deltaY, deltaSeconds);
      } else {
        this.modelPan = applyMineradio3DStagePan(
          this.modelPan,
          deltaX,
          deltaY,
          this.host.clientHeight,
          this.orbit.radius,
          this.camera.fov
        );
      }
      this.scheduleRender();
    };
    this.onPointerUp = (event) => {
      if (event.pointerId !== this.activePointerId) return;
      this.releasePointer();
      this.scheduleRender();
    };
    this.onWheel = (event) => {
      if (this.destroyed || !this.visible) return;
      event.preventDefault();
      this.orbitTarget = applyMineradio3DOrbitWheel(this.orbitTarget, event.deltaY, this.orbit.radius);
      this.scheduleRender();
    };
    this.onDoubleClick = () => {
      if (this.destroyed || !this.visible) return;
      this.orbitTarget = { ...this.orbitBaseline };
      this.stageSpin = { x: 0, y: 0, velocityX: 0, velocityY: 0 };
      this.modelPan = { x: 0, y: 0 };
      this.stageRoot.position.set(0, 0, 0);
      this.scheduleRender();
    };
    this.render = () => {
      if (this.destroyed || !this.visible) return;
      this.karaokePaintPending = false;
      this.updateCamera();
      const now = performance.now();
      const deltaSeconds = clamp2((now - this.lastStageFrameAt) / 1e3, 1 / 240, 0.08);
      this.lastStageFrameAt = now;
      this.trackScrollOffset = easeMineradio3DTrackOffset(this.trackScrollOffset, this.trackTargetOffset, deltaSeconds);
      if (!this.activePointerMode) this.stageSpin = advanceMineradio3DStageSpin(this.stageSpin, deltaSeconds);
      if (Math.abs(this.stageSpin.x) >= PARTICLE_ROTATION_REBASE_LIMIT || Math.abs(this.stageSpin.y) >= PARTICLE_ROTATION_REBASE_LIMIT) {
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
          index - this.state.activeLineIndex
        );
        const isCurrent = index === this.state.activeLineIndex;
        const fullTextWidth = mesh.graphemeOffsets[mesh.graphemeOffsets.length - 1] ?? 0;
        const karaokeFillWidth = isCurrent ? resolveMineradio3DKaraokeFillWidth(line, this.state.currentTime, mesh.graphemeOffsets) : index < this.state.activeLineIndex ? fullTextWidth : 0;
        const karaokeChanged = Math.abs(karaokeFillWidth - mesh.lastKaraokeFillWidth) > 0.1;
        const canPaintKaraoke = !isCurrent || now - mesh.lastKaraokePaintAt >= KARAOKE_TEXTURE_FRAME_INTERVAL_MS;
        if (karaokeChanged && !canPaintKaraoke) this.karaokePaintPending = true;
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
          const translationFillWidth = isCurrent ? translationFullWidth * karaokeProgress : index < this.state.activeLineIndex ? translationFullWidth : 0;
          const translationChanged = Math.abs(translationFillWidth - mesh.lastTranslationKaraokeFillWidth) > 0.1;
          if (translationChanged && !canPaintKaraoke) this.karaokePaintPending = true;
          if (translationChanged && canPaintKaraoke) {
            paintKaraokeTextCanvas(
              mesh.translationTexture.image,
              line.translation ?? "",
              this.state.theme,
              mesh.translationGraphemeOffsets,
              translationFillWidth,
              this.state.theme.secondaryColor
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
        mesh.group.visible = frame.opacity > 2e-3;
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
            index < this.state.activeLineIndex
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
        this.starRiver.rotation.copy(this.stageRoot.rotation);
        this.starRiver.position.copy(this.stageRoot.position);
        this.starRiver.update(
          deltaSeconds,
          this.renderer.getPixelRatio(),
          this.state.staticMode ?? false
        );
      }
      this.renderer.render(this.scene, this.camera);
      this.onVisualFrame?.(this.renderer.domElement);
    };
    this.tick = () => {
      this.animationFrameId = null;
      if (this.destroyed || !this.visible) return;
      this.render();
      const effectAnimating = this.starRiver !== null && !this.state.staticMode;
      if (!this.state.paused || effectAnimating || this.hasInteractiveMotion()) this.scheduleRender();
    };
    this.state = state;
    this.visible = state.visible !== false;
    this.scene.add(this.stageRoot);
    this.renderer = new THREE2.WebGLRenderer({ alpha: true, antialias: true, powerPreference: "high-performance" });
    try {
      this.renderer.setClearColor(0, 0);
      this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
      this.renderer.domElement.dataset.visualizerCanvas = "mineradio-3d";
      this.renderer.domElement.tabIndex = 0;
      this.renderer.domElement.style.width = "100%";
      this.renderer.domElement.style.height = "100%";
      this.renderer.domElement.style.display = "block";
      this.renderer.domElement.style.cursor = "grab";
      this.renderer.domElement.style.touchAction = "none";
      this.renderer.domElement.addEventListener("pointerdown", this.onPointerDown);
      this.renderer.domElement.addEventListener("pointermove", this.onPointerMove);
      this.renderer.domElement.addEventListener("pointerup", this.onPointerUp);
      this.renderer.domElement.addEventListener("pointercancel", this.onPointerUp);
      this.renderer.domElement.addEventListener("lostpointercapture", this.onPointerUp);
      this.renderer.domElement.addEventListener("wheel", this.onWheel, { passive: false });
      this.renderer.domElement.addEventListener("dblclick", this.onDoubleClick);
      this.host.appendChild(this.renderer.domElement);
      this.resizeObserver = new ResizeObserver(() => this.resize());
      this.resizeObserver.observe(host);
      this.resize();
      this.update(state);
    } catch (error) {
      this.destroy();
      throw error;
    }
  }
  // Visibility suspends work without resetting the authored camera or lyric pose.
  setVisible(visible) {
    if (this.destroyed || this.visible === visible) return;
    this.visible = visible;
    if (!visible) {
      this.stop();
      this.releasePointer();
      this.starRiver?.dispose();
      this.starRiver = null;
    } else {
      this.lastStageFrameAt = performance.now();
      this.resize();
    }
  }
  releasePointer() {
    const pointer = this.activePointerId;
    this.activePointerId = null;
    this.activePointerMode = null;
    const canvas = this.renderer.domElement;
    if (pointer !== null && canvas.hasPointerCapture?.(pointer)) canvas.releasePointerCapture(pointer);
    canvas.style.cursor = "grab";
  }
  getCanvas() {
    return this.renderer.domElement;
  }
  refreshSceneFrame() {
    if (this.destroyed || !this.visible) return;
    this.renderer.render(this.scene, this.camera);
    this.onVisualFrame?.(this.renderer.domElement);
  }
  update(nextState) {
    if (this.destroyed || !this.visible) return;
    const trackChanged = this.trackLines !== nextState.lines || this.state.showTranslation !== nextState.showTranslation;
    const stableActiveLineIndex = resolveMineradio3DStableActiveLineIndex(
      nextState.activeLineIndex,
      this.state.activeLineIndex,
      nextState.lines.length,
      trackChanged
    );
    const activeLineChanged = stableActiveLineIndex !== this.state.activeLineIndex;
    const nextThemeSignature = `${nextState.theme.primaryColor}|${nextState.theme.secondaryColor}|${nextState.theme.accentColor}|${nextState.theme.karaokeFillColor ?? ""}|${nextState.theme.fontFamily ?? ""}|${nextState.theme.fontFamilyStack?.join(",") ?? ""}|${nextState.showTranslation}`;
    this.state = { ...nextState, activeLineIndex: stableActiveLineIndex };
    if (nextState.visualizerEffectMode === "star-river") {
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
  resize() {
    if (this.destroyed || !this.visible) return;
    const width = Math.max(1, this.host.clientWidth);
    const height = Math.max(1, this.host.clientHeight);
    this.camera.aspect = width / height;
    this.stageRoot.scale.setScalar(resolveMineradio3DResponsiveScale(this.camera.aspect));
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(width, height, false);
    this.scheduleRender();
  }
  createMesh(line, index) {
    const canvas = createTextCanvas(line.fullText, this.state.theme);
    const textContext = canvas.getContext("2d");
    if (!textContext) throw new Error("Mineradio 3D lyrics requires a canvas 2D context.");
    const graphemeOffsets = measureGraphemeOffsets(textContext, line.fullText || "...");
    const initialKaraokeFillWidth = index < this.state.activeLineIndex ? graphemeOffsets[graphemeOffsets.length - 1] ?? 0 : index === this.state.activeLineIndex ? resolveMineradio3DKaraokeFillWidth(line, this.state.currentTime, graphemeOffsets) : 0;
    paintKaraokeTextCanvas(canvas, line.fullText, this.state.theme, graphemeOffsets, initialKaraokeFillWidth);
    const texture = new THREE2.CanvasTexture(canvas);
    texture.minFilter = THREE2.LinearFilter;
    texture.magFilter = THREE2.LinearFilter;
    const aspect = canvas.width / canvas.height;
    const geometry = new THREE2.PlaneGeometry(1.72 * aspect, 1.72);
    const backTexture = createBackfaceTexture(texture);
    const material = new THREE2.MeshBasicMaterial({
      map: texture,
      transparent: true,
      depthWrite: false,
      side: THREE2.FrontSide
    });
    const backMaterial = new THREE2.MeshBasicMaterial({
      map: backTexture,
      transparent: true,
      depthWrite: false,
      side: THREE2.BackSide
    });
    const text = new THREE2.Mesh(geometry, material);
    const backText = new THREE2.Mesh(geometry, backMaterial);
    const glow = createLyricGlowLayer(canvas, geometry, this.state.theme.primaryColor, -0.012);
    const group = new THREE2.Group();
    group.add(glow.back, backText, glow.front, text);
    let translation;
    let backTranslation;
    let translationTexture;
    let backTranslationTexture;
    let translationGeometry;
    let translationMaterial;
    let backTranslationMaterial;
    let translationGlow;
    let translationGraphemeOffsets = [];
    let initialTranslationKaraokeFillWidth = 0;
    const translationText = this.state.showTranslation ? resolveTranslationText(line) : "";
    if (translationText) {
      const translationCanvas = createTextCanvas(translationText, this.state.theme, {
        fontSize: 42,
        fontWeight: 650,
        color: this.state.theme.secondaryColor,
        height: TRANSLATION_CANVAS_HEIGHT
      });
      const translationContext = translationCanvas.getContext("2d");
      if (!translationContext) throw new Error("Mineradio 3D lyrics requires a canvas 2D context.");
      translationGraphemeOffsets = measureGraphemeOffsets(translationContext, translationText);
      const translationFullWidth = translationGraphemeOffsets[translationGraphemeOffsets.length - 1] ?? 0;
      const karaokeProgress = (graphemeOffsets[graphemeOffsets.length - 1] ?? 0) > 0 ? initialKaraokeFillWidth / (graphemeOffsets[graphemeOffsets.length - 1] ?? 1) : 0;
      initialTranslationKaraokeFillWidth = index < this.state.activeLineIndex ? translationFullWidth : index === this.state.activeLineIndex ? translationFullWidth * karaokeProgress : 0;
      paintKaraokeTextCanvas(
        translationCanvas,
        translationText,
        this.state.theme,
        translationGraphemeOffsets,
        initialTranslationKaraokeFillWidth,
        this.state.theme.secondaryColor
      );
      translationTexture = new THREE2.CanvasTexture(translationCanvas);
      translationTexture.minFilter = THREE2.LinearFilter;
      translationTexture.magFilter = THREE2.LinearFilter;
      const translationAspect = translationCanvas.width / translationCanvas.height;
      translationGeometry = new THREE2.PlaneGeometry(1.18 * translationAspect, 1.18);
      backTranslationTexture = createBackfaceTexture(translationTexture);
      translationMaterial = new THREE2.MeshBasicMaterial({
        map: translationTexture,
        transparent: true,
        depthWrite: false,
        side: THREE2.FrontSide
      });
      backTranslationMaterial = new THREE2.MeshBasicMaterial({
        map: backTranslationTexture,
        transparent: true,
        depthWrite: false,
        side: THREE2.BackSide
      });
      translation = new THREE2.Mesh(translationGeometry, translationMaterial);
      backTranslation = new THREE2.Mesh(translationGeometry, backTranslationMaterial);
      translationGlow = createLyricGlowLayer(translationCanvas, translationGeometry, this.state.theme.secondaryColor, 3e-3);
      const translationFrame = resolveMineradio3DTranslationFrame(
        index === this.state.activeLineIndex,
        1,
        void 0,
        index < this.state.activeLineIndex
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
        true
      );
      translationGlow.material.opacity = translationGlowOpacity;
      translationGlow.backMaterial.opacity = translationGlowOpacity;
      group.add(translationGlow.back, backTranslation, translationGlow.front, translation);
    }
    const initialFrame = resolveMineradio3DLineFrame(
      (this.trackVirtualIndices[index] ?? index) - this.trackScrollOffset,
      lineKaraokeProgress(line, this.state.currentTime),
      void 0,
      index - this.state.activeLineIndex
    );
    group.position.set(initialFrame.x, initialFrame.y, initialFrame.z);
    group.scale.setScalar(initialFrame.scale);
    material.opacity = initialFrame.opacity;
    backMaterial.opacity = initialFrame.opacity;
    const initialGlowOpacity = resolveLyricGlowOpacity(
      initialFrame.opacity,
      index === this.state.activeLineIndex,
      false
    );
    glow.material.opacity = initialGlowOpacity;
    glow.backMaterial.opacity = initialGlowOpacity;
    group.visible = initialFrame.opacity > 2e-3;
    this.stageRoot.add(group);
    return {
      signature: `${line.fullText}|${line.translation ?? ""}|${this.state.theme.primaryColor}|${this.state.theme.secondaryColor}|${this.state.theme.accentColor}|${this.state.theme.karaokeFillColor ?? ""}|${this.state.theme.fontFamily ?? ""}|${this.state.theme.fontFamilyStack?.join(",") ?? ""}|${this.state.showTranslation}`,
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
      lastKaraokePaintAt: 0
    };
  }
  syncMeshes() {
    const visibleIndices = new Set(resolveMineradio3DLineWindow(this.state.activeLineIndex, this.state.lines.length));
    this.meshes.forEach((mesh, index) => {
      if (visibleIndices.has(index)) return;
      disposeStageMesh(mesh);
      this.meshes.delete(index);
    });
    visibleIndices.forEach((index) => {
      const line = this.state.lines[index];
      const signature = `${line.fullText}|${line.translation ?? ""}|${this.state.theme.primaryColor}|${this.state.theme.secondaryColor}|${this.state.theme.accentColor}|${this.state.theme.karaokeFillColor ?? ""}|${this.state.theme.fontFamily ?? ""}|${this.state.theme.fontFamilyStack?.join(",") ?? ""}|${this.state.showTranslation}`;
      const existing = this.meshes.get(index);
      if (existing?.signature === signature) return;
      if (existing) disposeStageMesh(existing);
      this.meshes.set(index, this.createMesh(line, index));
    });
  }
  updateCamera() {
    this.orbit = easeMineradio3DOrbitCamera(this.orbit, this.orbitTarget);
    this.camera.position.copy(resolveMineradio3DOrbitPosition(this.orbit, this.orbitPositionTarget));
    this.camera.lookAt(0, 0, 0);
    this.camera.updateProjectionMatrix();
  }
  scheduleRender() {
    if (this.destroyed || !this.visible || this.animationFrameId !== null) return;
    this.animationFrameId = requestAnimationFrame(this.tick);
  }
  hasInteractiveMotion() {
    const orbitMoving = Math.abs(this.orbit.theta - this.orbitTarget.theta) > 1e-4 || Math.abs(this.orbit.phi - this.orbitTarget.phi) > 1e-4 || Math.abs(this.orbit.radius - this.orbitTarget.radius) > 2e-3;
    const rotationMoving = this.activePointerMode === "rotate" || Math.abs(this.stageSpin.velocityX) > 0 || Math.abs(this.stageSpin.velocityY) > 0 || Math.abs(this.stageRoot.rotation.x - this.stageSpin.x) > 1e-4 || Math.abs(this.stageRoot.rotation.y - this.stageSpin.y) > 1e-4;
    const panMoving = this.activePointerMode === "pan" || Math.abs(this.stageRoot.position.x - this.modelPan.x) > 1e-4 || Math.abs(this.stageRoot.position.y - this.modelPan.y) > 1e-4;
    const trackMoving = resolveMineradio3DTrackMotionActive(this.trackScrollOffset, this.trackTargetOffset);
    return orbitMoving || rotationMoving || panMoving || trackMoving || this.karaokePaintPending;
  }
  start() {
    if (this.animationFrameId !== null || this.destroyed || !this.visible || this.state.paused) return;
    this.animationFrameId = requestAnimationFrame(this.tick);
  }
  stop() {
    if (this.animationFrameId === null) return;
    cancelAnimationFrame(this.animationFrameId);
    this.animationFrameId = null;
  }
  destroy() {
    if (this.destroyed) return;
    this.releasePointer();
    this.destroyed = true;
    this.stop();
    this.resizeObserver?.disconnect();
    this.renderer.domElement.removeEventListener("pointerdown", this.onPointerDown);
    this.renderer.domElement.removeEventListener("pointermove", this.onPointerMove);
    this.renderer.domElement.removeEventListener("pointerup", this.onPointerUp);
    this.renderer.domElement.removeEventListener("pointercancel", this.onPointerUp);
    this.renderer.domElement.removeEventListener("lostpointercapture", this.onPointerUp);
    this.renderer.domElement.removeEventListener("wheel", this.onWheel);
    this.renderer.domElement.removeEventListener("dblclick", this.onDoubleClick);
    this.meshes.forEach(disposeStageMesh);
    this.meshes.clear();
    this.starRiver?.dispose();
    this.starRiver = null;
    this.scene.remove(this.stageRoot);
    this.renderer.dispose();
    this.renderer.forceContextLoss();
    this.renderer.domElement.width = 1;
    this.renderer.domElement.height = 1;
    this.trackLines = null;
    this.trackVirtualIndices = [];
    this.state = { ...this.state, lines: [] };
    this.renderer.domElement.remove();
  }
};
export {
  Mineradio3DStageRuntime,
  advanceMineradio3DStageSpin,
  applyMineradio3DOrbitWheel,
  applyMineradio3DStageDrag,
  applyMineradio3DStagePan,
  createMineradio3DOrbitState,
  easeMineradio3DOrbitCamera,
  easeMineradio3DTrackOffset,
  rebaseMineradio3DStageSpin,
  resolveMineradio3DBackfaceTextureTransform,
  resolveMineradio3DKaraokeFillWidth,
  resolveMineradio3DLineFrame,
  resolveMineradio3DLineWindow,
  resolveMineradio3DOrbitPosition,
  resolveMineradio3DPointerMode,
  resolveMineradio3DPrimaryVirtualIndices,
  resolveMineradio3DResponsiveScale,
  resolveMineradio3DRotationEase,
  resolveMineradio3DStableActiveLineIndex,
  resolveMineradio3DTrackMotionActive,
  resolveMineradio3DTranslationFrame
};
