import React, { useEffect, useRef, useState } from 'react';
import { motion, type MotionValue, useMotionValueEvent } from 'framer-motion';
import {
    Check,
    Disc3,
    Focus,
    Heart,
    ListOrdered,
    ListMusic,
    Pause,
    Play,
    Repeat,
    Repeat1,
    Shuffle,
    SkipBack,
    SkipForward,
    Volume1,
    Volume2,
    VolumeX,
} from 'lucide-react';
import ProgressBar from './ProgressBar';
import ResilientCoverImage from './app/ResilientCoverImage';
import { PlayerState, type PlaybackMode, type SongResult } from '../types';
import LiquidGlassControlSurface from './liquid-glass/LiquidGlassControlSurface';
import type { LumenicBackgroundMedia, VisualizerBackgroundMode } from '../types';
import type { PlaybackAudioQuality, ProviderMembership } from '../types/onlineMusic';
import {
    getAudioQualityPresentation,
    type AudioQualityMembershipBadge,
} from '../music/domain/audioQualityPresentation';
import { getPlaybackModeLabel } from '../utils/playbackMode';

interface FloatingPlayerControlsProps {
    currentSong: SongResult | null;
    coverUrl?: string | null;
    artistName?: string | null;
    playerState: PlayerState;
    currentTime: MotionValue<number>;
    duration: number;
    loopMode: PlaybackMode;
    currentView: 'home' | 'player';
    audioSrc: string | null;
    canTogglePlay?: boolean;
    audioQuality: PlaybackAudioQuality;
    providerMemberships?: ProviderMembership[];
    isLiked?: boolean;
    volume?: number;
    isMuted?: boolean;
    onSeek: (time: number) => void;
    onSeekStart?: () => void;
    onSeekEnd?: () => void;
    onTogglePlay: () => void;
    onToggleLoop: () => void;
    onLike?: () => void;
    onPreviousTrack?: () => void;
    onNextTrack?: () => void;
    onOpenQueue?: () => void;
    onOpenArtist?: () => void;
    onSetVolume?: (volume: number) => void;
    onPreviewVolume?: (volume: number) => void;
    onToggleMute?: () => void;
    onAudioQualityChange?: (quality: PlaybackAudioQuality) => Promise<boolean | void> | boolean | void;
    onNavigateToPlayer: () => void;
    noTrackText?: string;
    primaryColor?: string;
    secondaryColor?: string;
    isHidden?: boolean;
    isImmersiveMode?: boolean;
    onToggleImmersiveMode?: () => void;
    hideControlBar?: boolean;
    controlsDisabled?: boolean;
    visualizerBackgroundMode: VisualizerBackgroundMode | null;
    lumenicBackgroundMedia: LumenicBackgroundMedia | null;
}

