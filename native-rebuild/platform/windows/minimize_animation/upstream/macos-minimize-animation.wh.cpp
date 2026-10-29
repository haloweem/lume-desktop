// ==LumeMod==
// @id              macos-minimize-animation
// @name            MacOS Minimize Animation
// @name:zh-CN      macOS 最小化动画
// @description     Smooth macOS-style genie minimize and restore (open) animations for every window.
// @description:zh-CN 为所有窗口提供流畅的 macOS 神奇效果最小化、还原与打开动画。
// @version         3.1.2
// @author          Abdullah Masood
// @github          https://github.com/Abdullah-Masood-05
// @include         *
// @compilerOptions -ldwmapi -lgdi32 -ld2d1 -lole32 -loleaut32 -luuid -lshell32
// @license         MIT
// ==/LumeMod==

// ==LumeModReadme==
/*
## Settings
- **Animation duration** - how long the effect lasts (50-2000 ms).
- **Animate window restore (open)** - also play the reverse genie when restoring.
- **Animate app launch (experimental)** - also play the genie when an application
  window first opens. **Experimental and off by default**: it may briefly flash the
  window before animating, may not catch borderless apps, and can occasionally fire
  on splash / dialog windows. Leave it off and the mod behaves exactly like plain
  minimize / restore.
- **Multi-monitor support (experimental)** - play the genie on the monitor the
  window is on, targeting that monitor's taskbar edge, instead of always the
  primary monitor's. **Experimental and off by default.**
- **Unhide taskbar (for taskbar auto-hide)** - if your taskbar is set to
  auto-hide, briefly reveal it during the minimize so the genie has a visible dock
  to flow into, then let it slide back once the animation finishes.
- **Taskbar unhide duration** - how long (ms) to keep the taskbar revealed before
  performing the deferred minimize on the auto-hide path.

## Notes
- Works on all top-level windows; child / tiny / hidden windows are skipped.
- DWM transitions are temporarily disabled on the animated window and restored
  afterwards, so the system's own minimize/restore animation doesn't fight ours.
- Minimize snapshots are captured from the window itself, so the taskbar and other
  windows never bleed into the animation (even for maximized / fullscreen apps).
  Windows with DWM acrylic / translucency may appear opaque during the genie, since
  the composited backdrop isn't part of the window's own render - a known
  limitation.
*/
// ==/LumeModReadme==

// ==LumeModReadme:zh-CN==
/*
## 设置

- **动画时长**：动画持续时间，范围为 50 至 2000 毫秒。
- **还原窗口时播放动画**：从任务栏还原窗口时反向播放神奇效果。
- **应用启动动画（实验性）**：应用窗口首次打开时也播放神奇效果。此功能默认关闭，可能在动画前短暂闪烁窗口，可能无法捕获无边框应用，也可能偶尔作用于启动画面或对话框。
- **多显示器支持（实验性）**：在窗口所在的显示器上播放动画，并汇聚到该显示器的任务栏边缘，而不是始终使用主显示器。此功能默认关闭。
- **显示自动隐藏的任务栏**：如果任务栏设置为自动隐藏，最小化时短暂显示任务栏，让窗口流入可见的目标位置；动画结束后再允许任务栏收回。
- **任务栏显示时长**：在自动隐藏模式下延迟执行最小化前，任务栏保持显示的时间。

## 注意事项

- 适用于所有顶层窗口；子窗口、尺寸过小的窗口和隐藏窗口会被跳过。
- 播放动画期间会临时禁用目标窗口的 DWM 过渡效果，结束后再恢复，避免系统动画与本动画冲突。
- 最小化快照直接从目标窗口捕获，因此即使应用最大化或全屏，任务栏和其他窗口也不会混入动画画面。使用 DWM 亚克力或半透明效果的窗口在动画中可能显示为不透明，这是因为合成背景不属于窗口自身画面，属于已知限制。
*/
// ==/LumeModReadme:zh-CN==

// ==LumeModSettings==
/*
- duration_ms: 450
  $name: Animation duration (ms)
  $name:zh-CN: 动画时长（毫秒）
  $description: How long the genie animation lasts. Clamped to 50-2000.
  $description:zh-CN: 神奇效果动画的持续时间，数值限制在 50 至 2000 毫秒之间。
- open_animation: true
  $name: Animate window restore (open)
  $name:zh-CN: 还原窗口时播放动画
  $description: Play the reverse genie animation when a window is restored from the taskbar.
  $description:zh-CN: 从任务栏还原窗口时，反向播放神奇效果动画。
- launch_animation: false
  $name: Animate app launch (experimental)
  $name:zh-CN: 应用启动动画（实验性）
  $description: >-
    Play a genie when an application window first opens. Experimental: may briefly
    flash the window before animating, may not catch borderless apps, and can
    occasionally fire on splash/dialog windows.
  $description:zh-CN: >-
    应用窗口首次打开时播放神奇效果。此功能仍处于实验阶段：动画开始前窗口可能短暂闪烁，
    可能无法捕获无边框应用，也可能偶尔作用于启动画面或对话框。
- multi_monitor: false
  $name: Multi-monitor support (experimental)
  $name:zh-CN: 多显示器支持（实验性）
  $description: >-
    Play the genie on the monitor the window is on and target that monitor's
    taskbar edge, instead of always the primary monitor's.
  $description:zh-CN: >-
    在窗口所在的显示器上播放神奇效果，并以该显示器的任务栏边缘为目标，
    而不是始终使用主显示器。
- unhide_taskbar: true
  $name: Unhide taskbar (for taskbar auto-hide)
  $name:zh-CN: 显示自动隐藏的任务栏
  $description: >-
    If the taskbar is set to auto-hide, briefly reveal it during the minimize so
    the genie has a visible dock to flow into, then let it slide back afterwards.
  $description:zh-CN: >-
    如果任务栏设置为自动隐藏，最小化时会短暂显示任务栏，让窗口流入可见的目标位置，
    动画结束后再允许任务栏收回。
- unhide_duration_ms: 450
  $name: Taskbar unhide duration (ms)
  $name:zh-CN: 任务栏显示时长（毫秒）
  $description: >-
    How long to keep the taskbar revealed before performing the deferred minimize
    on the auto-hide path. Clamped to 0-5000.
  $description:zh-CN: >-
    在自动隐藏模式下延迟执行最小化前，任务栏保持显示的时间，
    数值限制在 0 至 5000 毫秒之间。
- engine: modern
  $name: Animation style
  $name:zh-CN: 动画样式
  $description: >-
    Modern style uses a smooth mesh warp that narrows the window into the taskbar.
    Classic style uses a smooth strip-based funnel for the traditional genie effect
    with lower resource usage.
  $description:zh-CN: >-
    现代样式通过平滑网格变形，让窗口逐渐收束并流入任务栏。
    经典样式通过平滑收口呈现传统的神奇效果，资源占用更低。
  $options:
  - modern: Modern style
  - classic: Classic style
  $options:zh-CN:
  - modern: 现代样式
  - classic: 经典样式
*/
// ==/LumeModSettings==

#include <windows.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <math.h>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <uiautomation.h>
#include <shellapi.h>

#ifndef DWMWA_EXTENDED_FRAME_BOUNDS
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
#endif

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 2
#endif

// Ported from Potassiumuncher's genie engine (github.com/Potassiumuncher).
#define PI 3.14159265f
#define GHOST_X_TILES 20
#define GHOST_Y_TILES 20
#define COMPAT_TASKBAR_HEIGHT 70
#define COMPAT_ICON_SIZE      33

// Ported from Potassiumuncher's genie engine (github.com/Potassiumuncher).
struct Geometry { float x, y, width, height; };

