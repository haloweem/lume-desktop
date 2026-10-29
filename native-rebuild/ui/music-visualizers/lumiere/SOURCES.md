# 绘光 / Lumiere

Original implementation: chthollyphile/folia-major, commit
7701347918a4177c1c57b538d7556db9606be67a. Copyright (c) 2026 chthollyphile.
Folia code is distributed under AGPL-3.0; see licenses/Folia-AGPL-3.0.txt.

The original Lumiere program compiler, light rigs, typography, shaders and Pixi
runtime are bundled without rewriting their visual design. Lume's adapter
supplies native playback time, lyrics, spectrum, metadata and font
preferences directly. Colors always use Folia DEFAULT_THEME; generic font and
karaoke color preferences do not override Lumiere. The Folia React settings panel and application services
are not included. Lumiere-specific tuning uses upstream defaults. Untimed LRC
lines use Folia's original weighted word/CJK/punctuation timing and render hints;
authored word times stay unchanged. The subtitle overlay is adapted to DOM with
the original content selection, font sizes, fonts, colors, glow, blurred two-line
lookahead and final-credit suppression. Its bottom clearance follows Lume's bar;
its position uses a CSS ease-out transition instead of Folia's React spring.
Song handover uses the original commit/signature/instrumental decisions, driven
by native snapshots and a 3-second deadline instead of a second React RAF loop.
Hidden views destroy their runtime; late initialization is cancelled. The
canvas is registered with Lume's liquid-glass scene capture.

The bundle imports Pixi's `pixi.js/unsafe-eval` compatibility module. This
replaces dynamically generated uniform upload functions with static functions,
so the production `script-src 'self'` policy stays unchanged (no eval allowed).

Lume's extraction also disables Pixi autoDensity CSS sizing: the canvas retains
the upstream 100% width/height while its drawing buffer follows host size and
DPR. A public resize hook is called on window resize to handle WebView zoom
changes even when the host's CSS dimensions remain unchanged. The original
source stays verbatim; these two adaptations are reproducible in the extractor.

The exact imported sources are retained in source/. manifest.json records
source and bundle hashes and dependency versions. Runtime dependencies and their
license notices are in licenses/. @pixi/colord's npm distribution omits its MIT
text; that notice is preserved from Lume's existing reference dependency copy.

Rebuild with tools/extract-lumiere.mjs, the pinned upstream checkout and the
dependency prefix used by the manifest. Outputs stay inside this directory.

## Detail comparison (2026-10-14)

| Area | Upstream source | Lume integration |
| --- | --- | --- |
| Palette | services/baseThemes.ts, lumiere/scene.ts | Exact default theme and original palette resolver; no generic karaoke override |
| Light, fog, bloom, dust, trails, line art | lumiere/rigs, light, lineart, types.ts | Original runtime and DEFAULT_LUMIERE_TUNING, unchanged |
| Word timing and short-line tails | lyrics/parserCore.ts, renderHints.ts | Extracted original buildTimedWords; original annotateLyricLines; authored times preserved |
| Camera, scene transitions, opening, credits | lumiereProgram.ts, lumiereUnit.ts, credits.ts | Original compiler and runtime; actual Lume track duration supplied |
| Translation / upcoming lyrics | VisualizerSubtitleOverlay.tsx, subtitleFontSizes.ts, runtime.ts | Original selection helpers; DOM rendering, two blurred upcoming lines, glow, line-change fade and no final translation over credits |
| Song changes and late lyrics | songHandover.ts, VisualizerLumiere.tsx | Original decision helpers, 2-second playback confirmation / 3-second fallback, old scene retained until new words arrive |
| Background, controls, immersive layout | VisualizerShell.tsx | Lume owns these; subtitles clear the actual Lume bar and move down when it hides |
| Custom theme keyword colors/icons | Theme configuration | Upstream default theme has no custom keyword/icon list; no invented replacements |

This is a comparison against the pinned commit above, not a claim that Folia's
entire settings UI or every possible custom theme has been ported.
