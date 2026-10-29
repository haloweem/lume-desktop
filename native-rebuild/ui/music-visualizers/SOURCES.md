# Mineradio 3D Runtime

`three-d.js` is the native UI adapter. It imports static, local browser ESM only.
No React, Electron, Node runtime, installer, network import, effect selector, or
CSS imitation of the 3D scene is used. This document covers the 3D renderer;
registration and other lyric renderers belong to the parent integration task.

## Interface

```js
import {mountThreeD} from './music-visualizers/three-d.js';
const view = mountThreeD(host, state);
view.update(state);
view.dispose();
```

`state` contains `lines`, `activeLineIndex`, `currentTime` in seconds, `playing`,
`visible`, and optional `current: {title, artist, artwork}`. Each lyric line has
`fullText`, `startTime`, `endTime`, `words: [{text, startTime, endTime}]`, and an
optional `translation`. Replace the `lines` array when text or timings change;
reuse it for playback updates. The caller owns time progression and active-line
selection. The adapter does not run its own playback timer. After disposal,
`update` and repeated `dispose` calls do nothing.

The host must have a nonzero layout size. The canvas is transparent and occupies
the host; the parent owns background, track metadata and overlays. `current`
metadata is accepted but unused by the default lyric scene. The original
`DEFAULT_VISUALIZER_EFFECT_MODE` is `none`; its optional Star River dependency is
preserved in the vendored source and runtime but never instantiated by this
adapter. No effect option is exposed.

`theme` is the original `DEFAULT_THEME` exported by `src/services/baseThemes.ts`:
Midnight Default, background `#09090b`, primary/accent `#f4f4f5`, secondary
`#71717a`, sans, normal animation intensity. The original renderer falls back to
`system-ui, sans-serif` and karaoke fill `#ffffff`. `showTranslation` is always
true. No new font asset or replacement visual parameter is introduced.

## Source And Licenses

Read-only reference: `H:/refactor/history/lume1.0.1/third_party/folia-major/upstream`.

- Folia Major: <https://github.com/chthollyphile/folia-major>, recorded reviewed
  revision `fa057895f916d0c2482785a150dce51ad6fc928c`, AGPL-3.0. The historical
  checkout includes Lume modifications; exact input hashes, rather than the
  upstream revision alone, identify this migration's source.
- Original stage: `src/components/visualizer/mineradio3d/stageRuntime.ts`.
  Its source SHA-256 is
  `3ad496d3a46769c20d12822a855b3fc54cb63ab99a68039431a686b7b4076c2a`.
  The complete file and all direct runtime dependencies are retained unchanged
  under `vendor/source/`, along with the original React wrapper as reference only.
- Mineradio: <https://github.com/XxHuberrr/Mineradio>, GPL-3.0,
  revision `328a087a14e79d5267a3de54d29108c34ed6cf60`. The retained Star River
  geometry and shaders derive from
  `public/js/modules/02-visual/00-pointer-cover-particles.js`, preset 5.
- Three.js: <https://github.com/mrdoob/three.js>, version **0.128.0**, MIT.
  The original `build/three.module.js` is retained verbatim. The shipped module
  is built with esbuild and exports the symbols used by the original runtimes.
- esbuild: <https://github.com/evanw/esbuild>, version **0.25.12**, MIT.
  Used only at build time; its license is included for provenance.

`vendor/licenses/` retains the historical Folia notice, full AGPL-3.0 and GPL-3.0
texts, and Three/esbuild MIT notices. The GPL text is copied from the historical
Lume root license. Original authorship/license comments in retained sources are
unchanged. `vendor/manifest.json` records every original input hash and every
generated source, runtime and license hash. `vendor/.gitattributes` disables
newline conversion so these byte hashes survive Git checkout.

## Native Lifecycle Changes

The extractor applies exact, counted substitutions and saves the reviewable
result at `vendor/source/stageRuntime.native.ts`. It fails if a patch no longer
matches. Lume native rebuild modifications made on 2026-10-02:

