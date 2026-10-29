// src/types.ts
var DEFAULT_CLASSIC_TUNING = {
  enableWordRotation: true,
  breathingFloatMultiplier: 1,
  useLegacyLayout: false,
  wordSpacing: 0.7
};
var DEFAULT_CADENZA_TUNING = {
  fontScale: 1.12,
  widthRatio: 0.72,
  motionAmount: 1,
  glowIntensity: 1,
  beamIntensity: 0
};
var DEFAULT_PARTITA_TUNING = {
  showGuideLines: true,
  useSemanticLayout: true,
  staggerMin: 20,
  staggerMax: 100
};
var DEFAULT_FUME_TUNING = {
  hidePrintSymbols: false,
  disableGeometricBackground: false,
  backgroundObjectOpacity: 0.5,
  textHoldRatio: 1,
  cameraTrackingMode: "smooth",
  cameraSpeed: 1,
  glowIntensity: 1,
  heroScale: 1
};
var DEFAULT_TILT_TUNING = {
  splitProbability: 0.75,
  tiltStyleProbability: 0.35,
  colorScheme: "default"
};
var DEFAULT_SONNET_TUNING = {
  lyricsFontColor: "#ffffff",
  karaokeFillColor: "#38bdf8",
  glowColor: "#60a5fa",
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
  outerFrameMode: "full",
  textureResolution: 1.5
};
var DEFAULT_MONET_TUNING = {
  keywordColoringEnabled: true,
  showDescription: true,
  audioStyle: "bar",
  fontScale: 1.2,
  portraitSource: "cover",
  portraitOffsetX: 0,
  portraitStyle: "square",
  showPortraitDragHanger: true
};

export {
  DEFAULT_CLASSIC_TUNING,
  DEFAULT_CADENZA_TUNING,
  DEFAULT_PARTITA_TUNING,
  DEFAULT_FUME_TUNING,
  DEFAULT_TILT_TUNING,
  DEFAULT_SONNET_TUNING,
  DEFAULT_MONET_TUNING
};
