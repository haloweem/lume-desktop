import type { LineRenderHints } from './utils/lyrics/renderHints';
import type { MediaId, PlaybackAudioQuality, PlaybackSourceRef, ProviderCatalogRef } from './types/onlineMusicCore';

export interface LyricRuby {
  text: string;
  startTime: number; // Seconds
  endTime: number; // Seconds
}

export interface LyricSyllable {
  text: string;
  startTime: number; // Seconds
  endTime: number; // Seconds
  endsWithSpace?: boolean;
  ruby?: LyricRuby[];
  obscene?: boolean;
  emptyBeat?: number;
}

export interface LyricAlternateText {
  role: 'translation' | 'romanization' | string;
  language?: string;
  text: string;
  syllables?: LyricSyllable[];
}

export interface Word {
  text: string;
  startTime: number; // Seconds
  endTime: number; // Seconds
  syllables?: LyricSyllable[];
}

export interface LyricBackgroundVocal {
  text: string;
  startTime: number; // Seconds
  endTime: number; // Seconds
  words: Word[];
  agentId?: string;
  translation?: string;
  romanization?: string;
  alternateTexts?: LyricAlternateText[];
}

export type SubtitleContentMode = 'translation' | 'romanization' | 'none';

export interface LyricAgent {
  id: string;
  name?: string;
  type?: string;
}

export interface Line {
  words: Word[];
  startTime: number;
  endTime: number;
  fullText: string;
  translation?: string;
  id?: string;
  agentId?: string;
  songPart?: string;
  blockIndex?: number;
  romanization?: string;
  alternateTexts?: LyricAlternateText[];
  backgroundVocal?: LyricBackgroundVocal;
  backgroundVocals?: LyricBackgroundVocal[];
  renderHints?: LineRenderHints;
  isChorus?: boolean;
  chorusEffect?: 'bars' | 'circles' | 'beams';
}

export interface LyricData {
  lines: Line[];
  title?: string;
  artist?: string;
  isWordByWord?: boolean;
  ttml?: {
    timingMode?: 'Word' | 'Line';
    agents?: Record<string, LyricAgent>;
  };
}

export interface Theme {
  name: string;
  backgroundColor: string;
  primaryColor: string;
  accentColor: string;
  secondaryColor: string;
  karaokeFillColor?: string;
  fontStyle: 'sans' | 'serif' | 'mono';
  fontFamily?: string;
  fontFamilyStack?: string[];
  fontWeight?: number;
  animationIntensity: 'calm' | 'normal' | 'chaotic';
  wordColors?: { word: string; color: string; }[];
  lyricsIcons?: string[];
  provider?: string;
  description?: string;
}

export type CustomLyricsFontSource = 'system' | 'uploaded';

export interface StoredCustomLyricsFont {
  source: CustomLyricsFontSource;
  family: string;
  label?: string | null;
  fontId?: string;
}

export interface DualTheme {
  light: Theme;
  dark: Theme;
}

export type ThemeMode = 'default' | 'ai' | 'custom';

export type BuiltinVisualizerMode = 'classic' | 'cadenza' | 'partita' | 'fume' | 'tilt' | 'monet' | 'sonnet' | 'gallery' | '3d';
export type VisualizerMode = BuiltinVisualizerMode | (string & {});
export type VisualizerFrameRate = 'off' | 120 | 90 | 60;

export type HomeViewTab = 'playlist' | 'recommendations' | 'local' | 'albums' | 'navidrome' | 'recent';

export type PlaybackMode = 'off' | 'one' | 'all' | 'shuffle';
export type QueueAddBehavior = 'append' | 'next';
export type NowPlayingConnectionStatus = 'disabled' | 'connecting' | 'connected' | 'error';

export interface NowPlayingTrackSnapshot {
  id: string | null;
  title: string;
  artist: string;
  album: string;
  coverUrl: string | null;
  durationMs: number | null;
  isVideo?: boolean;
  isAdvertisement?: boolean;
}

export interface NowPlayingLyricPayload {
  source: string | null;
  title: string;
  artist: string;
  durationMs: number | null;
  hasLyric: boolean;
  hasTranslatedLyric: boolean;
  hasKaraokeLyric: boolean;
  lrc: string | null;
  translatedLyric: string | null;
  karaokeLyric: string | null;
}

export interface ClassicTuning {
  enableWordRotation: boolean;
  breathingFloatMultiplier: number;
  useLegacyLayout?: boolean;
  wordSpacing?: number;
}

