#pragma once
#include "platform/windows/lume_window_ids.h"
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

namespace lume::platform::windows::window_layer {
enum class Kind { Desktop, Popup, Editor, ExplicitTool, Island };
struct Context {
    HWND foreground = nullptr;
    bool foreignForeground = false;
    bool allDisplays = false;
    bool exclusive = false;
    std::vector<RECT> fullscreen;
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
inline Context Capture() {
    Context result; result.foreground = GetForegroundWindow();
    const HWND foreground = result.foreground;
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
            struct Scan { RECT bounds; std::vector<RECT>* displays; } scan{bounds, &result.fullscreen};
            EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
                auto& value = *reinterpret_cast<Scan*>(data); MONITORINFO info{sizeof(info)};
                if (GetMonitorInfoW(monitor, &info) && Covers(value.bounds, info.rcMonitor)) value.displays->push_back(info.rcMonitor);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&scan));
        }
    }
    QUERY_USER_NOTIFICATION_STATE state{};
    if (SUCCEEDED(SHQueryUserNotificationState(&state)) &&
        (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE)) {
        result.exclusive = true;
        if(result.fullscreen.empty())result.allDisplays = true;
    }
    return result;
}
inline bool Protected(const RECT& bounds, const Context& state, bool hideFullscreen = true) {
    return state.allDisplays || ((hideFullscreen || state.exclusive) && std::any_of(state.fullscreen.begin(), state.fullscreen.end(),
        [&](const RECT& display) { return Intersects(bounds, display); }));
}
inline thread_local Context context;
inline thread_local bool enabled = false, applying = false;
inline thread_local std::function<void(HWND, Kind, bool)> observer;
inline thread_local std::function<HWND()> applicationCeiling;
// Injection seam for isolated tests; production always reads the actual desktop.
inline thread_local std::function<Context()> contextReader;
inline Context ReadContext() { return contextReader ? contextReader() : Capture(); }
struct Entry {
    Kind kind; HWND owner; bool wanted; bool suppressed; bool pending = false;
    bool aboveApplications = false, hideFullscreen = true;
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
inline bool DesktopFamily(HWND window) {
    const auto role=reinterpret_cast<ULONG_PTR>(GetPropW(window,lume::window_ids::LayerRoleProp));
    return role==static_cast<ULONG_PTR>(Kind::Desktop)+1 || role==static_cast<ULONG_PTR>(Kind::Popup)+1 ||
        role==static_cast<ULONG_PTR>(Kind::Island)+1;
}
inline bool Blocked(HWND window, const Entry& entry, const Context& state, const RECT* proposed = nullptr) {
    if (!enabled || entry.kind == Kind::ExplicitTool) return false;
    if (entry.owner && (!IsWindow(entry.owner) || Suppressed(entry.owner))) return true;
    if (entry.owner && entry.kind == Kind::Popup && !IsWindowVisible(entry.owner)) return true;
    RECT bounds{}; if(proposed)bounds=*proposed;else GetWindowRect(window, &bounds);
    if (IsRectEmpty(&bounds) && entry.owner) GetWindowRect(entry.owner, &bounds);
    const auto* island=IslandPolicy(entry);
    return Protected(bounds,state,island?island->hideFullscreen:true);
}
inline void HideCompanions(HWND owner) {
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        const HWND parent = reinterpret_cast<HWND>(data);
        if (GetPropW(window, lume::window_ids::GlassSurfaceOwnerProp) == parent) {
            // The material worker owns this HWND and checks the owner's current
            // suppression flag before every show; never wait on that thread here.
            PostMessageW(window, WM_APP + 1, 0, 0);
            ShowWindowAsync(window, SW_HIDE);
        } else if (GetPropW(window, lume::window_ids::DesktopCompanionOwnerProp) == parent) {
            ShowWindowAsync(window, SW_HIDE);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(owner));
}
inline UINT RestackMessage() { static const UINT value = RegisterWindowMessageW(L"Lume.DesktopLevel.v1"); return value; }
inline UINT PolicyMessage() { static const UINT value = RegisterWindowMessageW(L"Lume.DisplayPolicy.v1"); return value; }
inline void Apply(HWND window) {
    const auto found = windows.find(window); if (found == windows.end() || !IsWindow(window)) return;
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
inline void Refresh() { if (enabled) Update(ReadContext()); }
inline void WantVisible(HWND window) {
    const auto i=windows.find(window);if(i==windows.end())return;
    i->second.wanted=true;
    if(enabled)context=ReadContext();
    Apply(window);
}
inline void Start() { enabled = true; Refresh(); }
inline void Stop() { enabled = false; Update({}); observer = {}; }
inline LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR) {
    if(message==PolicyMessage()) {
        const auto i=windows.find(window);if(i!=windows.end())i->second.pending=false;
        if(enabled)context=ReadContext();Apply(window);return 0;
    }
    if (message == WM_NCDESTROY) {
        windows.erase(window); RemovePropW(window, lume::window_ids::LayerSuppressedProp);
        RemovePropW(window, lume::window_ids::LayerRoleProp); RemoveWindowSubclass(window, Procedure, id);
        return DefSubclassProc(window, message, w, l);
    }
    if (message == WM_WINDOWPOSCHANGING && l) {
        auto* position = reinterpret_cast<WINDOWPOS*>(l);
        auto found = windows.find(window);
        if (found != windows.end() && !applying) {
            if (position->flags & SWP_HIDEWINDOW) found->second.wanted = false;
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
            if (found->second.kind != Kind::ExplicitTool && !Overlay(found->second) && !(position->flags & SWP_NOZORDER) &&
                position->hwndInsertAfter == HWND_TOPMOST) {
                const HWND ceiling = applicationCeiling ? applicationCeiling() : nullptr;
                position->hwndInsertAfter = found->second.kind == Kind::Popup && ceiling ? ceiling : HWND_NOTOPMOST;
                position->flags |= SWP_NOACTIVATE | SWP_NOOWNERZORDER;
            }
            if(enabled && found->second.kind==Kind::Popup && !Overlay(found->second) && context.foreignForeground &&
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
} // namespace lume::platform::windows::window_layer
