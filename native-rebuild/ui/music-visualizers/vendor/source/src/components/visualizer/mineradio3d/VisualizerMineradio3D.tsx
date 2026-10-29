import { useEffect, useRef } from 'react';
import {
    publishLiquidGlassWebGLSceneFrame,
    registerLiquidGlassSceneCanvas,
    unregisterLiquidGlassWebGLSceneCanvas,
} from '../../liquid-glass/LiquidGlassWebGLSceneBridge';
import type { VisualizerSharedProps } from '../definition';
import VisualizerShell from '../VisualizerShell';
import {
    captureLumeTopVisualFrame,
    getLumeEmbeddedTopInset,
    getLumeFullBleedLayerStyle,
    registerLumeTopVisualLayer,
    type LumeTopVisualLayerRegistration,
} from '../lumeTopVisualCompositor';
import { Mineradio3DStageRuntime, type Mineradio3DStageState } from './stageRuntime';

const VisualizerMineradio3D = (props: VisualizerSharedProps) => {
    const {
        currentTime,
        currentLineIndex,
        lines,
        theme,
        audioPower,
        audioBands,
        paused = false,
        staticMode = false,
        showText = true,
        visualizerEffectMode,
        coverUrl,
    } = props;
    const hostRef = useRef<HTMLDivElement>(null);
    const runtimeRef = useRef<Mineradio3DStageRuntime | null>(null);
    const topVisualLayerRef = useRef<LumeTopVisualLayerRegistration | null>(null);
    const topInset = getLumeEmbeddedTopInset();
    const stateRef = useRef<Mineradio3DStageState>({
        lines: showText ? lines : [],
        activeLineIndex: currentLineIndex,
        currentTime: currentTime.get(),
        theme,
        showTranslation: true,
        paused,
        staticMode,
        visualizerEffectMode,
        coverUrl,
    });

    stateRef.current = {
        lines: showText ? lines : [],
        activeLineIndex: currentLineIndex,
        currentTime: currentTime.get(),
        theme,
        showTranslation: true,
        paused,
        staticMode,
        visualizerEffectMode,
        coverUrl,
    };

    useEffect(() => {
        const host = hostRef.current;
        if (!host) return;
        const topVisualCanvas = document.createElement('canvas');
        topVisualCanvas.dataset.lumenicTopVisualSnapshot = 'mineradio-3d';
        topVisualCanvas.style.position = 'absolute';
        topVisualCanvas.style.left = '0';
        topVisualCanvas.style.top = '0';
        topVisualCanvas.style.visibility = 'hidden';
        topVisualCanvas.style.pointerEvents = 'none';
        host.appendChild(topVisualCanvas);
        const runtime = new Mineradio3DStageRuntime(
            host,
            stateRef.current,
            canvas => {
                const capturedTopFrame = captureLumeTopVisualFrame(canvas, topVisualCanvas, topInset);
                publishLiquidGlassWebGLSceneFrame(canvas);
                if (capturedTopFrame) topVisualLayerRef.current?.requestFrame();
            },
        );
        runtimeRef.current = runtime;
        registerLiquidGlassSceneCanvas(runtime.getCanvas(), () => runtime.refreshSceneFrame());
        topVisualLayerRef.current = registerLumeTopVisualLayer({
            canvas: topVisualCanvas,
            order: 20,
            role: 'lyrics',
        });
        const unsubscribe = currentTime.on('change', time => {
            runtime.update({ ...stateRef.current, currentTime: time });
        });
        return () => {
            unsubscribe();
            topVisualLayerRef.current?.dispose();
            topVisualLayerRef.current = null;
            unregisterLiquidGlassWebGLSceneCanvas(runtime.getCanvas());
            runtime.destroy();
            topVisualCanvas.remove();
            runtimeRef.current = null;
        };
    }, [currentTime, topInset]);

    useEffect(() => {
        runtimeRef.current?.update(stateRef.current);
    }, [currentLineIndex, lines, paused, staticMode, showText, theme, visualizerEffectMode, coverUrl]);

    return (
        <VisualizerShell
            theme={theme}
            audioPower={audioPower}
            audioBands={audioBands}
            sharedProps={props}
            sceneEffectMode={visualizerEffectMode === 'star-river' ? 'star-river' : undefined}
        >
            <div
                ref={hostRef}
                data-visualizer="mineradio-3d"
                className="absolute inset-x-0 bottom-0 z-10 pointer-events-auto"
                style={getLumeFullBleedLayerStyle(topInset)}
            />
        </VisualizerShell>
    );
};

export default VisualizerMineradio3D;
