# Lume music runtime sources

The four enabled platforms are NetEase Cloud Music, QQ Music, KuGou and Qishui.
Apple Music, Spotify, YouTube Music and Bodian are not included in the shipped
provider bundle or the Electron backend closure.

- Folia: `https://github.com/chthollyphile/folia-major`, tag `v0.7.28`, commit
  `a44758bbc02929225ca7304b5a4c4580d7b4e87b`, AGPL-3.0. The provider bundle is
  built from the vendored TypeScript providers. Backend files are copied without
  changing their authentication protocols. `folia/LICENSE` is retained.
- Node.js: version `24.15.0`, Windows x64 `node.exe`, downloaded or independently
  matched against `https://nodejs.org/dist/v24.15.0/SHASUMS256.txt`. Expected SHA-256:
  `3331e1ffe19874215472217c5e94f5a0c6d8e18c4ac7111d3937aa0ad5e9b4a5`.
  The unchanged official executable is distributed as `LumeMusicProviderHost.exe`;
  Node's complete bundled license is retained as `licenses/NODE-LICENSE.txt`.
- NetEase API: `@neteasecloudmusicapienhanced/api` `4.41.1` (MIT).
- QQ API: `@yakult-green-tea/qq-music-api` `3.3.6`; its package LICENSE and
  ATTRIBUTION are retained.
- KuGou API: `MakcRe/KuGouMusicApi` tag `v1.6.0`, installed from the exact GitHub
  tag archive in `package-lock.json`; its package license is retained.
- Qishui API: `qishui-music-api` `0.2.2`; its package license is retained.
- Koffi: `3.3.1`, including only the Windows x64 optional native package selected
  by npm on the build host. Used for Windows DPAPI credential encryption.

`package-lock.json` pins the complete npm dependency graph and archive integrity.
The production stage is installed with `npm ci --omit=dev --ignore-scripts`; all
package license and notice files remain in `node_modules`. The generated
`runtime-inventory.json` lists every shipped file's SHA-256 and every installed
package's version/license metadata. It excludes tests, provider TypeScript
sources, build tools and unused Folia application modules from the runtime stage.

The 2026-11-18 Lume patch adds `lumeMembership.ts` to the provider normalization
boundary (QQ, NetEase, KuGou). It retains song membership display metadata in
private providerData; upstream authentication, audio resolution and lyrics are unchanged.

Other Lume-specific adaptations live in `backend/`, `provider/`, `main.cjs` and
`secure-store.cjs`. They provide private stdio process ownership, authenticated
IPv4-loopback API transport, Windows encrypted persistence and a native host
bridge. The loopback token is generated per runtime and is never forwarded to
upstream music services. Source distribution and installer signing are separate
release operations.