export const DEFAULT_CLASSIC_TUNING: ClassicTuning = {
  enableWordRotation: true,
  breathingFloatMultiplier: 1,
  useLegacyLayout: false,
  wordSpacing: 0.7,
};

export interface CadenzaTuning {
  fontScale: number;
  widthRatio: number;
  motionAmount: number;
  glowIntensity: number;
  beamIntensity: number;
}

export const DEFAULT_CADENZA_TUNING: CadenzaTuning = {
  fontScale: 1.12,
  widthRatio: 0.72,
  motionAmount: 1,
  glowIntensity: 1,
  beamIntensity: 0,
};

export interface PartitaTuning {
  showGuideLines: boolean;
  useSemanticLayout: boolean;
  staggerMin: number;
  staggerMax: number;
}

export const DEFAULT_PARTITA_TUNING: PartitaTuning = {
  showGuideLines: true,
  useSemanticLayout: true,
  staggerMin: 20,
  staggerMax: 100,
};

export interface FumeTuning {
  hidePrintSymbols: boolean;
  disableGeometricBackground: boolean;
  backgroundObjectOpacity: number;
  textHoldRatio: number;
  cameraTrackingMode: 'stepped' | 'smooth';
  cameraSpeed: number;
  glowIntensity: number;
  heroScale: number;
}

export const DEFAULT_FUME_TUNING: FumeTuning = {
  hidePrintSymbols: false,
  disableGeometricBackground: false,
  backgroundObjectOpacity: 0.5,
  textHoldRatio: 1,
  cameraTrackingMode: 'smooth',
  cameraSpeed: 1,
  glowIntensity: 1,
  heroScale: 1,
};

export type TiltColorScheme = 'default' | 'swap' | 'accentAll' | 'primaryAll';

export interface TiltTuning {
  splitProbability: number;
  tiltStyleProbability: number;
  colorScheme?: TiltColorScheme;
}

export const DEFAULT_TILT_TUNING: TiltTuning = {
  splitProbability: 0.75,
  tiltStyleProbability: 0.35,
  colorScheme: 'default',
};

export type SonnetOuterFrameMode = 'none' | 'frame' | 'full';

export interface SonnetTuning {
  lyricsFontColor: string;
  karaokeFillColor: string;
  glowColor: string;
  cameraIntensity: number;
  typographyMotion: number;
  mgDensity: number;
  showOnlyText: boolean;
  showGuide: boolean;
  showBackgroundMg: boolean;
  showFixedGeo: boolean;
  showGiantDecorativeText: boolean;
  showBackgroundDecor: boolean;
  enableTransitions: boolean;
  outerFrameMode: SonnetOuterFrameMode;
  textureResolution: number;
}

export const DEFAULT_SONNET_TUNING: SonnetTuning = {
  lyricsFontColor: '#ffffff',
  karaokeFillColor: '#38bdf8',
  glowColor: '#60a5fa',
  cameraIntensity: 1,
  typographyMotion: 1,
  mgDensity: 1,
  showOnlyText: false,
  showGuide: true,
  showBackgroundMg: true,
  showFixedGeo: true,
  showGiantDecorativeText: true,
  showBackgroundDecor: true,
  enableTransitions: true,
  outerFrameMode: 'full',
  textureResolution: 1.5,
};

export type MonetBackgroundSource = 'cover-derived' | 'uploaded-global';
export type MonetBackgroundLayout = 'full-overlay' | 'half-pane-gradient';
export type MonetBackgroundWashColorMode = 'theme' | 'custom';
export type NomandBackgroundSource = 'cover-derived' | 'uploaded-global';
export type NomandBackgroundDitheringType = '2x2' | '4x4' | '8x8';
export type LatentBackgroundDisplayMode = 'dithering' | 'mesh' | 'both';
export type LatentBackgroundColorSource = 'cover-theme' | 'cover-only';
export type MonetAudioStyle = 'bar' | 'line';
export type MonetPortraitSource = 'cover' | 'custom';
export type BuiltinVisualizerBackgroundMode = 'common' | 'monet' | 'nomand' | 'latent' | 'url' | 'sora' | 'wallpaper';
export type VisualizerBackgroundMode = BuiltinVisualizerBackgroundMode | (string & {});

export interface UrlBackgroundItem {
  id: string;
  url: string;
  note: string;
}

