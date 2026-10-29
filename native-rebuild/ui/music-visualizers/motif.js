import {
  ACESFilmicToneMapping,AdditiveBlending,BokehPass,BufferAttribute,BufferGeometry,CanvasTexture,Color,
  DoubleSide,EffectComposer,Fog,Group,Mesh,MeshPhysicalMaterial,MeshStandardMaterial,OutputPass,
  PCFShadowMap,PerspectiveCamera,PlaneGeometry,PMREMGenerator,PointLight,Points,RenderPass,RepeatWrapping,
  RoomEnvironment,SRGBColorSpace,Scene,ShaderMaterial,SpotLight,UnrealBloomPass,Vector2,Vector3,WebGLRenderer,
} from './motif-vendor/three.module.js';
import {registerMusicMaterialSceneSource} from '../music-material/registry.js';
import {buildMotifScore,keySignature} from './motif-score.js';
import {createMotifLyrics} from './motif-lyrics.js';

// 音符律动: the song's melody, engraved from the native analysis, lies on a
// sheet of paper in 3D. An amber light hops from note to note and a violet
// one keeps the beat on the staff below; both land on the tracked beat grid,
// so they move with the audio. Sung characters are engraved between the
// staves. Shot like the reference still: a dark sheet lit almost only by the
// two lights, raised black-metal print catching them along its bevels,
// pastel neon trails, depth of field, bloom and filmic tone mapping (full
// Three r185 bundle in motif-vendor).

const ROW = 3.5;              // distance between systems
const GAP = 0.16;             // staff space
const TOP = 0.7;              // top staff line inside a row
const BOTTOM = TOP + 4 * GAP;
const RHYTHM = BOTTOM + 1.5;  // middle line of the beat staff
const UNIT = 1.0;             // world units per beat
const MEASURE = 4 * UNIT + 0.5;
const PER_ROW = 4;
const BALL = 0.085;           // radius of a trail's head
const LIT_LIFE = 4.5;         // seconds a played note stays lit
const LIT_COUNT = 20;         // notes kept lit per voice
const NOTE_LIGHTS = 2;        // of those, the freshest that light the paper
// Each voice: the saturated colour its light casts on the paper, the pastel
// body of its neon and the pale centre where the neon is hottest.
const VOICES = [
  {light: [1.0, 0.62, 0.2], body: [1.0, 0.84, 0.36], core: [1.0, 0.97, 0.8]},
  {light: [0.55, 0.32, 1.0], body: [0.62, 0.47, 1.0], core: [0.9, 0.85, 1.0]},
];
// A low, close camera grazing the page, like the reference macro shot.
const HOME = Object.freeze({theta: -0.55, phi: 1.1, distance: 4.3});
const linear = rgb => { const c = new Color().setRGB(...rgb, SRGBColorSpace); return new Vector3(c.r, c.g, c.b); };
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
// Staff position of a diatonic step (C4 = 0); the bottom line is E4.
const stepZ = step => BOTTOM - (step - 2) * GAP / 2;

// ---- geometry -------------------------------------------------------------
function geometry(positions, indices, uvs) {
  const result = new BufferGeometry();
  result.setAttribute('position', new BufferAttribute(new Float32Array(positions), 3));
  if (uvs) result.setAttribute('uv', new BufferAttribute(new Float32Array(uvs), 2));
  if (indices) result.setIndex(indices);
  return result;
}

// Convex polygon `points` moved `amount` inward (negative: outward), edge by
// edge, so thin strokes keep their length.
function inset(points, amount) {
  const n = points.length;
  const cx = points.reduce((sum, p) => sum + p[0], 0) / n, cz = points.reduce((sum, p) => sum + p[1], 0) / n;
  const inward = points.map((p, i) => {
    const q = points[(i + 1) % n], dx = q[0] - p[0], dz = q[1] - p[1], length = Math.hypot(dx, dz) || 1;
    let nx = -dz / length, nz = dx / length;
    if (nx * (cx - p[0]) + nz * (cz - p[1]) < 0) { nx = -nx; nz = -nz; }
    return [nx, nz];
  });
  return points.map((p, i) => {
    const a = inward[(i + n - 1) % n], b = inward[i], k = amount / Math.max(0.2, 1 + a[0] * b[0] + a[1] * b[1]);
    return [p[0] + (a[0] + b[0]) * k, p[1] + (a[1] + b[1]) * k];
  });
}
// Distance from a convex polygon's centroid to its nearest edge.
function inradius(points) {
  const n = points.length;
  const cx = points.reduce((sum, p) => sum + p[0], 0) / n, cz = points.reduce((sum, p) => sum + p[1], 0) / n;
  return Math.min(...points.map((p, i) => {
    const q = points[(i + 1) % n], dx = q[0] - p[0], dz = q[1] - p[1];
    return Math.abs((cx - p[0]) * dz - (cz - p[1]) * dx) / (Math.hypot(dx, dz) || 1);
  }));
}

// The print, batched per row into one mesh: every mark is a convex polygon
// (an open head is a ring) cut from thin metal sheet that floats a little
// above the paper, so the key light throws its soft shadow beside it. Its
// edge is a narrow rounded bevel whose normals turn from outward to straight
// up, so a passing light draws a fine bright line along each edge.
const HOVER = 0.022, INK_THICK = 0.0035, BEVEL = 0.005;
const INK_TOP = HOVER + INK_THICK;
class Ink {
  constructor() { this.polygons = []; this.rings = []; this.glyphs = []; }
  polygon(points) { this.polygons.push(points); }
  rect(x0, z0, x1, z1) { this.polygon([[x0, z0], [x1, z0], [x1, z1], [x0, z1]]); }
  line(x0, z0, x1, z1, width) {
    const dx = x1 - x0, dz = z1 - z0, length = Math.hypot(dx, dz) || 1;
    const nx = -dz / length * width / 2, nz = dx / length * width / 2;
    this.polygon([[x0 + nx, z0 + nz], [x1 + nx, z1 + nz], [x1 - nx, z1 - nz], [x0 - nx, z0 - nz]]);
  }
  // Engraved head: an ellipse tilted up to the right; open heads keep a rim.
  head(x, z, open) {
    const ellipse = (kx, kz) => headOutline(x, z, kx, kz);
    if (open) this.rings.push([ellipse(1, 1), ellipse(0.62, 0.55)]);
    else this.polygon(ellipse(1, 1));
  }
  dot(x, z, r = 0.024) {
    this.polygon(Array.from({length: 12}, (_, i) => [x + Math.cos(i / 12 * Math.PI * 2) * r, z + Math.sin(i / 12 * Math.PI * 2) * r]));
  }
  glyph(name, x, z, size) { this.glyphs.push({...GLYPHS[name], x, z, size: size / GLYPH_QUAD}); }