typedef LRESULT (WINAPI *DefWindowProcW_t)(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
DefWindowProcW_t DefWindowProcW_Original;

typedef BOOL (WINAPI *ShowWindow_t)(HWND hWnd, int nCmdShow);
ShowWindow_t ShowWindow_Original;

typedef BOOL (WINAPI *ShowWindowAsync_t)(HWND hWnd, int nCmdShow);
ShowWindowAsync_t ShowWindowAsync_Original;

typedef BOOL (WINAPI *SetWindowPlacement_t)(HWND hWnd, const WINDOWPLACEMENT* lpwndpl);
SetWindowPlacement_t SetWindowPlacement_Original;

typedef BOOL (WINAPI *CloseWindow_t)(HWND hWnd);
CloseWindow_t CloseWindow_Original;

typedef BOOL (WINAPI *SetWindowPos_t)(HWND hWnd, HWND hWndInsertAfter, int X, int Y,
                                      int cx, int cy, UINT uFlags);
SetWindowPos_t SetWindowPos_Original;

// Shared Direct2D factory (multi-threaded: several worker threads may render at
// once). Ported from Potassiumuncher's genie engine.
ID2D1Factory* g_d2dFactory = nullptr;

struct MacGenieAnimData {
    HWND hRealWnd;
    HBITMAP hBitmap;      // 32-bit top-down premultiplied DIB snapshot (w x h)
    void* pBits;          // its bits (fed straight to D2D CreateBitmap)
    RECT targetRect;      // the window's DWM extended-frame rect when anim started
    HMONITOR hMon;        // monitor the genie targets (window's when multi-monitor
                          // support is on, else the primary)
    int width;
    int height;
    int targetDockX;      // taskbar button X located via UI Automation
    BOOL isRising;        // FALSE = minimize (flow in), TRUE = restore (flow out)
    LONG_PTR originalExStyle;
    BOOL hiddenByCloak;   // rising only: TRUE when hidden via DWM cloak
    HANDLE hFirstFrameShown; // falling only: event signaled once the ghost's first
                             // frame is composed; NULL when unused
    int durationMs;
    // Auto-hide "unhide" (deferred minimize) - Potassiumuncher's engine. Only used
    // on the DefWindowProcW SC_MINIMIZE path when the taskbar is set to auto-hide.
    BOOL requestedUnhide;
    HWND hNextApp;
    int  unhideDurationMs;
    BOOL deferredMinimize;
};

// Handed to MacGenieLaunchThread: launch hiding uses DWM cloak, while the
// original style remains part of the shared rising-animation contract.
struct MacGenieLaunchData {
    HWND     hWnd;
    LONG_PTR originalExStyle;
};

// Cached snapshot for the restore animation. The DIB section owns pBits, so
// DeleteObject(hBmp) frees both. Snapshot model from Potassiumuncher's engine.
struct SnapCache { HBITMAP hBmp; void* pBits; int w; int h; };

// --- THE VAULTS ---
std::unordered_map<HWND, SnapCache> g_SnapshotCache;
std::unordered_map<HWND, int> g_IconPositions;            // per-window learned icon X
std::unordered_map<std::wstring, int> g_ProcessIconPositions; // per-process fallback
std::unordered_set<HWND> g_LaunchSeen;   // windows we've already shown/animated once
std::unordered_set<HWND> g_AnimActive;   // windows with a genie currently in flight
std::mutex g_CacheMutex;
std::unordered_set<HWND> g_CloakedWindows;
std::mutex g_CloakMutex;

// --- SETTINGS ---
std::atomic<int> g_durationMs{450};
std::atomic<bool> g_openAnimation{true};
std::atomic<bool> g_launchAnimation{false};
std::atomic<bool> g_multiMonitor{false};
std::atomic<int>  g_unhideDurationMs{450};
std::atomic<bool> g_unhideEnabled{true};
// false = modern Direct2D genie (Potassiumuncher's engine), true = the original
// v2.2.0 strip-based GDI genie, offered as an opt-in "classic" style.
std::atomic<bool> g_classicEngine{false};

// --- UNLOAD COORDINATION ---
// Lume unmaps the mod DLL right after uninit, so any worker thread still
// running mod code at that point would crash its host process. Workers register
// here and abort promptly once g_unloading is set; Wh_ModBeforeUninit waits for
// the count to drain before the DLL goes away. (MINE's drain model is kept in
// preference to HIS's join-and-pump StopAndJoinAllThreads: pumping messages while
// unloading can re-enter our six hooks under @include *.)
std::atomic<bool> g_unloading{false};
std::atomic<int>  g_workerCount{0};
std::mutex g_WorkerGateMutex;
HANDLE g_unloadEvent = NULL;

bool MacGenieRegisterWork() {
    std::lock_guard<std::mutex> lock(g_WorkerGateMutex);
    if (g_unloading.load(std::memory_order_acquire)) {
        return false;
    }
    g_workerCount.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void MacGenieUnregisterWork() {
    g_workerCount.fetch_sub(1, std::memory_order_release);
}

bool WaitForNextAnimationFrame() {
    return g_unloadEvent &&
           WaitForSingleObject(g_unloadEvent, 8) == WAIT_TIMEOUT &&
           !g_unloading.load(std::memory_order_acquire);
}

void MacGenieLoadSettings() {
    int ms = Wh_GetIntSetting(L"duration_ms");
    if (ms < 50) ms = 50;
    if (ms > 2000) ms = 2000;
    g_durationMs.store(ms, std::memory_order_relaxed);
    g_openAnimation.store(Wh_GetIntSetting(L"open_animation") != 0, std::memory_order_relaxed);
    g_launchAnimation.store(Wh_GetIntSetting(L"launch_animation") != 0, std::memory_order_relaxed);
    g_multiMonitor.store(Wh_GetIntSetting(L"multi_monitor") != 0, std::memory_order_relaxed);

    int unhide_ms = Wh_GetIntSetting(L"unhide_duration_ms");
    if (unhide_ms < 0) unhide_ms = 0;
    if (unhide_ms > 5000) unhide_ms = 5000;
    g_unhideDurationMs.store(unhide_ms, std::memory_order_relaxed);
    g_unhideEnabled.store(Wh_GetIntSetting(L"unhide_taskbar") != 0, std::memory_order_relaxed);

    PCWSTR engine = Wh_GetStringSetting(L"engine");
    g_classicEngine.store(engine && wcscmp(engine, L"classic") == 0, std::memory_order_relaxed);
    Wh_FreeStringSetting(engine);
}

void MacGenieSetDwmTransitions(HWND hWnd, BOOL enable) {
    BOOL disable = !enable;
    DwmSetWindowAttribute(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable));
}

// Hide / show a window at the DWM level. A cloaked window is simply not rendered
// (frame included) while staying "visible" to Win32 and painting normally
// underneath, so a restore can happen invisibly under the genie with no
// layered-surface rebuild flash. Kept from MINE in preference to HIS's
// WS_EX_LAYERED + alpha 0 restore hide, which flashes the bare frame.
bool MacGenieSetCloak(HWND hWnd, BOOL cloak) {
    std::lock_guard<std::mutex> lock(g_CloakMutex);
    if (cloak && g_unloading.load(std::memory_order_acquire)) {
        return false;
    }

    HRESULT hr = DwmSetWindowAttribute(hWnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
    if (cloak) {
        if (SUCCEEDED(hr)) {
            g_CloakedWindows.insert(hWnd);
        }
    } else if (SUCCEEDED(hr)) {
        g_CloakedWindows.erase(hWnd);
    } else if (!IsWindow(hWnd)) {
        g_CloakedWindows.erase(hWnd);
    }
    return SUCCEEDED(hr);
}

size_t MacGenieRestoreAllCloakedWindows() {
    std::lock_guard<std::mutex> lock(g_CloakMutex);
    BOOL cloak = FALSE;
    for (auto it = g_CloakedWindows.begin(); it != g_CloakedWindows.end();) {
        if (!IsWindow(*it)) {
            it = g_CloakedWindows.erase(it);
            continue;
        }
        HRESULT hr = DwmSetWindowAttribute(*it, DWMWA_CLOAK, &cloak, sizeof(cloak));
        if (SUCCEEDED(hr)) {
            it = g_CloakedWindows.erase(it);
        } else {
            ++it;
        }
    }
    return g_CloakedWindows.size();
}

// Undo a rising caller's pre-animation hide (used whenever the animation can't run
// or is refused): uncloak or un-hide depending on how the window was hidden, and
// re-enable its DWM transitions.
static void MacGenieUndoRisingHide(HWND hWnd, LONG_PTR originalExStyle, BOOL cloakHidden) {
    if (cloakHidden) {
        MacGenieSetCloak(hWnd, FALSE);
    } else {
        SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
        if (!(originalExStyle & WS_EX_LAYERED)) {
            SetWindowLongPtrW(hWnd, GWL_EXSTYLE, originalExStyle);
        }
    }
    MacGenieSetDwmTransitions(hWnd, TRUE);
}

// Should we animate this window at all? Skip child / tiny windows so the effect
// only fires on real top-level windows. The size check stays on the placement /
// window rect; the animation geometry (below) uses the DWM extended frame bounds.
static bool MacGenieShouldAnimate(HWND hWnd) {
    if (!hWnd) return false;
    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
    if (style & WS_CHILD) return false;

    RECT r;
    if (IsIconic(hWnd)) {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(wp);
        if (!GetWindowPlacement(hWnd, &wp)) return false;
        r = wp.rcNormalPosition;
    } else if (!GetWindowRect(hWnd, &r)) {
        return false;
    }
    if ((r.right - r.left) < 40 || (r.bottom - r.top) < 40) return false;
    return true;
}

// A real, top-level window with a title bar (skips child windows, tool windows,
// and borderless popups / menus / tooltips / splash screens).
static bool MacGenieIsLaunchWindow(HWND hWnd) {
    if (!hWnd) return false;
    if (GetAncestor(hWnd, GA_ROOT) != hWnd) return false;
    LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
    if (!(style & WS_CAPTION)) return false;
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return false;
    return MacGenieShouldAnimate(hWnd);
}

// -------------------------------------------------------------------------
// Direct2D genie renderer helpers - ported verbatim from Potassiumuncher's
// genie engine (github.com/Potassiumuncher).
// -------------------------------------------------------------------------

// Ported from Potassiumuncher's genie engine (github.com/Potassiumuncher).
static ID2D1PathGeometry* CreateQuadGeo(
    ID2D1Factory* factory,
    D2D1_POINT_2F p0, D2D1_POINT_2F p1,
    D2D1_POINT_2F p2, D2D1_POINT_2F p3)
{
    ID2D1PathGeometry* geo = nullptr;
    factory->CreatePathGeometry(&geo);
    if (!geo) return nullptr;
    ID2D1GeometrySink* sink = nullptr;
    geo->Open(&sink);
    sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(p1);
    sink->AddLine(p2);
    sink->AddLine(p3);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    return geo;
}

// Ported from Potassiumuncher's genie engine (github.com/Potassiumuncher).
static D2D1_POINT_2F BloatPoint(D2D1_POINT_2F p, D2D1_POINT_2F c, float amount = 0.5f) {
    float dx = p.x - c.x; float dy = p.y - c.y;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.001f) return p;
    return D2D1::Point2F(p.x + (dx/len)*amount, p.y + (dy/len)*amount);
}

// Ported from Potassiumuncher's genie engine (github.com/Potassiumuncher).
HWND FindTaskbarForMonitor(HMONITOR hMon) {
    HWND hMainTray = FindWindowW(L"Shell_TrayWnd", NULL);
    HMONITOR mainMon = MonitorFromWindow(hMainTray, MONITOR_DEFAULTTOPRIMARY);
    if (hMon == mainMon || !hMon) return hMainTray;

    HWND hSecTray = NULL;
    while ((hSecTray = FindWindowExW(NULL, hSecTray, L"Shell_SecondaryTrayWnd", NULL)) != NULL) {
        if (MonitorFromWindow(hSecTray, MONITOR_DEFAULTTONULL) == hMon) {
            return hSecTray;
        }
    }
    return hMainTray;
}

// Ported from Potassiumuncher's genie engine (github.com/Potassiumuncher).
// Locates the app's taskbar button via UI Automation and returns its center X
// (falls back to fallbackX, then to the per-window / per-process learned cache).
// Runs on the hook (app UI) thread: CoInitializeEx(APARTMENTTHREADED) matches the
// typical GUI-thread apartment (S_FALSE) or reports RPC_E_CHANGED_MODE on an MTA
// thread; either way CoUninitialize is balanced only when we actually initialized.
int GetTaskbarButtonX(HWND hWndApp, int fallbackX, HMONITOR hMon) {
    int targetX = fallbackX;
    bool uiaFound = false;

    std::wstring procNameLower = L"";
    DWORD ownerPid = 0;
    GetWindowThreadProcessId(hWndApp, &ownerPid);
    if (ownerPid) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ownerPid);
        if (hProc) {
            WCHAR exePath[MAX_PATH] = {0};
            DWORD exePathLen = MAX_PATH;
            if (QueryFullProcessImageNameW(hProc, 0, exePath, &exePathLen)) {
                WCHAR* name = wcsrchr(exePath, L'\\');
                if (name) {
                    procNameLower = (name + 1);
                    size_t dotPos = procNameLower.find(L'.');
                    if (dotPos != std::wstring::npos) procNameLower = procNameLower.substr(0, dotPos);
                    std::transform(procNameLower.begin(), procNameLower.end(), procNameLower.begin(), ::towlower);
                }
            }
            CloseHandle(hProc);
        }
    }

    std::wstring processKey = procNameLower;
    if (!processKey.empty() && hMon) {
        processKey += L"_" + std::to_wstring(reinterpret_cast<size_t>(hMon));
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool coInit = (hr == S_OK || hr == S_FALSE);

    if (hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) {
        IUIAutomation* pAutomation = nullptr;
        HRESULT hrUia = CoCreateInstance(__uuidof(CUIAutomation8), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&pAutomation);
        if (FAILED(hrUia)) {
            hrUia = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&pAutomation);
        }

        if (SUCCEEDED(hrUia) && pAutomation) {
            HWND hTray = FindTaskbarForMonitor(hMon);
            if (hTray) {
                IUIAutomationElement* pTrayElement = nullptr;
                if (SUCCEEDED(pAutomation->ElementFromHandle(hTray, &pTrayElement)) && pTrayElement) {

                    WCHAR titleW[512] = {0};
                    GetWindowTextW(hWndApp, titleW, 512);
                    std::wstring titleLower = titleW;
                    std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::towlower);

                    std::wstring procHintLower = procNameLower;
                    if (procNameLower == L"chrome") procHintLower = L"google chrome";
                    else if (procNameLower == L"msedge") procHintLower = L"microsoft edge";
                    else if (procNameLower == L"firefox") procHintLower = L"firefox";
                    else if (procNameLower == L"brave") procHintLower = L"brave";
                    else if (procNameLower == L"opera") procHintLower = L"opera";
                    else if (procNameLower == L"vivaldi") procHintLower = L"vivaldi";

                    IUIAutomationCondition* pButtonCond = nullptr;
                    IUIAutomationCondition* pListItemCond = nullptr;
                    IUIAutomationCondition* pOrCond = nullptr;

                    VARIANT varBtn; varBtn.vt = VT_I4; varBtn.lVal = UIA_ButtonControlTypeId;
                    pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId, varBtn, &pButtonCond);

                    VARIANT varList; varList.vt = VT_I4; varList.lVal = UIA_ListItemControlTypeId;
                    pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId, varList, &pListItemCond);

                    if (pButtonCond && pListItemCond) {
                        pAutomation->CreateOrCondition(pButtonCond, pListItemCond, &pOrCond);
                    }

                    IUIAutomationElementArray* pArray = nullptr;
                    if (pOrCond && SUCCEEDED(pTrayElement->FindAll(TreeScope_Descendants, pOrCond, &pArray)) && pArray) {
                        int length = 0;
                        pArray->get_Length(&length);

                        MONITORINFO mi = {0};
                        mi.cbSize = sizeof(MONITORINFO);
                        GetMonitorInfoW(hMon, &mi);
                        int monRight = mi.rcMonitor.right;

                        int bestScore = 0;

                        for (int i = 0; i < length; i++) {
                            IUIAutomationElement* pItem = nullptr;
                            if (SUCCEEDED(pArray->GetElement(i, &pItem)) && pItem) {
                                BSTR name;
                                if (SUCCEEDED(pItem->get_CurrentName(&name)) && name) {
                                    std::wstring uiaNameLower = name;
                                    std::transform(uiaNameLower.begin(), uiaNameLower.end(), uiaNameLower.begin(), ::towlower);

                                    if (!uiaNameLower.empty()) {
                                        int score = 0;

                                        if (titleLower == uiaNameLower) score += 1000;
                                        if (!titleLower.empty() && titleLower.find(uiaNameLower) != std::wstring::npos) score += 500;
                                        if (!uiaNameLower.empty() && uiaNameLower.find(titleLower) != std::wstring::npos) score += 500;

                                        if (!procNameLower.empty() && uiaNameLower.find(procNameLower) != std::wstring::npos) score += 400;
                                        if (!procHintLower.empty() && procHintLower != procNameLower &&
                                            uiaNameLower.find(procHintLower) != std::wstring::npos) score += 900;

                                        std::wstring currentWord;
                                        for (wchar_t c : titleLower) {
                                            if (iswalnum(c)) {
                                                currentWord += c;
                                            } else {
                                                if (currentWord.length() >= 4 && uiaNameLower.find(currentWord) != std::wstring::npos) score += 50;
                                                currentWord.clear();
                                            }
                                        }
                                        if (currentWord.length() >= 4 && uiaNameLower.find(currentWord) != std::wstring::npos) score += 50;

                                        if (uiaNameLower.find(L"start") != std::wstring::npos) score -= 500;
                                        if (uiaNameLower.find(L"search") != std::wstring::npos) score -= 500;
                                        if (uiaNameLower.find(L"task view") != std::wstring::npos) score -= 500;
                                        if (uiaNameLower.find(L"widgets") != std::wstring::npos) score -= 500;

                                        if (score > bestScore) {
                                            RECT bRect;
                                            if (SUCCEEDED(pItem->get_CurrentBoundingRectangle(&bRect))) {
                                                if (bRect.right > bRect.left && bRect.left < monRight - 50) {
                                                    bestScore = score;
                                                    targetX = bRect.left + (bRect.right - bRect.left) / 2;
                                                    uiaFound = true;
                                                }
                                            }
                                        }
                                    }
                                    SysFreeString(name);
                                }
                                pItem->Release();
                            }
                        }
                        pArray->Release();
                    }
                    if (pButtonCond) pButtonCond->Release();
                    if (pListItemCond) pListItemCond->Release();
                    if (pOrCond) pOrCond->Release();
                    pTrayElement->Release();
                }
            }
            pAutomation->Release();
        }
        if (coInit) CoUninitialize();
    }

    std::lock_guard<std::mutex> lock(g_CacheMutex);
    if (uiaFound) {
        g_IconPositions[hWndApp] = targetX;
        if (!processKey.empty()) {
            g_ProcessIconPositions[processKey] = targetX;
        }
    } else if (g_IconPositions.count(hWndApp)) {
        targetX = g_IconPositions[hWndApp];
    } else if (!processKey.empty() && g_ProcessIconPositions.count(processKey)) {
        targetX = g_ProcessIconPositions[processKey];
    }

    return targetX;
}

