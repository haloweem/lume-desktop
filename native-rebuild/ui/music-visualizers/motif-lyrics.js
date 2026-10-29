import {
  BufferAttribute,BufferGeometry,CanvasTexture,Color,Group,Mesh,MeshPhysicalMaterial,NoColorSpace,SRGBColorSpace,Vector2,
} from './motif-vendor/three.module.js';

// 音符律动 engraving: the sung characters (and the score's music symbols)
// cut from thin dark metal that floats a little above the paper, with
// bevelled edges, casting a real soft shadow. Nothing animates;
// physically based shading does the work: the passing coloured lights
// reflect in the bevels, the faces turned away stay in shade. Characters are
// set slightly unevenly (offset, turned, sized) so a line reads hand-placed.

// Quads are scaled up so the glyph (a share of its cell, leaving room for
// the bevel blur) keeps its nominal size.
const QUAD_SCALE = 1.2;
const FONT = '"Noto Serif SC","Source Han Serif SC","Songti SC","STSong","SimSun",serif';

// Items: {text, x, z, size, time?, font?, weight?}. `metal` holds the
// MeshPhysicalMaterial settings shared with the rest of the print.
export function createMotifLyrics({document, metal, cell: CELL = 128, atlas: ATLAS = 2048, share = 0.62,
  hover = 0.003, thick: THICK = 0.03, jitter = true}) {
  // Enough stacked slices that the edge reads as a solid wall, not stripes.
  const LAYERS = Math.max(4, Math.round(THICK / 0.0008));
  const COLUMNS = Math.floor(ATLAS / CELL);
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = ATLAS;
  const context = canvas.getContext('2d', {willReadFrequently: true});
  const normalCanvas = document.createElement('canvas');
  normalCanvas.width = normalCanvas.height = ATLAS;
  const normalContext = normalCanvas.getContext('2d');
  normalContext.fillStyle = 'rgb(128,128,255)';
  normalContext.fillRect(0, 0, ATLAS, ATLAS);
  const atlas = new CanvasTexture(canvas);
  atlas.colorSpace = SRGBColorSpace;
  atlas.anisotropy = 8;
  const normals = new CanvasTexture(normalCanvas);
  normals.colorSpace = NoColorSpace;
  normals.anisotropy = 8;
  const cells = new Map();
  // The top face carries the bevel normals; the walls darken towards the paper.
  const lacquer = {...metal, color: new Color(metal.color), map: atlas, alphaTest: 0.5, vertexColors: true};
  const top = new MeshPhysicalMaterial({...lacquer, normalMap: normals, normalScale: new Vector2(0.8, 0.8)});
  const sides = new MeshPhysicalMaterial(lacquer);

  // One atlas cell per character, with a normal map from its blurred height.
  function cell(text, font = FONT, weight = 'bold') {
    const name = `${font}|${weight}|${text}`;
    if (cells.has(name)) return cells.get(name);
    const index = cells.size;
    if (index >= COLUMNS * COLUMNS) return cells.get(cells.keys().next().value);
    const x = (index % COLUMNS) * CELL, y = Math.floor(index / COLUMNS) * CELL;
    context.clearRect(x, y, CELL, CELL);
    context.fillStyle = '#fff';
    context.textAlign = 'center';
    context.textBaseline = 'middle';
    context.font = `${weight} ${Math.round(CELL * share)}px ${font}`;
    context.fillText(text, x + CELL / 2, y + CELL / 2 + CELL * 0.03);
    const alpha = context.getImageData(x, y, CELL, CELL).data;
    let height = new Float32Array(CELL * CELL);
    for (let i = 0; i < height.length; i++) height[i] = alpha[i * 4 + 3] / 255;
    // Three box blurs ≈ a gaussian: a rounded bevel about 3 px wide.
    for (let pass = 0; pass < 3; pass++) for (const horizontal of [true, false]) {
      const next = new Float32Array(height.length);
      for (let py = 0; py < CELL; py++) for (let px = 0; px < CELL; px++) {
        let sum = 0, count = 0;
        for (let k = -2; k <= 2; k++) {
          const qx = horizontal ? px + k : px, qy = horizontal ? py : py + k;
          if (qx < 0 || qy < 0 || qx >= CELL || qy >= CELL) continue;
          sum += height[qy * CELL + qx]; count++;
        }
        next[py * CELL + px] = sum / count;
      }
      height = next;
    }
    const image = normalContext.createImageData(CELL, CELL);
    for (let py = 0; py < CELL; py++) for (let px = 0; px < CELL; px++) {
      const at = (qx, qy) => height[Math.min(CELL - 1, Math.max(0, qy)) * CELL + Math.min(CELL - 1, Math.max(0, qx))];
      const dx = (at(px + 1, py) - at(px - 1, py)) * 3, dy = (at(px, py - 1) - at(px, py + 1)) * 3;
      const length = Math.hypot(dx, dy, 1), o = (py * CELL + px) * 4;
      image.data[o] = (-dx / length * 0.5 + 0.5) * 255;
      image.data[o + 1] = (-dy / length * 0.5 + 0.5) * 255;
      image.data[o + 2] = (1 / length * 0.5 + 0.5) * 255;
      image.data[o + 3] = 255;
    }
    normalContext.putImageData(image, x, y);
    const entry = {u0: x / ATLAS, v0: 1 - (y + CELL) / ATLAS, u1: (x + CELL) / ATLAS, v1: 1 - y / ATLAS};
    cells.set(name, entry);
    atlas.needsUpdate = normals.needsUpdate = true;
    return entry;
  }

  function buildRow(lyrics) {
    const group = new Group();
    if (!lyrics.length) return group;
    let seed = 1;
    const random = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };
    const build = (layers, material) => {
      const position = [], uv = [], color = [], index = [];
      for (const lyric of lyrics) {
        const entry = cell(lyric.text, lyric.font, lyric.weight);
        seed = (Math.floor(Math.abs(lyric.time ?? 0) * 997) % 2147483646) + 1;
        // A gentle, unhurried unevenness: turn ±7°, lift ±0.09, size ±8%.
        const turn = jitter ? (random() - 0.5) * 0.24 : 0, lift = jitter ? (random() - 0.5) * 0.18 : 0;
        const scale = jitter ? 0.92 + random() * 0.16 : 1;
        const size = lyric.size * scale * QUAD_SCALE, c = Math.cos(turn), s = Math.sin(turn);
        for (const layer of layers) {
          const base = position.length / 3, y = hover + layer * THICK;
          for (const [lx, lz, u, v] of [[-0.5, 0.5, entry.u0, entry.v0], [0.5, 0.5, entry.u1, entry.v0],
            [0.5, -0.5, entry.u1, entry.v1], [-0.5, -0.5, entry.u0, entry.v1]]) {
            position.push(lyric.x + (lx * c - lz * s) * size, y, lyric.z + lift + (lx * s + lz * c) * size);
            uv.push(u, v);
            // The walls darken towards the paper, as if in their own shade.
            const shade = layer >= 1 ? 1 : 0.35 + 0.45 * layer;
            color.push(shade, shade, shade);
          }
          index.push(base, base + 1, base + 2, base, base + 2, base + 3);
        }
      }
      const shape = new BufferGeometry();
      shape.setAttribute('position', new BufferAttribute(new Float32Array(position), 3));
      shape.setAttribute('uv', new BufferAttribute(new Float32Array(uv), 2));
      shape.setAttribute('color', new BufferAttribute(new Float32Array(color), 3));
      shape.setAttribute('normal', new BufferAttribute(new Float32Array(position.length).map((_, i) => i % 3 === 1 ? 1 : 0), 3));
      shape.setIndex(index);
      const mesh = new Mesh(shape, material);
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      return mesh;
    };
    group.add(build(Array.from({length: LAYERS - 1}, (_, i) => i / (LAYERS - 1)), sides), build([1], top));
    return group;
  }

  return {
    buildRow,
    reset() {
      cells.clear();
      context.clearRect(0, 0, ATLAS, ATLAS);
      normalContext.fillRect(0, 0, ATLAS, ATLAS);
      atlas.needsUpdate = normals.needsUpdate = true;
    },
    dispose() { top.dispose(); sides.dispose(); atlas.dispose(); normals.dispose(); },
  };
}