  mesh(material) {
    const positions = [], normals = [], indices = [];
    const vertex = (x, y, z, nx = 0, ny = 1, nz = 0) => {
      positions.push(x, y, z); normals.push(nx, ny, nz);
      return positions.length / 3 - 1;
    };
    // Every face looks up or outward-and-up: wind each triangle to match.
    const triangle = (a, b, c) => {
      const p = i => positions.slice(i * 3, i * 3 + 3), [A, B, C] = [p(a), p(b), p(c)];
      const up = (B[2] - A[2]) * (C[0] - A[0]) - (B[0] - A[0]) * (C[2] - A[2]);
      if (up >= 0) indices.push(a, b, c); else indices.push(a, c, b);
    };
    const fan = (loop, y) => {
      const ids = loop.map(([x, z]) => vertex(x, y, z));
      for (let i = 1; i < ids.length - 1; i++) triangle(ids[0], ids[i], ids[i + 1]);
    };
    const band = (outer, inner, y0, y1, slope) => {
      const n = outer.length;
      const ids = outer.map(([x, z], i) => {
        const dx = x - inner[i][0], dz = z - inner[i][1], length = Math.hypot(dx, dz) || 1;
        const tilt = Math.hypot(1, slope);
        return [vertex(x, y0, z, dx / length / tilt, slope / tilt, dz / length / tilt), vertex(inner[i][0], y1, inner[i][1])];
      });
      for (let i = 0; i < n; i++) {
        const [a, b] = ids[i], [c, d] = ids[(i + 1) % n];
        triangle(a, c, d); triangle(a, d, b);
      }
    };
    for (const polygon of this.polygons) {
      const top = inset(polygon, Math.min(BEVEL, 0.5 * inradius(polygon)));
      band(polygon, top, HOVER, INK_TOP, 0.55);
      fan(top, INK_TOP);
    }
    for (const [outer, hole] of this.rings) {
      const bevel = Math.min(BEVEL, 0.3 * (inradius(outer) - inradius(hole)));
      const top = inset(outer, bevel), lip = inset(hole, -bevel);
      band(outer, top, HOVER, INK_TOP, 0.55);
      band(hole, lip, HOVER, INK_TOP, 0.55);
      const ids = top.map(([x, z], i) => [vertex(x, INK_TOP, z), vertex(lip[i][0], INK_TOP, lip[i][1])]);
      for (let i = 0; i < ids.length; i++) {
        const [a, b] = ids[i], [c, d] = ids[(i + 1) % ids.length];
        triangle(a, c, d); triangle(a, d, b);
      }
    }
    const shape = geometry(positions, indices);
    shape.setAttribute('normal', new BufferAttribute(new Float32Array(normals), 3));
    const mesh = new Mesh(shape, material);
    mesh.castShadow = mesh.receiveShadow = true;
    return mesh;
  }
}
// A note head's outline: an ellipse tilted up to the right, scaled by kx/kz.
function headOutline(x, z, kx = 1, kz = 1, segments = 24) {
  const tilt = -0.4, rx = 0.1 * kx, rz = 0.068 * kz;
  return Array.from({length: segments}, (_, i) => {
    const a = i / segments * Math.PI * 2, ex = Math.cos(a) * rx, ez = Math.sin(a) * rz;
    return [x + ex * Math.cos(tilt) - ez * Math.sin(tilt), z + ex * Math.sin(tilt) + ez * Math.cos(tilt)];
  });
}

// Music symbols are engraved like the sung characters (see motif-lyrics),
// from the system's music font; sizes are given as the old flat quad sizes.
const MUSIC_FONT = '"Segoe UI Symbol","Noto Music","Bravura",serif';
const DIGIT_FONT = '"Times New Roman",serif';
const GLYPH_QUAD = 1.2;
const GLYPHS = {
  clef: {text: '\u{1D11E}', font: MUSIC_FONT, weight: 'normal'}, sharp: {text: '♯', font: MUSIC_FONT, weight: 'normal'},
  flat: {text: '♭', font: MUSIC_FONT, weight: 'normal'}, natural: {text: '♮', font: MUSIC_FONT, weight: 'normal'},
  eight: {text: '8', font: DIGIT_FONT, weight: 'bold'}, four: {text: '4', font: DIGIT_FONT, weight: 'bold'},
};

// ---- materials ------------------------------------------------------------
// Paper: smooth, heavy satin stock. A faint tileable felt varies its tone
// and sheen a little, so the light on it never looks like a flat gradient;
// no fibres show at this distance.
function paperTextures(document) {
  const size = 512;
  let seed = 11;
  const random = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };
  // Value noise that wraps at the tile edge.
  const noise = (cells, octaves) => {
    const field = new Float32Array(size * size);
    for (let octave = 0; octave < octaves; octave++) {
      const n = cells << octave, grid = Array.from({length: n * n}, random);
      const at = (a, b) => grid[(b % n) * n + (a % n)];
      for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
        const gx = x / size * n, gy = y / size * n, ix = Math.floor(gx), iy = Math.floor(gy);
        let fx = gx - ix, fy = gy - iy;
        fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
        const top = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * fx;
        const bottom = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * fx;
        field[y * size + x] += (top + (bottom - top) * fy - 0.5) / (1 << octave);
      }
    }
    return field;
  };
  const mottle = noise(3, 4), felt = noise(40, 2);
  const make = value => {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = size;
    const context = canvas.getContext('2d'), image = context.createImageData(size, size);
    for (let i = 0; i < size * size; i++) {
      const v = Math.max(0, Math.min(255, value(mottle[i], felt[i], random())));
      image.data[i * 4] = image.data[i * 4 + 1] = image.data[i * 4 + 2] = v;
      image.data[i * 4 + 3] = 255;
    }
    context.putImageData(image, 0, 0);
    const texture = new CanvasTexture(canvas);
    texture.wrapS = texture.wrapT = RepeatWrapping;
    texture.repeat.set(9, 9);
    texture.anisotropy = 16;
    return texture;
  };
  const color = make((m, felt, grain) => 232 + m * 18 + felt * 6 + (grain - 0.5) * 4);
  color.colorSpace = SRGBColorSpace;
  // Satin, not gloss: a broad soft sheen and no grain to catch it.
  const roughness = make((m, felt) => 190 + m * 20 + felt * 10);
  return {color, roughness};
}
const GLOW_VERTEX = `
attribute float aAlpha;
attribute float aSize;
uniform float uScale;
varying float vAlpha;
void main(){
  vec4 mv=modelViewMatrix*vec4(position,1.0);
  vAlpha=aAlpha;
  gl_PointSize=aSize*uScale/max(0.1,-mv.z);
  gl_Position=projectionMatrix*mv;
}`;
const GLOW_FRAGMENT = `
uniform vec3 uColor;
uniform float uGain;
varying float vAlpha;
void main(){
  float d=length(gl_PointCoord-0.5)*2.0;
  float a=pow(max(0.0,1.0-d),2.2)*vAlpha;
  gl_FragColor=vec4(uColor*a*uGain,1.0);
}`;
function glowPoints(count, color) {
  const shape = new BufferGeometry();
  shape.setAttribute('position', new BufferAttribute(new Float32Array(count * 3), 3));
  shape.setAttribute('aAlpha', new BufferAttribute(new Float32Array(count), 1));
  shape.setAttribute('aSize', new BufferAttribute(new Float32Array(count), 1));
  const points = new Points(shape, new ShaderMaterial({
    vertexShader: GLOW_VERTEX, fragmentShader: GLOW_FRAGMENT, transparent: true, depthWrite: false,
    blending: AdditiveBlending, uniforms: {uColor: {value: new Vector3(...color)}, uScale: {value: 300}, uGain: {value: 1}},
  }));
  points.frustumCulled = false;
  return points;
}