// Ported from Potassiumuncher's genie engine (github.com/Potassiumuncher).
// The macOS "lamp" curve: maps a mesh vertex (tx,ty in 0..1) at progress p to its
// warped screen position, given the window rect w and the taskbar icon rect i.
static void CalculateLampVertexMacOS(float tx, float ty, float p, const Geometry& w, const Geometry& i, float *outX, float *outY) {
    float split = 0.3f;
    float k = (p <= split) ? (p / split) : 1.0f;
    float j = (p > split) ? ((p - split) / (1.0f - split)) : 0.0f;

    float expandHeight = (i.y - w.y - w.height);
    float fullHeight = (i.y - w.y) - (expandHeight * (1.0f - k));
    float height = fullHeight - (j * fullHeight);

    float y = ty * height;
    float x = tx * (i.width) + tx * (w.width - i.width) * (1.0f - j) * (1.0f - ty) + tx * (w.width - i.width) * (1.0f - k) * ty;

    float offsetX = (i.x - w.x) * (y / (fullHeight + 0.1f)) * k + (i.x - w.x) * j;
    float offsetY = i.y - w.y - height - (expandHeight * (1.0f - k));

    float effectX = sinf(((height - y) / fullHeight) * 2.0f * PI + PI) * (w.x + w.width * tx - (i.x + i.width * tx)) / 7.0f * k;

    *outX = w.x + x + offsetX + effectX;
    *outY = w.y + y + offsetY;
}

