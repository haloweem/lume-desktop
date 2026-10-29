# Offline notes editor

Tiptap **3.31.4**, bundled by esbuild **0.25.12** for the standalone notes page.
The application uses the checked-in `editor.mjs` directly and does not run npm
or contact a CDN. KaTeX is shared with `../notes-math/katex.mjs`.

`package-lock.json` pins the complete dependency tree and each official npm
archive's integrity. `manifest.json` records versions, archive origins, license
paths and the output bundle's SHA-256. All dependency license texts are included
under `licenses/`, including build dependencies.

To reproduce from the source checkout:

```powershell
node native-rebuild/ui/tools/vendor-notes-editor.mjs
```

The script's `npm ci` needs network access for dependencies absent from the npm
cache. It installs into a temporary directory with lifecycle scripts disabled,
then bundles the exports from `ui/tools/notes-editor-entry.mjs`. An explicit
`--workdir` can reuse a build-artifact directory; no install occurs in the source
UI folder. Bundle output is deterministic for the pinned source and esbuild
version. The locally implemented adapter is `ui/notes-rich-editor.js`.
