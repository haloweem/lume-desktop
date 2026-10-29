# Player Material Integration

This is the historical FloatingPlayerControls WebGL2 material, not native desktop
glass and not CSS backdrop blur. The material covers the whole player bar.

```js
import {
  mountMusicMaterial,
  registerMusicMaterialSceneSource,
} from './music-material.js';

const material = mountMusicMaterial(playerBar, {
  root: musicRoot,
  getScene: () => backgroundRenderer?.getScene() ?? null,
  visible: nativeVisible && pageVisible,
  onError: error => report(error),
  onIssue: issues => reportSceneIssues(issues),
});
material.update({ visible: nativeVisible && pageVisible });
// Before replacing the footer, navigation teardown, or leaving music:
material.dispose();
```

- `surface` is the whole `.MusicPlayerBar`, never its individual buttons.
- `root` is the same-document element containing stage iframe and parent effects.
  It defaults to the closest `[data-music-root]`, then documentElement.
- `getScene()` is the existing `{canvas, media, mediaSource}` background descriptor.
  Only its composited `canvas` is borrowed. No media is decoded, played or stopped.
- `update({getScene, root, visible})` accepts any subset; `update()` requests a frame.
- Dispose/remount when the surface element is replaced. Moving just the owned
  canvas is unsupported. A second mount on the same surface disposes its predecessor.
- The mount returns `canvas`, `snapshot()`, `update()` and idempotent `dispose()`.
- `snapshot().state`: initializing, active, hidden, context-lost, fallback, disposed.
  `snapshot().scene.issues` reports incomplete capture. `active` means the material
  rendered, not that every possible scene source was available.
- Owned selector: `canvas[data-music-material]`. State attributes on that canvas:
  `data-music-material-state` and `data-music-material-scene-complete`.
- Hiding immediately cancels material RAF and releases GPU/scene buffers. Showing
  rebuilds them. This deliberately omits the React wrapper's 1500ms hidden cache.
- Context restoration recreates the historical renderer. No CSS approximation is
  installed on failure; the caller may retain its explicitly labeled fallback.
- Active chrome is gated on shader compilation/linking and an error-free render.
  Context loss and WebGL draw/upload errors keep or restore the parent's fallback.

## Parent CSS

Keep original whole-bar geometry: bottom 32px, centered, width
`clamp(980px,68vw,1240px)` bounded by viewport minus 32px, height
`clamp(104px,6.2vw,112px)`, pill border radius. The host is positioned, with
transparent background, border zero, box-shadow none, backdrop-filter none,
overflow visible. Put progress/content above z-index 0. The material canvas owns
only its absolute/inset-zero sizing, inherited radius and pointer-events none.
Do not change the panels' original 24px blur.

```css
.MusicPlayerBar:has(> canvas[data-music-material-state="active"]) {
  background: transparent;
  border: 0;
  box-shadow: none;
  backdrop-filter: none;
  -webkit-backdrop-filter: none;
}
```

The fallback styles return when the state changes away from active. A transparent
1px border still insets the canvas; the historical host uses border zero.

## Scene Boundary And Fidelity

The shaders, optical preset, model and renderer are extracted unmodified. They
retain the original viewport optical reference, 96px overscan and DPR cap 1.5.
One owned material canvas/renderer samples a bar-sized scene region. The existing
full-window wallpaper canvas remains its single media source.

Same-origin renderer iframe documents (including allow-scripts/allow-same-origin
sandbox frames) are traversed directly; no new message protocol is necessary.
Coordinates account for iframe offset, border and positive axis-aligned scale.
The historical text, opacity-group and overflow painters are reused with
ownerDocument-based style lookup. Iframe opacity is applied as one group.
The extractor explicitly copies DOMRect clip coordinates, fixing the historical
spread of non-enumerable native rectangle properties that silently dropped text.
Parent effects, Fume/Sonnet lyric canvases and Gallery markers are selected.

This retains the historical scene capture's limitations: it is not a browser
screenshot, does not rasterize arbitrary images, CSS masks, gradients, filters,
text shadows or rotated text. Gallery artwork canvases can be copied, but their
decorative CSS is not reconstructed. Cross-origin frames and rotated/skewed
iframe ancestors are explicitly reported, not silently claimed as captured.

The current 3D adapter uses a volatile WebGL backbuffer. The producer owner must
register this synchronous hook while its runtime exists:

```js
// Producer imports only this WeakMap boundary, never the material/shader bundle.
import {registerMusicMaterialSceneSource} from './music-material/registry.js';
const unregister = registerMusicMaterialSceneSource(runtime.getCanvas(), {
  refresh: () => runtime.refreshSceneFrame(),
});
// Producer teardown:
unregister();
runtime.destroy();
```

The refresh must be synchronous and must not call material.update/render
recursively. It draws the existing scene, not advances playback. Material copies
the backbuffer immediately afterward. Without this hook, it deliberately omits
volatile 3D pixels and reports `3d-snapshot-provider-required`.

The seven current iframe modes and current rain/snow/star-river canvases retain
their pixels and need no producer modification. Visible scene leaves are polled
because their extracted publishing bridges are disabled. Hidden/disposed
material never keeps a RAF running.

## Extraction And Staging

Run from native-rebuild/ui with the existing historical esbuild/Babel packages:

```powershell
node tools/extract-music-material.mjs H:/refactor/history/lume1.0.1/third_party/folia-major/upstream
node tools/extract-music-material.mjs H:/refactor/history/lume1.0.1/third_party/folia-major/upstream --check
$env:LUME_UI_BROWSER_TESTS='1'
node --test tests/music-material.test.js tests/music-material-browser.test.js
```

The source hashes, generated hash and adaptations are in reference/manifest.json;
the original AGPL license and source are under reference/licenses and
reference/source. No React, Three, account, playback or media-registry runtime
is imported by this material bundle.

Parent owns CMake changes: stage music-material.js, music-material/scene.js,
music-material/registry.js,
music-material/reference/material.js and attribution/manifest files; exclude
source, tools, tests and verification evidence. Extend stale-runtime cleanup
consistently with existing extracted music directories.

## Verified Browser Boundaries

The opt-in suite uses local fixture data and blocks non-local requests. It checks
all nine real renderer modules and three parent effect modes, text-only native
DOMRect capture, iframe border/scale/opacity clipping, desktop/mobile material
pixels, context loss/restoration, repeated mounting, and hidden/disposed RAF
cleanup. The production mountMusicView scenario uses the default lazy import and
real background/visualizer owners with no material factory substitution. It
checks 3D producer registration, Gallery/reference switching, retained footer and
WebGL context across panel opening, song structural updates, active chrome
stacking, and buffer release. Evidence is under the repository's
build-artifacts/native-rebuild/music-material-verification/ directory.