export interface MonetBackgroundTuning {
  backgroundSource: MonetBackgroundSource;
  backgroundLayout: MonetBackgroundLayout;
  backgroundBlurPx: number;
  backgroundOverlayOpacity: number;
  backgroundGrayscale: number;
  backgroundSaturation: number;
  backgroundWash: number;
  backgroundHalfPaneOffsetX: number;
  backgroundWashColorMode: MonetBackgroundWashColorMode;
  backgroundWashCustomColor: string;
}

export interface NomandBackgroundTuning {
  imageSource: NomandBackgroundSource;
  ditheringType: NomandBackgroundDitheringType;
  size: number;
  colorSteps: number;
  originalColors: boolean;
  inverted: boolean;
  overlayEnabled: boolean;
  overlayOpacity: number;
}

export interface LatentBackgroundTuning {
  displayMode: LatentBackgroundDisplayMode;
  colorSource: LatentBackgroundColorSource;
  dynamicOnlyInPlayer: boolean;
  enhancedBeatResponse: boolean;
  ditheringSpeed: number;
  ditheringAudioSpeed: number;
  ditheringSize: number;
  ditheringOpacity: number;
  meshSpeed: number;
  meshAudioSpeed: number;
  meshDistortion: number;
  meshSwirl: number;
  overlayEnabled: boolean;
  overlayOpacity: number;
}

export interface MonetTuning {
  keywordColoringEnabled: boolean;
  showDescription: boolean;
  audioStyle: MonetAudioStyle;
  fontScale: number;
  portraitSource: MonetPortraitSource;
  portraitOffsetX?: number;
  portraitStyle?: 'square' | 'rectangular';
  showPortraitDragHanger?: boolean;
}

export const DEFAULT_MONET_BACKGROUND_TUNING: MonetBackgroundTuning = {
  backgroundSource: 'cover-derived',
  backgroundLayout: 'full-overlay',
  backgroundBlurPx: 6,
  backgroundOverlayOpacity: 0.74,
  backgroundGrayscale: 0,
  backgroundSaturation: 1.05,
  backgroundWash: 0.34,
  backgroundHalfPaneOffsetX: 0,
  backgroundWashColorMode: 'theme',
  backgroundWashCustomColor: '#8fb7ff',
};

export const DEFAULT_NOMAND_BACKGROUND_TUNING: NomandBackgroundTuning = {
  imageSource: 'cover-derived',
  ditheringType: '8x8',
  size: 3,
  colorSteps: 4,
  originalColors: false,
  inverted: false,
  overlayEnabled: true,
  overlayOpacity: 0.35,
};

export const DEFAULT_LATENT_BACKGROUND_TUNING: LatentBackgroundTuning = {
  displayMode: 'both',
  colorSource: 'cover-theme',
  dynamicOnlyInPlayer: true,
  enhancedBeatResponse: true,
  ditheringSpeed: 0.1,
  ditheringAudioSpeed: 1.2,
  ditheringSize: 2.5,
  ditheringOpacity: 0.55,
  meshSpeed: 0.3,
  meshAudioSpeed: 2,
  meshDistortion: 0.8,
  meshSwirl: 0.1,
  overlayEnabled: true,
  overlayOpacity: 0.35,
};

export const DEFAULT_MONET_TUNING: MonetTuning = {
  keywordColoringEnabled: true,
  showDescription: true,
  audioStyle: 'bar',
  fontScale: 1.2,
  portraitSource: 'cover',
  portraitOffsetX: 0,
  portraitStyle: 'square',
  showPortraitDragHanger: true,
};

export interface StoredMonetBackgroundImage {
  id: string;
  name: string;
  mimeType: string;
  blob: Blob;
}

export interface MonetBackgroundImage {
  id: string;
  name: string;
  url: string;
}

export type LumenicBackgroundMediaKind = 'image' | 'video';

interface LumenicBackgroundMediaMetadata {
  id: string;
  name: string;
  mimeType: string;
  kind: LumenicBackgroundMediaKind;
}

export type StoredLumenicBackgroundMedia = LumenicBackgroundMediaMetadata & (
  | { blob: Blob; protectedWallpaperId?: never }
  | { protectedWallpaperId: string; blob?: never }
);

export interface LumenicBackgroundMedia {
  id: string;
  name: string;
  mimeType: string;
  kind: LumenicBackgroundMediaKind;
  url: string;
  protectedWallpaperId?: string;
}

export interface StoredMonetPortraitImage {
  id: string;
  name: string;
  mimeType: string;
  blob: Blob;
}

export interface MonetPortraitImage {
  id: string;
  name: string;
  url: string;
}

