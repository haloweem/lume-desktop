import * as THREE from 'three';
import {
    STAR_RIVER_VERTEX_SHADER,
    STAR_RIVER_FRAGMENT_SHADER,
} from './starRiverPointShaders';

// Owns MR preset 5's GPU resources; hosts supply camera/rotation and the frame clock.
export const STAR_RIVER_SCENE_SCALE = (16 / 6.6) * Math.tan(44 * Math.PI / 360) / Math.tan(45 * Math.PI / 360);
const GRID = 183;

function createDotTexture() {
    const size = 64;
    const data = new Uint8Array(size * size * 4);
    const stops = [[0, 0.96], [0.42, 0.78], [0.72, 0.22], [1, 0]];
    for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
            const radius = Math.min(1, Math.hypot(x + 0.5 - 32, y + 0.5 - 32) / 31);
            const index = Math.max(1, stops.findIndex(stop => stop[0] >= radius));
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
    geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute('aUv', new THREE.BufferAttribute(uv, 2));
    geometry.setAttribute('aRand', new THREE.BufferAttribute(seeds, 1));
    return geometry;
}

export class StarRiverParticles extends THREE.Group {
    private readonly geometry = createGeometry();
    private readonly dot = createDotTexture();
    private readonly fallback = new THREE.DataTexture(new Uint8Array([28, 28, 40, 255]), 1, 1, THREE.RGBAFormat);
    private readonly uniforms = {
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
        uCoverTex: { value: this.fallback as THREE.Texture },
    };
    private readonly material: THREE.ShaderMaterial;
    private coverUrl: string | null = null;
    private coverTexture: THREE.Texture | null = null;
    private pendingCover: THREE.Texture | null = null;
    private disposed = false;

    constructor(private readonly onTextureReady?: () => void) {
        super();
        this.name = 'mineradio-star-river';
        this.scale.setScalar(STAR_RIVER_SCENE_SCALE);
        this.fallback.needsUpdate = true;
        this.material = new THREE.ShaderMaterial({
            uniforms: { ...this.uniforms, uBloomSize: { value: 1 } },
            vertexShader: STAR_RIVER_VERTEX_SHADER,
            fragmentShader: STAR_RIVER_FRAGMENT_SHADER,
            transparent: true,
            depthWrite: false,
            blending: THREE.NormalBlending,
        });
        const points = new THREE.Points(this.geometry, this.material);
        points.frustumCulled = false;
        points.renderOrder = -1;
        this.add(points);
    }

    setCoverUrl(url?: string | null) {
        const nextUrl = url || null;
        if (this.disposed || this.coverUrl === nextUrl) return;
        this.coverUrl = nextUrl;
        this.pendingCover?.dispose();
        this.pendingCover = null;
        this.coverTexture?.dispose();
        this.coverTexture = null;
        this.uniforms.uCoverTex.value = this.fallback;
        if (!nextUrl) return;
        const pending = new THREE.TextureLoader().load(nextUrl, texture => {
            if (this.disposed || this.pendingCover !== texture) {
                texture.dispose();
                return;
            }
            this.pendingCover = null;
            this.coverTexture = texture;
            this.uniforms.uCoverTex.value = texture;
            this.onTextureReady?.();
        }, undefined, () => {
            pending.dispose();
            if (this.pendingCover === pending) this.pendingCover = null;
        });
        this.pendingCover = pending;
    }

    update(deltaSeconds: number, pixelRatio: number, staticMode: boolean) {
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
        this.pendingCover?.dispose();
        this.coverTexture?.dispose();
        this.pendingCover = this.coverTexture = null;
    }
}