// -------------------------------------------------------------------------
// Genie Animation Thread
//
// MINE's loop and lifecycle - g_workerCount, g_unloading early-out, DwmFlush
// pacing, the first-frame sync event, g_AnimActive teardown - with HIS's Direct2D
// mesh renderer as the per-frame draw call. HIS's original busy for(;;) with no
// compose gating is dropped in favor of MINE's DwmFlush vsync pace-gate.
// -------------------------------------------------------------------------
DWORD WINAPI MacGenieAnimThread(LPVOID lpParam) {
    MacGenieAnimData* data = (MacGenieAnimData*)lpParam;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    // Full virtual-screen canvas: HIS's genie mesh is written in screen-space
    // coordinates (it can spill outside the window rect), so the ghost and its
    // render surface span every monitor. This is the one place we adopt HIS's
    // full-screen ghost over MINE's tight bounding box, because the mesh geometry
    // structurally requires a screen-space canvas.
    int vLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1;

    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);

    BITMAPINFO bmi = {{0}};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = vWidth;
    bmi.bmiHeader.biHeight = -vHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pTargetBits = nullptr;
    HBITMAP hTargetBmp = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, &pTargetBits, NULL, 0);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hTargetBmp);

    // Full-screen layered ghost. Created hidden (plain STATIC class, torn down by
    // this thread) so we keep MINE's in-thread ghost lifecycle instead of HIS's
    // registered window class + g_activeGhosts vector.
    HWND hGhost = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        L"STATIC", NULL, WS_POPUP,
        vLeft, vTop, vWidth, vHeight,
        NULL, NULL, NULL, NULL);

    // --- Direct2D setup (Potassiumuncher's engine) ---
    ID2D1DCRenderTarget* rt = nullptr;
    ID2D1Bitmap* snapshotBmp = nullptr;
    ID2D1BitmapBrush* bmpBrush = nullptr;

    if (g_d2dFactory) {
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
        );
        g_d2dFactory->CreateDCRenderTarget(&rtProps, &rt);
        if (rt) {
            D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
            );
            rt->CreateBitmap(D2D1::SizeU(data->width, data->height), data->pBits, data->width * 4, bmpProps, &snapshotBmp);
            if (snapshotBmp) {
                D2D1_BITMAP_BRUSH_PROPERTIES brushProps = D2D1::BitmapBrushProperties(
                    D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
                );
                rt->CreateBitmapBrush(snapshotBmp, &brushProps, nullptr, &bmpBrush);
            }
        }
    }
    bool d2dOk = (rt && bmpBrush);

    // --- Target geometry (Potassiumuncher's engine), using the monitor picked by
    // the multi-monitor setting (data->hMon), so "off" keeps the primary-monitor
    // behavior and "on" targets the window's monitor taskbar edge. ---
    Geometry wGeom = { (float)data->targetRect.left, (float)data->targetRect.top, (float)data->width, (float)data->height };

    MONITORINFO mi = {0};
    mi.cbSize = sizeof(MONITORINFO);
    HMONITOR hMon = data->hMon;
    GetMonitorInfoW(hMon, &mi);

    UINT dpiX = 96, dpiY = 96;
    HMODULE hShcore = LoadLibraryW(L"Shcore.dll");
    if (hShcore) {
        typedef HRESULT (WINAPI *GetDpiForMonitor_t)(HMONITOR, int, UINT*, UINT*);
        auto pGetDpiForMonitor = (GetDpiForMonitor_t)GetProcAddress(hShcore, "GetDpiForMonitor");
        if (pGetDpiForMonitor) pGetDpiForMonitor(hMon, 0, &dpiX, &dpiY);
        FreeLibrary(hShcore);
    }
    float dpiScale = dpiY / 96.0f;
    float scaledTaskbarHeight = COMPAT_TASKBAR_HEIGHT * dpiScale;
    float scaledIconSize = COMPAT_ICON_SIZE * dpiScale;

    float iGeomTaskbarTop = (float)mi.rcMonitor.bottom - scaledTaskbarHeight;

    HWND hTrayGeom = FindTaskbarForMonitor(hMon);
    if (hTrayGeom) {
        RECT tr;
        if (GetWindowRect(hTrayGeom, &tr)) {
            int th = tr.bottom - tr.top;
            if (th > 0) iGeomTaskbarTop = (float)tr.top;
        }
    }

    Geometry iGeom = {
        (float)data->targetDockX - 11.0f,
        iGeomTaskbarTop,
        22.0f,
        scaledIconSize
    };

    const double animDur = (double)data->durationMs;
    LARGE_INTEGER qpcFreq, qpcStart, qpcNow;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcStart);

    int xTiles = GHOST_X_TILES;
    int yTiles = GHOST_Y_TILES;
    std::vector<std::vector<D2D1_POINT_2F>> grid(yTiles + 1, std::vector<D2D1_POINT_2F>(xTiles + 1));

    ID2D1PathGeometry* cachedOutlineGeo = nullptr;
    float cachedLeft = -1e9f, cachedRight = -1e9f, cachedTop = -1e9f, cachedBottom = -1e9f;

    BOOL firstFrame = TRUE;

    if (d2dOk) {
        for (;;) {
            QueryPerformanceCounter(&qpcNow);
            double elapsedMs = (qpcNow.QuadPart - qpcStart.QuadPart) * 1000.0 / qpcFreq.QuadPart;
            BOOL lastFrame = (elapsedMs >= animDur);

            float raw_p = (float)fmin(elapsedMs / animDur, 1.0);
            float eased_p = raw_p * raw_p * (3.0f - 2.0f * raw_p);
            // Minimize runs 0->1, restore runs 1->0 so the same warp plays in reverse.
            float t = data->isRising ? (1.0f - eased_p) : eased_p;

            RECT bindRect = { 0, 0, vWidth, vHeight };
            rt->BindDC(hMemDC, &bindRect);
            rt->BeginDraw();
            rt->Clear(D2D1::ColorF(0, 0, 0, 0));
            rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

            for (int y = 0; y <= yTiles; y++) {
                for (int x = 0; x <= xTiles; x++) {
                    float tx = (float)x / xTiles, ty = (float)y / yTiles;
                    float px, py;
                    // Warp this mesh vertex. CalculateLampVertexMacOS returns SCREEN
                    // (virtual-desktop) coordinates - wGeom / iGeom are both
                    // screen-space (targetRect, targetDockX, taskbar-top Y are all
                    // virtual-screen). The ONLY conversion to the ghost's
                    // full-virtual-screen canvas is the (px - vLeft, py - vTop)
                    // subtraction below - vLeft/vTop are negative on monitors left of
                    // / above the primary. Every mesh coordinate passes through here
                    // exactly once; nothing downstream re-offsets.
                    CalculateLampVertexMacOS(tx, ty, t, wGeom, iGeom, &px, &py);
                    grid[y][x] = D2D1::Point2F(px - vLeft, py - vTop);
                }
            }

            bool degenerate = false;
            ID2D1PathGeometry* outlineGeo = nullptr;
            {
                float left   = grid[0][0].x, right  = grid[0][0].x;
                float top    = grid[0][0].y, bottom = grid[0][0].y;
                for (int gy = 0; gy <= yTiles; gy++) {
                    for (int gx = 0; gx <= xTiles; gx++) {
                        left   = fminf(left,   grid[gy][gx].x);
                        right  = fmaxf(right,  grid[gy][gx].x);
                        top    = fminf(top,    grid[gy][gx].y);
                        bottom = fmaxf(bottom, grid[gy][gx].y);
                    }
                }

                // Belt-and-suspenders guard: if the warped mesh has collapsed to a
                // near-zero-width/height box, don't present it - that is what would
                // flash as a single horizontal line for one frame (seen at the tail
                // of the collapse, and at the very start of a restore where the mesh
                // emerges from the dock). Skipping these frames removes the flash
                // regardless of monitor / arrangement.
                degenerate = (right - left) < 2.0f || (bottom - top) < 2.0f;

                bool needRebuild = (cachedOutlineGeo == nullptr)
                    || fabsf(left   - cachedLeft)   > 0.5f
                    || fabsf(right  - cachedRight)  > 0.5f
                    || fabsf(top    - cachedTop)    > 0.5f
                    || fabsf(bottom - cachedBottom) > 0.5f;

                if (needRebuild) {
                    if (cachedOutlineGeo) { cachedOutlineGeo->Release(); cachedOutlineGeo = nullptr; }
                    cachedLeft = left; cachedRight = right; cachedTop = top; cachedBottom = bottom;

                    float w = right - left;
                    float h = bottom - top;
                    float r = fminf(8.0f, fminf(w, h) / 2.0f);
                    const float K = 0.5523f;
                    float L = left, T = top, R = right, B = bottom;

                    g_d2dFactory->CreatePathGeometry(&cachedOutlineGeo);
                    if (cachedOutlineGeo) {
                        ID2D1GeometrySink* sink = nullptr;
                        cachedOutlineGeo->Open(&sink);
                        sink->BeginFigure(D2D1::Point2F(L + r, T), D2D1_FIGURE_BEGIN_FILLED);
                        sink->AddLine(D2D1::Point2F(R - r, T));
                        { D2D1_BEZIER_SEGMENT s;
                          s.point1 = D2D1::Point2F(R - r + K*r, T);
                          s.point2 = D2D1::Point2F(R, T + r - K*r);
                          s.point3 = D2D1::Point2F(R, T + r);
                          sink->AddBezier(s); }
                        sink->AddLine(D2D1::Point2F(R, B - r));
                        { D2D1_BEZIER_SEGMENT s;
                          s.point1 = D2D1::Point2F(R, B - r + K*r);
                          s.point2 = D2D1::Point2F(R - r + K*r, B);
                          s.point3 = D2D1::Point2F(R - r, B);
                          sink->AddBezier(s); }
                        sink->AddLine(D2D1::Point2F(L + r, B));
                        { D2D1_BEZIER_SEGMENT s;
                          s.point1 = D2D1::Point2F(L + r - K*r, B);
                          s.point2 = D2D1::Point2F(L, B - r + K*r);
                          s.point3 = D2D1::Point2F(L, B - r);
                          sink->AddBezier(s); }
                        sink->AddLine(D2D1::Point2F(L, T + r));
                        { D2D1_BEZIER_SEGMENT s;
                          s.point1 = D2D1::Point2F(L, T + r - K*r);
                          s.point2 = D2D1::Point2F(L + r - K*r, T);
                          s.point3 = D2D1::Point2F(L + r, T);
                          sink->AddBezier(s); }
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                        sink->Close();
                        sink->Release();
                    }
                }
                outlineGeo = cachedOutlineGeo;
            }

            if (outlineGeo) {
                ID2D1Layer* layer = nullptr;
                rt->CreateLayer(&layer);
                D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters();
                layerParams.geometricMask = outlineGeo;
                layerParams.maskAntialiasMode = D2D1_ANTIALIAS_MODE_ALIASED;
                rt->PushLayer(&layerParams, layer);

                for (int y = 0; y < yTiles; y++) {
                    for (int x = 0; x < xTiles; x++) {
                        D2D1_POINT_2F p1 = grid[y][x];
                        D2D1_POINT_2F p2 = grid[y][x+1];
                        D2D1_POINT_2F p3 = grid[y+1][x];
                        D2D1_POINT_2F p4 = grid[y+1][x+1];

                        D2D1_POINT_2F c = D2D1::Point2F((p1.x+p2.x+p3.x+p4.x)/4.0f, (p1.y+p2.y+p3.y+p4.y)/4.0f);

                        float quadW = fmaxf(fabsf(p2.x - p1.x), fabsf(p4.x - p3.x));
                        float quadH = fmaxf(fabsf(p3.y - p1.y), fabsf(p4.y - p2.y));
                        float bloatAmt = fminf(quadW, quadH) * 0.04f;
                        bloatAmt = fmaxf(0.15f, fminf(0.35f, bloatAmt));

                        ID2D1PathGeometry* quadGeo = CreateQuadGeo(g_d2dFactory,
                            BloatPoint(p1, c, bloatAmt), BloatPoint(p2, c, bloatAmt),
                            BloatPoint(p4, c, bloatAmt), BloatPoint(p3, c, bloatAmt));

                        if (quadGeo) {
                            float sx = ((float)x / xTiles) * wGeom.width;
                            float sy = ((float)y / yTiles) * wGeom.height;
                            float sw = wGeom.width / xTiles;
                            float sh = wGeom.height / yTiles;

                            float m11 = (p2.x - p1.x) / sw;
                            float m12 = (p2.y - p1.y) / sw;
                            float m21 = (p3.x - p1.x) / sh;
                            float m22 = (p3.y - p1.y) / sh;
                            float m31 = p1.x - sx * m11 - sy * m21;
                            float m32 = p1.y - sx * m12 - sy * m22;

                            bmpBrush->SetTransform(D2D1::Matrix3x2F(m11, m12, m21, m22, m31, m32));
                            rt->FillGeometry(quadGeo, bmpBrush);
                            quadGeo->Release();
                        }
                    }
                }
                rt->PopLayer();
                if (layer) layer->Release();
            }
            rt->EndDraw();

            // Only present / reveal the ghost on a non-degenerate frame, so a
            // collapsed mesh never flashes as a line. The ghost stays hidden until
            // the first real frame (restore) and simply holds its last good frame if
            // the tail collapses (minimize) - both invisible to the eye.
            if (!degenerate) {
                POINT ptDst = { vLeft, vTop };
                SIZE sz = { vWidth, vHeight };
                POINT ptSrc = { 0, 0 };
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                UpdateLayeredWindow(hGhost, hScreenDC, &ptDst, &sz, hMemDC, &ptSrc, 0, &bf, ULW_ALPHA);

                if (firstFrame) {
                    ShowWindow(hGhost, SW_SHOWNOACTIVATE);
                }
            }

            if (lastFrame) break;

            // Mod is being unloaded: cut the animation short. The teardown below
            // still reveals the real window (rising) and re-enables its DWM
            // transitions.
            if (g_unloading.load(std::memory_order_relaxed)) break;

            if (!WaitForNextAnimationFrame()) break;

            if (firstFrame && !degenerate) {
                firstFrame = FALSE;
                // The first real frame has now been composed on screen. For a
                // minimize the hook is holding the REAL minimize (or, on the
                // auto-hide path, the cloak-hide) back until this moment - otherwise
                // the window would vanish a few frames before the genie appears.
                if (data->hFirstFrameShown) SetEvent(data->hFirstFrameShown);
            }
        }
    }

    if (cachedOutlineGeo) { cachedOutlineGeo->Release(); cachedOutlineGeo = nullptr; }

    // -------------------- COMMON TEARDOWN --------------------
    if (data->isRising) {
        // Reveal the real window the same way it was hidden.
        if (data->hiddenByCloak) {
            MacGenieSetCloak(data->hRealWnd, FALSE);
        } else {
            SetLayeredWindowAttributes(data->hRealWnd, 0, 255, LWA_ALPHA);
            if (!(data->originalExStyle & WS_EX_LAYERED)) {
                SetWindowLongPtrW(data->hRealWnd, GWL_EXSTYLE, data->originalExStyle);
            }
        }
    }

    // For the auto-hide deferred path we keep DWM transitions disabled through the
    // real (deferred) minimize just below, so the system's own minimize animation
    // never plays; every other path re-enables them now.
    if (!data->deferredMinimize) {
        MacGenieSetDwmTransitions(data->hRealWnd, TRUE);
    }

    if (bmpBrush) { bmpBrush->Release(); bmpBrush = nullptr; }
    if (snapshotBmp) { snapshotBmp->Release(); snapshotBmp = nullptr; }
    if (rt) { rt->Release(); rt = nullptr; }

    SelectObject(hMemDC, hOldBmp);
    DeleteObject(hTargetBmp);
    DeleteObject(data->hBitmap);   // frees data->pBits (DIB section)
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    DestroyWindow(hGhost);

    // Unblock the waiting minimize hook (in case we bailed before the first frame
    // was ever composed), then release the event (this thread owns it).
    if (data->hFirstFrameShown) {
        SetEvent(data->hFirstFrameShown);
        CloseHandle(data->hFirstFrameShown);
    }

    // Auto-hide "unhide" completion - Potassiumuncher's engine, reconciled with
    // MINE's cloak hide. The caller cloaked the real window after our first frame
    // (flash-free, unlike HIS's WS_EX_LAYERED+alpha hide) and deferred the real
    // minimize to us so the revealed taskbar didn't slide away mid-animation.
    if (data->deferredMinimize) {
        int unhideMs = data->unhideDurationMs;
        int animMs = data->durationMs;
        if (data->requestedUnhide && unhideMs > animMs &&
            !g_unloading.load(std::memory_order_relaxed)) {
            Sleep(unhideMs - animMs);
        }

        // Perform the delayed minimize with transitions still disabled (instant, no
        // system animation). GenieBypass stops our own DefWindowProcW hook from
        // re-triggering the genie. Hardened with IsWindow guards (HIS's unhide path
        // could race a closed window / silent SetForegroundWindow failure).
        if (!g_unloading.load(std::memory_order_relaxed) && IsWindow(data->hRealWnd)) {
            SetPropW(data->hRealWnd, L"GenieBypass", (HANDLE)1);
            SendMessageTimeoutW(data->hRealWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0, SMTO_NORMAL, 1000, NULL);
            RemovePropW(data->hRealWnd, L"GenieBypass");
        }
        // Always uncloak so we never leave an invisible, non-minimized window, then
        // re-enable transitions for future system animations.
        if (IsWindow(data->hRealWnd)) {
            MacGenieSetCloak(data->hRealWnd, FALSE);
            MacGenieSetDwmTransitions(data->hRealWnd, TRUE);
        }
        // Shift focus so the taskbar slides back down natively.
        if (data->requestedUnhide && !g_unloading.load(std::memory_order_relaxed)) {
            if (data->hNextApp && IsWindow(data->hNextApp) && IsWindowVisible(data->hNextApp)) {
                SetForegroundWindow(data->hNextApp);
            } else {
                SetForegroundWindow(FindWindowW(L"Progman", NULL));
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        g_AnimActive.erase(data->hRealWnd);
    }
    delete data;
    MacGenieUnregisterWork();
    return 0;
}

// -------------------------------------------------------------------------
// CLASSIC engine: the mod's original (v2.2.0) per-output-scanline GDI strip warp,
// kept as an opt-in alternative to Potassiumuncher's Direct2D engine (some users
// preferred it). It consumes the SAME MacGenieAnimData contract the modern engine
// does - snapshot bitmap, extended-frame rect, dock target, first-frame event - and
// runs the SAME teardown (reveal, DWM transitions, auto-hide deferred minimize,
// worker drain), so it drops in behind the identical setup/capture path; only the
// per-frame rendering differs. Selected by the "Animation style" setting.
// -------------------------------------------------------------------------
DWORD WINAPI MacGenieAnimThreadClassic(LPVOID lpParam) {
    MacGenieAnimData* data = (MacGenieAnimData*)lpParam;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    const int W = data->width;
    const int H = data->height;
    // The monitor the genie plays on, in virtual-screen coords (from the shared
    // setup: the primary when multi-monitor support is off, else the window's).
    RECT mon;
    MONITORINFO mmi; mmi.cbSize = sizeof(mmi);
    if (data->hMon && GetMonitorInfoW(data->hMon, &mmi)) {
        mon = mmi.rcMonitor;
    } else {
        mon.left = 0; mon.top = 0;
        mon.right = GetSystemMetrics(SM_CXSCREEN);
        mon.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    const int origLeft = data->targetRect.left;
    const int origTop  = data->targetRect.top;
    const float origCenterX = (float)origLeft + W * 0.5f;

    // Where the genie funnels to: the learned taskbar icon X, at the bottom of the
    // monitor the genie plays on.
    int dockX = data->targetDockX;
    if (dockX < mon.left) dockX = mon.left;
    if (dockX > mon.right) dockX = mon.right;
    const float dockXf = (float)dockX;
    const float dockY  = (float)mon.bottom;
    float neckW = W * 0.03f;
    if (neckW < 12.0f) neckW = 12.0f;
    if (neckW > 60.0f) neckW = 60.0f;

    // Bounding box the funnel can occupy.
    int boundLeft   = (origLeft < dockX ? origLeft : dockX) - W / 2;
    int boundRight  = ((origLeft + W) > dockX ? (origLeft + W) : dockX) + W / 2;
    int boundTop    = origTop;
    int boundBottom = mon.bottom;
    if (boundLeft < mon.left) boundLeft = mon.left;
    if (boundRight > mon.right) boundRight = mon.right;
    if (boundTop < mon.top) boundTop = mon.top;
    int boundW = boundRight - boundLeft;
    int boundH = boundBottom - boundTop;
    if (boundW < 1) boundW = 1;
    if (boundH < 1) boundH = 1;

    // Layered "ghost" window (plain STATIC class - no custom WndProc, so no clash
    // with the modern engine's ghost class). Created hidden, shown after frame 1.
    HWND hGhost = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
        L"STATIC", NULL, WS_POPUP,
        boundLeft, boundTop, boundW, boundH,
        NULL, NULL, NULL, NULL);

    HDC hScreenDC = GetDC(NULL);

    // Source pixels as a readable 32-bit top-down DIB.
    HDC hSrcDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hOldSrc = (HBITMAP)SelectObject(hSrcDC, data->hBitmap);

    BITMAPINFO sbmi;
    ZeroMemory(&sbmi, sizeof(sbmi));
    sbmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    sbmi.bmiHeader.biWidth = W;
    sbmi.bmiHeader.biHeight = -H;       // negative height = top-down
    sbmi.bmiHeader.biPlanes = 1;
    sbmi.bmiHeader.biBitCount = 32;
    sbmi.bmiHeader.biCompression = BI_RGB;
    BYTE* srcBits = NULL;
    HBITMAP hSrcDib = CreateDIBSection(hScreenDC, &sbmi, DIB_RGB_COLORS, (void**)&srcBits, NULL, 0);
    HDC hSrcDibDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hOldSrcDib = (HBITMAP)SelectObject(hSrcDibDC, hSrcDib);
    BitBlt(hSrcDibDC, 0, 0, W, H, hSrcDC, 0, 0, SRCCOPY);
    GdiFlush();
    const int srcStride = W * 4;

    // Canvas: 32-bit top-down DIB for genuine per-pixel alpha.
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = boundW;
    bmi.bmiHeader.biHeight = -boundH;   // negative height = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    BYTE* pBits = NULL;
    HBITMAP hCanvas = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, (void**)&pBits, NULL, 0);
    HDC hCanvasDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hOldCanvas = (HBITMAP)SelectObject(hCanvasDC, hCanvas);
    const int canvasStride = boundW * 4;

    const float SPREAD = 0.65f;   // lower rows lead the morph (bottom-first neck)
    const size_t canvasBytes = (size_t)boundW * 4 * boundH;

    const double totalMs = (double)data->durationMs;

    auto morphAt = [&](float v, float tt) -> float {
        float m = tt * (1.0f + SPREAD) - (1.0f - v) * SPREAD;
        if (m < 0.0f) m = 0.0f;
        if (m > 1.0f) m = 1.0f;
        return m * m * (3.0f - 2.0f * m);
    };

    float* yb = new float[H + 1];

    // Time-based progress paced by a short unload-aware frame wait.
    LARGE_INTEGER qpcFreq, qpcStart, qpcNow;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcStart);

    BOOL firstFrame = TRUE;
    for (;;) {
        QueryPerformanceCounter(&qpcNow);
        double elapsedMs = (qpcNow.QuadPart - qpcStart.QuadPart) * 1000.0 / qpcFreq.QuadPart;
        BOOL lastFrame = (elapsedMs >= totalMs);
        float progress = lastFrame ? 1.0f : (float)(elapsedMs / totalMs);

        // 0 = full window at its place, 1 = collapsed at the dock; restore reverses.
        float tt = data->isRising ? (1.0f - progress) : progress;

        float fade = 1.0f;
        if (tt > 0.8f) fade = (1.0f - tt) / 0.2f;
        if (fade < 0.0f) fade = 0.0f;
        if (fade > 1.0f) fade = 1.0f;

        memset(pBits, 0, canvasBytes);

        // Vertical map: where each source row lands on screen this frame.
        for (int k = 0; k <= H; ++k) {
            float v = (float)k / (float)H;
            float e = morphAt(v, tt);
            float idY = (float)origTop + (float)H * v;
            yb[k] = idY + (dockY - idY) * e;
        }

        int kSeg = 0;
        for (int yC = 0; yC < boundH; ++yC) {
            float screenY = (float)(yC + boundTop) + 0.5f;
            if (screenY < yb[0] || screenY >= yb[H]) continue;

            while (kSeg < H - 1 && yb[kSeg + 1] <= screenY) kSeg++;
            float segH = yb[kSeg + 1] - yb[kSeg];
            float frac = segH > 1e-4f ? (screenY - yb[kSeg]) / segH : 0.0f;
            float v = ((float)kSeg + frac) / (float)H;

            float em = morphAt(v, tt);
            float width = (float)W + (neckW - (float)W) * em;
            if (width < 1.0f) width = 1.0f;
            float cx = origCenterX + (dockXf - origCenterX) * em;
            float leftCanvas = (cx - width * 0.5f) - (float)boundLeft;

            int srcRow = (int)(v * (float)H);
            if (srcRow < 0) srcRow = 0;
            if (srcRow > H - 1) srcRow = H - 1;
            const BYTE* srcRowPtr = srcBits + (size_t)srcRow * srcStride;
            BYTE* dstRowPtr = pBits + (size_t)yC * canvasStride;

            int xStart = (int)leftCanvas;
            int xEnd   = (int)(leftCanvas + width) + 1;
            if (xStart < 0) xStart = 0;
            if (xEnd > boundW) xEnd = boundW;

            float invW = 1.0f / width;
            for (int xC = xStart; xC < xEnd; ++xC) {
                float u = ((float)xC + 0.5f - leftCanvas) * invW;
                if (u < 0.0f || u >= 1.0f) continue;
                int srcX = (int)(u * (float)W);
                if (srcX < 0) srcX = 0;
                if (srcX > W - 1) srcX = W - 1;
                const BYTE* sp = srcRowPtr + (size_t)srcX * 4;
                BYTE* dp = dstRowPtr + (size_t)xC * 4;
                dp[0] = sp[0];
                dp[1] = sp[1];
                dp[2] = sp[2];
                dp[3] = 255;      // opaque; the constant alpha below applies the fade
            }
        }

        POINT ptDst = { boundLeft, boundTop };
        SIZE  sz    = { boundW, boundH };
        POINT ptSrc = { 0, 0 };
        BLENDFUNCTION bf;
        bf.BlendOp = AC_SRC_OVER;
        bf.BlendFlags = 0;
        bf.SourceConstantAlpha = (BYTE)(255.0f * fade);
        bf.AlphaFormat = AC_SRC_ALPHA;
        UpdateLayeredWindow(hGhost, hScreenDC, &ptDst, &sz, hCanvasDC, &ptSrc, 0, &bf, ULW_ALPHA);

        if (firstFrame) {
            ShowWindow(hGhost, SW_SHOWNOACTIVATE);
        }

        if (lastFrame) break;
        if (g_unloading.load(std::memory_order_relaxed)) break;

        if (!WaitForNextAnimationFrame()) break;

        if (firstFrame) {
            firstFrame = FALSE;
            // First frame is on screen; release the minimize hook it was holding.
            if (data->hFirstFrameShown) SetEvent(data->hFirstFrameShown);
        }
    }

    // -------------------- COMMON TEARDOWN (identical to the modern engine) -----
    if (data->isRising) {
        // Reveal the real window the same way it was hidden.
        if (data->hiddenByCloak) {
            MacGenieSetCloak(data->hRealWnd, FALSE);
        } else {
            SetLayeredWindowAttributes(data->hRealWnd, 0, 255, LWA_ALPHA);
            if (!(data->originalExStyle & WS_EX_LAYERED)) {
                SetWindowLongPtrW(data->hRealWnd, GWL_EXSTYLE, data->originalExStyle);
            }
        }
    }

    // The auto-hide deferred path keeps transitions disabled through the real
    // (deferred) minimize below; every other path re-enables them now.
    if (!data->deferredMinimize) {
        MacGenieSetDwmTransitions(data->hRealWnd, TRUE);
    }

    delete[] yb;
    SelectObject(hCanvasDC, hOldCanvas);
    SelectObject(hSrcDibDC, hOldSrcDib);
    SelectObject(hSrcDC, hOldSrc);
    DeleteObject(hCanvas);
    DeleteObject(hSrcDib);
    DeleteObject(data->hBitmap);
    DeleteDC(hCanvasDC);
    DeleteDC(hSrcDibDC);
    DeleteDC(hSrcDC);
    ReleaseDC(NULL, hScreenDC);
    DestroyWindow(hGhost);

    if (data->hFirstFrameShown) {
        SetEvent(data->hFirstFrameShown);
        CloseHandle(data->hFirstFrameShown);
    }

    // Auto-hide "unhide" completion (Potassiumuncher's engine) - same as the modern
    // thread, so the auto-hide feature behaves identically under the classic style.
    if (data->deferredMinimize) {
        int unhideMs = data->unhideDurationMs;
        int animMs = data->durationMs;
        if (data->requestedUnhide && unhideMs > animMs &&
            !g_unloading.load(std::memory_order_relaxed)) {
            Sleep(unhideMs - animMs);
        }
        if (!g_unloading.load(std::memory_order_relaxed) && IsWindow(data->hRealWnd)) {
            SetPropW(data->hRealWnd, L"GenieBypass", (HANDLE)1);
            SendMessageTimeoutW(data->hRealWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0, SMTO_NORMAL, 1000, NULL);
            RemovePropW(data->hRealWnd, L"GenieBypass");
        }
        if (IsWindow(data->hRealWnd)) {
            MacGenieSetCloak(data->hRealWnd, FALSE);
            MacGenieSetDwmTransitions(data->hRealWnd, TRUE);
        }
        if (data->requestedUnhide && !g_unloading.load(std::memory_order_relaxed)) {
            if (data->hNextApp && IsWindow(data->hNextApp) && IsWindowVisible(data->hNextApp)) {
                SetForegroundWindow(data->hNextApp);
            } else {
                SetForegroundWindow(FindWindowW(L"Progman", NULL));
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        g_AnimActive.erase(data->hRealWnd);
    }
    delete data;
    MacGenieUnregisterWork();
    return 0;
}

// -------------------------------------------------------------------------
// Core Setup Engine & Smart Tracking Logic
// -------------------------------------------------------------------------
// Returns TRUE if the worker thread was spawned (the animation is now in flight).
// `originalExStyle` is the window's extended style captured before hiding.
// Launch and restore both use DWM cloak; the layered branch remains only as a
// fallback for older internal rising callers. The deferred* args drive the auto-hide
// "unhide" path (falling only): the caller cloaks the window after our first frame
// and we perform the real minimize at teardown.
bool StartMacGenieAnim(HWND hWnd, BOOL rising, LONG_PTR originalExStyle,
                       BOOL cloakHidden = FALSE, BOOL deferredMinimize = FALSE,
                       BOOL requestedUnhide = FALSE, HWND hNextApp = NULL) {
    if (!MacGenieRegisterWork()) {
        if (rising) MacGenieUndoRisingHide(hWnd, originalExStyle, cloakHidden);
        else MacGenieSetDwmTransitions(hWnd, TRUE);
        return false;
    }
    auto AbortCall = [&]() -> bool {
        if (rising) MacGenieUndoRisingHide(hWnd, originalExStyle, cloakHidden);
        else MacGenieSetDwmTransitions(hWnd, TRUE);
        MacGenieUnregisterWork();
        return false;
    };

    // Animation geometry via DWM extended frame bounds (Goal D / bug #4670):
    // GetWindowRect returns the legacy frame (invisible resize borders / shadow),
    // which is offset a few px from where the pixels actually are on composited
    // windows. Snapshot AND warp both use the extended-frame rect, so keyboard /
    // AutoHotkey minimizes are pixel-aligned. Scheme from Potassiumuncher's engine.
    RECT winRect;
    if (!GetWindowRect(hWnd, &winRect)) {
        return AbortCall();
    }
    RECT rect = winRect, extRect;
    if (SUCCEEDED(DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &extRect, sizeof(extRect)))) {
        rect = extRect;
    }
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    int offsetX = rect.left - winRect.left;
    int offsetY = rect.top - winRect.top;
    int rawW = winRect.right - winRect.left;
    int rawH = winRect.bottom - winRect.top;

    if (w <= 0 || h <= 0 || rawW <= 0 || rawH <= 0) {
        return AbortCall();
    }

    // One genie per window at a time (MINE). Any window we animate also counts as
    // "seen", so the launch path won't fire on it later. Refused while unloading.
    bool blocked = g_unloading.load(std::memory_order_relaxed);
    if (!blocked) {
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        blocked = !g_AnimActive.insert(hWnd).second;
        if (!blocked) g_LaunchSeen.insert(hWnd);
    }
    if (blocked) {
        if (rising) {
            MacGenieUndoRisingHide(hWnd, originalExStyle, cloakHidden);
        } else {
            bool owned;
            {
                std::lock_guard<std::mutex> lock(g_CacheMutex);
                owned = g_AnimActive.count(hWnd) != 0;
            }
            if (!owned) MacGenieSetDwmTransitions(hWnd, TRUE);
        }
        MacGenieUnregisterWork();
        return false;
    }

    // Monitor the genie targets. Multi-monitor support (experimental) targets the
    // window's monitor; otherwise the primary, which reproduces the old behavior.
    bool multiMon = g_multiMonitor.load(std::memory_order_relaxed);
    HMONITOR hMon = multiMon
        ? MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) {
        mi.rcMonitor.left = 0; mi.rcMonitor.top = 0;
        mi.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
        mi.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;

    // Default dock target before UIA (Potassiumuncher's engine): left-aligned
    // taskbars start near the monitor's left, centered ones at its middle.
    DWORD alignVal = 1, dataSize = sizeof(alignVal);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                 L"TaskbarAl", RRF_RT_REG_DWORD, NULL, &alignVal, &dataSize);
    int learnedTargetX = (alignVal == 0) ? (mi.rcMonitor.left + 160)
                                         : (mi.rcMonitor.left + monWidth / 2);

    // --- Per-window targeting ---
    // UI Automation (GetTaskbarButtonX) matches taskbar buttons by title / process
    // name, which can't reliably tell apart several windows of the SAME process
    // (e.g. multiple Brave profiles): they tie on the process-name score and it
    // resolves them all to the first-opened window's button. So MINE's cursor +
    // per-window signal takes priority; UIA is only a fallback for a window we've
    // never seen minimized from the taskbar (frame 0 of the warp is the identity,
    // so targetDockX is only needed for later frames).
    //
    // 1. Cursor over the taskbar = the user clicked THIS window's own taskbar
    //    button, so its X is exactly the icon - the only fully reliable per-window
    //    signal when windows share a process. Learn it, keyed by HWND (never shared
    //    across sibling windows).
    POINT pt;
    GetCursorPos(&pt);
    RECT workArea;
    MONITORINFO cursorMi;
    cursorMi.cbSize = sizeof(cursorMi);
    if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &cursorMi)) {
        workArea = cursorMi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    }

    if (!PtInRect(&workArea, pt)) {
        learnedTargetX = pt.x;                        // steal the click X
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        g_IconPositions[hWnd] = learnedTargetX;       // remember it for this window
    } else {
        // 2. Title-bar / keyboard minimize: locate the taskbar button via UI
        //    Automation EVERY time (Potassiumuncher's targeting). Reusing a
        //    per-window cache here made the button-minimize land past the icon when
        //    the cached spot was stale; running UIA fresh lands it on the icon.
        //    GetTaskbarButtonX still falls back to the per-window / per-process cache
        //    (including a position learned from an earlier taskbar click) internally
        //    if UIA can't match, so nothing is lost.
        learnedTargetX = GetTaskbarButtonX(hWnd, learnedTargetX, hMon);
    }

    MacGenieAnimData* data = new MacGenieAnimData();
    data->hRealWnd = hWnd;
    data->targetRect = rect;
    data->hMon = hMon;
    data->width = w;
    data->height = h;
    data->isRising = rising;
    data->targetDockX = learnedTargetX;
    data->originalExStyle = originalExStyle;
    data->hiddenByCloak = cloakHidden;
    data->hFirstFrameShown = NULL;
    data->durationMs = g_durationMs.load(std::memory_order_relaxed);
    data->requestedUnhide = requestedUnhide;
    data->hNextApp = hNextApp;
    data->unhideDurationMs = g_unhideDurationMs.load(std::memory_order_relaxed);
    data->deferredMinimize = deferredMinimize;
    data->pBits = nullptr;
    data->hBitmap = NULL;

    HDC hScreenDC = GetDC(NULL);

    auto AbortSetup = [&]() -> bool {
        if (hScreenDC) ReleaseDC(NULL, hScreenDC);
        if (rising) MacGenieUndoRisingHide(hWnd, originalExStyle, cloakHidden);
        else MacGenieSetDwmTransitions(hWnd, TRUE);
        if (data->hBitmap) DeleteObject(data->hBitmap);
        delete data;
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        g_AnimActive.erase(hWnd);
        MacGenieUnregisterWork();
        return false;
    };

    // Premultiplied 32-bit top-down DIB - D2D's CreateBitmap reads pBits directly.
    BITMAPINFO bmi = {{0}};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    data->hBitmap = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, &data->pBits, NULL, 0);

    if (!data->hBitmap || !data->pBits) {
        return AbortSetup();
    }

    // Rising-no-cache fallback only: copy a rawW x rawH PrintWindow capture into the
    // w x h snapshot at the extended-frame offset, forcing every pixel OPAQUE (GDI
    // gives no reliable alpha; the rounded-rect D2D mask supplies clean corners).
    auto CopyOpaque = [&](void* srcBits) {
        DWORD* src = (DWORD*)srcBits;
        DWORD* dst = (DWORD*)data->pBits;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int sx = x + offsetX;
                int sy = y + offsetY;
                if (sx >= 0 && sx < rawW && sy >= 0 && sy < rawH) {
                    dst[y * w + x] = 0xFF000000u | (src[sy * rawW + sx] & 0x00FFFFFFu);
                } else {
                    dst[y * w + x] = 0;
                }
            }
        }
    };

    // Rising-no-cache fallback: grab the (cloaked, off-screen-invisible) window into
    // a rawW x rawH DIB via PrintWindow, then CopyOpaque into the extended-frame
    // snapshot. Only used when there's no cached minimize snapshot to restore from -
    // the window isn't on screen to BitBlt, so PrintWindow is the only option here.
    auto CaptureNow = [&]() -> bool {
        HDC hTempDC = CreateCompatibleDC(hScreenDC);
        if (!hTempDC) return false;
        BITMAPINFO bmiTemp = bmi;
        bmiTemp.bmiHeader.biWidth = rawW;
        bmiTemp.bmiHeader.biHeight = -rawH;
        void* pTempBits = nullptr;
        HBITMAP hTempBmp = CreateDIBSection(hScreenDC, &bmiTemp, DIB_RGB_COLORS, &pTempBits, NULL, 0);
        if (!hTempBmp || !pTempBits) {
            if (hTempBmp) DeleteObject(hTempBmp);
            DeleteDC(hTempDC);
            return false;
        }
        HBITMAP hOldTempBmp = (HBITMAP)SelectObject(hTempDC, hTempBmp);
        BOOL captured = PrintWindow(hWnd, hTempDC, PW_RENDERFULLCONTENT);
        if (!captured) {
            SelectObject(hTempDC, hOldTempBmp);
            DeleteObject(hTempBmp);
            DeleteDC(hTempDC);
            return false;
        }
        GdiFlush();
        CopyOpaque(pTempBits);
        SelectObject(hTempDC, hOldTempBmp);
        DeleteObject(hTempBmp);
        DeleteDC(hTempDC);
        return true;
    };

    if (rising) {
        BOOL fromCache = FALSE;
        {
            std::lock_guard<std::mutex> lock(g_CacheMutex);
            auto it = g_SnapshotCache.find(hWnd);
            if (it != g_SnapshotCache.end()) {
                SnapCache& c = it->second;
                if (c.w == w && c.h == h) {
                    memcpy(data->pBits, c.pBits, (size_t)w * h * 4);
                    fromCache = TRUE;
                }
                DeleteObject(c.hBmp);
                g_SnapshotCache.erase(it);
            }
        }
        // No cached minimize snapshot (e.g. window was already minimized before the
        // mod loaded): snapshot the freshly-restored (cloaked) window directly.
        if (!fromCache && !CaptureNow()) {
            return AbortSetup();
        }
    } else {
        // Falling: capture the WINDOW ONLY via PrintWindow (Potassiumuncher's
        // method), NOT a screen BitBlt. A screen grab of the window rect also pulls
        // in whatever is behind / around the window - most visibly the taskbar on
        // maximized / fullscreen apps, which then warps inside the genie. PrintWindow
        // renders just the window's own content, so the taskbar (a separate window)
        // can never bleed in. Tradeoff: DWM acrylic/translucency backdrops aren't
        // part of the window's own render, so translucent windows animate opaque - a
        // documented limitation, far less jarring than the taskbar appearing.
        if (!CaptureNow()) {
            return AbortSetup();
        }
        // Cache a copy keyed by HWND for the later restore animation.
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        auto it = g_SnapshotCache.find(hWnd);
        if (it != g_SnapshotCache.end()) {
            DeleteObject(it->second.hBmp);
            g_SnapshotCache.erase(it);
        }
        void* pCacheBits = nullptr;
        HBITMAP hCacheBmp = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, &pCacheBits, NULL, 0);
        if (hCacheBmp && pCacheBits) {
            memcpy(pCacheBits, data->pBits, (size_t)w * h * 4);
            g_SnapshotCache[hWnd] = { hCacheBmp, pCacheBits, w, h };
        } else if (hCacheBmp) {
            DeleteObject(hCacheBmp);
        }
    }

    ReleaseDC(NULL, hScreenDC);

    // For a minimize (normal OR deferred) hold the caller back until the ghost's
    // first frame is composed - otherwise the window (or, on the deferred path, the
    // cloak-hide) would happen a few frames before the genie appears (a visible
    // gap, especially on power-throttled CPUs). Two handles to one event: the
    // thread owns its duplicate, we own and wait on the original.
    HANDLE hFirstShown = NULL;
    if (!rising) {
        hFirstShown = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (hFirstShown &&
            !DuplicateHandle(GetCurrentProcess(), hFirstShown, GetCurrentProcess(),
                             &data->hFirstFrameShown, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            data->hFirstFrameShown = NULL;
        }
    }
    bool waitForFirstFrame = (data->hFirstFrameShown != NULL);

    if (!MacGenieRegisterWork()) {
        if (hFirstShown) CloseHandle(hFirstShown);
        if (data->hFirstFrameShown) CloseHandle(data->hFirstFrameShown);
        return AbortSetup();
    }
    // Same setup and capture for both styles; only the render thread differs.
    LPTHREAD_START_ROUTINE renderProc =
        g_classicEngine.load(std::memory_order_relaxed) ? MacGenieAnimThreadClassic
                                                        : MacGenieAnimThread;
    HANDLE hThread = CreateThread(NULL, 0, renderProc, data, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
        if (hFirstShown) {
            if (waitForFirstFrame) {
                WaitForSingleObject(hFirstShown, 200);   // capped: worst case = old behavior
            }
            CloseHandle(hFirstShown);
        }
        MacGenieUnregisterWork();
        return true;
    }

    // Thread couldn't start - undo our state so the window isn't left invisible or
    // with transitions permanently disabled.
    MacGenieUnregisterWork();
    if (rising) {
        MacGenieUndoRisingHide(hWnd, data->originalExStyle, data->hiddenByCloak);
    } else {
        MacGenieSetDwmTransitions(hWnd, TRUE);
    }
    if (hFirstShown) CloseHandle(hFirstShown);
    if (data->hFirstFrameShown) CloseHandle(data->hFirstFrameShown);
    DeleteObject(data->hBitmap);
    delete data;
    {
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        g_AnimActive.erase(hWnd);
    }
    MacGenieUnregisterWork();
    return false;
}

