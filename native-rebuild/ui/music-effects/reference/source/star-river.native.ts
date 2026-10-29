import * as THREE from 'three';
import {StarRiverParticles} from './StarRiverParticles';
export function mountStarRiverCanvas(canvas, state, {onError} = {}) {
    let renderer = null, river = null, observer = null, frame = 0, disposed = false;
    try {
        renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, preserveDrawingBuffer: true });
        renderer.setClearColor(0, 0);
        renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
        const camera = new THREE.PerspectiveCamera(44, 1, 0.1, 300);
        camera.position.set(0, 16 * Math.sin(0.06), 16 * Math.cos(0.06));
        camera.lookAt(0, 0, 0);
        const scene = new THREE.Scene();
        river = new StarRiverParticles();
        scene.add(river);

        const resize = () => {
            const width = Math.max(1, canvas.clientWidth);
            const height = Math.max(1, canvas.clientHeight);
            renderer.setSize(width, height, false);
            camera.aspect = width / height;
            camera.updateProjectionMatrix();
        };
        observer = new ResizeObserver(resize);
        observer.observe(canvas);
        resize();
        let lastTime = performance.now();

        const draw = (now: number) => {
            if (disposed) return;
            try {
            const dt = Math.min(0.08, (now - lastTime) / 1000);
            lastTime = now;
            if (!document.hidden) {
                river.update(dt, renderer.getPixelRatio(), false);
                renderer.render(scene, camera);

            }
            frame = requestAnimationFrame(draw);
            } catch (error) { dispose(); onError?.(error); }
        };
        frame = requestAnimationFrame(draw);

        const update = state => river.setCoverUrl(state.current?.artwork || null);
        update(state);
        return {update, dispose};
    } catch (error) { dispose(); throw error; }
    function dispose() {
        if (disposed) return;
        disposed = true;
        cancelAnimationFrame(frame);
        observer?.disconnect();
        river?.dispose();
        renderer?.dispose();
        renderer?.forceContextLoss();
        if (!renderer) {
            const context = canvas.getContext('webgl2') || canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
            context?.getExtension('WEBGL_lose_context')?.loseContext();
        }
        canvas.width = canvas.height = 1;
    }
}