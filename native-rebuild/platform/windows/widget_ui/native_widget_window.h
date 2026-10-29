#pragma once
#include "core/widget/widget_preferences.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include "platform/windows/material/widget_material_runtime.h"
#include <functional>
#include <memory>
#include <optional>
namespace lume::platform::windows {
enum class WidgetAction { Close,Settings,Reset,Lock,Liquid,Frosted,Bounds,Refresh,Solid };
struct WidgetWindowPorts {
    std::function<void(WidgetCanvas&,float,float)> draw;
    std::function<void(WidgetAction,const widget::Preferences&)> action;
    std::function<void()> update;
    std::function<void()> materialChanged;
    // Optional feature input/timer handling; common lifecycle cannot be intercepted.
    std::function<std::optional<LRESULT>(HWND,UINT,WPARAM,LPARAM)> event={};
    std::function<std::vector<LumeMaterialPanel>(float,float)> panels={};
    std::function<bool(float,float)> interactiveAt={};
    bool canRefresh=false;
    bool canDropFiles=false;
    bool showCloseButton=true;
    // Reserve a transparent strip below the glass body (DIP); foreground still
    // uses the full client area. The feature owns when this geometry changes.
    std::function<unsigned(float,float)> materialBottomInset={};
    // False: no material behind the foreground (a bare widget over the
    // wallpaper). Hover shows a faint pane so the window stays findable.
    bool glass=true;
    // With glass off: false keeps the window bare even under the pointer.
    bool hoverPane=true;
    // True: always at the bottom of the desktop layer, under the Dock and
    // other widgets; a click does not raise it, and only what it draws takes
    // the mouse (clicks between its bars reach whatever is below).
    bool bottom=false;
    // Optional own right-click menu. Close/settings/reset/lock results (1, 11,
    // 10, 12) are handled as usual; any other nonzero result goes to command.
    std::function<UINT(HWND,POINT,const widget::Preferences&)> menu={};
    std::function<void(UINT,const widget::Preferences&)> command={};
    // True: holds desktop icons (the organizer). It stays above every other
    // widget and below the Dock; clicking a widget never lifts it over this.
    bool shelf=false;
    // Fixed hit region (DIP) for a widget whose drawing changes every frame.
    // Then draw output goes to a click-through display window, and only these
    // rects take the mouse: they follow the layout (asked again on size and
    // Configure), never the frame, so the clickable area does not flicker
    // with the content and stays grabbable when the content is empty.
    std::function<std::vector<WidgetRect>(float,float)> hitShape={};
};
// UI-thread owner; no data sampling, persistence, business models or timers.
class NativeWidgetWindow final {
public:
    NativeWidgetWindow(HINSTANCE,std::wstring title,widget::Limits,WidgetWindowPorts);
    ~NativeWidgetWindow();
    bool Configure(const widget::Preferences&);
    bool Start();
    void Stop();
    bool Paint();
    // Start() reports false when the desktop layer refused the window; callers
    // surface that as its own error instead of a generic start failure.
    bool PlacementFailed()const;
    HWND Handle()const;
    widget::Preferences Bounds()const;
    WidgetMaterialState MaterialState()const;
    static constexpr UINT UpdateMessage=WM_APP+0x620;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
