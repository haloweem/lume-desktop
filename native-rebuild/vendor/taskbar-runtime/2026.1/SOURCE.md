# Lume Taskbar Core source

Lume Taskbar Core is built from TranslucentTB 2026.1 under GPL-3.0.

- Upstream repository: https://github.com/TranslucentTB/TranslucentTB
- Upstream tag: `2026.1`
- Pinned commit: `322e2b7395a51975150126276308b415970e080b`
- Lume patch: `lume-taskbar-core.patch`
- License: `LICENSE.md` (GPL-3.0)

The patch adds the `LUME_TASKBAR_CORE` build define, identifies the executable
as `Lume Taskbar Core`, forces a transparent desktop taskbar configuration,
disables all state-specific appearance overrides, hides the tray icon, disables
configuration saving and logging, suppresses the upstream welcome window, and
restores the stock taskbar synchronously when shutdown begins instead of waiting
for XAML teardown or the process-death fallback. The normal upstream build
remains unchanged when the define is absent.

After the build, `tools/brand-taskbar-core.ps1` replaces every icon group in
`LumeTaskbarCore.exe` (x64 and arm64) with `packaging/lume.ico`, so the process
no longer shows the TranslucentTB artwork. Only icon resources change.

## Rebuild

1. Clone the upstream repository and checkout the pinned commit, including its
   submodules.
2. Apply `lume-taskbar-core.patch` with `git apply`.
3. Restore the solution's NuGet packages.
4. Install the vcpkg dependency union for both `x64-windows` and
   `arm64-windows`, plus `detours` and `wil` for the corresponding
   `*-windows-static` triplets. Use the upstream overlay ports in
   `vcpkg/ports`.
5. Run `tools/build-translucenttb-core.ps1` from the Lume repository with the
   required Visual Studio, vcpkg, UWP MSBuild and VC Runtime SDK paths.
   Then run `native-rebuild/tools/brand-taskbar-core.ps1 -Executable <exe>
   -Icon native-rebuild/packaging/lume.ico` for each architecture.

The build used MSBuild 17.14.40, MSVC 14.44.35207, Windows SDK 10.0.26100.0,
and vcpkg commit `84bab45d415d22042bd0b9081aea57f362da3f35`.

`resources.pri` and the two splash assets are unmodified files from the
TranslucentTB 2026.1 portable payload. `settings.json` is retained for runtime
inventory compatibility, but a `LUME_TASKBAR_CORE` build ignores external
TranslucentTB settings and always uses the fixed Lume configuration.

The application-local Microsoft Visual C++ runtime dependency is documented
separately in `MSVC-RUNTIME-SOURCES.md`. Those signed Microsoft DLLs remain
unmodified and retain Microsoft's distribution terms; they are not part of
the GPL-licensed core sources.