- Gate scheduling, rendering, resize and interaction on visibility. Hiding
  cancels RAF and releases pointer capture. Showing resizes, resets elapsed frame
  time and resumes using the latest state. Document visibility also gates work.
- Preserve original drag rotation, inertia, wheel reversal, middle-button pan,
  double-click reset, camera, translation layout, text/glow textures and karaoke.
  Handle lost pointer capture without a stuck drag.
- Dispose the original textures/materials/geometries, disconnect the observer,
  remove input/document listeners, dispose the renderer, explicitly lose its
  WebGL context, release the canvas pixel store and drop retained lyric arrays.
  A failure after renderer creation also invokes cleanup before rethrowing.
- Finish a deferred karaoke repaint when a paused seek occurs within the
  original 30Hz texture interval; the interval and visual math are unchanged.

Importing the module creates no canvas or WebGL context. Only `mountThreeD`
creates one renderer; visibility changes never create another renderer. A
hidden mount creates the renderer but no lyric meshes or RAF. Hidden updates
retain the latest state without uploading/rebuilding textures. An already
visible scene keeps its bounded current GPU resources while hidden, so camera
pose and interactions survive resuming. Call `dispose` when leaving 3D.

The original bounds are retained: 11 cached lyric rows, a 7-row animation
corridor, 5 primary cinema slots, texture scale 1.5, glow scale 0.5, DPR capped at
1.5, perspective FOV 44, radius 16, radius limits 8..28. No texture dimensions,
antialiasing, blur passes, camera parameters or opacity values were reduced.
Canvas font rasterization remains OS/browser dependent, as in the original.

## Reproduction And Verification

From the repository root, using the existing historical dependency read-only:

```powershell
node native-rebuild/ui/tools/extract-music-3d.mjs H:/refactor/history/lume1.0.1/third_party/folia-major/upstream
node native-rebuild/ui/tools/extract-music-3d.mjs H:/refactor/history/lume1.0.1/third_party/folia-major/upstream --check
```

The tool auto-discovers esbuild 0.25.12 in that reference's direct or pnpm
dependencies. It runs no package manager or installer. To rebuild solely from
the included source snapshot, point to any existing esbuild 0.25.12 package:

```powershell
$env:LUME_ESBUILD_PATH='H:/refactor/history/lume1.0.1/third_party/folia-major/upstream/node_modules/.pnpm/esbuild@0.25.12/node_modules/esbuild'
node native-rebuild/ui/tools/extract-music-3d.mjs --check
```

Omitting `--check` writes only this renderer's generated `vendor/` files.
`--check` compares all generated bytes without writing. The tool fixes the
original TypeScript class-field semantics explicitly, avoiding ambient tsconfig
differences between the reference checkout and saved source snapshot.

```powershell
node --test native-rebuild/ui/tests/music-3d*.test.js
$env:LUME_3D_BROWSER_TESTS='1'
node --test native-rebuild/ui/tests/music-3d-browser.test.js
```

Unit tests require Node 24 for its module hooks. They retain real Three scene,
geometry and texture objects and replace only the GPU/DOM boundary. They cover
hidden scheduling, document visibility, pointer capture, resource disposal,
failed mount, repeated mounts, window eviction/seeks, paused repaint and the
original math/timing values. Provenance tests check the local static import
graph and byte hashes.

The optional browser test uses an existing Playwright installation (override
`LUME_PLAYWRIGHT_DIR`) and Microsoft Edge, serves only the 3D fixture on an
ephemeral loopback port, then closes browser and server. It verifies nonblank
canvas pixels, center framing, drag motion and actual WebGL context loss at
1440x900 and 390x844. Screenshots are under
`build-artifacts/native-rebuild/music-visual-20261002/three-d-runtime`; these are
test evidence, not runtime assets. No installed Lume application is started.

Standalone WebGL verification does not replace the parent task's integrated
page/WebView2 screenshots. The adapter deliberately exposes no theme/effect
selector and does not port the React visualizer shell or liquid-glass snapshot
bridge. Long-line sizing and five-slot viewport clipping remain the authored
runtime behavior.
