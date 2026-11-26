#pragma once
#include "platform/windows/display_monitors.h"
#include "core/dynamic_island/island_model.h"
#include "core/dynamic_island/compact_widgets.h"
#include "core/dynamic_island/expanded_widgets.h"
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
// Ephemeral notifications never enter the todo store or reminder history.
// Without an activation callback the capsule opens its full message inside
// the island. App notifications activate their source directly on click.
struct IslandNotification {
    std::string id;
    std::wstring title, body;
    std::function<void()> activate;
    // A fixed bundled asset key, never a notification-supplied path or URL.
    std::string icon{};
    // New messages from one app refresh its capsule instead of building up
    // a queue of stale generic alerts. Empty keeps independent notices.
    std::string coalesceKey{};
    // A scheduled voice reminder must start on arrival rather than waiting
    // behind an unrelated capsule. Ordinary notifications keep FIFO behavior.
    bool immediate = false;
    unsigned durationMs = 10000;
    // UI-thread visibility lease. All-screen mirrors share one voice owner.
    std::function<void(bool)> presented{};
    // Current normalized PCM amplitude; empty for silent/default styles.
    std::function<float()> audioLevel{};
    std::function<bool()> audioPlaying{};
    // Shared voice owner selects captions from the actual audio device position.
    std::function<std::wstring()> voiceCaption{};
};
enum class IslandSize { Small, Standard, Large };
enum class IslandShape { Standard, Capsule };
enum class IslandClockStyle { Simple, Progress, Digital, Date, Flip, Pixel };
enum class IslandCalendarStyle { Default, ChineseAlmanac };
enum class IslandCountdownStyle { Standard, Violet };
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
enum class IslandAnimationSpeed { Slow, Normal, Fast };
enum class IslandBounce { Subtle, Normal, Bouncy };
inline unsigned SnapIslandHoverDelay(unsigned ms) { return ms<100?100:ms>1000?1000:((ms+25)/50)*50; }
using IslandMonitor = DisplayMonitor;
std::vector<IslandMonitor> ConnectedIslandMonitors();
// User-facing visibility and layering choices.
struct IslandDisplay {
    int screen=-1;               // -1 follows the primary display; 0 means all; 1..64 selects a connected screen.
    bool hideFullscreen = true;  // Step aside for fullscreen apps, games and presentations.
    bool topmost = false;        // Explicitly chosen overlay above ordinary applications.
    bool hoverExpand = false;    // Expand after resting the pointer on the island.
    unsigned hoverDelayMs = 300; // 100..1000 ms in 50 ms steps.
    IslandAnimationSpeed animationSpeed = IslandAnimationSpeed::Normal;
    IslandBounce bounce = IslandBounce::Normal;
    bool autoHide = true;        // Idle: tuck against the screen top, leaving a sliver to hover.
    IslandSize size = IslandSize::Standard;
    IslandMaterial material = IslandMaterial::None;
    IslandShape shape = IslandShape::Capsule;
    IslandClockStyle clockStyle = IslandClockStyle::Simple;
    IslandCalendarStyle calendarStyle = IslandCalendarStyle::Default;
    int countdownDate = 0;       // Days since 1970-01-01; zero means unset.
    bool countdownCountUp = false;
    std::string countdownTitle;
    bool countdownReminder = false;
    dynamic_island::CompactWidget leftWidget=dynamic_island::CompactWidget::None;
    dynamic_island::CompactWidget rightWidget=dynamic_island::CompactWidget::None;
    std::vector<dynamic_island::ExpandedWidget> expandedWidgets{dynamic_island::ExpandedWidget::Music};
    IslandCountdownStyle countdownStyle = IslandCountdownStyle::Standard;
    std::uint32_t countdownTextColor = 0x262626;
    std::uint32_t countdownBackgroundColor = 0x2183c0;
    std::uint32_t countdownSolidColor = 0x8264a5;
    unsigned countdownTextAlpha = 255;
    unsigned countdownBackgroundAlpha = 255;
    unsigned countdownSolidAlpha = 255;
};
inline bool operator==(const IslandDisplay& a, const IslandDisplay& b) {
    return a.expandedWidgets==b.expandedWidgets&&a.screen==b.screen&&a.leftWidget==b.leftWidget&&a.rightWidget==b.rightWidget&&a.hideFullscreen == b.hideFullscreen && a.topmost == b.topmost &&
           a.hoverExpand == b.hoverExpand && a.hoverDelayMs == b.hoverDelayMs &&
           a.animationSpeed == b.animationSpeed && a.bounce == b.bounce && a.size == b.size && a.material == b.material &&
           a.autoHide == b.autoHide && a.shape == b.shape && a.clockStyle == b.clockStyle && a.calendarStyle == b.calendarStyle &&
           a.countdownDate == b.countdownDate && a.countdownTitle == b.countdownTitle &&
           a.countdownCountUp == b.countdownCountUp &&
           a.countdownReminder == b.countdownReminder && a.countdownStyle == b.countdownStyle &&
           a.countdownTextColor == b.countdownTextColor && a.countdownBackgroundColor == b.countdownBackgroundColor &&
           a.countdownSolidColor == b.countdownSolidColor && a.countdownTextAlpha == b.countdownTextAlpha &&
           a.countdownBackgroundAlpha == b.countdownBackgroundAlpha && a.countdownSolidAlpha == b.countdownSolidAlpha;
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
    using EditWidget = std::function<void(dynamic_island::ExpandedWidget,HWND)>;
    IslandWindow(HINSTANCE, Act, std::function<bool()> notificationAllowed = {}, EditWidget = {});
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
    // Briefly appears above ordinary applications, without activation or a
    // permanent layering change. Returns false while notifications are blocked.
    bool ShowNotification(const IslandNotification&);
    // Clears both visible and queued notification content immediately.
    void ClearNotifications();
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
