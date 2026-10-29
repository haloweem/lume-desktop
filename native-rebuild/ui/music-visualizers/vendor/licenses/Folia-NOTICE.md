# Folia Major Notice

Lume's Lumenic music module is planned to incorporate selected source files
from Folia Major.

- Upstream project: https://github.com/chthollyphile/folia-major
- Upstream revision reviewed: `fa057895f916d0c2482785a150dce51ad6fc928c`
- Upstream license: GNU Affero General Public License, version 3 (AGPL-3.0)
- Copyright: Folia Major contributors

Any copied or modified Folia Major source file must retain its copyright and
license notices and identify Lume's modifications. The distributed combined
work must provide its complete corresponding source under the applicable GPL
v3 and AGPL v3 terms, include this notice and the AGPL v3 text, and expose an
appropriate legal notice in Lumenic.

Folia Major's names, logos, artwork, music, cover art, account credentials,
and third-party service configuration are not included by this notice.

## Mineradio Star River

The star river particle shaders and geometry in
`upstream/src/components/visualizer/effects/StarRiverParticles.ts` and
`starRiverPointShaders.ts` are adapted from XxHuberrr/Mineradio, GPL-3.0:
https://github.com/XxHuberrr/Mineradio
Revision: `328a087a14e79d5267a3de54d29108c34ed6cf60`.
Source: `public/js/modules/02-visual/00-pointer-cover-particles.js`, preset 5.
Lume modifications isolate the preset, adapt scene units, connect Lume's cover
texture, and share the 3D lyric scene's camera and interaction. Particle flow
is independent of playback; audio-driven enlargement and brightening are disabled.