export enum PlayerState {
  IDLE = 'IDLE',
  PLAYING = 'PLAYING',
  PAUSED = 'PAUSED',
}

export interface StatusMessage {
  type: 'error' | 'success' | 'info';
  text: string;
  nonce?: number;
  durationMs?: number;
  actionLabel?: string;
  onAction?: () => void;
  cancelLabel?: string;
  onCancel?: () => void;
  persistent?: boolean;
}

// Netease / Search API Types

export interface NeteaseUser {
  userId: number;
  nickname: string;
  avatarUrl: string;
  backgroundUrl?: string;
  vipType?: number;
}

export interface NeteasePlaylist {
  id: number;
  name: string;
  coverImgUrl: string;
  trackCount: number;
  playCount: number;
  updateTime: number;
  trackUpdateTime: number;
  creator: NeteaseUser;
  description?: string;
  specialType?: 'cloud';
}

export interface Artist {
  id: MediaId;
  name: string;
  entityId?: string;
  catalogRef?: ProviderCatalogRef;
}

export interface Album {
  id: MediaId;
  name: string;
  coverUrl?: string;
  entityId?: string;
  catalogRef?: ProviderCatalogRef;
}

export interface SongPrivilege {
  id?: number;
  fee?: number;
  payed?: number;
  st?: number;
  pl?: number;
  dl?: number;
  flag?: number;
  cs?: boolean;
}

export interface NoCopyrightRecommendation {
  type?: number;
  typeDesc?: string;
  songId?: string | number;
  thirdPartySong?: unknown | null;
  expInfo?: unknown | null;
}

export type LyricProviderSource = 'netease' | 'qq' | 'kugou' | 'amll';
export type AmllDbPlatform = 'ncm' | 'qq';

export interface ReplayGainInfo {
  /** ReplayGain gain values in decibels. */
  trackGain?: number;
  albumGain?: number;
  /** ReplayGain peak values as positive linear ratios. */
  trackPeak?: number;
  albumPeak?: number;
}

export interface SongResult {
  id: MediaId;
  name: string;
  artists: Artist[];
  album: Album;
  durationMs: number;
  isPureMusic?: boolean;
  aliases?: string[];
  translatedNames?: string[];
  t?: 0 | 1 | 2;
  sourceType?: 'netease' | 'cloud';
  sourceRef?: PlaybackSourceRef;
  fee?: number;
  noCopyrightRcmd?: NoCopyrightRecommendation | null;
  resourceState?: boolean;
  privilege?: SongPrivilege;
  onlineLyricsState?: OnlineLyricsState;
  matchedLyricsSource?: LyricProviderSource;
  matchedLyricsProviderPlatform?: AmllDbPlatform;
  replayGain?: ReplayGainInfo;
  playbackQuality?: PlaybackAudioQuality;
  qqMid?: string;
  kgHash?: string;
  amllDbPlatform?: AmllDbPlatform;
}

export interface OnlineLyricsState {
  lyricsSource: 'online' | 'imported';
  importedLyrics?: LyricData | null;
  importedLyricsName?: string | null;
  hasOnlineOverride?: boolean;
  onlineOverrideLyrics?: LyricData | null;
  matchedSongId?: MediaId;
  matchedIsPureMusic?: boolean;
  matchedLyricsSource?: LyricProviderSource;
  matchedLyricsProviderPlatform?: AmllDbPlatform;
}

export interface SearchResponse {
  result?: {
    songs?: SongResult[];
    songCount?: number;
  };
  code: number;
}

// Local Music Types

export type LocalLyricsPriority = 'local' | 'online';

export interface LocalSong {
  id: string; // UUID for local file
  fileName: string;
  filePath: string; // File path for reference
  fileHandle?: FileSystemFileHandle; // For re-accessing the file (not persisted, stored in memory)
  duration: number; // milliseconds
  fileSize: number; // bytes
  fileLastModified?: number; // milliseconds since epoch
  fileSignature?: string; // Lightweight file identity for incremental scans
  mimeType: string;
  bitrate?: number; // bps
  addedAt: number; // timestamp

  // Canonical metadata and retained source snapshots
  title: string;
  titleOrigin: import('./types/localLibrary').LocalSongTitleOrigin;
  importedMetadata: import('./types/localLibrary').LocalSongImportedMetadata;
  onlineMetadata?: import('./types/localLibrary').LocalSongOnlineMetadata;
  trackNumber?: number;
  discNumber?: number;
  embeddedMetadataVersion?: number;