// -------------------------------------------------------------------------
// Hooks
// -------------------------------------------------------------------------
DWORD WINAPI MacGenieLaunchThread(LPVOID lpParam);   // defined below

// Arm the launch animation without changing the application's composition mode.
// DWM cloak keeps Chromium/Electron DirectComposition surfaces intact.
static bool MacGenieLaunchPrepare(HWND hWnd, int nCmdShow, LONG_PTR* origExOut) {
    if (g_unloading.load(std::memory_order_relaxed)) return false;
    if (!g_launchAnimation.load(std::memory_order_relaxed)) return false;
    if (nCmdShow != SW_SHOW && nCmdShow != SW_SHOWNORMAL &&
        nCmdShow != SW_SHOWDEFAULT && nCmdShow != SW_SHOWMAXIMIZED) return false;
    if (IsWindowVisible(hWnd) || IsIconic(hWnd)) return false;
    if (!MacGenieIsLaunchWindow(hWnd)) return false;
    if (!MacGenieRegisterWork()) return false;
    {
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        if (!g_LaunchSeen.insert(hWnd).second) {
            MacGenieUnregisterWork();
            return false;
        }
    }
    MacGenieSetDwmTransitions(hWnd, FALSE);
    LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    *origExOut = exStyle;
    if (!MacGenieSetCloak(hWnd, TRUE)) {
        MacGenieSetDwmTransitions(hWnd, TRUE);
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        g_LaunchSeen.erase(hWnd);
        MacGenieUnregisterWork();
        return false;
    }
    return true;
}

