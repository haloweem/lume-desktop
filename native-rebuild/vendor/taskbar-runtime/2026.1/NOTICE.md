# TranslucentTB runtime notice

The Windows 11 taskbar core in this directory is built from TranslucentTB
2026.1 source and redistributed under GPL-3.0.

- Upstream: https://github.com/TranslucentTB/TranslucentTB
- Release: https://github.com/TranslucentTB/TranslucentTB/releases/tag/2026.1
- Source commit: `322e2b7395a51975150126276308b415970e080b`
- x64 archive SHA-256: `50C9BFB4A0E013B46CDF902221BD7B13EE4A278A3E95122E6C581FA5BD472E68`
- ARM64 archive SHA-256: `35DD872EF1519DCC6C8FDBA27C87AC51ACD7B15C85DB5A4388C68483482BAE6A`

The executable and adjacent DLLs are rebuilt for x64 and ARM64 with the patch
in `lume-taskbar-core.patch`. The Lume build uses a fixed transparent taskbar
configuration and exposes no independent tray or welcome interface. Lume
manages the core lifetime through `LumeTaskbarHelper.exe`.

Complete source and reproducible build instructions are documented in
`SOURCE.md`. Lume's integration source is part of the Lume source repository.
