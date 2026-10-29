# Music Effects

This leaf owns only disposable visual effects. `../music-effects.js` exports
`mountMusicEffects(host, {onError} = {})`, returning `{update(state), dispose()}`.
`update` is awaitable. There are no native, account, preferences, playback-clock,
application-store or background-decoder dependencies.

State:

```js
{
  mode: 'none', // none | rain-window | snow | star-river
  visualizer: 'classic', // all nine visualizers; 3d delegates star-river
  visible: true,
  scene: {canvas, media, mediaSource}, // or null; owned by the background compositor
  current: {artwork}, // or null
}
```

The caller owns host size and stacking. `host.dataset.effectMode` is the actual
independent selection (`none` when hidden or star-river is delegated to 3d).
`host.dataset.effectState` is `idle`, `loading`, `ready`, `error`, or `disposed`.
Ready means mounted, not necessarily that the first animation frame has drawn.
Snow's canvas uses `mix-blend-mode: screen`; canvases are pointer-transparent.
Effects sit above the shared background but below lyric content and playback
controls. Historical VisualizerRenderer's rain sibling has z-index 5, while
the lyric content escapes the non-stacking Shell at z-index 10. DOM order alone
does not mean rain is above lyrics. Gallery's opaque cover backdrop, when used,
also stays above rain; do not invert the layers just to expose the effect.

Only visible effects import runtime leaves. Rain additionally waits for a nonzero
shared canvas and the original two PNG sprites; it never fetches scene media.
Hiding, including document visibility, aborts asset loads, cancels RAF, disconnects
observers and loses GL contexts. Restoration creates a new runtime. Ordinary
playback pause is intentionally ignored, matching historical `staticMode=false`.

Failures report `onError`, release owned resources and latch across ordinary
native ticks. A selected mode change, hide/restore, or actual scene change
(canvas identity/size, media source identity, media URL/kind) permits retry.
Fresh wrapper objects and artwork/playback changes do not clear the error latch.
The optional `load(mode, host, {signal})` injection exists for lifecycle tests;
it resolves a synchronous mount function returning `{update, dispose}`.

## Provenance

Do not edit `reference/*.js` or other generated reference files. Regenerate with
`tools/extract-music-effects.mjs <read-only upstream>`, and append `--check` for a
byte-for-byte reproduction check. It uses existing upstream esbuild 0.25.12;
there is no installation step. The manifest records source/output SHA-256 hashes,
adaptations, and the reused Three bundle's hash. Original source snapshots and
Folia/Mineradio/Three/esbuild licenses accompany the generated leaves.

Snow and StarRiverCanvas setup/frame loops are extracted from the historical
components; particle parameters and shaders are retained. The two build-time
extractors share only the cancellable cover-loading patch: pending images lose
their listeners and source on replacement/disposal, while successful textures
keep Three 0.128.0 loader parameters. Rain physics,
shaders and actual overlay defaults are retained. Extraction removes React and
app compositor bridges, adds error/cleanup boundaries, isolates rain sprite state
per runtime and adds Canvas to the existing live-background refresh branch.
All changes are expressed as counted substitutions that fail on source drift.

## Verification And Limits

From `native-rebuild/ui`, run `node --test tests/music-effects*.test.js`.
For real installed Edge coverage, set `LUME_EFFECTS_BROWSER_TESTS=1` first.
The browser test serves only the standalone effect leaves and reused Three,
then closes its server/browser. Screenshots and pixel metrics go to
`build-artifacts/native-rebuild/music-effects`, never the shipped UI subtree.

Known differences: hiding restarts effect time and rain accumulation; rain samples
the shared compositor's already-cropped/faded Canvas instead of decoding the
original media again. Historical frame-publication/global top-inset bridges are
not included; the caller owns geometry/layers. This is not same-scene historical
visual sign-off, full T04 completion, or verification of the main 3d shared effect,
settings, preferences, or native packaging.