// ---- layout ---------------------------------------------------------------
// Every row holds four measures after its clef and key signature; the first
// row also carries the 4/4 time signature.
export function layoutMotif(score) {
  const fifths = score.key?.fifths ?? 0;
  const header = row => 0.95 + Math.abs(fifths) * 0.2 + (row === 0 ? 0.45 : 0) + 0.15;
  const rows = Math.max(1, Math.ceil(score.measures / PER_ROW));
  const place = beat => {
    const measure = Math.floor((beat - score.firstBeat) / 4);
    const row = Math.max(0, Math.floor(measure / PER_ROW));
    const inRow = measure - row * PER_ROW;
    const offset = beat - score.firstBeat - measure * 4;
    return {row, x: header(row) + inRow * MEASURE + 0.3 + offset * UNIT, z0: row * ROW};
  };
  const melody = score.notes.map(note => {
    const p = place(note.beat);
    return {...note, row: p.row, x: p.x, z: p.z0 + stepZ(note.step)};
  });
  // One beat note for every beat the song is audible, intro and outro included.
  const pulse = [];
  for (let b = score.firstBeat; b < score.firstBeat + score.measures * 4; b++) {
    const p = place(b), time = score.grid.timeAt(b);
    if (time >= (score.start ?? -Infinity) - 0.05 && time <= (score.end ?? Infinity))
      pulse.push({time, beat: b, row: p.row, x: p.x, z: p.z0 + RHYTHM});
  }
  // Characters are as large as their spacing allows, so dense lines never overlap.
  return {melody, pulse, rows, header, fifths, octaveDown: score.octaveDown, measures: score.measures};
}

function engraveRow(layout, row) {
  const ink = new Ink(), z0 = row * ROW;
  const left = 0.15, measures = Math.min(PER_ROW, layout.measures - row * PER_ROW);
  const right = layout.header(row) + measures * MEASURE;
  // The melody staff and the beat staff.
  const end = RHYTHM + 2 * GAP;
  for (let i = 0; i < 5; i++) {
    ink.rect(left, z0 + TOP + i * GAP - 0.006, right, z0 + TOP + i * GAP + 0.006);
    ink.rect(left, z0 + RHYTHM + (i - 2) * GAP - 0.006, right, z0 + RHYTHM + (i - 2) * GAP + 0.006);
  }
  ink.rect(left - 0.01, z0 + TOP, left + 0.012, z0 + end);
  // Clef, its octave mark, key signature and (first row) time signature.
  ink.glyph('clef', left + 0.42, z0 + stepZ(4) - 0.08, 1.4);
  if (layout.octaveDown) ink.glyph('eight', left + 0.4, z0 + BOTTOM + 0.36, 0.22);
  keySignature(layout.fifths).forEach(({letter, alter}, i) => {
    // Treble key signature lines: sharps F5 C5 G5 D5 A4 E5 B4, flats B4 E5 A4 D5 G4 C5 F4.
    const steps = alter > 0 ? [10, 7, 11, 8, 5, 9, 6] : [6, 9, 5, 8, 4, 7, 3];
    ink.glyph(alter > 0 ? 'sharp' : 'flat', left + 0.95 + i * 0.2, z0 + stepZ(steps[i]) - (alter < 0 ? 0.05 : 0), 0.42);
    void letter;
  });
  if (row === 0) {
    const x = left + 0.95 + Math.abs(layout.fifths) * 0.2 + 0.2;
    ink.glyph('four', x, z0 + TOP + GAP, 0.5);
    ink.glyph('four', x, z0 + TOP + 3 * GAP, 0.5);
  }
  for (let m = 1; m <= measures; m++) {
    const x = layout.header(row) + m * MEASURE;
    const last = row * PER_ROW + m === layout.measures;
    ink.rect(x - (last ? 0.03 : 0.008), z0 + TOP, x + (last ? 0.03 : 0.008), z0 + end);
    if (last) ink.rect(x - 0.1, z0 + TOP, x - 0.085, z0 + end);
  }
  for (const note of layout.melody) if (note.row === row) engraveNote(ink, note, z0);
  for (const beat of layout.pulse) if (beat.row === row) {
    // Beat staff: a quarter note on the middle line, stem down, every beat.
    ink.head(beat.x, beat.z, false);
    ink.rect(beat.x - 0.1, beat.z, beat.x - 0.084, beat.z + 3.4 * GAP);
  }
  return ink;
}
function engraveNote(ink, note, z0) {
  const open = note.beats >= 2, z = note.z;
  ink.head(note.x, z, open);
  // Ledger lines below C4 and above A5.
  for (let step = 0; step >= note.step; step -= 2) ink.rect(note.x - 0.17, z0 + stepZ(step) - 0.006, note.x + 0.17, z0 + stepZ(step) + 0.006);
  for (let step = 12; step <= note.step; step += 2) ink.rect(note.x - 0.17, z0 + stepZ(step) - 0.006, note.x + 0.17, z0 + stepZ(step) + 0.006);
  if (note.beats < 4) {
    const up = note.step < 6, length = 3.4 * GAP;
    const sx = note.x + (up ? 0.092 : -0.092), tip = up ? z - length : z + length;
    ink.rect(sx - 0.008, Math.min(z, tip), sx + 0.008, Math.max(z, tip));
    const flags = note.beats <= 0.25 ? 2 : note.beats <= 0.5 || note.beats === 0.75 ? 1 : 0;
    for (let f = 0; f < flags; f++) {
      const fz = tip + (up ? 1 : -1) * f * 0.11;
      ink.line(sx, fz, sx + 0.17, fz + (up ? 0.22 : -0.22), 0.04);
    }
  }
  if ([0.75, 1.5, 3].includes(note.beats)) ink.dot(note.x + 0.19, z - (note.step % 2 === 0 ? GAP / 2 : 0));
  if (note.accidental !== null && note.accidental !== undefined)
    ink.glyph(note.accidental > 0 ? 'sharp' : note.accidental < 0 ? 'flat' : 'natural', note.x - 0.24,
      z - (note.accidental < 0 ? 0.05 : 0), 0.42);
}

