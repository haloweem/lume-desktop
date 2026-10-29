# Shared native widget material

`WidgetMaterialRuntime` is a per-window lease on `LumeGlass.dll`. It loads the DLL
from the executable directory on Attach, updates the material and overlay geometry,
joins the render worker on Detach, and unloads its DLL reference. The DLL shares a
D3D11 device and per-HWND Windows Graphics Capture sessions between leases. Removing
the final lease releases captures, textures, device and worker threads. There is no
Electron, Node, helper process or cloud dependency.

`State()` and `Status()` distinguish initialization, actual successfully presented
frames, and transparent fallback. A loaded DLL or successful Attach does not prove
material readiness. `NativeCalendarWindow::MaterialStatus()` exposes these values
to the host. Missing/unsupported capture leaves the existing foreground usable.
`LumeMaterialStatusChangedMessage` notifies the owner when state changes; the host
can bind `SetMaterialChanged` to refresh settings without a polling timer.

## Build and stage

The main application uses MinGW; the capture DLL uses installed MSVC x64 Build
Tools and Windows SDK C++/WinRT. The boundary is the versioned POD C ABI in
`material_api.h`, and the DLL statically links the MSVC CRT with `/MT`.

Run `build-material.ps1 -OutputDirectory <absolute-output-directory>` with
PowerShell. Build-time tools are discovered with vswhere; the build does not
download dependencies. Add `widget_material_runtime.cpp` to the application
library that owns native widgets. Stage **LumeGlass.dll and LumeGlass.NOTICE**
beside LumeShell.exe; do not stage the `.obj`, `.exp`, or MSVC import `.lib`.

`compile-material-shaders.cpp` compiles the liquid/frosted and scene blit/clear
shaders during the build with the original `D3DCOMPILE_OPTIMIZATION_LEVEL3` flags.
Its generated `material-shader-bytecode.h` stays in the build directory and is
embedded in the DLL. Neither this helper executable nor the generated header is
shipped. Opening a widget or popup only creates shader objects from those bytes;
the runtime DLL no longer imports D3DCompiler. The shader regression compares
embedded material bytes with source compilation and renders the embedded liquid
shader through WARP. This removes repeated compilation without keeping a hidden
window, GPU device or capture session alive after the last lease.

`tests/unit/native_material_test.cpp` dynamically loads an explicit DLL path and
uses isolated temporary windows. It checks invalid handles, actual liquid and
frosted captured/presented frames, shared-device ownership, final resource release
and DLL reload. It requires an interactive Windows desktop with WGC support and is
an opt-in integration check, not a headless unit test.

## Capture and visual scope

The retained native implementation captures only windows below the owner. Its own
surface is excluded by HWND and owner identity; lower material surfaces enter via
shared shader views. There is no desktop/monitor feedback capture and no hiding the
material from screenshots. Rendering uses window-sized composition buffers and a
clipped scene region with the original optical sample margin. The calendar uses
original optical profile 1, original liquid/frosted shader parameters and its
month-picker frosted panel geometry.

## Optical profiles

| profile | user | backdrop |
| --- | --- | --- |
| 0 | liquid widgets / music bar | full-resolution scene |
| 1 | frosted widgets, calendar, editors, **launchpad (frosted)** | full-resolution scene |
| 2 | original frosted panel overlay | full-resolution scene |
| 3 | Dock, **launchpad (liquid)** | full-resolution scene, 3x wider kernel |
| 4 | (reserved) | scene downsampled 4x, then the same kernel |

The frosted pane tint lives in one branch (`materials/frosted-glass.h`):

| surface | colour |
| --- | --- |
| profiles 0-2 (widgets, calendar, launchpad) | `soft * 0.58` |
| inner panels (same profiles) | `lerp(colour, white, 0.08)` |
| profile 3 (Dock) | `soft * 0.486 + 0.045`, 28% rim + top sheen |

Scale the backdrop, never add to it: an additive lift compresses the channel
differences and turns a blue wallpaper into neutral grey.

The launchpad deliberately shares profile 1 with the widgets instead of keeping a
Dock-style profile of its own. Earlier attempts (profile 3, then a reserved
profile 4 with a downsampled backdrop, then a temporary profile 5) each made the
launchpad drift away from the widgets, and the `>= 4` gate on
`SetBackdropScale` once fed the *liquid* shader a 1/4-size backdrop, which
flattened the glass body. The rule is now: frosted launchpad = profile 1, liquid
launchpad = profile 3, and only profile 4 (reserved, unused) triggers the
downsample.

The original low-power scheduler, interaction springs, DPI geometry and Explorer
host capture remain. Failed initialization/presentation retries after two seconds;
removed shared devices retire before a new generation is acquired. Retry waits are
interruptible by Detach. Full-size source-window WGC buffers still
cost GPU memory, and each active widget has its own render worker. Device removal,
capture-denied windows, mixed GPU monitors and all desktop wallpaper hosts have not
been exhaustively validated. The calendar's separate task editor and date/reminder
dropdowns now lease this runtime and use the original frosted-panel path. They wait
for an actually presented first frame before showing, with an editable opaque
fallback on failure or after three seconds. The month picker inherits its parent's
material state. The scheduled reminder notification window remains opaque. These
integrations do not establish full visual parity across every background, DPI and
monitor.

2026-09-27 圆角修复：液态和磨砂材质仅在轮廓附近按物理像素进行
8×8 几何覆盖采样，内部和远离轮廓的像素直接取完整或零覆盖。
阴影按剩余覆盖合成，不新增整窗超采样纹理，也不扩大常驻捕获范围。
`material_shader_test.cpp` 使用独立 64×64 面积参考验证四档 DPI 的边缘，
同时保留中心亮度、预乘透明度和折射检查。完整范围见
[试装反馈报告](../../../../docs/verification/2026-09-27-trial-feedback.md)；
这些检查不替代不同显卡和显示器的实机验收。

During Detach, UI-side retirement pumps sent window messages while waiting for the
render worker, avoiding the worker's window-operation/UI-join deadlock. Lease
retirement precedes DLL detachment. The liquid body's extra white haze was removed
at the user's request; edge highlights and refraction remain. See the
[material audit](../../../../docs/reference/original-ui/material-audit-2026-09-26.md)
and [implementation status](../../../../docs/verification/2026-09-26-implementation-status.md)
for historical checks, first-frame latency and outstanding acceptance.

The earlier `D3D11GlassBackend`/`GlassService` scaffold is not used by this runtime:
it has no real texture output and must not be treated as material readiness.

## Source and license

Adapted from the read-only original Lume 1.0.1 native renderer at
`src/lume-electron-shell/native/liquid-glass-window.cc`. N-API entrypoints were
removed and replaced by Attach/Update/Detach/Inspect/Statistics C functions. The
surface window class uses the DLL module identity and is unregistered on final
detach, allowing reload. Failed-device retirement and retry handling were added;
the swap-chain latency handle now has exception-safe lifetime. `glass-capture.h`, `glass-scene.h`, `glass-wake.h`,
`glass-interaction.h`, `desktop-window.h`, and `materials/*.h` retain the original
native implementation. Original file hashes are in `SOURCE-SHA256.txt`.

The original complete material notices are preserved in `LumeGlass.NOTICE`,
including ybouane/liquidglass, iyinchao/liquid-glass-studio (MIT), and
Kyant0/AndroidLiquidGlass (Apache-2.0) attributions and license text. The notice must
accompany distributed binaries.
