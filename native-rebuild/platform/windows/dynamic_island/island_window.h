#pragma once
#include "core/dynamic_island/island_model.h"
#include "core/widget/widget_preferences.h"
#include "core/calendar/calendar_date.h"
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
// Pages of the expanded island; the mouse wheel moves between them.
// Reminder lists the reminders shown in this session, once there is one.
// Weather shows the weather component's current reading (it never fetches).
// Performance reuses the performance widget's view and samples only while open.
enum class IslandPage { Music, Calendar, Todo, Reminder, Weather, Performance };
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
// User-facing visibility and layering choices.
struct IslandDisplay {
    bool hideFullscreen = true;  // Step aside for fullscreen apps, games and presentations.
    bool topmost = true;         // Above windows; false keeps it in the desktop layer.
    bool hoverExpand = false;    // Expand after resting the pointer on the island.
    bool autoHide = true;        // Idle: tuck against the screen top, leaving a sliver to hover.
    bool messages = true;        // Show new QQ / WeChat / ... messages (their taskbar button flashing).
    IslandSize size = IslandSize::Standard;
    IslandMaterial material = IslandMaterial::None;
};
inline bool operator==(const IslandDisplay& a, const IslandDisplay& b) {
    return a.hideFullscreen == b.hideFullscreen && a.topmost == b.topmost &&
           a.hoverExpand == b.hoverExpand && a.size == b.size && a.material == b.material &&
           a.autoHide == b.autoHide && a.messages == b.messages;
}
inline bool operator!=(const IslandDisplay& a, const IslandDisplay& b) { return !(a == b); }

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
    void Stop();
    // Applies new display options; recreates the window when layering changes.
    bool SetDisplay(const IslandDisplay&);
    void Update(IslandPresentation);
    // Weather page content; the host reads it from the weather component.
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
    // Verification: completes running animations immediately.
    void Settle();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