// Second half of the launch sequence: spawn the reveal worker (or roll back).
static void MacGenieLaunchCommit(HWND hWnd, LONG_PTR originalExStyle) {
    if (g_unloading.load(std::memory_order_acquire)) {
        MacGenieUndoRisingHide(hWnd, originalExStyle, TRUE);
        MacGenieUnregisterWork();
        return;
    }
    if (!MacGenieRegisterWork()) {
        MacGenieUndoRisingHide(hWnd, originalExStyle, TRUE);
        MacGenieUnregisterWork();
        return;
    }
    MacGenieLaunchData* ld = new MacGenieLaunchData();
    ld->hWnd = hWnd;
    ld->originalExStyle = originalExStyle;
    HANDLE h = CreateThread(NULL, 0, MacGenieLaunchThread, ld, 0, NULL);
    if (h) {
        CloseHandle(h);
        MacGenieUnregisterWork();
    } else {
        MacGenieUnregisterWork();
        delete ld;
        MacGenieUndoRisingHide(hWnd, originalExStyle, TRUE);
        MacGenieUnregisterWork();
    }
}

BOOL WINAPI ShowWindow_Hook(HWND hWnd, int nCmdShow) {
    if (nCmdShow == SW_MINIMIZE || nCmdShow == SW_SHOWMINIMIZED || nCmdShow == SW_SHOWMINNOACTIVE) {
        if (MacGenieShouldAnimate(hWnd)) {
            MacGenieSetDwmTransitions(hWnd, FALSE);
            StartMacGenieAnim(hWnd, FALSE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
        }
        return ShowWindow_Original(hWnd, nCmdShow);
    }

    if ((nCmdShow == SW_RESTORE || nCmdShow == SW_SHOWNORMAL) && IsIconic(hWnd)) {
        if (g_openAnimation.load(std::memory_order_relaxed) && MacGenieShouldAnimate(hWnd)) {
            MacGenieSetDwmTransitions(hWnd, FALSE);
            // Cloak (frame-level hide, no layered-surface rebuild flash) - MINE.
            MacGenieSetCloak(hWnd, TRUE);
            BOOL res = ShowWindow_Original(hWnd, nCmdShow);
            StartMacGenieAnim(hWnd, TRUE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE), TRUE);
            return res;
        }
        return ShowWindow_Original(hWnd, nCmdShow);
    }

    LONG_PTR launchOrigEx;
    if (MacGenieLaunchPrepare(hWnd, nCmdShow, &launchOrigEx)) {
        BOOL res = ShowWindow_Original(hWnd, nCmdShow);
        MacGenieLaunchCommit(hWnd, launchOrigEx);
        return res;
    }

    return ShowWindow_Original(hWnd, nCmdShow);
}

