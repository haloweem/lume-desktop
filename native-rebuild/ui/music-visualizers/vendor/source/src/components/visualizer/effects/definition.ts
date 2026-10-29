export type VisualizerEffectMode = 'none' | 'rain' | 'rain-window' | 'ripples' | 'snow' | 'stars' | 'star-river';

export interface VisualizerEffectOption {
    mode: VisualizerEffectMode;
    labelKey: string;
    labelFallback: string;
}

export const DEFAULT_VISUALIZER_EFFECT_MODE: VisualizerEffectMode = 'none';

export const VISUALIZER_EFFECT_OPTIONS: readonly VisualizerEffectOption[] = [
    { mode: 'none', labelKey: 'options.visualizerEffectNone', labelFallback: 'Off' },
    { mode: 'rain-window', labelKey: 'options.visualizerEffectRainWindow', labelFallback: 'Rain Window' },
    { mode: 'snow', labelKey: 'options.visualizerEffectSnow', labelFallback: 'Snow' },
    { mode: 'star-river', labelKey: 'options.visualizerEffectStarRiver', labelFallback: 'Star River' },
] as const;

const VISUALIZER_EFFECT_MODES = new Set(VISUALIZER_EFFECT_OPTIONS.map(option => option.mode));

export const resolveVisualizerEffectMode = (value: unknown): VisualizerEffectMode => (
    typeof value === 'string' && VISUALIZER_EFFECT_MODES.has(value as VisualizerEffectMode)
        ? value as VisualizerEffectMode
        : DEFAULT_VISUALIZER_EFFECT_MODE
);

export const getVisualizerEffectModeLabel = (
    mode: VisualizerEffectMode,
    t: (key: string) => string,
) => {
    const option = VISUALIZER_EFFECT_OPTIONS.find(candidate => candidate.mode === mode)
        ?? VISUALIZER_EFFECT_OPTIONS[0];
    const translated = t(option.labelKey);
    return translated === option.labelKey ? option.labelFallback : translated;
};