// ---- motion ---------------------------------------------------------------
function lastAtOrBefore(notes, time) {
  let low = 0, high = notes.length;
  while (low < high) {
    const middle = (low + high) >>> 1;
    if (notes[middle].time <= time) low = middle + 1; else high = middle;
  }
  return low - 1;
}
// Resting on a note, or mid-hop toward the next one; long gaps rest first
// and hop in the last 0.55 s so the landing is on the note's own onset.
export function ballAt(notes, time, out = {x: 0, y: 0, z: 0}) {
  if (!notes.length) { out.x = 0.6; out.y = BALL + 0.5; out.z = TOP; return out; }
  const index = lastAtOrBefore(notes, time);
  if (index < 0 || index >= notes.length - 1) {
    const note = index < 0 ? notes[0] : notes[notes.length - 1];
    out.x = note.x; out.z = note.z; out.y = BALL + (index < 0 ? 0.6 : 0);
    return out;
  }
  const from = notes[index], to = notes[index + 1];
  const hop = Math.min(0.55, to.time - from.time);
  return arc(from, to, clamp((time - (to.time - hop)) / hop, 0, 1), out);
}
// Point at time fraction `p` (0..1) of the hop from one note to the next.
// The path is one clean parabola; only the pace eases in and out.
function arc(from, to, p, out) {
  return along(from, to, p * p * (3 - 2 * p), out);
}
// Point at fraction `s` of the hop's parabola.
function along(from, to, s, out) {
  const distance = Math.hypot(to.x - from.x, to.z - from.z);
  const height = clamp(0.3 + distance * 0.22, 0.3, 2.4);
  out.x = from.x + (to.x - from.x) * s;
  out.z = from.z + (to.z - from.z) * s;
  out.y = BALL + height * 4 * s * (1 - s);
  return out;
}
// The hop in flight at `time`: its notes and how far along the path it is.
function hopAt(notes, time) {
  const index = lastAtOrBefore(notes, time);
  const from = notes[index], to = notes[index + 1];
  if (!from || !to) return null;
  const hop = Math.min(0.55, to.time - from.time), p = (time - (to.time - hop)) / hop;
  return p > 0 && p < 1 ? {from, to, s: p * p * (3 - 2 * p)} : null;
}

// Neon along the hop arcs: the live trail of each voice plus the last few
// completed hops, fading. Strips face the camera and are shaded as a lit
// tube seen side-on — |side| is the offset from its axis, so sqrt(1 - side²)
// is how squarely the tube faces the camera there: the pale centre where it
// does, the pastel body towards the rim. The head is the stroke's own
// rounded end, so a trail reads as one solid stroke.
const NEON = `
uniform vec3 uBody;
uniform vec3 uCore;
uniform float uGain;
vec3 neon(float facing){return mix(uBody,uCore,pow(facing,24.0))*uGain;}`;
const RIBBON_VERTEX = `
attribute float aAlpha;
attribute float aSide;
varying float vAlpha;
varying float vSide;
void main(){vAlpha=aAlpha;vSide=aSide;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}`;
// The strip is GLOW_SPREAD times as wide as the tube: the tube in the middle,
// a soft glow in the light's saturated colour around it. Output is
// premultiplied, so the tube covers what is behind it and the glow only adds.
const GLOW_SPREAD = 1.8;
const RIBBON_FRAGMENT = `${NEON}
uniform vec3 uGlow;
varying float vAlpha;
varying float vSide;
void main(){
  float r=abs(vSide)*${GLOW_SPREAD.toFixed(2)};
  float tube=1.0-smoothstep(0.88,1.0,r);
  float glow=exp(-3.0*max(0.0,r-1.0)/${(GLOW_SPREAD - 1).toFixed(2)})*(1.0-abs(vSide));
  vec3 color=neon(sqrt(max(0.0,1.0-min(r*r,1.0))))*tube+uGlow*glow*0.12*(1.0-tube);
  gl_FragColor=vec4(color*vAlpha,tube*vAlpha);
}`;
// Just over white: pastel after tone mapping, with a thin bloom at the edge.
const NEON_GAIN = 1.15;
const neonUniforms = (voice, gain) => ({
  uBody: {value: linear(voice.body)}, uCore: {value: linear(voice.core)}, uGain: {value: gain},
  uGlow: {value: linear(voice.light)},
});
// Stroke profile from the tail (0) to the head (1): a fine point swelling
// steadily, easing into the full width over the last stretch.
const stroke = t => Math.max(0.03, t ** 1.2 * (1.6 - 0.6 * t));
// Rounds a stroke's end: `rest` is how far the sample is from it, `radius` the
// width there, so the end is a half circle whatever the stroke's length.
const roundEnd = (rest, radius) => rest >= radius ? 1 : Math.sqrt(Math.max(0, 1 - (1 - rest / radius) ** 2));
// A played note's head lit from within: pale at the centre, the voice's
// colour towards the rim, with a soft edge.
const LIT_VERTEX = `
attribute float aRim;
varying float vRim;
void main(){vRim=aRim;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}`;
const LIT_FRAGMENT = `${NEON}
uniform float uOpacity;
varying float vRim;
void main(){
  gl_FragColor=vec4(neon(1.0-pow(vRim,4.0)),uOpacity*(1.0-smoothstep(0.86,1.0,vRim)));
}`;
// Filled heads glow from the centre out; open heads keep their hole and glow
// as a ring, brightest along the middle of the band.
function litHead(open) {
  const segments = 32, positions = [], rim = [], indices = [];
  const loops = open ? [[1.08, 1.08, 1], [0.82, 0.78, 0], [0.56, 0.5, 1]] : [[0, 0, 0], [1.08, 1.08, 1]];
  for (const [kx, kz, edge] of loops) for (const [x, z] of headOutline(0, 0, kx || 1e-4, kz || 1e-4, segments)) {
    positions.push(x, 0, z); rim.push(edge);
  }
  for (let loop = 0; loop < loops.length - 1; loop++) for (let i = 0; i < segments; i++) {
    const a = loop * segments + i, b = loop * segments + (i + 1) % segments;
    indices.push(a, b, b + segments, a, b + segments, a + segments);
  }
  const shape = geometry(positions, indices);
  shape.setAttribute('aRim', new BufferAttribute(new Float32Array(rim), 1));
  return shape;
}
const SAMPLES = 48, GHOSTS = 4, GHOST_LIFE = 2.4;
class Ribbons {
  constructor(voice) {
    const strips = GHOSTS + 1, vertices = strips * SAMPLES * 2;
    this.position = new BufferAttribute(new Float32Array(vertices * 3), 3);
    this.alpha = new BufferAttribute(new Float32Array(vertices), 1);
    const side = new Float32Array(vertices).map((_, i) => i % 2 ? 1 : -1);
    const indices = [];
    for (let s = 0; s < strips; s++) for (let i = 0; i < SAMPLES - 1; i++) {
      const a = (s * SAMPLES + i) * 2;
      indices.push(a, a + 1, a + 2, a + 1, a + 3, a + 2);
    }
    const shape = new BufferGeometry();
    shape.setAttribute('position', this.position);
    shape.setAttribute('aAlpha', this.alpha);
    shape.setAttribute('aSide', new BufferAttribute(side, 1));
    shape.setIndex(indices);
    this.mesh = new Mesh(shape, new ShaderMaterial({
      vertexShader: RIBBON_VERTEX, fragmentShader: RIBBON_FRAGMENT, transparent: true, depthWrite: false,
      premultipliedAlpha: true, side: DoubleSide, uniforms: neonUniforms(voice, NEON_GAIN),
    }));
    this.mesh.frustumCulled = false;
    this.points = Array.from({length: SAMPLES}, () => ({x: 0, y: 0, z: 0}));
    this.lengths = new Float32Array(SAMPLES);
  }
  // `points` are filled by the caller. `radius(t, rest)` is the tube's radius
  // at fraction t along the stroke, `rest` world units before its end;
  // `alpha(t)` its opacity.
  strip(index, camera, radius, alpha) {
    const base = index * SAMPLES * 2, points = this.points, lengths = this.lengths;
    for (let i = 1; i < SAMPLES; i++) lengths[i] = lengths[i - 1] +
      Math.hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y, points[i].z - points[i - 1].z);
    const total = lengths[SAMPLES - 1];
    for (let i = 0; i < SAMPLES; i++) {
      const p = points[i], q = points[Math.min(SAMPLES - 1, i + 1)], r = points[Math.max(0, i - 1)];
      const tx = q.x - r.x, ty = q.y - r.y, tz = q.z - r.z;
      const vx = camera.x - p.x, vy = camera.y - p.y, vz = camera.z - p.z;
      let sx = ty * vz - tz * vy, sy = tz * vx - tx * vz, sz = tx * vy - ty * vx;
      const length = Math.hypot(sx, sy, sz) || 1, t = i / (SAMPLES - 1);
      const w = radius(t, total - lengths[i]) * GLOW_SPREAD / length;
      sx *= w; sy *= w; sz *= w;
      this.position.setXYZ(base + i * 2, p.x - sx, p.y - sy, p.z - sz);
      this.position.setXYZ(base + i * 2 + 1, p.x + sx, p.y + sy, p.z + sz);
      const a = alpha(t);
      this.alpha.setX(base + i * 2, a);
      this.alpha.setX(base + i * 2 + 1, a);
    }
  }
  hide(index) { for (let i = 0; i < SAMPLES * 2; i++) this.alpha.setX(index * SAMPLES * 2 + i, 0); }
  commit() { this.position.needsUpdate = this.alpha.needsUpdate = true; }
}

