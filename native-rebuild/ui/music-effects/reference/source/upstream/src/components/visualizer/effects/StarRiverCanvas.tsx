import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { StarRiverParticles } from './StarRiverParticles';
import { getLumeEmbeddedTopInset, getLumeFullBleedLayerStyle, registerLumeTopVisualLayer } from '../lumeTopVisualCompositor';
import { publishLiquidGlassWebGLSceneFrame, unregisterLiquidGlassWebGLSceneCanvas } from '../../liquid-glass/LiquidGlassWebGLSceneBridge';

// Fixed-camera host for non-3D lyrics; the 3D stage owns its own shared-scene instance.
export default function StarRiverCanvas({ paused = false, coverUrl }: {
    paused?: boolean;
    coverUrl?: string | null;
}) {
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const riverRef = useRef<StarRiverParticles | null>(null);
    const pausedRef = useRef(paused);
    pausedRef.current = paused;

    useEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;
        const renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, preserveDrawingBuffer: true });
        renderer.setClearColor(0, 0);
        renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
        const camera = new THREE.PerspectiveCamera(44, 1, 0.1, 300);
        camera.position.set(0, 16 * Math.sin(0.06), 16 * Math.cos(0.06));
        camera.lookAt(0, 0, 0);
        const scene = new THREE.Scene();
        const river = new StarRiverParticles();
        riverRef.current = river;
        scene.add(river);
        const layer = registerLumeTopVisualLayer({ canvas, order: 10, role: 'effect' });
        const resize = () => {
            const width = Math.max(1, canvas.clientWidth);
            const height = Math.max(1, canvas.clientHeight);
            renderer.setSize(width, height, false);
            camera.aspect = width / height;
            camera.updateProjectionMatrix();
        };
        const observer = new ResizeObserver(resize);
        observer.observe(canvas);
        resize();
        let lastTime = performance.now();
        let frame = 0;
        const draw = (now: number) => {
            const dt = Math.min(0.08, (now - lastTime) / 1000);
            lastTime = now;
            if (!document.hidden) {
                river.update(dt, renderer.getPixelRatio(), pausedRef.current);
                renderer.render(scene, camera);
                publishLiquidGlassWebGLSceneFrame(canvas);
                layer.requestFrame();
            }
            frame = requestAnimationFrame(draw);
        };
        frame = requestAnimationFrame(draw);
        return () => {
            cancelAnimationFrame(frame);
            observer.disconnect();
            layer.dispose();
            unregisterLiquidGlassWebGLSceneCanvas(canvas);
            river.dispose();
            renderer.dispose();
            riverRef.current = null;
        };
    }, []);

    useEffect(() => { riverRef.current?.setCoverUrl(coverUrl); }, [coverUrl]);

    return <canvas ref={canvasRef} data-visualizer-effect="star-river" data-lumenic-liquid-glass-scene-effect="true" className="pointer-events-none absolute inset-x-0 bottom-0 h-full w-full" style={getLumeFullBleedLayerStyle(getLumeEmbeddedTopInset())} />;
}