const formatTime = (time: number) => {
    if (!Number.isFinite(time) || time < 0) return '00:00';
    const minutes = Math.floor(time / 60);
    const seconds = Math.floor(time % 60);
    return `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
};

const CONTROL_BUTTON_CLASS = [
    'flex h-10 w-10 shrink-0 items-center justify-center rounded-full',
    'text-white transition-[color,opacity] duration-150',
    'hover:text-white active:opacity-75',
    'focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-white/70',
    'disabled:cursor-not-allowed disabled:opacity-30',
].join(' ');

const LIQUID_GLASS_PLAYER_CONTENT_PREVIEW = false;

type PlayerIconButtonProps = React.ButtonHTMLAttributes<HTMLButtonElement> & {
    active?: boolean;
};

const PlayerIconButton: React.FC<PlayerIconButtonProps> = ({
    active = false,
    className = '',
    children,
    ...props
}) => (
    <button
        type="button"
        className={`${CONTROL_BUTTON_CLASS} ${active ? 'text-white' : ''} ${className}`}
        {...props}
    >
        {children}
    </button>
);

const TimeReadout: React.FC<{
    currentTime: MotionValue<number>;
    duration: number;
}> = ({ currentTime, duration }) => {
    const [elapsed, setElapsed] = useState(() => Math.floor(currentTime.get()));

    useMotionValueEvent(currentTime, 'change', (latest) => {
        const nextSecond = Math.floor(Math.max(0, latest));
        setElapsed(previous => previous === nextSecond ? previous : nextSecond);
    });

    return (
        <span className="whitespace-nowrap font-mono text-[11px] tabular-nums text-white/55">
            {formatTime(elapsed)} / {formatTime(duration)}
        </span>
    );
};

const QUALITY_MEMBERSHIP_BADGES: Record<AudioQualityMembershipBadge, {
    label: string;
    iconUrl?: string;
    className?: string;
    fallbackText?: string;
}> = {
    'qq-green-vip': {
        label: '绿钻会员',
        iconUrl: 'https://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png',
    },
    'qq-super-vip': {
        label: '超级会员',
        iconUrl: 'https://y.qq.com/mediastyle/lv-icon/v14/2x/svip5.png',
    },
    'netease-vip': { label: '黑胶VIP' },
    'netease-svip': { label: '黑胶SVIP' },
    'kugou-vip': {
        label: 'VIP',
        fallbackText: 'VIP',
        className: 'border-[#ff9a24]/75 bg-[#f28a16] text-white',
    },
};

const resolveQualityMembershipIcon = (
    badge: AudioQualityMembershipBadge,
    memberships: ProviderMembership[],
): ProviderMembership | undefined => memberships.find(membership => membership.kind === badge && membership.iconUrl);

const QualityMembershipBadge: React.FC<{
    badge: AudioQualityMembershipBadge;
    memberships: ProviderMembership[];
}> = ({ badge, memberships }) => {
    const presentation = QUALITY_MEMBERSHIP_BADGES[badge];
    const membership = resolveQualityMembershipIcon(badge, memberships);
    const iconUrl = membership?.iconUrl ?? presentation.iconUrl;
    if (iconUrl) {
        return (
            <span
                data-lumenic-quality-membership-badge={badge}
                title={presentation.label}
                className="inline-flex h-4 min-w-4 max-w-12 shrink-0 items-center justify-start"
            >
                <img
                    data-lumenic-quality-membership-icon={badge}
                    src={iconUrl}
                    alt=""
                    className={`h-full w-auto max-w-12 object-contain ${membership?.active === false ? 'grayscale opacity-45' : ''}`}
                />
            </span>
        );
    }
    if (!presentation.fallbackText) return null;
    return (
        <span
            data-lumenic-quality-membership-badge={badge}
            title={presentation.label}
            className={`inline-flex h-4 shrink-0 items-center border px-1 text-[7px] font-bold leading-none ${presentation.className || ''}`}
        >
            {presentation.fallbackText}
        </span>
    );
};

const FloatingPlayerControls: React.FC<FloatingPlayerControlsProps> = ({
    currentSong,
    coverUrl,
    artistName,
    playerState,
    currentTime,
    duration,
    loopMode,
    currentView,
    canTogglePlay = false,
    audioQuality,
    providerMemberships = [],
    isLiked = false,
    volume = 1,
    isMuted = false,
    onSeek,
    onSeekStart,
    onSeekEnd,
    onTogglePlay,
    onToggleLoop,
    onLike,
    onPreviousTrack,
    onNextTrack,
    onOpenQueue,
    onOpenArtist,
    onSetVolume,
    onPreviewVolume,
    onToggleMute,
    onAudioQualityChange,
    onNavigateToPlayer,
    noTrackText = 'No Track',
    isHidden = false,
    isImmersiveMode = false,
    onToggleImmersiveMode,
    hideControlBar = false,
    controlsDisabled = false,
    visualizerBackgroundMode,
    lumenicBackgroundMedia,
}) => {
    const [isQualityMenuOpen, setIsQualityMenuOpen] = useState(false);
    const [pendingQuality, setPendingQuality] = useState<PlaybackAudioQuality | null>(null);
    const [sliderVolume, setSliderVolume] = useState(isMuted ? 0 : volume);
    const [isImmersiveBarRevealed, setIsImmersiveBarRevealed] = useState(false);
    const pendingVolumeRef = useRef(sliderVolume);
    const immersiveRevealArmedRef = useRef(false);
    const qualityMenuRef = useRef<HTMLDivElement>(null);

    useEffect(() => {
        const nextVolume = isMuted ? 0 : volume;
        setSliderVolume(nextVolume);
        pendingVolumeRef.current = nextVolume;
    }, [isMuted, volume]);

    useEffect(() => {
        setIsImmersiveBarRevealed(false);
        immersiveRevealArmedRef.current = false;
    }, [isImmersiveMode]);

    useEffect(() => {
        setIsQualityMenuOpen(false);
        setPendingQuality(null);
    }, [currentSong?.id]);

    useEffect(() => {
        if (!isQualityMenuOpen) return;
        const closeOnOutsidePointer = (event: PointerEvent) => {
            if (!qualityMenuRef.current?.contains(event.target as Node)) {
                setIsQualityMenuOpen(false);
            }
        };
        document.addEventListener('pointerdown', closeOnOutsidePointer);
        return () => document.removeEventListener('pointerdown', closeOnOutsidePointer);
    }, [isQualityMenuOpen]);

    const effectivePrimaryColor = '#ffffff';
    const effectiveSecondaryColor = 'rgba(255,255,255,0.62)';
    const trackColor = 'rgba(255,255,255,0.18)';
    const handleBarClick = () => {
        if (currentView === 'home') {
            onNavigateToPlayer();
        }
    };
    const handlePlayButtonClick = (event: React.MouseEvent<HTMLButtonElement>) => {
        event.stopPropagation();
        const shouldEnterPlayer = currentView === 'home' && playerState !== PlayerState.PLAYING;
        onTogglePlay();
        if (shouldEnterPlayer) {
            onNavigateToPlayer();
        }
    };
    const stopAndRun = (event: React.MouseEvent, callback?: () => void) => {
        event.stopPropagation();
        callback?.();
    };
    const updateVolumePreview = (nextVolume: number) => {
        pendingVolumeRef.current = nextVolume;
        setSliderVolume(nextVolume);
        onPreviewVolume?.(nextVolume);
    };
    const commitVolume = () => {
        onSetVolume?.(pendingVolumeRef.current);
    };
    const VolumeIcon = isMuted || sliderVolume === 0
        ? VolumeX
        : sliderVolume < 0.5
            ? Volume1
            : Volume2;
    const playbackModeLabel = getPlaybackModeLabel(loopMode);
    const qualityPresentation = getAudioQualityPresentation(currentSong, audioQuality);
    const shouldHidePlayerBar = isImmersiveMode ? !isImmersiveBarRevealed : isHidden;

    const handleQualitySelect = async (
        event: React.MouseEvent<HTMLButtonElement>,
        quality: PlaybackAudioQuality,
    ) => {
        event.stopPropagation();
        if (!onAudioQualityChange || pendingQuality || quality === audioQuality) {
            setIsQualityMenuOpen(false);
            return;
        }
        setIsQualityMenuOpen(false);
        setPendingQuality(quality);
        try {
            await onAudioQualityChange(quality);
        } finally {
            setPendingQuality(null);
        }
    };

    const handleImmersiveHoverEnter = () => {
        if (isImmersiveMode && immersiveRevealArmedRef.current) {
            setIsImmersiveBarRevealed(true);
        }
    };
    const handleImmersiveHoverLeave = () => {
        if (isImmersiveMode) {
            immersiveRevealArmedRef.current = true;
            setIsImmersiveBarRevealed(false);
        }
    };

    if (hideControlBar) {
        return null;
    }

    return (
        <>
            <div
                data-lumenic-immersive-hover-zone={isImmersiveMode ? 'active' : undefined}
                className="pointer-events-none absolute bottom-0 left-1/2 z-[59] h-40 w-[clamp(980px,68vw,1240px)] max-w-[calc(100vw-32px)] -translate-x-1/2"
                style={{ pointerEvents: isImmersiveMode ? 'auto' : 'none' }}
                onPointerEnter={handleImmersiveHoverEnter}
                onPointerLeave={handleImmersiveHoverLeave}
            >
              <motion.div
                className="pointer-events-none absolute bottom-8 left-1/2 z-[1] flex w-full -translate-x-1/2 justify-center"
                initial={false}
                animate={{
                    opacity: shouldHidePlayerBar ? 0 : 1,
                    y: shouldHidePlayerBar ? 24 : 0,
                    scale: shouldHidePlayerBar ? 0.97 : 1,
                }}
                transition={{ duration: 0.26, ease: 'easeOut' }}
                style={{ pointerEvents: shouldHidePlayerBar ? 'none' : 'auto' }}
                onClick={(event) => event.stopPropagation()}
            >
                <div
                    data-lumenic-player-bar="mr"
                    data-lumenic-liquid-glass-controls="true"
                    className="pointer-events-auto relative h-[clamp(104px,6.2vw,112px)] w-[clamp(980px,68vw,1240px)] max-w-[calc(100vw-32px)] cursor-pointer overflow-visible rounded-full border-0 bg-transparent shadow-none"
                    onClick={handleBarClick}
                >
                    <LiquidGlassControlSurface
                        expanded
                        hidden={shouldHidePlayerBar}
                        backgroundMode={visualizerBackgroundMode}
                        media={lumenicBackgroundMedia}
                    />

                    {!LIQUID_GLASS_PLAYER_CONTENT_PREVIEW && (
                        <div className="absolute left-7 right-7 top-2.5 z-20 max-[900px]:left-6 max-[900px]:right-6">
                            <ProgressBar
                                variant="player-bar"
                                currentTime={currentTime}
                                duration={duration}
                                onSeek={onSeek}
                                onSeekStart={onSeekStart}
                                onSeekEnd={onSeekEnd}
                                primaryColor={effectivePrimaryColor}
                                secondaryColor={effectiveSecondaryColor}
                                trackColor={trackColor}
                                disabled={controlsDisabled}
                            />
                        </div>
                    )}

                    {!LIQUID_GLASS_PLAYER_CONTENT_PREVIEW && (
                    <div className="pointer-events-none relative z-30 grid h-full grid-cols-[minmax(180px,1fr)_auto_minmax(220px,1fr)] items-center gap-5 px-7 pb-3 pt-7 max-[1750px]:grid-cols-[minmax(140px,1fr)_auto_minmax(190px,1fr)] max-[1750px]:gap-3 max-[900px]:grid-cols-[minmax(82px,1fr)_auto_minmax(168px,1fr)] max-[900px]:px-5">
                        <div
                            data-lumenic-player-metadata
                            className="pointer-events-auto flex min-w-0 items-center gap-3.5"
                        >
                            <div className="flex h-16 w-16 shrink-0 items-center justify-center overflow-hidden rounded-lg border border-white/15 bg-black/20 shadow-[0_7px_22px_rgba(0,0,0,0.22)] max-[1750px]:h-14 max-[1750px]:w-14 max-[900px]:h-12 max-[900px]:w-12">
                                {coverUrl ? (
                                    <ResilientCoverImage src={coverUrl} alt="" className="h-full w-full object-cover" draggable={false} />
                                ) : (
                                    <Disc3 size={28} strokeWidth={1.6} className="text-white/50" />
                                )}
                            </div>
                            <div className="min-w-0 max-[900px]:hidden">
                                <div className="truncate text-[15px] font-semibold text-white">
                                    {currentSong?.name || noTrackText}
                                </div>
                                <button
                                    type="button"
                                    data-lumenic-player-artist
                                    disabled={!onOpenArtist}
                                    className="mt-0.5 block max-w-full truncate text-left text-[11px] leading-tight text-white/50 transition-colors hover:text-white disabled:cursor-default disabled:opacity-100"
                                    onClick={(event) => stopAndRun(event, onOpenArtist)}
                                >
                                    {artistName || '\u00a0'}
                                </button>
                            </div>
                        </div>

                        <div
                            data-lumenic-player-transport
                            className="pointer-events-auto flex items-center justify-center gap-3"
                        >
                            <PlayerIconButton
                                data-lumenic-player-like={isLiked ? 'liked' : 'unliked'}
                                title={isLiked ? '取消喜欢' : '喜欢'}
                                aria-label={isLiked ? '取消喜欢' : '喜欢'}
                                aria-pressed={isLiked}
                                active={isLiked}
                                disabled={controlsDisabled || !onLike}
                                onClick={(event) => stopAndRun(event, onLike)}
                            >
                                <Heart
                                    size={20}
                                    strokeWidth={2}
                                    fill={isLiked ? 'currentColor' : 'none'}
                                    className={isLiked ? 'text-[#ff4054]' : 'text-white'}
                                />
                            </PlayerIconButton>
                            <PlayerIconButton
                                data-lumenic-playback-mode={loopMode}
                                title={playbackModeLabel}
                                aria-label={playbackModeLabel}
                                active={loopMode !== 'off'}
                                disabled={controlsDisabled}
                                onClick={(event) => stopAndRun(event, onToggleLoop)}
                            >
                                {loopMode === 'off'
                                    ? <ListOrdered size={20} strokeWidth={2} />
                                    : loopMode === 'one'
                                        ? <Repeat1 size={20} strokeWidth={2} />
                                        : loopMode === 'shuffle'
                                            ? <Shuffle size={20} strokeWidth={2} />
                                            : <Repeat size={20} strokeWidth={2} />}
                            </PlayerIconButton>
                            <PlayerIconButton
                                title="上一首"
                                aria-label="上一首"
                                disabled={controlsDisabled || !onPreviousTrack}
                                onClick={(event) => stopAndRun(event, onPreviousTrack)}
                            >
                                <SkipBack size={20} strokeWidth={2} fill="currentColor" />
                            </PlayerIconButton>
                            <button
                                type="button"
                                title={playerState === PlayerState.PLAYING ? '暂停' : '播放'}
                                aria-label={playerState === PlayerState.PLAYING ? '暂停' : '播放'}
                                disabled={!canTogglePlay || controlsDisabled}
                                className="mx-2 flex h-14 w-14 shrink-0 items-center justify-center rounded-full text-white transition-opacity duration-150 hover:opacity-90 active:opacity-75 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-white/80 disabled:cursor-not-allowed disabled:opacity-35"
                                onClick={handlePlayButtonClick}
                            >
                                {playerState === PlayerState.PLAYING
                                    ? <Pause size={24} strokeWidth={2} fill="currentColor" />
                                    : <Play size={24} strokeWidth={2} fill="currentColor" className="ml-0.5" />}
                            </button>
                            <PlayerIconButton
                                title="下一首"
                                aria-label="下一首"
                                disabled={controlsDisabled || !onNextTrack}
                                onClick={(event) => stopAndRun(event, onNextTrack)}
                            >
                                <SkipForward size={20} strokeWidth={2} fill="currentColor" />
                            </PlayerIconButton>
                        </div>

                        <div
                            data-lumenic-player-utilities
                            className="pointer-events-auto flex min-w-0 items-center justify-end gap-3"
                        >
                            <PlayerIconButton
                                title="播放队列"
                                aria-label="播放队列"
                                disabled={!onOpenQueue}
                                onClick={(event) => stopAndRun(event, onOpenQueue)}
                            >
                                <ListMusic size={20} strokeWidth={1.8} />
                            </PlayerIconButton>
                            <div ref={qualityMenuRef} className="relative z-40">
                                <button
                                    type="button"
                                    data-lumenic-audio-quality-trigger
                                    title={`音质：${qualityPresentation.currentLabel}`}
                                    aria-label={`音质：${qualityPresentation.currentLabel}`}
                                    aria-haspopup={qualityPresentation.selectable ? 'menu' : undefined}
                                    aria-expanded={qualityPresentation.selectable ? isQualityMenuOpen : undefined}
                                    disabled={controlsDisabled || !qualityPresentation.selectable || pendingQuality !== null}
                                    className="flex h-10 min-w-12 shrink-0 items-center justify-center px-1 text-[11px] font-semibold text-white transition-[color,opacity] duration-150 hover:text-white active:opacity-75 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-white/70 disabled:cursor-default disabled:opacity-55"
                                    onClick={(event) => {
                                        event.stopPropagation();
                                        if (qualityPresentation.selectable) {
                                            setIsQualityMenuOpen(previous => !previous);
                                        }
                                    }}
                                >
                                    {qualityPresentation.currentLabel}
                                </button>
                                {isQualityMenuOpen && qualityPresentation.selectable && (
                                    <motion.div
                                        data-lumenic-audio-quality-menu
                                        role="menu"
                                        initial={{ opacity: 0, y: 6, scale: 0.98 }}
                                        animate={{ opacity: 1, y: 0, scale: 1 }}
                                        transition={{ duration: 0.16, ease: 'easeOut' }}
                                        className="absolute bottom-[calc(100%+8px)] right-0 w-64 overflow-hidden rounded-lg border border-white/20 bg-black/55 p-1.5 shadow-[0_14px_38px_rgba(0,0,0,0.34)] backdrop-blur-2xl"
                                        onClick={(event) => event.stopPropagation()}
                                    >
                                        {qualityPresentation.options.map(option => {
                                            const selected = option.value === audioQuality;
                                            return (
                                                <button
                                                    key={option.value}
                                                    type="button"
                                                    role="menuitemradio"
                                                    aria-checked={selected}
                                                    data-lumenic-audio-quality-option={option.value}
                                                    className="flex min-h-11 w-full items-center justify-between gap-3 rounded-md px-3 py-2 text-left text-white/75 transition-colors duration-150 hover:bg-white/[0.14] hover:text-white focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-white/60"
                                                    onClick={(event) => void handleQualitySelect(event, option.value)}
                                                >
                                                    <span className="min-w-0 flex-1">
                                                        <span className="flex min-w-0 items-center gap-1.5 text-[12px] font-medium text-white/90">
                                                            <span className="shrink-0">{option.label}</span>
                                                            {option.membershipBadges?.map(badge => (
                                                                <QualityMembershipBadge key={badge} badge={badge} memberships={providerMemberships} />
                                                            ))}
                                                        </span>
                                                        <span className="mt-0.5 block truncate text-[10px] text-white/45">{option.detail}</span>
                                                    </span>
                                                    {selected && <Check size={15} strokeWidth={2.2} />}
                                                </button>
                                            );
                                        })}
                                    </motion.div>
                                )}
                            </div>
                            {onToggleImmersiveMode && (
                                <PlayerIconButton
                                    data-lumenic-immersive-toggle
                                    title={isImmersiveMode ? '退出沉浸模式' : '沉浸模式'}
                                    aria-label={isImmersiveMode ? '退出沉浸模式' : '沉浸模式'}
                                    aria-pressed={isImmersiveMode}
                                    active={isImmersiveMode}
                                    onClick={(event) => stopAndRun(event, onToggleImmersiveMode)}
                                >
                                    <Focus size={20} strokeWidth={1.8} />
                                </PlayerIconButton>
                            )}
                            <div className="group/volume relative z-40">
                                <PlayerIconButton
                                    title={isMuted ? '取消静音' : '静音'}
                                    aria-label={isMuted ? '取消静音' : '静音'}
                                    disabled={!onToggleMute}
                                    onClick={(event) => stopAndRun(event, onToggleMute)}
                                >
                                    <VolumeIcon size={20} strokeWidth={1.8} />
                                </PlayerIconButton>
                                <div
                                    data-lumenic-volume-popover
                                    className="pointer-events-none absolute bottom-[calc(100%+6px)] left-1/2 flex w-14 -translate-x-1/2 translate-y-1 flex-col items-center gap-2 rounded-xl border border-white/20 bg-white/18 px-2 py-3 opacity-0 shadow-[0_12px_32px_rgba(0,0,0,0.28)] backdrop-blur-2xl transition-[opacity,transform] after:absolute after:-bottom-1.5 after:left-1/2 after:h-3 after:w-3 after:-translate-x-1/2 after:rotate-45 after:border-b after:border-r after:border-white/20 after:bg-white/18 after:content-[''] group-hover/volume:pointer-events-auto group-hover/volume:translate-y-0 group-hover/volume:opacity-100 group-focus-within/volume:pointer-events-auto group-focus-within/volume:translate-y-0 group-focus-within/volume:opacity-100"
                                    onClick={(event) => event.stopPropagation()}
                                >
                                    <input
                                        data-lumenic-volume-slider="vertical"
                                        type="range"
                                        min="0"
                                        max="1"
                                        step="0.01"
                                        value={sliderVolume}
                                        aria-label="音量"
                                        className="relative z-10 h-28 w-4 cursor-pointer accent-[#ff4054]"
                                        style={{ writingMode: 'vertical-lr', direction: 'rtl' }}
                                        onInput={(event) => updateVolumePreview(Number(event.currentTarget.value))}
                                        onChange={(event) => updateVolumePreview(Number(event.currentTarget.value))}
                                        onPointerUp={commitVolume}
                                        onPointerCancel={commitVolume}
                                        onKeyUp={commitVolume}
                                        onBlur={commitVolume}
                                    />
                                    <span className="relative z-10 text-[10px] font-medium tabular-nums text-white/65">
                                        {Math.round(sliderVolume * 100)}%
                                    </span>
                                </div>
                            </div>
                            <div className="ml-2 max-[1400px]:hidden">
                                <TimeReadout currentTime={currentTime} duration={duration} />
                            </div>
                        </div>
                    </div>
                    )}
                </div>
              </motion.div>
            </div>

        </>
    );
};

export default FloatingPlayerControls;
