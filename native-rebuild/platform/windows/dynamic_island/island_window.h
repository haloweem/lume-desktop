#pragma once
#include "core/dynamic_island/island_model.h"
#include "core/dynamic_island/compact_widgets.h"
#include "core/widget/widget_preferences.h"
#include "core/calendar/calendar_date.h"
#include "core/performance/performance_metrics.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace lume::platform::windows {
struct IslandPresentation {
    dynamic_island::Media media;
    std::wstring time, date;
    unsigned remaining = 0;
    bool focusing = false, finished = false, english = false;
};
enum class IslandSize { Small, Standard, Large };
enum class IslandShape { Standard, Capsule };
// Playback includes an idle empty state; active reminders have their own view.
enum class IslandPage { Music, Reminder };
struct IslandTodo {
    std::string id;
    std::wstring title, time;
    bool completed = false, projected = false;
    std::uint32_t color = 0;  // RGB of the todo's colour label; 0 for none.
    std::wstring notes;
};
// The shared calendar todo store, read on demand (no copy is kept by the window).
struct IslandTodoSource {
    std::function<std::vector<IslandTodo>(calendar::Date)> forDay;
    std::function<bool(calendar::Date)> hasTodos;
    std::function<void(const IslandTodo&)> toggle;
    std::function<void()> openCalendar;
    std::function<void(const std::string&)> openTask;
    // Shared todo editor (same form as the calendar); an empty id creates one on the day.
    std::function<void(const std::string&, calendar::Date)> edit;
    std::function<void(const IslandTodo&)> remove;
    std::function<bool()> writable;
    // Current state of one todo, for the reminder page; empty when deleted.
    std::function<std::optional<IslandTodo>(const std::string&)> find;
};
// None keeps the classic opaque black capsule.
enum class IslandMaterial { None, Liquid, Frosted };
struct IslandMonitor {
    std::wstring device;
    RECT bounds{}, work{};
    bool primary=false;
};
inline bool operator==(const IslandMonitor& a,const IslandMonitor& b) {
    return a.device==b.device&&a.primary==b.primary&&EqualRect(&a.bounds,&b.bounds)&&EqualRect(&a.work,&b.work);
}
std::vector<IslandMonitor> ConnectedIslandMonitors();
// User-facing visibility and layering choices.
struct IslandDisplay {
    int screen=-1;               // -1 follows the primary display; 0 means all; 1..64 selects a connected screen.
    bool hideFullscreen = true;  // Step aside for fullscreen apps, games and presentations.
    bool topmost = false;        // Explicitly chosen overlay above ordinary applications.
    bool hoverExpand = false;    // Expand after resting the pointer on the island.
    bool autoHide = true;        // Idle: tuck against the screen top, leaving a sliver to hover.
    bool messages = true;        // Show new QQ / WeChat / ... messages (their taskbar button flashing).
    IslandSize size = IslandSize::Standard;
    IslandMaterial material = IslandMaterial::None;
    IslandShape shape = IslandShape::Capsule;
    dynamic_island::CompactWidget leftWidget=dynamic_island::CompactWidget::None;
    dynamic_island::CompactWidget rightWidget=dynamic_island::CompactWidget::None;
};
inline bool operator==(const IslandDisplay& a, const IslandDisplay& b) {
    return a.screen==b.screen&&a.leftWidget==b.leftWidget&&a.rightWidget==b.rightWidget&&a.hideFullscreen == b.hideFullscreen && a.topmost == b.topmost &&
           a.hoverExpand == b.hoverExpand && a.size == b.size && a.material == b.material &&
           a.autoHide == b.autoHide && a.messages == b.messages && a.shape == b.shape;
}
inline bool operator!=(const IslandDisplay& a, const IslandDisplay& b) { return !(a == b); }
struct IslandPreviewFrame {
    WidgetBitmap pixels;
    unsigned width=0,height=0;
    float cornerRadius=0;
    float shapeMix=0,topOffset=0;
    bool animating=false;
    std::uint32_t accent=0;
};

// UI-thread window. Owns only drawing, input and short-lived animation timers.
// It never activates, hooks the mouse only while expanded (to collapse on an
// outside click), hides at once when a fullscreen application or game is in
// front on its monitor, and tucks against the screen top over maximized windows.
// Square cover from a Media::artwork data URL, as the island draws it.
std::shared_ptr<const WidgetBitmap> DecodeMediaArtwork(const std::string&);
class IslandWindow final {
public:
    using Act = std::function<void(dynamic_island::Action)>;
    IslandWindow(HINSTANCE, Act);
    ~IslandWindow();
    bool Start(const widget::Preferences&, const IslandDisplay& = {});
    // Bind this window to one screen; the session owns the all-screen group.
    void SetMonitor(const IslandMonitor&);
    void Stop();
    // Applies new display options; recreates the window when layering changes.
    bool SetDisplay(const IslandDisplay&);
    void Update(IslandPresentation);
    // Shared weather readings for the compact widgets.
    void SetWeather(dynamic_island::Weather);
    void SetTodoSource(IslandTodoSource);
    // Shows a todo reminder in the island for a few seconds; false when the
    // island cannot show it now (stopped, stepping aside for a fullscreen app,
    // or in the desktop layer where windows may cover it).
    bool ShowNotice(const std::string& taskId, const std::wstring& title, const std::wstring& time);
    // Repaints after the shared todo store changed.
    void RefreshTodos();
    IslandPage Page() const;
    void Expand(bool);
    HWND Handle() const;
    bool Expanded() const;
    // True while the island is on screen (not hidden for idle or fullscreen).
    bool Shown() const;
    // Copy the actual composed foreground for rendering verification.
    WidgetBitmap Frame() const;
    // Same drawing code, isolated canvas: no HWND, timers, hooks or live state changes.
    IslandPreviewFrame Preview(const IslandDisplay&,bool expanded,float advanceSeconds=0);
    void ReleasePreview();
    // Latest sample used by the compact widgets.
    std::optional<performance::Snapshot> PerformanceReading() const;
    // Real current readings for the chooser, never sample/demo values.
    std::wstring CompactReadings();
    // Verification: completes running animations immediately.
    void Settle();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