LRESULT WINAPI DefWindowProcW_Hook(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
    if (Msg == WM_DESTROY) {
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        auto it = g_SnapshotCache.find(hWnd);
        if (it != g_SnapshotCache.end()) {
            DeleteObject(it->second.hBmp);
            g_SnapshotCache.erase(it);
        }
        g_IconPositions.erase(hWnd);
        g_LaunchSeen.erase(hWnd);
    }

    if (Msg == WM_SYSCOMMAND) {
        UINT cmd = wParam & 0xFFF0;
        if (cmd == SC_MINIMIZE) {
            // Our own deferred minimize (auto-hide path) re-enters here via
            // SendMessageTimeoutW - let it pass straight through, no recursion.
            if (GetPropW(hWnd, L"GenieBypass")) {
                return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);
            }
            if (MacGenieShouldAnimate(hWnd)) {
                // Auto-hide taskbar? Reveal it, defer the real minimize until the
                // animation finishes so it doesn't slide away mid-genie.
                // (Potassiumuncher's engine, reconciled with MINE's cloak hide.)
                BOOL requestedUnhide = FALSE;
                HWND hNext = NULL;
                if (g_unhideEnabled.load(std::memory_order_relaxed)) {
                    HWND hTray = FindTaskbarForMonitor(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST));
                    if (hTray) {
                        APPBARDATA abd = { sizeof(APPBARDATA) };
                        abd.hWnd = hTray;
                        UINT uState = (UINT)SHAppBarMessage(ABM_GETSTATE, &abd);
                        if (uState & ABS_AUTOHIDE) {
                            requestedUnhide = TRUE;
                            HWND hwndIter = GetWindow(hWnd, GW_HWNDNEXT);
                            while (hwndIter) {
                                if (IsWindowVisible(hwndIter) && !IsIconic(hwndIter) &&
                                    GetAncestor(hwndIter, GA_ROOT) == hwndIter &&
                                    GetWindowTextLengthW(hwndIter) > 0 &&
                                    hwndIter != hWnd && hwndIter != hTray) {
                                    DWORD exStyle = GetWindowLongPtrW(hwndIter, GWL_EXSTYLE);
                                    if (!(exStyle & WS_EX_TOOLWINDOW)) { hNext = hwndIter; break; }
                                }
                                hwndIter = GetWindow(hwndIter, GW_HWNDNEXT);
                            }
                            SetForegroundWindow(hTray);
                        }
                    }
                }

                MacGenieSetDwmTransitions(hWnd, FALSE);
                if (requestedUnhide) {
                    // Deferred path: animate, then (in the worker) cloak-hide is
                    // applied by us here after the first frame, and the worker does
                    // the real minimize + focus juggle at the end.
                    bool ok = StartMacGenieAnim(hWnd, FALSE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE),
                                                FALSE, TRUE, TRUE, hNext);
                    if (ok) {
                        // First frame is up (StartMacGenieAnim waited): hide the real
                        // window with a flash-free cloak; worker minimizes it later.
                        MacGenieSetCloak(hWnd, TRUE);
                        return 0;
                    }
                    // Spawn failed: fall back to a normal immediate minimize.
                    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);
                }
                // Normal minimize (MINE): first-frame gate, then real minimize.
                StartMacGenieAnim(hWnd, FALSE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
            }
            return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);
        }
        else if (cmd == SC_RESTORE && IsIconic(hWnd) &&
                 g_openAnimation.load(std::memory_order_relaxed) &&
                 MacGenieShouldAnimate(hWnd)) {
            MacGenieSetDwmTransitions(hWnd, FALSE);
            MacGenieSetCloak(hWnd, TRUE);
            LRESULT res = DefWindowProcW_Original(hWnd, Msg, wParam, lParam);
            StartMacGenieAnim(hWnd, TRUE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE), TRUE);
            return res;
        }
    }
    return DefWindowProcW_Original(hWnd, Msg, wParam, lParam);
}

