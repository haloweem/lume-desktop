# Gallery renderer

`gallery.js` exports `mountGallery(host, state) => {update(state), dispose()}`.
The caller loads `gallery.css` and provides an absolute, full-screen `inset: 0`
host. The stage reserves `bottom: 176px` for the caller's footer at bottom 32px,
height 104-112px. There is no controller, mode registry, footer, or native bridge
inside this renderer.

State uses seconds, including word times:

```js
{
  lines: [{startTime, endTime, fullText, words: [{text, startTime, endTime}], translation}],
  activeLineIndex, currentTime, playing, visible,
  current: {title, artist, artwork}
}
```

The controller owns the playback clock and calls `update` with current seconds.
Gallery schedules RAF only for unfinished entry/exit/position/scale transitions.
Equivalent cloned lyric snapshots retain rows, measurement results and manual
scroll position. Lines should be treated as immutable. `visible: false` or a
hidden document suspends the disc, every owned RAF and the browsing reset timer.
Resume continues the remaining reset interval and paused spring time. Dispose is
idempotent and removes listeners, ResizeObserver, timers, RAF, animation state,
DOM, per-mount Pretext/Monet caches and canvas measurement contexts.

## Actual Reference

Read-only reference: `H:/refactor/history/lume1.0.1/third_party/folia-major/upstream`.

- `src/components/visualizer/gallery/VisualizerGallery.tsx` imports
  **`../monet/MonetLyricsRail.tsx`**, not `GalleryLyricsRail.tsx`.
- `MonetLyricsRail.tsx` supplies the tone, layout, collision insets, active gap
  18px/inactive gap 14px, 46% focus, glow and feathered karaoke mask, 11%/88%
  viewport mask, entry +/-34px, exit +/-38px, and discrete browsing behavior.
- `monetLyricsModel.ts`, `utils/lyrics/graphemeTiming.ts`, and
  `utils/lyrics/renderHints.ts` are compiled into `gallery-vendor/monet-core.js`.
  Pretext's original line-breaking and canvas measurement code is included.
- `visualizer/runtime.ts` supplies intro/gap/outro anchor selection semantics.
  `galleryLyricsModel.ts` is consulted only for the artwork/background
  precedence. Its unrelated lyric renderer is not shipped.
- `utils/fontStacks.ts`, `stores/useSettingsUiStore.ts`, and
  `app/presentation/buildVisualizerTheme.ts` establish the default sans stack,
  white lyric/karaoke color and 600 lyric weight.
- `services/lumenicBackgroundMedia.ts` and
  `backgrounds/wallpaper/WallpaperMedia.tsx` establish the custom default image.

The fixed custom media takes precedence over track artwork exactly as in the old
default Gallery. Backdrop, picture sleeve and record use the same relative
`assets/music/forest.webp` URL. The asset is supplied by the main task's
`tools/extract-music-reference.mjs`; this renderer never reads shell wallpaper.
The original image SHA-256 is
`d99625b4717b80a35a2cc9dab51f2460ce44bed500baee65e5961152379fbfc3`.

## Preserved Behavior And Adaptations

- Automatic window: 3 before, 4 after, at most 8 rows. Manual: 4 before, 4 after,
  at most 9 rows. One retiring window is retained, so total DOM stays <=18 rows
  even across repeated large seeks. No 2000-line DOM list.
- Wheel threshold 72px, touch threshold 52px, direction reversal clears the
  accumulator, at most one row per event, 1800ms idle reset. Non-pixel wheel units
  are converted to pixels. Lyrics do not seek audio.
- Native Motion generator: y `stiffness:142, damping:28, mass:0.82`; scale
  `150/30/0.78`. Interrupted springs retain analytic velocity. Opacity 280ms,
  blur 320ms, exit 200ms use the original `[.32,.72,0,1]` cubic Bezier.
- Record rotates every 24s and pauses with playback. Reduced motion disables
  record rotation and snaps rail transitions.
- Translation and keyword coloring are off, matching the actual Gallery entry.
  No other lyric mode or theme registry is imported. Empty lyrics explicitly
  show `暂无歌词` instead of the original silent blank.
- Original sans fallback families are retained; no online font fetch is added.
  Existing system fonts determine final glyph rasterization. Font sizes use the
  old clamp's pixel values at 768/1024/1280/1440/1600 breakpoints instead of vw
  scaling. Small-height layouts reduce title size, constrain sleeve/disc to the
  artwork track, and maintain the footer clearance.
- Overwide timed tokens split at grapheme/word boundaries using their existing
  per-grapheme timestamps so mobile wrapping cannot repeat one horizontal mask
  on multiple lines. Long text remains limited to 3 active / 2 inactive lines.
- Measurement uses the original Pretext model, with the original approximate
  canvas-unavailable fallback. Canvas fallback uses grapheme-safe width estimates.
  Font-loading and ResizeObserver changes invalidate layouts and fill offsets.

## Reproduction And Licenses

Run from repository root, with the existing reference dependencies installed:

```powershell
node native-rebuild/ui/tools/extract-music-gallery.mjs H:/refactor/history/lume1.0.1/third_party/folia-major/upstream
node native-rebuild/ui/tools/extract-music-gallery.mjs H:/refactor/history/lume1.0.1/third_party/folia-major/upstream --check
$env:LUME_JSDOM_PATH='H:/refactor/history/lume1.0.1/src/vscode-windhawk-ui/node_modules/jsdom/lib/api.js'
node --test native-rebuild/ui/tests/music-gallery*.js
```

`--check` is read-only and compares every generated artifact byte for byte.
`gallery-vendor/manifest.json` records relative source paths, SHA-256 hashes,
generated output hashes and versions. No absolute machine paths are embedded in
runtime outputs. Extraction modifies only `gallery-vendor/` and never the source
checkout. Build tool: Rolldown 1.1.5. Runtime dependencies:

| Dependency | Version | License |
| --- | --- | --- |
| Folia model and derived Gallery UI | archived reference | AGPL-3.0; `Folia-AGPL-3.0.txt` |
| @chenglou/pretext | 0.0.8 | MIT; `pretext-LICENSE.txt` |
| motion-dom | 12.42.2 | MIT; `motion-dom-LICENSE.txt` |
| motion-utils | 12.39.0 | MIT; `motion-utils-LICENSE.txt` |

The 140,199 bytes of generated JS are unminified browser ES modules; no React,
React DOM, Node, network package lookup, JSX or bundler is needed at runtime.
The extraction wraps Monet/Pretext in a per-mount factory, removes unused keyword
color re-exports and adds explicit cache/context cleanup. Motion is limited to
the native spring and cubicBezier generators, with no global scheduler.

## Verification Boundary

Node/jsdom tests cover windowing, gap anchors, gestures/reset, collision math,
grapheme timing and splitting, spring velocity, snapshot retention, pause/hide,
cleanup, cache isolation, resize, empty state, and generated hashes. Canvas metrics
in DOM tests are deterministic substitutes, not a visual-fidelity claim.
Real browser screenshots, font rendering, controller registration, CSS inclusion
and CMake staging belong to the main task. No full build or git commit is done by
this subtask.