// Light on the paper: a soft pool under each glowing thing, added to the lit
// sheet — a bright core close in and a long faint tail, so a lit note sits in
// its own glow the way it does in the reference shot.
const POOL_VERTEX = `
attribute vec2 aCorner;
attribute float aStrength;
varying vec2 vCorner;
varying float vStrength;
void main(){vCorner=aCorner;vStrength=aStrength;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}`;
const POOL_FRAGMENT = `
uniform vec3 uColor;
varying vec2 vCorner;
varying float vStrength;
void main(){
  float d=length(vCorner);
  float a=(exp(-d*d*10.0)*0.75+exp(-d*3.5)*0.25)*(1.0-smoothstep(0.75,1.0,d));
  gl_FragColor=vec4(uColor*a*vStrength,1.0);
}`;
class Pools {
  constructor(count, color) {
    this.position = new BufferAttribute(new Float32Array(count * 12), 3);
    this.strength = new BufferAttribute(new Float32Array(count * 4), 1);
    const corners = [], indices = [];
    for (let i = 0; i < count; i++) {
      corners.push(-1, -1, 1, -1, 1, 1, -1, 1);
      indices.push(i * 4, i * 4 + 2, i * 4 + 1, i * 4, i * 4 + 3, i * 4 + 2);
    }
    const shape = new BufferGeometry();
    shape.setAttribute('position', this.position);
    shape.setAttribute('aCorner', new BufferAttribute(new Float32Array(corners), 2));
    shape.setAttribute('aStrength', this.strength);
    shape.setIndex(indices);
    this.mesh = new Mesh(shape, new ShaderMaterial({vertexShader: POOL_VERTEX, fragmentShader: POOL_FRAGMENT,
      transparent: true, depthWrite: false, blending: AdditiveBlending, side: DoubleSide, uniforms: {uColor: {value: color}}}));
    this.mesh.frustumCulled = false;
  }
  set(index, x, z, radius, strength) {
    [[-1, -1], [1, -1], [1, 1], [-1, 1]].forEach(([cx, cz], k) => {
      this.position.setXYZ(index * 4 + k, x + cx * radius, 0.0015, z + cz * radius);
      this.strength.setX(index * 4 + k, strength);
    });
  }
  commit() { this.position.needsUpdate = this.strength.needsUpdate = true; }
}

// ---- runtime --------------------------------------------------------------
/**
 * Mount the 音符律动 stage in a sized host.
 * State: {score, lines, currentTime, duration, visible, playing}.
 * @returns {{update(state: object): void, dispose(): void}}
 */