// Custom-title-bar apps (Zed/GPUI, some Electron) minimize via ShowWindowAsync,
// SetWindowPlacement, or CloseWindow. Shared minimize kick-off for those paths (no
// unhide handling - that lives on the SC_MINIMIZE path only).
static void MacGenieTryMinimizeAnim(HWND hWnd) {
    if (!IsWindowVisible(hWnd) || IsIconic(hWnd)) return;
    if (!MacGenieShouldAnimate(hWnd)) return;
    MacGenieSetDwmTransitions(hWnd, FALSE);
    StartMacGenieAnim(hWnd, FALSE, GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
}

BOOL WINAPI ShowWindowAsync_Hook(HWND hWnd, int nCmdShow) {
    if (nCmdShow == SW_MINIMIZE || nCmdShow == SW_SHOWMINIMIZED ||
        nCmdShow == SW_SHOWMINNOACTIVE) {
        MacGenieTryMinimizeAnim(hWnd);
    } else {
        LONG_PTR launchOrigEx;
        if (MacGenieLaunchPrepare(hWnd, nCmdShow, &launchOrigEx)) {
            BOOL res = ShowWindowAsync_Original(hWnd, nCmdShow);
            MacGenieLaunchCommit(hWnd, launchOrigEx);
            return res;
        }
    }
    return ShowWindowAsync_Original(hWnd, nCmdShow);
}

BOOL WINAPI SetWindowPlacement_Hook(HWND hWnd, const WINDOWPLACEMENT* lpwndpl) {
    if (lpwndpl && (lpwndpl->showCmd == SW_MINIMIZE ||
                    lpwndpl->showCmd == SW_SHOWMINIMIZED ||
                    lpwndpl->showCmd == SW_SHOWMINNOACTIVE)) {
        MacGenieTryMinimizeAnim(hWnd);
    }
    return SetWindowPlacement_Original(hWnd, lpwndpl);
}

BOOL WINAPI CloseWindow_Hook(HWND hWnd) {
    MacGenieTryMinimizeAnim(hWnd);
    return CloseWindow_Original(hWnd);
}

// Store / UWP apps show their first window via SetWindowPos + SWP_SHOWWINDOW.
BOOL WINAPI SetWindowPos_Hook(HWND hWnd, HWND hWndInsertAfter, int X, int Y,
                              int cx, int cy, UINT uFlags) {
    if (uFlags & SWP_SHOWWINDOW) {
        LONG_PTR launchOrigEx;
        if (MacGenieLaunchPrepare(hWnd, SW_SHOW, &launchOrigEx)) {
            BOOL res = SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
            MacGenieLaunchCommit(hWnd, launchOrigEx);
            return res;
        }
    }
    return SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

// -------------------------------------------------------------------------
// App-launch animation (experimental)
// -------------------------------------------------------------------------
DWORD WINAPI MacGenieLaunchThread(LPVOID lpParam) {
    MacGenieLaunchData* ld = (MacGenieLaunchData*)lpParam;
    HWND hWnd = ld->hWnd;
    LONG_PTR originalExStyle = ld->originalExStyle;
    delete ld;

    // Let the freshly-shown, composition-safe cloaked window paint once.
    if (!g_unloadEvent ||
        WaitForSingleObject(g_unloadEvent, 60) != WAIT_TIMEOUT) {
        if (IsWindow(hWnd)) {
            MacGenieUndoRisingHide(hWnd, originalExStyle, TRUE);
        } else {
            MacGenieSetCloak(hWnd, FALSE);
        }
        MacGenieUnregisterWork();
        return 0;
    }

    if (g_unloading.load(std::memory_order_relaxed) ||
        !IsWindow(hWnd) || IsIconic(hWnd) || !IsWindowVisible(hWnd)) {
        if (IsWindow(hWnd)) {
            MacGenieUndoRisingHide(hWnd, originalExStyle, TRUE);
        } else {
            MacGenieSetCloak(hWnd, FALSE);
        }
        MacGenieUnregisterWork();
        return 0;
    }

    // Rising genie; launch and restore use the same composition-safe cloak path.
    StartMacGenieAnim(hWnd, TRUE, originalExStyle, TRUE);
    MacGenieUnregisterWork();
    return 0;
}

BOOL Wh_ModInit() {
    g_unloading.store(false, std::memory_order_release);
    g_unloadEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_unloadEvent) return FALSE;

    MacGenieLoadSettings();

    // Direct2D factory for the genie renderer (Potassiumuncher's engine). Released
    // in Wh_ModUninit. If it fails, workers skip the animation gracefully.
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
                                   __uuidof(ID2D1Factory),
                                   reinterpret_cast<void**>(&g_d2dFactory));
    if (FAILED(hr)) g_d2dFactory = nullptr;

    Wh_SetFunctionHook((void*)DefWindowProcW, (void*)DefWindowProcW_Hook, (void**)&DefWindowProcW_Original);
    Wh_SetFunctionHook((void*)ShowWindow, (void*)ShowWindow_Hook, (void**)&ShowWindow_Original);
    Wh_SetFunctionHook((void*)ShowWindowAsync, (void*)ShowWindowAsync_Hook, (void**)&ShowWindowAsync_Original);
    Wh_SetFunctionHook((void*)SetWindowPlacement, (void*)SetWindowPlacement_Hook, (void**)&SetWindowPlacement_Original);
    Wh_SetFunctionHook((void*)CloseWindow, (void*)CloseWindow_Hook, (void**)&CloseWindow_Original);
    Wh_SetFunctionHook((void*)SetWindowPos, (void*)SetWindowPos_Hook, (void**)&SetWindowPos_Original);
    return TRUE;
}

void Wh_ModSettingsChanged() {
    MacGenieLoadSettings();
}

// Lume unmaps the mod DLL right after Wh_ModUninit returns, so any worker
// thread still running mod code would crash its host. Signal workers to abort and
// wait for them to drain before any shared renderer or cache state is released.
void Wh_ModBeforeUninit() {
    {
        std::lock_guard<std::mutex> lock(g_WorkerGateMutex);
        g_unloading.store(true, std::memory_order_release);
    }
    if (g_unloadEvent) SetEvent(g_unloadEvent);
    MacGenieRestoreAllCloakedWindows();
    while (g_workerCount.load(std::memory_order_acquire) > 0) {
        Sleep(1);
    }
    while (MacGenieRestoreAllCloakedWindows() > 0) {
        Sleep(1);
    }
}

void Wh_ModUninit() {
    if (g_d2dFactory) {
        g_d2dFactory->Release();
        g_d2dFactory = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_CacheMutex);
        for (auto& pair : g_SnapshotCache) {
            DeleteObject(pair.second.hBmp);
        }
        g_SnapshotCache.clear();
        g_IconPositions.clear();
        g_ProcessIconPositions.clear();
        g_LaunchSeen.clear();
    }
    if (g_unloadEvent) {
        CloseHandle(g_unloadEvent);
        g_unloadEvent = NULL;
    }
}
