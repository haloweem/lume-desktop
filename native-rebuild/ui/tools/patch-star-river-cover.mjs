// Both bundles adapt the same historical class; keep cancellation identical.
export function patchStarRiverCover(source) {
  let text = source.replaceAll('\r\n', '\n');
  const replace = (from, to, count = 1) => {
    if (text.split(from).length - 1 !== count) throw new Error(`Star river patch drift: ${from}`);
    text = text.split(from).join(to);
  };
  replace('    private pendingCover: THREE.Texture | null = null;',
    '    private pendingCover: THREE.Texture | null = null;\n    private cancelCover: (() => void) | null = null;');
  replace('        this.pendingCover?.dispose();',
    '        this.cancelCover?.();\n        this.pendingCover?.dispose();', 2);
  replace(`        const pending = new THREE.TextureLoader().load(nextUrl, texture => {
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
        this.pendingCover = pending;`, `        const pending = new THREE.Texture();
        const image = document.createElementNS('http://www.w3.org/1999/xhtml', 'img');
        let active = true;
        const detach = () => {
            image.removeEventListener('load', loaded);
            image.removeEventListener('error', failed);
            this.cancelCover = null;
        };
        const loaded = () => {
            if (!active) return;
            active = false;
            detach();
            // Match Three 0.128 TextureLoader without retaining its uncancellable image.
            pending.image = image;
            const isJPEG = nextUrl.search(/\\.jpe?g($|\\?)/i) > 0 || nextUrl.search(/^data\\:image\\/jpeg/) === 0;
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
            image.removeAttribute('src');
            pending.dispose();
            this.pendingCover = null;
        };
        this.pendingCover = pending;
        this.cancelCover = () => {
            if (!active) return;
            active = false;
            detach();
            image.removeAttribute('src');
        };
        image.addEventListener('load', loaded);
        image.addEventListener('error', failed);
        if (nextUrl.substr(0, 5) !== 'data:') image.crossOrigin = 'anonymous';
        image.src = nextUrl;`);
  return text;
}