export function mountMotif(host, state) {
  const document = host.ownerDocument, window = document.defaultView;
  const renderer = new WebGLRenderer({antialias: true, alpha: false, preserveDrawingBuffer: true});
  renderer.setClearColor(0x121010, 1);
  renderer.toneMapping = ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = PCFShadowMap;
  const canvas = renderer.domElement;
  canvas.style.cssText = 'display:block;width:100%;height:100%;touch-action:none;cursor:grab';
  canvas.setAttribute('aria-label', '音符律动');
  // The player's liquid glass samples canvases tagged as scene.
  canvas.dataset.lumenicLiquidGlassSceneEffect = '';
  host.append(canvas);
  const scene = new Scene();
  scene.background = new Color(0x121010);
  // Far paper melts into the dark like a shallow-lit set; pulled far back,
  // only the glowing lines remain.
  scene.fog = new Fog(0x121010, 7, 22);
  const camera = new PerspectiveCamera(40, 1, 0.05, 200);
  const disposables = [];
  const keep = value => { disposables.push(value); return value; };

  // Studio environment: soft fill for the paper and something for the metal
  // to reflect, so the print reads as metal rather than as flat black.
  const pmrem = new PMREMGenerator(renderer);
  const room = new RoomEnvironment();
  const environment = keep(pmrem.fromScene(room, 0.04).texture);
  room.dispose?.();
  pmrem.dispose();
  scene.environment = environment;
  scene.environmentIntensity = 0.09;
  // Key: one soft spot from above, behind and to the right, like a studio
  // light over the desk, following the music. It falls off towards the edges
  // of the shot and throws the floating print's soft shadows forward and to
  // the left. The coloured lights below do the rest.
  const key = new SpotLight(0xfff4ec, 2.6, 0, 0.5, 0.9, 0);
  key.castShadow = true;
  key.shadow.mapSize.set(2048, 2048);
  key.shadow.camera.near = 2;
  key.shadow.camera.far = 16;
  key.shadow.bias = -0.0002;
  key.shadow.normalBias = 0.002;
  key.shadow.radius = 3;
  scene.add(key, key.target);

  const paperMaps = paperTextures(document);
  for (const texture of Object.values(paperMaps)) keep(texture);
  // Neutral grey stock, a little satin.
  const paperMaterial = keep(new MeshStandardMaterial({color: new Color(0x86817b), map: paperMaps.color,
    roughnessMap: paperMaps.roughness, roughness: 0.92, metalness: 0}));
  const paperShape = keep(new PlaneGeometry(90, 90));
  paperShape.rotateX(-Math.PI / 2);
  const paper = new Mesh(paperShape, paperMaterial);
  paper.receiveShadow = true;
  scene.add(paper);

  // All print — staves, notes, signs and the sung characters — is dark
  // satin metal: the room's soft reflection on its faces, the coloured
  // lights mirrored along its edges. No lacquer coat, so no wet gloss.
  const metal = {color: new Color(0x625c55), metalness: 1, roughness: 0.34, shadowSide: DoubleSide};
  const inkMaterial = keep(new MeshPhysicalMaterial(metal));
  const glyphLayer = createMotifLyrics({document, metal, hover: HOVER, thick: INK_THICK, cell: 256, atlas: 1024,
    share: 0.8, jitter: false});
  // Coloured lights per voice: the trail's head, the middle of its trail and
  // the notes it has just lit. They wash the paper around them and mirror in
  // the metal print; the pools below add the bright light right under them.
  const LIGHTS_PER_VOICE = 2 + NOTE_LIGHTS;
  const lights = Array.from({length: 2 * LIGHTS_PER_VOICE}, () => {
    const light = new PointLight(0xffffff, 0, 6, 2);
    scene.add(light);
    return light;
  });
  const setLight = (voice, slot, x, y, z, strength) => {
    const light = lights[voice * LIGHTS_PER_VOICE + slot];
    light.position.set(x, y, z);
    light.intensity = strength;
    light.color.setRGB(...VOICES[voice].light, SRGBColorSpace);
  };

  // Post: a hint of depth of field focused on the trail's head (only the far
  // page softens), bloom on what is truly bright,
  // then filmic tone mapping and sRGB output.
  const composer = new EffectComposer(renderer);
  composer.addPass(new RenderPass(scene, camera));
  const bokeh = new BokehPass(scene, camera, {focus: 7, aperture: 0.0004, maxblur: 0.0015});
  composer.addPass(bokeh);
  // A restrained bloom: only the neon and lit notes cross the threshold, and
  // their halo stays thin.
  const bloom = new UnrealBloomPass(new Vector2(256, 256), 0.32, 0.25, 1.0);
  composer.addPass(bloom);
  composer.addPass(new OutputPass());
  // Quality steps down on slow machines: no depth of field, then no shadows.
  let quality = 2, slowFrames = 0;
  const sheet = new Group();
  scene.add(sheet);
  const rowGroups = new Map();

  const litShapes = [keep(litHead(false)), keep(litHead(true))];
  const voices = VOICES.map(voice => {
    // Soft glow sprites in the light's saturated colour: one round each lit
    // note (slot 0..LIT_COUNT-1) and one round the trail's head (the last).
    const glows = glowPoints(LIT_COUNT + 1, linear(voice.light));
    glows.material.uniforms.uGain.value = 1.2;
    keep(glows.geometry); keep(glows.material);
    const ribbons = new Ribbons(voice);
    keep(ribbons.mesh.geometry); keep(ribbons.mesh.material);
    // Light pools on the paper, laid out like the glows.
    const pools = new Pools(LIT_COUNT + 1, linear(voice.light));
    keep(pools.mesh.geometry); keep(pools.mesh.material);
    // Lit note heads, HDR so the bloom haloes them.
    const heads = Array.from({length: LIT_COUNT}, () => {
      const material = keep(new ShaderMaterial({vertexShader: LIT_VERTEX, fragmentShader: LIT_FRAGMENT,
        transparent: true, depthWrite: false, side: DoubleSide, uniforms: {...neonUniforms(voice, 1), uOpacity: {value: 0}}}));
      const mesh = new Mesh(litShapes[0], material);
      mesh.visible = false;
      scene.add(mesh);
      return mesh;
    });
    scene.add(glows, ribbons.mesh, pools.mesh);
    return {glows, ribbons, pools, heads, position: {x: 0, y: 0, z: 0}};
  });

  let latest = null, layout = null, scoreKey = null;
  let disposed = false, frame = 0, width = 0, height = 0, lastFrame = 0;
  const orbit = {...HOME, panX: 0, panZ: 0, vTheta: 0, vPhi: 0};
  const goal = {...HOME, panX: 0, panZ: 0, home: false};
  const target = new Vector3(), focus = new Vector3(), lens = new Vector3();
  let focusReady = false, drag = null;

  function clearRows() {
    for (const group of rowGroups.values()) {
      sheet.remove(group);
      group.traverse(item => item.geometry?.dispose());
    }
    rowGroups.clear();
  }
  function syncRows(activeRow) {
    if (!layout) return;
    for (const [row, group] of rowGroups) if (Math.abs(row - activeRow) > 5) {
      sheet.remove(group);
      group.traverse(item => item.geometry?.dispose());
      rowGroups.delete(row);
    }
    for (let row = Math.max(0, activeRow - 4); row <= Math.min(layout.rows - 1, activeRow + 4); row++) {
      if (rowGroups.has(row)) continue;
      const ink = engraveRow(layout, row);
      const group = new Group();
      group.add(ink.mesh(inkMaterial), glyphLayer.buildRow(ink.glyphs));
      rowGroups.set(row, group);
      sheet.add(group);
    }
  }
  function rebuild(next) {
    const key = next?.score ? `${next.score.trackId}:${next.score.pitch.length}:${next.lines?.length ?? 0}` : null;
    if (key === scoreKey) return;
    scoreKey = key;
    clearRows();
    layout = next?.score ? layoutMotif(buildMotifScore({lines: next.lines, score: next.score, duration: next.duration})) : null;
    focusReady = false;
  }

  const sample = {x: 0, y: 0, z: 0};
  function place(time) {
    const sequences = [layout?.melody ?? [], layout?.pulse ?? []];
    voices.forEach((voice, index) => {
      const notes = sequences[index];
      const p = ballAt(notes, time, voice.position);
      const shown = notes.length > 0;
      voice.ribbons.mesh.visible = shown;
      const last = lastAtOrBefore(notes, time);
      const sinceLanding = last >= 0 ? time - notes[last].time : Infinity;
      const flash = sinceLanding < 0.3 ? 1 - sinceLanding / 0.3 : 0;
      // Live trail: the whole flight so far, drawn along the hop's parabola
      // from the note it left — a fine point there, swelling smoothly to the
      // full head, rounded off. At rest only the lit note shows.
      const ribbons = voice.ribbons, flight = hopAt(notes, time), pools = voice.pools;
      const glows = voice.glows.geometry.attributes;
      if (flight) {
        ribbons.points.forEach((point, i) => along(flight.from, flight.to, flight.s * i / (SAMPLES - 1), point));
        ribbons.strip(0, camera.position, (t, rest) => BALL * stroke(t) * roundEnd(rest, BALL), () => 1);
      } else ribbons.hide(0);
      // Afterglow: the last completed hops linger as strokes of the same
      // shape, rounded where they landed, thinning a little as they fade.
      for (let k = 0; k < GHOSTS; k++) {
        const from = notes[last - k - 1], to = notes[last - k];
        const age = to ? time - to.time : Infinity;
        // A jump to the next row is not a hop worth a lingering arc.
        if (!from || !(age >= 0 && age < GHOST_LIFE) || from.row !== to.row) { ribbons.hide(k + 1); continue; }
        ribbons.points.forEach((point, i) => along(from, to, i / (SAMPLES - 1), point));
        const fade = (1 - age / GHOST_LIFE) ** 1.5, end = BALL * (0.4 + 0.35 * fade);
        ribbons.strip(k + 1, camera.position, (t, rest) => end * stroke(t) * roundEnd(rest, end), () => fade);
      }
      ribbons.commit();
      // The head in flight: a halo round it, its light on the print it passes
      // and a pool on the paper that tightens and brightens as it comes down.
      const height = Math.max(0, p.y - BALL), mid = ribbons.points[SAMPLES >> 1];
      glows.position.setXYZ(LIT_COUNT, p.x, p.y, p.z);
      glows.aAlpha.setX(LIT_COUNT, flight ? 0.12 + flash * 0.08 : 0);
      glows.aSize.setX(LIT_COUNT, 0.35);
      pools.set(LIT_COUNT, p.x, p.z, 0.45 + height * 0.9, flight ? 0.9 / (1 + height * 2.5) : 0);
      setLight(index, 0, p.x, p.y + 0.04, p.z, flight ? 0.9 + flash * 0.3 : 0);
      setLight(index, 1, mid.x, mid.y + 0.04, mid.z, flight ? 0.4 * flight.s : 0);
      // Played notes stay lit: a bright flash on landing settling into a soft
      // afterglow. Derived from time alone, so seeking and pausing never
      // leave stale light.
      for (let slot = 0; slot < LIT_COUNT; slot++) {
        const note = notes[last - slot];
        const age = note ? time - note.time : Infinity;
        const lit = age >= 0 && age < LIT_LIFE;
        const head = voice.heads[slot];
        head.visible = lit;
        glows.aAlpha.setX(slot, 0);
        pools.set(slot, 0, 0, 0, 0);
        if (slot < NOTE_LIGHTS) setLight(index, 2 + slot, 0, 0, 0, 0);
        if (!lit) continue;
        const fade = (1 - age / LIT_LIFE) ** 1.4;
        const strength = age < 0.18 ? 1.6 - age * 3 : 0.55 + 0.45 * Math.exp(-(age - 0.18) * 2.5);
        head.geometry = litShapes[note.beats >= 2 ? 1 : 0];
        head.position.set(note.x, INK_TOP + 0.001, note.z);
        head.material.uniforms.uGain.value = 1.25 + 0.6 * strength;
        head.material.uniforms.uOpacity.value = Math.min(1, 0.6 + 0.4 * strength) * fade;
        glows.position.setXYZ(slot, note.x, INK_TOP + 0.01, note.z);
        glows.aAlpha.setX(slot, 0.08 * strength * fade);
        glows.aSize.setX(slot, 0.4 + (age < 0.25 ? (0.25 - age) * 1.2 : 0));
        // A bright pool hugging the note with a long soft tail on the paper.
        pools.set(slot, note.x, note.z, 0.75, 0.7 * strength * fade);
        // A little above the note: washes the page around it and glints in the print.
        if (slot < NOTE_LIGHTS) setLight(index, 2 + slot, note.x, 0.3, note.z, 1.1 * strength * fade);
      }
      glows.position.needsUpdate = glows.aAlpha.needsUpdate = glows.aSize.needsUpdate = true;
      pools.commit();
    });
    syncRows(Math.max(0, Math.floor(lead().position.z / ROW)));
  }
  // The view follows the beat, which moves from the first sound to the last;
  // without a beat track, the melody.
  const lead = () => layout?.pulse.length ? voices[1] : voices[0];

  function resize() {
    const box = host.getBoundingClientRect();
    const w = Math.max(1, Math.round(box.width)), h = Math.max(1, Math.round(box.height));
    if (w === width && h === height) return;
    width = w; height = h;
    renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
    renderer.setSize(w, h, false);
    composer.setPixelRatio(renderer.getPixelRatio());
    composer.setSize(w, h);
    bloom.resolution.set(w, h);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    const scale = h * renderer.getPixelRatio() * 0.9;
    for (const voice of voices) voice.glows.material.uniforms.uScale.value = scale;
  }
  const visible = () => latest?.visible !== false && !document.hidden;
  function request() { if (!frame && !disposed) frame = window.requestAnimationFrame(render); }
  function render(now = window.performance.now()) {
    frame = 0;
    if (disposed || !latest) return;
    const dt = Math.min(0.05, lastFrame ? (now - lastFrame) / 1000 : 0.016);
    lastFrame = now;
    resize();
    place(latest.currentTime ?? 0);
    // The view follows the beat between both staves; the paper follows the view.
    focus.set(lead().position.x, 0, Math.floor(lead().position.z / ROW) * ROW + (TOP + RHYTHM) / 2);
    // A seek far away cuts instead of gliding over unengraved paper.
    if (!focusReady || target.distanceTo(focus) > ROW * 2) { target.copy(focus); focusReady = true; }
    target.lerp(focus, 1 - Math.exp(-dt * 2.2));
    if (!drag) {
      orbit.theta += orbit.vTheta; orbit.phi = clamp(orbit.phi + orbit.vPhi, 0.18, 1.42);
      orbit.vTheta *= 0.92; orbit.vPhi *= 0.92;
      if (goal.home) {
        const k = 1 - Math.exp(-dt * 6);
        for (const key of ['theta', 'phi', 'distance', 'panX', 'panZ']) orbit[key] += (goal[key] - orbit[key]) * k;
        if (Math.abs(orbit.theta - goal.theta) + Math.abs(orbit.phi - goal.phi) +
          Math.abs(orbit.distance - goal.distance) < 1e-3) goal.home = false;
      }
    }
    const center = new Vector3(target.x + orbit.panX, 0, target.z + orbit.panZ);
    camera.position.set(
      center.x + orbit.distance * Math.sin(orbit.phi) * Math.sin(orbit.theta),
      orbit.distance * Math.cos(orbit.phi),
      center.z + orbit.distance * Math.sin(orbit.phi) * Math.cos(orbit.theta));
    camera.lookAt(center);
    paper.position.set(Math.round(center.x), 0, Math.round(center.z));
    // The sheet follows the view; its grain stays put (9 tiles over 90 units).
    for (const texture of Object.values(paperMaps)) texture.offset.set(-Math.round(center.x) / 10, Math.round(center.z) / 10);
    key.position.set(center.x + 3.8, 6.2, center.z - 1.4);
    key.target.position.copy(center);
    // Like a real lens: pulled back, more of the page is sharp.
    bokeh.uniforms.focus.value = camera.position.distanceTo(lens.set(lead().position.x, lead().position.y, lead().position.z));
    bokeh.uniforms.aperture.value = 0.0004 * Math.min(1, (HOME.distance / orbit.distance) ** 2);
    const started = window.performance.now();
    composer.render(dt);
    // A frame that takes more than ~28 ms for a while drops a quality step.
    if (window.performance.now() - started > 28 || dt > 0.045) slowFrames++; else slowFrames = Math.max(0, slowFrames - 1);
    if (slowFrames > 90 && quality > 0) {
      slowFrames = 0;
      quality--;
      if (quality === 1) bokeh.enabled = false;
      if (quality === 0) { renderer.shadowMap.enabled = false; paperMaterial.needsUpdate = true; }
    }
    const moving = drag || goal.home || Math.abs(orbit.vTheta) + Math.abs(orbit.vPhi) > 1e-4 ||
      target.distanceTo(focus) > 0.01;
    if (moving && visible()) request();
  }

  // Interaction shared in feel with the 3D style: drag rotates with inertia,
  // wheel zooms, right/middle drag pans, double-click returns home.
  function pointerDown(event) {
    if (!visible()) return;
    drag = {id: event.pointerId, x: event.clientX, y: event.clientY, pan: event.button !== 0};
    goal.home = false;
    try { canvas.setPointerCapture?.(event.pointerId); } catch { /* pointer already gone */ }
    canvas.style.cursor = 'grabbing';
    event.preventDefault();
  }
  function pointerMove(event) {
    if (!drag || event.pointerId !== drag.id) return;
    const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
    drag.x = event.clientX; drag.y = event.clientY;
    if (drag.pan) {
      const scale = orbit.distance / Math.max(1, height) * 1.2;
      const c = Math.cos(orbit.theta), s = Math.sin(orbit.theta);
      orbit.panX -= (dx * c + dy * s) * scale;
      orbit.panZ -= (-dx * s + dy * c) * scale;
    } else {
      orbit.vTheta = -dx * 0.006; orbit.vPhi = -dy * 0.005;
      orbit.theta += orbit.vTheta; orbit.phi = clamp(orbit.phi + orbit.vPhi, 0.18, 1.42);
    }
    request();
  }
  function release(event) {
    if (!drag || (event?.pointerId !== undefined && event.pointerId !== drag.id)) return;
    if (canvas.hasPointerCapture?.(drag.id)) canvas.releasePointerCapture(drag.id);
    drag = null;
    canvas.style.cursor = 'grab';
    request();
  }
  function wheel(event) {
    event.preventDefault();
    orbit.distance = clamp(orbit.distance * Math.exp(event.deltaY * 0.0012), 2.4, 22);
    goal.home = false;
    request();
  }
  function home() {
    Object.assign(goal, HOME, {panX: 0, panZ: 0, home: true});
    orbit.vTheta = orbit.vPhi = 0;
    request();
  }
  const menu = event => event.preventDefault();
  const listeners = [['pointerdown', pointerDown], ['pointermove', pointerMove], ['pointerup', release],
    ['pointercancel', release], ['lostpointercapture', release], ['dblclick', home], ['contextmenu', menu]];
  for (const [name, handler] of listeners) canvas.addEventListener(name, handler);
  canvas.addEventListener('wheel', wheel, {passive: false});

  const resizeObserver = typeof window.ResizeObserver === 'function' ? new window.ResizeObserver(() => request()) : null;
  resizeObserver?.observe(host);
  // The drawing buffer is preserved, so the last frame is always readable;
  // redrawing here would also reset the frame schedule.
  const releaseScene = registerMusicMaterialSceneSource(canvas, {refresh: () => {}});
  const visibilityChanged = () => {
    if (visible()) request();
    else { if (frame) window.cancelAnimationFrame(frame); frame = 0; release(); lastFrame = 0; }
  };
  document.addEventListener('visibilitychange', visibilityChanged);

  function update(next) {
    if (disposed) return;
    latest = next;
    rebuild(next);
    visibilityChanged();
  }
  update(state);

  return {
    update,
    dispose() {
      if (disposed) return;
      disposed = true;
      if (frame) window.cancelAnimationFrame(frame);
      release();
      document.removeEventListener('visibilitychange', visibilityChanged);
      resizeObserver?.disconnect();
      releaseScene();
      for (const [name, handler] of listeners) canvas.removeEventListener(name, handler);
      canvas.removeEventListener('wheel', wheel);
      clearRows();
      for (const item of disposables) item.dispose?.();
      glyphLayer.dispose();
      composer.dispose();
      bokeh.dispose?.();
      bloom.dispose();
      renderer.dispose();
      renderer.forceContextLoss?.();
      canvas.remove();
      latest = null;
    },
  };
}