  embeddedCover?: Blob; // Preferred local cover blob (folder cover or embedded art), stored in IndexedDB
  replayGain?: number; // ReplayGain track gain in dB
  replayGainTrackGain?: number; // ReplayGain track gain in dB
  replayGainTrackPeak?: number; // ReplayGain track peak ratio
  replayGainAlbumGain?: number; // ReplayGain album gain in dB
  replayGainAlbumPeak?: number; // ReplayGain album peak ratio

  // Lyrics matching result
  matchedLyrics?: LyricData;
  matchedIsPureMusic?: boolean;
  matchedLyricsSongId?: number | string; // Provider-scoped ID used for the saved lyric result
  hasManualLyricSelection?: boolean;
  folderName?: string; // Name of the folder if imported via folder import
  noAutoMatch?: boolean; // If true, do not attempt to auto-match metadata
  matchedLyricsSource?: LyricProviderSource;
  matchedLyricsProviderPlatform?: AmllDbPlatform;

  // User preferences for online data override (set via LyricMatchModal)
  lyricsSource?: 'local' | 'embedded' | 'online';  // Explicit lyrics source selection; undefined = the configured automatic priority
  useOnlineCover?: boolean;     // Prefer online cover over embedded cover

  // Local Lyrics (.lrc / .vtt / .ttml / .qrc / .yrc / .krc files)
  hasLocalLyrics?: boolean;
  localLyricsContent?: string;
  localLyricsFormat?: 'vtt' | 'ttml' | 'yrc' | 'qrc' | 'krc';
  hasLocalTranslationLyrics?: boolean;
  localTranslationLyricsContent?: string;

  // Embedded Lyrics (from file tags: ID3 USLT, Vorbis LYRICS, etc.)
  hasEmbeddedLyrics?: boolean;
  embeddedLyricsContent?: string;
  hasEmbeddedTranslationLyrics?: boolean;
  embeddedTranslationLyricsContent?: string;
}

export interface LocalLibrarySnapshotFile {
  name: string;
  relativePath: string;
  kind: 'audio' | 'lyric' | 'translationLyric' | 'cover' | 'other';
  size: number;
  lastModified: number;
  signature: string;
}

export interface LocalLibrarySnapshotNode {
  name: string;
  relativePath: string;
  hash: string;
  files: LocalLibrarySnapshotFile[];
  children: LocalLibrarySnapshotNode[];
}

export interface LocalLibrarySnapshot {
  rootFolderName: string;
  scannedAt: number;
  tree: LocalLibrarySnapshotNode;
}

export interface LocalPlaylist {
  id: string;
  name: string;
  songIds: string[];
  createdAt: number;
  updatedAt: number;
  isFavorite?: boolean;
}

export type LocalLibraryGroupType = 'folder' | 'album' | 'artist' | 'playlist';

export interface LocalLibraryGroup {
  type: LocalLibraryGroupType;
  name: string;
  songs: LocalSong[];
  coverUrl?: string | Blob;
  id: string;
  isVirtual?: boolean;
  trackCount?: number;
  description?: string;
  albumId?: number;
  entityId?: string;
  playlistId?: string;
}

export type {
  LocalLibraryAssignment,
  LocalLibraryAssignmentOrigin,
  LocalLibraryEntity,
  LocalLibraryEntityKind,
  LocalSongImportedMetadata,
  LocalSongMetadataSource,
  LocalSongOnlineMetadata,
  LocalSongReference,
  LocalSongTitleOrigin,
} from './types/localLibrary';

// Extend SongResult to support local files and Navidrome files
export interface UnifiedSong extends SongResult {
  sourceRef: PlaybackSourceRef;
  isLocal?: boolean;
  localRef?: import('./types/localLibrary').LocalSongReference;
  isNavidrome?: boolean;
  navidromeData?: any;
}

export type ReplayGainMode = 'off' | 'track' | 'album';

// Audio Analysis Types
import { MotionValue } from 'framer-motion';

export interface AudioBands {
    bass: MotionValue<number>;    // 20-150Hz (Circles)
    lowMid: MotionValue<number>;  // 150-400Hz (Squares)
    mid: MotionValue<number>;     // 400-1200Hz (Triangles)
    vocal: MotionValue<number>;   // 1000-3500Hz (Icons)
    treble: MotionValue<number>;  // 3500Hz+ (Crosses)
    spectrum?: MotionValue<Uint8Array<ArrayBuffer>>; // Raw analyser FFT magnitude bins for full-spectrum visualizers
  }
