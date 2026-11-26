#pragma once
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/shell_surface.h"
#include "platform/windows/widget_ui/desktop_peek.h"
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <cwchar>
#include <vector>

namespace lume::platform::windows::window_layer {
enum class Kind { Desktop, Popup, Editor, ExplicitTool, Island };
struct Context {
    HWND foreground = nullptr;
    bool foreignForeground = false;
    bool allDisplays = false;
    bool exclusive = false;
    std::vector<RECT> fullscreen;
    // Cached with the foreground policy, independently of fullscreen hiding.
    // Used only to suspend invisible desktop content, never to hide overlays.
    RECT foregroundBounds{};
};
inline bool Covers(const RECT& window, const RECT& monitor) {
    return window.left <= monitor.left && window.top <= monitor.top &&
           window.right >= monitor.right && window.bottom >= monitor.bottom;
}
inline bool Intersects(const RECT& a, const RECT& b) {
    RECT overlap{}; return IntersectRect(&overlap, &a, &b) != FALSE;
}
inline bool ShellDesktop(HWND window) {
    DWORD pid = 0, shell = 0;
    GetWindowThreadProcessId(window, &pid); GetWindowThreadProcessId(GetShellWindow(), &shell);
    wchar_t name[64]{}; GetClassNameW(window, name, 64);
    return shell && pid == shell && (!wcscmp(name, L"Progman") || !wcscmp(name, L"WorkerW"));
}
using shell_surface::ShellSurfaceImage;
using shell_surface::ExplorerSurfaceClass;
using shell_surface::ShellSurface;
inline bool ExclusiveNotificationState(QUERY_USER_NOTIFICATION_STATE state) {
    return state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
}
inline bool ForegroundCoversMonitor(const RECT& bounds, const RECT& monitor, bool maximized, DWORD style,
                                   QUERY_USER_NOTIFICATION_STATE notification) {
    // Changing taskbar/work-area visibility can give an ordinary maximized
    // window the full monitor rectangle for a few frames. Chromium custom
    // frames may retain THICKFRAME without CAPTION, so account for both.
    // A borderless/POPUP fullscreen can also report IsZoomed: its geometry,
    // and genuine exclusive/presentation state, must continue to protect.
    const bool ordinaryFrame = !(style & WS_POPUP) &&
        ((style & WS_THICKFRAME) || (style & WS_CAPTION) == WS_CAPTION);
    return Covers(bounds, monitor) &&
        (!maximized || !ordinaryFrame || ExclusiveNotificationState(notification));
}
// Keep geometry and notification-state decisions together. Clearing only the
// monitor list for a taskbar thumbnail would let stale QUNS fullscreen state
// immediately turn that same shell foreground into global suppression.
inline void ApplyForegroundProtection(Context& result, bool shellForeground, QUERY_USER_NOTIFICATION_STATE state) {
    result.exclusive = false;
    result.allDisplays = false;
    if (shellForeground) {
        result.fullscreen.clear();
        result.foreignForeground = false;
        result.foregroundBounds = {};
        return;
    }
    if (ExclusiveNotificationState(state)) {
        result.exclusive = true;
        result.allDisplays = result.fullscreen.empty();
    }
}
inline Context Capture() {
    Context result; result.foreground = GetForegroundWindow();
    const HWND foreground = result.foreground;
    QUERY_USER_NOTIFICATION_STATE state = QUNS_ACCEPTS_NOTIFICATIONS;
    if (FAILED(SHQueryUserNotificationState(&state))) state = QUNS_ACCEPTS_NOTIFICATIONS;
    DWORD pid = 0; GetWindowThreadProcessId(foreground, &pid);
    const bool application = foreground && IsWindowVisible(foreground) && !IsIconic(foreground) &&
        pid && pid != GetCurrentProcessId() && !ShellDesktop(foreground);
    result.foreignForeground = application;
    if (application) {
        RECT bounds{};
        DWORD cloaked = 0;
        const bool hidden = SUCCEEDED(DwmGetWindowAttribute(foreground, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
        if (!hidden && (SUCCEEDED(DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) ||
                        GetWindowRect(foreground, &bounds))) {
            const auto style = GetWindowLongPtrW(foreground, GWL_STYLE);
            const auto exStyle = GetWindowLongPtrW(foreground, GWL_EXSTYLE);
            if (!(style & WS_POPUP) && !(exStyle & (WS_EX_LAYERED | WS_EX_TRANSPARENT))) result.foregroundBounds = bounds;
            struct Scan {
                RECT bounds; bool maximized; DWORD style; QUERY_USER_NOTIFICATION_STATE notification;
                std::vector<RECT>* displays;
            } scan{bounds, IsZoomed(foreground) != FALSE,
                   static_cast<DWORD>(GetWindowLongPtrW(foreground, GWL_STYLE)), state, &result.fullscreen};
            EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
                auto& value = *reinterpret_cast<Scan*>(data); MONITORINFO info{sizeof(info)};
                if (GetMonitorInfoW(monitor, &info) && ForegroundCoversMonitor(value.bounds, info.rcMonitor,
                    value.maximized, value.style, value.notification)) value.displays->push_back(info.rcMonitor);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&scan));
        }
    }
    bool shellForeground = ShellDesktop(foreground) || ShellDesktop(GetAncestor(foreground, GA_ROOT));
    // Avoid opening the foreground process on ordinary windowed-app refreshes.
    if (!shellForeground && (!result.fullscreen.empty() || ExclusiveNotificationState(state)))
        shellForeground = ShellSurface(foreground);
    ApplyForegroundProtection(result, shellForeground, state);
    return result;
}
// Global budget for desktop surfaces, set on the watcher's UI thread.
inline thread_local bool balancedIdle=false;
inline thread_local std::function<bool(const Context&)> balancedPolicy;
inline thread_local bool fullscreenProtection=true;
inline bool Protected(const RECT& bounds, const Context& state, bool hideFullscreen = true) {
    return fullscreenProtection && (state.allDisplays || ((hideFullscreen || state.exclusive) &&
        std::any_of(state.fullscreen.begin(), state.fullscreen.end(),
            [&](const RECT& display) { return Intersects(bounds, display); })));
}
inline thread_local Context context;
inline thread_local bool enabled = false, applying = false;
inline thread_local std::function<void(HWND, Kind, bool)> observer;
inline thread_local std::function<HWND()> applicationCeiling;
inline thread_local std::function<bool()> desktopForeground;
// Injection seam for isolated tests; production always reads the actual desktop.
inline thread_local std::function<Context()> contextReader;
inline Context ReadContext() { return contextReader ? contextReader() : Capture(); }
struct Entry {
    Kind kind; HWND owner; bool wanted; bool suppressed; bool pending = false;
    bool aboveApplications = false, hideFullscreen = true;
    bool contentOccluded = false;
    bool fullscreenBypass = false;
};
inline thread_local std::unordered_map<HWND, Entry> windows;
inline const Entry* IslandPolicy(const Entry& entry) {
    const Entry* current=&entry;
    for(unsigned depth=0;depth<8;++depth) {
        if(current->kind==Kind::Island)return current;
        const auto parent=windows.find(current->owner);
        if(parent==windows.end())break;
        current=&parent->second;
    }
    return nullptr;
}
inline bool Overlay(const Entry& entry) {
    const auto* island=IslandPolicy(entry);
    return island && island->aboveApplications;
}
inline bool Suppressed(HWND window) {
    return window && GetPropW(window, lume::window_ids::LayerSuppressedProp) != nullptr;
}
inline bool Registered(HWND window) {
    return window && GetPropW(window, lume::window_ids::LayerRoleProp) != nullptr;
}
inline bool ResidentDesktop(const Entry& entry) {
    return !entry.owner && (entry.kind == Kind::Desktop || (entry.kind == Kind::Island && !Overlay(entry)));
}
inline bool WantsDesktop(HWND window) {
    const auto found = windows.find(window);
    return found != windows.end() && ResidentDesktop(found->second) && found->second.wanted;
}
// Explicit lifecycle hiding (for example a suspended clock) differs from
// Explorer minimizing/hiding a still-enabled resident surface during Win+D.
inline void WantHidden(HWND window) {
    const auto found = windows.find(window);
    if (found != windows.end()) found->second.wanted = false;
}
inline void RestoreDesktop(HWND window) {
    const auto found = windows.find(window);
    if (found == windows.end() || !ResidentDesktop(found->second) || !found->second.wanted ||
        found->second.suppressed || Suppressed(window) || !desktopForeground || !desktopForeground() ||
        (!IsIconic(window) && IsWindowVisible(window))) return;
    // Restore without activation. The module's restack handler immediately
    // places it inside its desktop band, under the same application ceiling.
    // No topmost transition is needed to undo Show Desktop minimization.
    ShowWindow(window, SW_SHOWNOACTIVATE);
}
inline bool DesktopFamily(HWND window) {
    const auto role=reinterpret_cast<ULONG_PTR>(GetPropW(window,lume::window_ids::LayerRoleProp));
    return role==static_cast<ULONG_PTR>(Kind::Desktop)+1 || role==static_cast<ULONG_PTR>(Kind::Popup)+1 ||
        role==static_cast<ULONG_PTR>(Kind::Island)+1;
}
inline bool Blocked(HWND window, const Entry& entry, const Context& state, const RECT* proposed = nullptr) {
    if (!enabled || entry.kind == Kind::ExplicitTool || entry.fullscreenBypass) return false;
    if (entry.owner && (!IsWindow(entry.owner) || Suppressed(entry.owner))) return true;
    if (entry.owner && entry.kind == Kind::Popup && !IsWindowVisible(entry.owner)) return true;
    RECT bounds{}; if(proposed)bounds=*proposed;else GetWindowRect(window, &bounds);
    if (IsRectEmpty(&bounds) && entry.owner) GetWindowRect(entry.owner, &bounds);
    const auto* island=IslandPolicy(entry);
    if (balancedIdle && entry.kind!=Kind::Editor) return true;
    return Protected(bounds,state,island?island->hideFullscreen:true);
}
inline void HideCompanions(HWND owner) {
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        const HWND parent = reinterpret_cast<HWND>(data);
        if (GetPropW(window, lume::window_ids::GlassSurfaceOwnerProp) == parent) {
            // The material worker owns this HWND and checks the owner's current
            // suppression flag before every show; never wait on that thread here.
            PostMessageW(window, lume::window_ids::GlassRestackMessage, 0, 0);
            ShowWindowAsync(window, SW_HIDE);
        } else if (GetPropW(window, lume::window_ids::DesktopCompanionOwnerProp) == parent) {
            ShowWindowAsync(window, SW_HIDE);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(owner));
}
inline UINT RestackMessage() { static const UINT value = RegisterWindowMessageW(L"Lume.DesktopLevel.v1"); return value; }
inline UINT PolicyMessage() { static const UINT value = RegisterWindowMessageW(L"Lume.DisplayPolicy.v1"); return value; }
inline UINT ContentVisibilityMessage() { static const UINT value = RegisterWindowMessageW(L"Lume.ContentVisibility.v1"); return value; }
inline bool ContentOccluded(HWND window) {
    if (!enabled || !context.foreignForeground || IsRectEmpty(&context.foregroundBounds)) return false;
    const auto found = windows.find(window);
    if (found == windows.end()) return false;
    const Entry* entry=&found->second;
    for(unsigned depth=0;entry->owner&&depth<8;++depth) {
        const auto owner=windows.find(entry->owner);if(owner==windows.end())return false;entry=&owner->second;
    }
    if(!ResidentDesktop(*entry)||entry->fullscreenBypass)return false;
    RECT bounds{};
    return GetWindowRect(window, &bounds) && !IsRectEmpty(&bounds) && Covers(context.foregroundBounds, bounds);
}
inline bool ContentVisible(HWND window) {
    return window && IsWindowVisible(window) && !IsIconic(window) && !Suppressed(window) && !ContentOccluded(window);
}
inline void Apply(HWND window) {
    const auto found = windows.find(window); if (found == windows.end() || !IsWindow(window)) return;
    const bool occluded = ContentOccluded(window);
    if (found->second.contentOccluded != occluded) {
        found->second.contentOccluded = occluded;
        if(occluded)SetPropW(window,lume::window_ids::ContentOccludedProp,reinterpret_cast<HANDLE>(1));
        else RemovePropW(window,lume::window_ids::ContentOccludedProp);
        PostMessageW(window, ContentVisibilityMessage(), 0, 0);
        // Only on coverage transitions. Wake worker-owned material HWNDs
        // without restacking, hiding or waiting for their message threads.
        EnumWindows([](HWND surface,LPARAM owner)->BOOL {
            if(GetPropW(surface,lume::window_ids::GlassSurfaceOwnerProp)==reinterpret_cast<HWND>(owner))
                PostMessageW(surface,ContentVisibilityMessage(),0,0);
            return TRUE;
        },reinterpret_cast<LPARAM>(window));
    }
    const Entry previous = found->second;
    const bool blocked = Blocked(window, previous, context);
    if (blocked == previous.suppressed && !(Suppressed(window) && !blocked) &&
        !(blocked && IsWindowVisible(window))) return;
    found->second.suppressed = blocked;
    const bool wasApplying = applying; applying = true;
    if (blocked) {
        SetPropW(window, lume::window_ids::LayerSuppressedProp, reinterpret_cast<HANDLE>(1));
        if (previous.kind == Kind::Popup) {
            found->second.wanted = false;
        }
        if (previous.kind == Kind::Popup || previous.kind == Kind::Desktop || previous.kind == Kind::Island)
            SendMessageW(window, WM_CANCELMODE, 0, 0);
        if (GetCapture() == window) ReleaseCapture();
        if (IsWindow(window)) ShowWindow(window, SW_HIDE);
        HideCompanions(window);
    } else {
        RemovePropW(window, lume::window_ids::LayerSuppressedProp);
        if (previous.wanted) {
            // Reappear at the bottom first. The module's normal placement code
            // repairs its band, without a one-frame jump over an application.
            const HWND ceiling = applicationCeiling ? applicationCeiling() : nullptr;
            SetWindowPos(window, Overlay(previous)?HWND_TOPMOST:
                previous.kind == Kind::Editor && ceiling ? ceiling : HWND_BOTTOM, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
            PostMessageW(window, RestackMessage(), 0, 0);
        }
    }
    applying = wasApplying;
    if (observer) observer(window, previous.kind, blocked);
}
inline void Update(const Context& next) {
    context = next;
    std::vector<HWND> roots, children;
    for (const auto& pair : windows) (pair.second.owner ? children : roots).push_back(pair.first);
    for (const auto window : roots) Apply(window);
    for (const auto window : children) Apply(window);
}
inline void Refresh() {
    if (!enabled) return;
    auto next=ReadContext();
    balancedIdle=fullscreenProtection && balancedPolicy && balancedPolicy(next);
    Update(next);
}
inline void WantVisible(HWND window) {
    const auto i=windows.find(window);if(i==windows.end())return;
    i->second.wanted=true;
    if(enabled)context=ReadContext();
    Apply(window);
}
inline void Start(bool protectFullscreen = true) {
    fullscreenProtection = protectFullscreen;
    enabled = true;
    Refresh();
}
inline void Stop() {
    enabled = false;
    fullscreenProtection = true;
    balancedPolicy={}; balancedIdle=false; Update({}); observer = {}; desktopForeground = {};
}
inline LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) {
    if (message == RestackMessage()) RestoreDesktop(window);
    if(message==PolicyMessage()) {
        const auto i=windows.find(window);if(i!=windows.end())i->second.pending=false;
        if(enabled)context=ReadContext();Apply(window);return 0;
    }
    if (message == WM_NCDESTROY) {
        windows.erase(window); RemovePropW(window, lume::window_ids::LayerSuppressedProp);
        RemovePropW(window,lume::window_ids::ContentOccludedProp);
        desktop_peek::Forget(window);
        RemovePropW(window, lume::window_ids::LayerRoleProp); RemoveWindowSubclass(window, Procedure, id);
        return DefSubclassProc(window, message, w, l);
    }
    if (message == WM_WINDOWPOSCHANGING && l) {
        auto* position = reinterpret_cast<WINDOWPOS*>(l);
        auto found = windows.find(window);
        if (found != windows.end() && !applying) {
            if (position->flags & SWP_HIDEWINDOW) {
                const bool desktopTransition = ResidentDesktop(found->second) &&
                    (IsIconic(window) || (desktopForeground && desktopForeground()));
                if (!desktopTransition) found->second.wanted = false;
            }
            if (position->flags & SWP_SHOWWINDOW) {
                found->second.wanted = true;
                if (enabled) context = ReadContext();
            }
            RECT proposed{};GetWindowRect(window,&proposed);
            const LONG width=proposed.right-proposed.left,height=proposed.bottom-proposed.top;
            if(!(position->flags&SWP_NOMOVE)){proposed.left=position->x;proposed.top=position->y;proposed.right=proposed.left+width;proposed.bottom=proposed.top+height;}
            if(!(position->flags&SWP_NOSIZE)){proposed.right=proposed.left+position->cx;proposed.bottom=proposed.top+position->cy;}
            if (Blocked(window, found->second, context,&proposed)) {
                if(found->second.kind==Kind::Popup)found->second.wanted=false;
                SetPropW(window, lume::window_ids::LayerSuppressedProp, reinterpret_cast<HANDLE>(1));
                position->flags = (position->flags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW | SWP_NOACTIVATE;
                if(!found->second.pending)found->second.pending=PostMessageW(window,PolicyMessage(),0,0)!=FALSE;
            } else if((position->flags&SWP_SHOWWINDOW) && (found->second.suppressed || Suppressed(window))) {
                // A deliberate show can follow its owner's restoration before
                // the queued family refresh. Clear the old policy state now;
                // otherwise a later protection event may skip hiding this popup.
                found->second.suppressed=false;
                RemovePropW(window,lume::window_ids::LayerSuppressedProp);
            }
            if (found->second.kind != Kind::ExplicitTool && !found->second.fullscreenBypass &&
                !Overlay(found->second) && !(position->flags & SWP_NOZORDER) &&
                position->hwndInsertAfter == HWND_TOPMOST) {
                const HWND ceiling = applicationCeiling ? applicationCeiling() : nullptr;
                position->hwndInsertAfter = found->second.kind == Kind::Popup && ceiling ? ceiling : HWND_NOTOPMOST;
                position->flags |= SWP_NOACTIVATE | SWP_NOOWNERZORDER;
            }
            if(enabled && found->second.kind==Kind::Popup && !found->second.fullscreenBypass && !Overlay(found->second) && context.foreignForeground &&
               ((position->flags&SWP_SHOWWINDOW)||!(position->flags&SWP_NOZORDER)) && applicationCeiling) {
                if(const HWND ceiling=applicationCeiling()){
                    position->hwndInsertAfter=ceiling;position->flags&=~SWP_NOZORDER;
                    position->flags|=SWP_NOACTIVATE|SWP_NOOWNERZORDER;
                }
            }
        }
    }
    return DefSubclassProc(window, message, w, l);
}
inline bool Register(HWND window, Kind kind, HWND owner = nullptr, bool aboveApplications = false, bool hideFullscreen = true) {
    if (!window || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return false;
    if (windows.count(window)) return true;
    if (!SetWindowSubclass(window, Procedure, 0x4c5952, 0)) return false;
    const auto inserted=windows.emplace(window, Entry{kind,owner,IsWindowVisible(window)!=FALSE,false,false,
        kind==Kind::Island&&aboveApplications,hideFullscreen});
    SetPropW(window, lume::window_ids::LayerRoleProp, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(kind) + 1));
    // Aero Peek previews another application by fading ordinary windows. The
    // Dock, widgets and their display companions remain part of the desktop,
    // including an island the user keeps above apps. Editors and transient
    // popups retain normal preview behavior; the manager is never registered
    // as a desktop surface. Fullscreen suppression still hides these HWNDs.
    if (kind == Kind::Desktop || kind == Kind::Island) desktop_peek::SetExcluded(window, true);
    if (kind != Kind::ExplicitTool && !Overlay(inserted.first->second) && (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST))
        SetWindowPos(window,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    if (enabled) { context = ReadContext(); Apply(window); }
    return true;
}
inline void ConfigureIsland(HWND window,bool aboveApplications,bool hideFullscreen) {
    const auto found=windows.find(window);
    if(found==windows.end()||found->second.kind!=Kind::Island)return;
    found->second.aboveApplications=aboveApplications;found->second.hideFullscreen=hideFullscreen;
    if(enabled){context=ReadContext();Apply(window);}
}
inline bool SetFullscreenBypass(HWND window,bool bypass) {
    const auto found=windows.find(window);
    if(found==windows.end())return false;
    if(found->second.kind==Kind::Popup) {
        const auto owner=windows.find(found->second.owner);
        if(bypass&&(owner==windows.end()||!owner->second.fullscreenBypass))return false;
    } else if(found->second.kind!=Kind::Desktop)return false;
    if(found->second.fullscreenBypass==bypass)return true;
    found->second.fullscreenBypass=bypass;
    if(enabled){context=ReadContext();Apply(window);}
    if(bypass)EnumWindows([](HWND surface,LPARAM owner)->BOOL {
        if(GetPropW(surface,lume::window_ids::GlassSurfaceOwnerProp)==reinterpret_cast<HWND>(owner))
            PostMessageW(surface,lume::window_ids::GlassRestackMessage,0,0);
        return TRUE;
    },reinterpret_cast<LPARAM>(window));
    return true;
}
} // namespace lume::platform::windows::window_layer
