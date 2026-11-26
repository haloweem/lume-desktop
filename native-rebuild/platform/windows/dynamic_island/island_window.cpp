#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/dynamic_island/compact_widgets.h"
#include "platform/windows/dynamic_island/island_performance_card.h"
#include "platform/windows/dynamic_island/island_clock_card.h"
#include "platform/windows/dynamic_island/island_almanac_card.h"
#include "platform/windows/dynamic_island/notification_icon.h"
#include "platform/windows/dynamic_island/notification_display_policy.h"
#include "vendor/nlohmann/json.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include "core/dynamic_island/island_shape.h"
#include "platform/windows/dynamic_island/island_shape_mask.h"
#include "core/calendar/calendar_date.h"
#include "core/calendar/lunar_calendar.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "platform/windows/text_util.h"
#include "platform/windows/performance/performance_monitor.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include "platform/windows/widget_ui/layered_surface.h"
#include "platform/windows/widget_ui/text_fit.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/dock/frame_clock.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/material/widget_material_runtime.h"
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <cmath>
#include <iterator>
#include <map>
#include <optional>
#include <utility>
#include <filesystem>
#include <fstream>

namespace lume::platform::windows {
namespace {
constexpr wchar_t ClassName[] = L"LumeWindowsDynamicIsland";
constexpr UINT_PTR TickTimer = 1, AnimationTimer = 2, HoverTimer = 3, CollapseTimer = 4, BarsTimer = 5, IdleTimer = 6, NoticeTimer = 7,
    CalendarTimer = 8, ClockFlipTimer = 9, EditHintTimer = 10;
constexpr UINT EditHintDelayMs = 500;
constexpr float EditHintAnimationSeconds = .28f;
constexpr UINT ForegroundMessage = WM_APP + 0x6e2, OutsideMessage = WM_APP + 0x6e3, FrameMessage = WM_APP + 0x6e4,
               PerformanceMessage = WM_APP + 0x6e5;
// Base sizes in DIP before the size option scales them.
constexpr float IdleWidth = 120, IdleHeight = 30;
constexpr float CompactWidth = 236, CompactHeight = 34;
constexpr float HoverGrowWidth = 12, HoverGrowHeight = 3;
constexpr float ExpandedWidth = 372, ExpandedHeight = 156;
constexpr float PageWidth = 404, PageHeight = 208;  // Reminder page.
constexpr float NoticeWidth = 340, NoticeHeight = 62;
constexpr UINT NoticeMilliseconds = 9000;
constexpr UINT NotificationMilliseconds = 10000;
constexpr int MaxReminders = 10;
// The canvas keeps this fixed size during animations, so no bitmap or render
// target is reallocated per frame; each frame presents only the live area.
// Leave room for the strongest bounce on the 404 x 208 reminder panel.
constexpr float CanvasWidth = 1536, CanvasHeight = 440;
constexpr std::uint32_t Green = 0x30d158;
constexpr float TopMargin = 8;
// Auto-hide: how long without activity before tucking, and what stays visible.
constexpr ULONGLONG IdleDelay = 3000, LeaveDelay = 1200;
constexpr float TuckedSliver = 6;
constexpr std::size_t MaxArtworkBytes = 2 * 1024 * 1024;
using Microsoft::WRL::ComPtr;

float Scale(IslandSize size) { return size == IslandSize::Small ? .86f : size == IslandSize::Large ? 1.4f : 1.2f; }

calendar::Date Today() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    return {now.wYear, now.wMonth, now.wDay};
}
double Seconds() {
    static LARGE_INTEGER frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
}
std::wstring Duration(unsigned seconds) {
    const auto s = seconds % 60;
    return std::to_wstring(seconds / 60) + L":" + (s < 10 ? L"0" : L"") + std::to_wstring(s);
}
// Days since 1970-01-01 for a proleptic Gregorian date.
long long CivilDays(calendar::Date d) {
    const int y = d.year - (d.month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (d.month + (d.month > 2 ? -3 : 9)) + 2) / 5 + d.day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<long long>(era) * 146097 + doe - 719468;
}
int IsoWeek(calendar::Date d) {
    const int weekday = calendar::Weekday(d), iso = weekday == 0 ? 7 : weekday;
    const auto thursday = calendar::AddDays(d, 4 - iso).value_or(d);
    return (calendar::DayOfYear(thursday) - 1) / 7 + 1;
}
// A light spring settle (about 6% overshoot) for positions, and an ease-out
// cubic for opacity; both reach exactly 1 at the end of the transition.
float SpringSettle(float t) {
    if (t >= 1) return 1;
    return 1 - std::exp(-6 * t) * std::cos(7 * t);
}
float EaseOut(float t) { t = std::clamp(t, 0.f, 1.f); return 1 - (1 - t) * (1 - t) * (1 - t); }
// The day's first festival, else its solar term; the flag marks a festival.
std::pair<std::wstring, bool> CalendarMark(calendar::Date d) {
    const auto lunar = calendar::LookupLunar(d);
    if (!lunar.festivals.empty()) return {text::Wide(lunar.festivals.front()), true};
    if (!lunar.solarTerm.empty()) return {text::Wide(lunar.solarTerm), false};
    return {};
}
// A solid triangle pointing left or right, its corners slightly rounded (the
// outline adds 1 DIP all round, so the outline is inset by that much).
void Triangle(WidgetCanvas& canvas, float cx, float cy, float width, float height, bool left, float alpha) {
    const float w = width - 2, h = height - 2;
    const float tip = cx + (left ? -w / 2 : w / 2), base = cx + (left ? w / 2 : -w / 2);
    canvas.Polygon({base, cy - h / 2, tip, cy, base, cy + h / 2}, 0xffffff, alpha, 2);
}
std::vector<BYTE> ArtworkBytes(const std::string& value) {
    constexpr std::string_view png = "data:image/png;base64,", jpeg = "data:image/jpeg;base64,";
    const auto prefix = value.rfind(png, 0) == 0 ? png.size() : value.rfind(jpeg, 0) == 0 ? jpeg.size() : 0;
    if (!prefix || value.size() > 2800000 || value.size() <= prefix) return {};
    DWORD bytes = 0;
    const auto count = static_cast<DWORD>(value.size() - prefix);
    if (!CryptStringToBinaryA(value.data() + prefix, count, CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &bytes,
                              nullptr, nullptr) || !bytes || bytes > MaxArtworkBytes)
        return {};
    std::vector<BYTE> result(bytes);
    if (!CryptStringToBinaryA(value.data() + prefix, count, CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, result.data(),
                              &bytes, nullptr, nullptr))
        return {};
    result.resize(bytes);
    return result;
}
std::shared_ptr<const WidgetBitmap> DecodeArtwork(const std::string& value) {
    const auto bytes = ArtworkBytes(value);
    if (bytes.empty()) return {};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapClipper> clip;
    ComPtr<IWICBitmapScaler> scale;
    ComPtr<IWICFormatConverter> convert;
    UINT w = 0, h = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) || !w || !h || w > 2048 || h > 2048)
        return {};
    const auto side = std::min(w, h);
    WICRect crop{static_cast<INT>((w - side) / 2), static_cast<INT>((h - side) / 2), static_cast<INT>(side),
                 static_cast<INT>(side)};
    const auto output = std::min(128u, side);
    if (FAILED(factory->CreateBitmapClipper(&clip)) || FAILED(clip->Initialize(frame.Get(), &crop)) ||
        FAILED(factory->CreateBitmapScaler(&scale)) ||
        FAILED(scale->Initialize(clip.Get(), output, output, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&convert)) ||
        FAILED(convert->Initialize(scale.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)))
        return {};
    auto bitmap = std::make_shared<WidgetBitmap>();
    bitmap->width = bitmap->height = output;
    bitmap->pixels.resize(static_cast<std::size_t>(output) * output);
    if (FAILED(convert->CopyPixels(nullptr, output * 4, output * output * 4,
                                   reinterpret_cast<BYTE*>(bitmap->pixels.data()))))
        return {};
    return bitmap;
}
// A lively accent taken from the artwork for the spectrum, like iOS.
std::uint32_t Accent(const std::shared_ptr<const WidgetBitmap>& cover) {
    constexpr std::uint32_t fallback = 0x30d158;
    if (!cover || cover->pixels.empty()) return fallback;
    double r = 0, g = 0, b = 0, weight = 0;
    for (const auto pixel : cover->pixels) {
        const double a = (pixel >> 24) / 255.0;
        if (a < .5) continue;
        const double pr = ((pixel >> 16) & 255) / a, pg = ((pixel >> 8) & 255) / a, pb = (pixel & 255) / a;
        const double high = std::max({pr, pg, pb}), low = std::min({pr, pg, pb});
        const double w = .05 + (high - low) / 255.0;  // Favour saturated pixels.
        r += pr * w; g += pg * w; b += pb * w; weight += w;
    }
    if (weight <= 0) return fallback;
    r /= weight; g /= weight; b /= weight;
    const double high = std::max({r, g, b}), low = std::min({r, g, b});
    if (high - low < 24) return 0xffffff;  // Grey artwork: white bars.
    // Brighten so the bars read on black.
    const double boost = 235.0 / std::max(1.0, high);
    const auto channel = [&](double v) { return static_cast<std::uint32_t>(std::clamp(v * boost, 0.0, 255.0)); };
    return (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

struct Spring {
    float x = 0, v = 0, target = 0;
    void Snap(float value) { x = target = value; v = 0; }
    // Damped spring; stiffness/damping chosen for a short, slight overshoot.
    void Step(float dt, float stiffness, float damping) {
        const float a = -stiffness * (x - target) - damping * v;
        v += a * dt;
        x += v * dt;
    }
    bool Settled() const { return std::abs(x - target) < .05f && std::abs(v) < .05f; }
};
struct SpringParameters { float stiffness, damping; };
// Scaling stiffness by rate squared and damping by rate changes duration
// without changing the damping ratio. Only the size springs gain overshoot.
SpringParameters MotionParameters(int axis, const IslandDisplay& display) {
    static constexpr float stiffness[6] = {210,260,300,170,260,230};
    static constexpr float damping[6] = {21,24,34,24,30,30};
    const float rate=display.animationSpeed==IslandAnimationSpeed::Slow?.55f:
        display.animationSpeed==IslandAnimationSpeed::Fast?1.75f:1.f;
    const float bounce=axis<2?(display.bounce==IslandBounce::Subtle?1.25f:
        display.bounce==IslandBounce::Bouncy?.64f:1.f):1.f;
    return {stiffness[axis]*rate*rate,damping[axis]*rate*bounce};
}
}  // namespace

std::vector<IslandMonitor> ConnectedIslandMonitors() { return ConnectedDisplayMonitors(); }

struct IslandWindow::Impl {
    HINSTANCE instance;
    Act act;
    HWND hwnd = nullptr;
    unsigned dpi = 96;
    widget::Preferences prefs;
    IslandDisplay display;
    std::optional<IslandMonitor> targetMonitor;
    IslandPresentation view;
    std::unique_ptr<WidgetCanvas> canvas;
    LayeredSurface surface;
    WidgetBitmap bitmap;
    IslandShapeMask shapeMask;
    std::unique_ptr<Impl> preview;
    std::shared_ptr<const WidgetBitmap> cover;
    std::string coverKey;
    std::uint32_t accent = 0x30d158;
    bool expanded = false, hoverExpanded = false, hovered = false, suppressed = false, visible = false;
    int pressed = -1, hoverHit = -1;
    std::optional<dynamic_island::ExpandedWidget> hoveredWidget;
    POINT editHintAnchor{};
    double editHintAnimationAt=0;
    bool editHintVisible=false,editHintAnimating=false;
    Spring width, height, presence, tuck, shape; // shape: 0 capsule, 1 attached.
    Spring pageFade;  // 0 right after a page change, 1 settled.
    float workspaceReveal=0; // Content opacity must not reverse with the shell's bounce.
    Spring previewScale;
    bool sampleRendering=false;
    IslandPage page = IslandPage::Music;
    calendar::Date today = Today();
    IslandTodoSource todos;
    struct CalendarInfo {
        std::wstring lunarDate, mark;
        bool festival = false;
    };
    std::map<int, CalendarInfo> calendarInfo;
    std::vector<IslandTodo> todoRows;
    bool todoCacheValid = false;
    bool todoWritable = true;
    dynamic_island::Weather weather;  // Projection of the weather component.
    CompactData compactData;
    IslandClockFlipState clockFlip;
    std::vector<std::wstring> heldFiles;
    // Keep samples warm for the window's lifetime: the worker posts
    // here, and the UI thread reads the latest snapshot.
    std::atomic<HWND> performanceTarget{nullptr};
    PerformanceMonitor performance{[this] {
        if (const HWND target = performanceTarget.load()) PostMessageW(target, PerformanceMessage, 0, 0);
    }};
    std::optional<performance::Snapshot> performanceSample;
    struct Notice {
        std::string id;
        std::wstring title, time;
        bool transient = false;
        std::function<void()> activate;
        ULONGLONG expiresAt = 0;
        std::shared_ptr<const WidgetBitmap> icon{};
        std::string coalesceKey{};
        unsigned durationMs = NotificationMilliseconds;
        std::function<void(bool)> presented{};
        bool presentationActive = false;
        std::function<float()> audioLevel{};
        std::function<bool()> audioPlaying{};
        std::function<std::wstring()> voiceCaption{};
    };
    std::optional<Notice> notice;
    bool notificationDetail = false;
    float notificationScroll = 0, notificationContentHeight = 0;
    std::vector<Notice> reminders;  // Newest first, at most MaxReminders.
    std::deque<Notice> queued;      // Notices waiting for the current one to finish.
    NotificationIconCache notificationIcons;
    mutable std::string notificationDisplayStatus;
    std::function<bool()> notificationAllowed;
    bool transientLayer = false;
    int pageDirection = 1;          // +1 slides in from the right, -1 from the left.
    std::vector<WidgetRect> reminderRows;
    struct WorkspaceHit { WidgetRect rect; int action; std::optional<IslandTodo> todo; };
    struct WorkspaceWidgetHit { WidgetRect rect; dynamic_island::ExpandedWidget widget; };
    std::vector<WorkspaceHit> workspaceHits;
    std::vector<WorkspaceWidgetHit> workspaceWidgets;
    // Calendar module: the shown month and selected day reset to today on
    // every expansion. Transitions are short time-based curves repainted by
    // CalendarTimer only while they run.
    static constexpr int CalendarDayHit=900,CalendarPrevHit=940,CalendarNextHit=941,CalendarTodayHit=942,CalendarAreaHit=950;
    static constexpr int WorkspaceEditFirst=1000;
    struct CalendarMotion { double start=-1; int direction=1; calendar::Date from{}; };
    calendar::Date calendarSelected{};
    std::optional<IslandAlmanacReading> almanacReading;
    int calendarYear=0,calendarMonth=0;
    CalendarMotion monthMotion,dayMotion;
    int calendarHover=-1,calendarHoverBefore=-1;
    double calendarHoverAt=-1,calendarPillAt=-1,calendarWheelAt=-1;
    bool calendarPill=false;
    float viewSlide=0;  // Horizontal page offset applied around the expanded content.
    FrameClock clock;
    HMONITOR refreshMonitor = nullptr;
    LONGLONG refreshPeriod = 0;
    ULONGLONG refreshRetryAt = 0;
    double lastSeconds = 0;
    float drawnWidth = 0, drawnHeight = 0;
    // Retain a stable allocation through transitions; release expanded high
    // water pixels once the compact shape has settled.
    float canvasWidth=480,canvasHeight=260;
    ULONGLONG activeUntil = 0;
    HWINEVENTHOOK foregroundHook = nullptr;
    bool watchingOutside=false;
    bool menuOpen = false, animating = false;
    WidgetMaterialRuntime glass;
    LumeMaterialParameters lastGlass{};
    EditWidget editWidget;
    static inline std::vector<Impl*> instances;
    static inline HHOOK outsideHook=nullptr;  // Shared only while at least one island is expanded.

    Impl(HINSTANCE i, Act a, std::function<bool()> allow = {}, EditWidget edit = {})
        : instance(i), act(std::move(a)), notificationAllowed(std::move(allow)), editWidget(std::move(edit)) {}
    ~Impl() { Stop(); }

    float ScaleFactor() const {
        if(!sampleRendering)return Scale(display.size);
        // The large, strongest-bounce workspace can exceed the fixed preview
        // viewport mid-flight. Fit that foreground while keeping desktop size.
        return std::min({previewScale.x,936.f/std::max(1.f,width.x),284.f/std::max(1.f,height.x)});
    }
    // Effective DPI folds the size option into the canvas, so layouts stay in base DIP.
    unsigned CanvasDpi() const { return static_cast<unsigned>(std::lround(static_cast<float>(dpi) * ScaleFactor())); }
    int Px(float value) const { return static_cast<int>(std::lround(value * static_cast<float>(CanvasDpi()) / 96.f)); }
    bool HasMedia() const { return view.media.available; }
    bool HasCompactWidgets() const {
        return display.leftWidget!=dynamic_island::CompactWidget::None||display.rightWidget!=dynamic_island::CompactWidget::None;
    }
    bool ReadMonitor(MONITORINFO& info) const {
        if(targetMonitor){info.rcMonitor=targetMonitor->bounds;info.rcWork=targetMonitor->work;info.dwFlags=targetMonitor->primary?MONITORINFOF_PRIMARY:0;return true;}
        return GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&info)!=FALSE;
    }
    bool Glass() const { return display.material != IslandMaterial::None; }
    bool AboveApplications() const { return display.topmost || transientLayer; }
    bool Attached() const {return display.shape==IslandShape::Standard;}
    bool Wanted() const { return !suppressed; }  // The idle pill always shows.
    bool Workspace() const {
        return display.expandedWidgets.size()!=1||display.expandedWidgets.front()!=dynamic_island::ExpandedWidget::Music;
    }
    struct WorkspaceGeometry { float width,height; };
    // One row: retain each module's proportions, fitting the complete row on
    // narrow displays rather than wrapping or squeezing individual contents.
    static constexpr float WorkspacePad=18,WorkspaceTop=16,WorkspaceGap=18,ModuleHeight=144;
    float WidgetWidth(dynamic_island::ExpandedWidget value) const {
        using W=dynamic_island::ExpandedWidget;
        switch(value){case W::Music:return 232;case W::CalendarWeather:return display.calendarStyle==IslandCalendarStyle::ChineseAlmanac?IslandAlmanacWidth:300;
        case W::Focus:return 130;case W::System:return 276;case W::Todos:return 232;case W::Clock:return 200;
        case W::Countdown:return display.countdownStyle==IslandCountdownStyle::Standard?248.f:320.f;}
        return 180;
    }
    float WidgetHeight(dynamic_island::ExpandedWidget value) const {
        if(value==dynamic_island::ExpandedWidget::Countdown)
            return display.countdownStyle==IslandCountdownStyle::Standard?208.f:144.f;
        return value==dynamic_island::ExpandedWidget::CalendarWeather&&display.calendarStyle==IslandCalendarStyle::ChineseAlmanac
            ?IslandAlmanacHeight:ModuleHeight;
    }
    float WorkspaceHeight() const {
        float result=ModuleHeight;
        for(const auto item:display.expandedWidgets)result=std::max(result,WidgetHeight(item));
        return result;
    }
    float WorkspaceNaturalWidth() const {
        const auto count=display.expandedWidgets.size();
        if(!count)return 372;
        float result=WorkspacePad*2+static_cast<float>(count-1)*WorkspaceGap;
        for(const auto item:display.expandedWidgets)result+=WidgetWidth(item);
        return result;
    }
    WorkspaceGeometry WorkspaceSize() const {
        if(display.expandedWidgets.empty())return {372,156};
        const float desired=WorkspaceNaturalWidth();
        float available=CanvasWidth;
        MONITORINFO monitor{sizeof(monitor)};
        if(!sampleRendering&&ReadMonitor(monitor))
            available=std::max(280.f,static_cast<float>(monitor.rcWork.right-monitor.rcWork.left)*96.f/
                (static_cast<float>(dpi)*Scale(display.size))-24);
        available=std::min(available,CanvasWidth);
        const float fit=std::min(1.f,available/desired);
        return {desired*fit,(WorkspaceTop*2+WorkspaceHeight())*fit};
    }

    void Targets() {
        shape.target=Attached()?1.f:0.f;
        if (reminders.empty() && page == IslandPage::Reminder) page = IslandPage::Music;
        if (!Workspace() && !HasMedia() && page == IslandPage::Music && !reminders.empty()) page = IslandPage::Reminder;
        if (notificationDetail && notice) { width.target = PageWidth; height.target = PageHeight; }
        else if (notice && !expanded) { width.target = NoticeWidth; height.target = NoticeHeight; }
        else if (expanded && page == IslandPage::Reminder) { width.target = PageWidth; height.target = PageHeight; }
        else if (expanded) {
            const auto size=WorkspaceSize();
            width.target=Workspace()?size.width:ExpandedWidth;
            height.target=Workspace()?size.height:ExpandedHeight;
        }
        else if (HasMedia()) {
            width.target = CompactWidth + (hovered ? HoverGrowWidth : 0);
            height.target = CompactHeight + (hovered ? HoverGrowHeight : 0);
        } else {
            width.target = IdleWidth + (hovered ? HoverGrowWidth * .5f : 0);
            height.target = IdleHeight + (hovered ? HoverGrowHeight * .5f : 0);
        }
        if(!expanded&&!notice&&HasCompactWidgets()){
            width.target=260.f;height.target=36.f;
        }
        presence.target = Wanted() ? 1.f : 0.f;
        const bool idle = display.autoHide && GetTickCount64() >= activeUntil;
        tuck.target = idle && !hovered && !expanded && !notice ? 1.f : 0.f;
    }
    // Keeps the island out for a while after something happens, then tucks it.
    void Activity(ULONGLONG hold = IdleDelay, bool animate = true) {
        activeUntil = std::max(activeUntil, GetTickCount64() + hold);
        if (!hwnd || !display.autoHide) return;
        SetTimer(hwnd, IdleTimer, static_cast<UINT>(activeUntil - GetTickCount64()) + 20, nullptr);
        if (animate) Animate();
    }
    static bool Animations() {
        BOOL animate = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
        return animate != FALSE;
    }
    // Starts or continues the spring animation toward the current targets.
    void Animate() {
        if (!hwnd) return;
        Targets();
        if (presence.target > 0 && !visible) {
            // Appear as a small droplet that springs open, like the iOS island.
            width.Snap(width.target * .5f);
            height.Snap(height.target * .72f);
            presence.Snap(0);
            Targets();  // Snap() also moved the targets.
            Show(true);
        }
        Pulse();
        if (suppressed || !Animations()) { Settle(); return; }
        // Step once per composed display frame (DwmFlush) with a precise clock;
        // a 15 ms timer drifts against the 15.6 ms tick and visibly judders.
        if (!animating) { animating = true; lastSeconds = Seconds(); clock.Start(hwnd, FrameMessage); }
        Frame();
    }
    void Settle() {
        if (!hwnd) return;
        Targets();
        width.Snap(width.target);
        height.Snap(height.target);
        presence.Snap(presence.target);
        tuck.Snap(tuck.target);
        pageFade.Snap(1);
        shape.Snap(shape.target);
        StopClock();
        if (presence.x <= 0) { Show(false); return; }
        if (!visible) Show(true);
        Place();
    }
    void StopClock() {
        if (animating) { animating = false; clock.Pause(); }
        if (hwnd) KillTimer(hwnd, AnimationTimer);
        trajectory.clear();
        keyframes.clear();
        shapeFrames.clear();
    }
    // The motion is sampled ahead (every TrajectoryStepMs, from the current
    // state to rest) whenever it starts or its targets change. Each frame reads
    // it at the moment the frame reaches the screen, and the glass is handed
    // the same curve, so the capsule's glass follows it on every refresh even
    // when this thread runs a frame late (it repeated a size, then jumped).
    static constexpr float TrajectoryStepMs = 2;
    struct Motion { float x[6], v[6]; };
    std::vector<Motion> trajectory;
    LONGLONG trajectoryStart = 0;
    float trajectoryTargets[6]{};
    IslandAnimationSpeed trajectorySpeed=IslandAnimationSpeed::Normal;
    IslandBounce trajectoryBounce=IslandBounce::Normal;
    Motion sampled{};
    std::vector<LumeMaterialKeyframe> keyframes;
    std::vector<float> shapeFrames;
    std::array<Spring*, 6> Springs() { return {&width, &height, &presence, &tuck, &pageFade,&shape}; }
    static LONGLONG QpcFrequency() {
        static const LONGLONG frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
        return frequency;
    }
    // The refresh this frame is shown on: the next vblank of the island's own
    // display, as the glass counts it. (DWM's timing info follows the fastest
    // monitor, 180 Hz next to a 100 Hz island, and was off by up to 8 ms.)
    void InvalidateDisplayTiming() {
        refreshMonitor = nullptr;
        refreshPeriod = 0;
        refreshRetryAt = 0;
    }
    LONGLONG NextDisplayQpc() {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const HMONITOR current = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        if (!refreshPeriod || current != refreshMonitor ||
            (refreshRetryAt && GetTickCount64() >= refreshRetryAt)) {
            // Display-mode enumeration belongs to configuration changes, not
            // every frame. Keep checking the monitor so crossing screens still
            // picks the correct period without changing the motion's clock.
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            MONITORINFOEXW monitor{};
            monitor.cbSize = sizeof(monitor);
            const bool read = GetMonitorInfoW(current, &monitor) &&
                EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1;
            const double hz = read ? mode.dmDisplayFrequency : 60.0;
            refreshMonitor = current;
            refreshPeriod = static_cast<LONGLONG>(static_cast<double>(QpcFrequency()) / hz);
            // Hot-plug and driver recovery may briefly make enumeration fail.
            // Retry without turning that transient fallback into a permanent
            // 60 Hz cache or querying a broken driver on every display frame.
            refreshRetryAt = read ? 0 : GetTickCount64() + 1000;
        }
        const LONGLONG period = refreshPeriod;
        const LONGLONG vblank = clock.LastVBlank();
        if (!vblank || now.QuadPart - vblank > 4 * period) return now.QuadPart + period;
        return vblank + std::max<LONGLONG>(1, (now.QuadPart - vblank + period - 1) / period) * period;
    }
    bool Resting(const Motion& m) const {
        const auto s = const_cast<Impl*>(this)->Springs();
        for (int i : {0, 1, 3, 4})
            if (std::abs(m.x[i] - s[i]->target) >= .05f || std::abs(m.v[i]) >= .05f) return false;
        return std::abs(m.x[2] - s[2]->target) < .02f&&std::abs(m.x[5]-s[5]->target)<.001f&&std::abs(m.v[5])<.005f;
    }
    void BuildTrajectory(LONGLONG start) {
        auto s = Springs();
        trajectory.clear();
        keyframes.clear();
        shapeFrames.clear();
        trajectoryStart = start;
        trajectorySpeed=display.animationSpeed;trajectoryBounce=display.bounce;
        Spring work[6];
        Motion m{};
        for (int i = 0; i < 6; ++i) { work[i] = *s[i]; m.x[i] = work[i].x; m.v[i] = work[i].v; trajectoryTargets[i] = work[i].target; }
        trajectory.push_back(m);
        sampled = m;
        SpringParameters parameters[6];
        for(int i=0;i<6;++i)parameters[i]=MotionParameters(i,display);
        for (int n = 0; n < 2000 && !Resting(m); ++n) {
            for (int i = 0; i < 6; ++i) work[i].Step(TrajectoryStepMs / 1000, parameters[i].stiffness, parameters[i].damping);
            work[2].x = std::clamp(work[2].x, 0.f, 1.f);
            for (int i = 0; i < 6; ++i) { m.x[i] = work[i].x; m.v[i] = work[i].v; }
            trajectory.push_back(m);
        }
    }
    double TrajectoryIndex(LONGLONG at) const {
        return static_cast<double>(at - trajectoryStart) * 1000.0 / static_cast<double>(QpcFrequency()) / TrajectoryStepMs;
    }
    void SampleTrajectory(LONGLONG at) {
        const double t = std::clamp(TrajectoryIndex(at), 0.0, static_cast<double>(trajectory.size() - 1));
        const auto index = static_cast<std::size_t>(t);
        const auto& a = trajectory[index];
        const auto& b = trajectory[std::min(index + 1, trajectory.size() - 1)];
        const float f = static_cast<float>(t - static_cast<double>(index));
        auto s = Springs();
        for (int i = 0; i < 6; ++i) {
            s[i]->x = sampled.x[i] = a.x[i] + (b.x[i] - a.x[i]) * f;
            s[i]->v = sampled.v[i] = a.v[i] + (b.v[i] - a.v[i]) * f;
        }
    }
    void Frame() {
        const LONGLONG shownAt = NextDisplayQpc();
        auto s = Springs();
        // Something snapped a spring since the last sample: start again from it.
        bool untouched = !trajectory.empty();
        for (int i = 0; i < 6 && untouched; ++i) untouched = s[i]->x == sampled.x[i] && s[i]->v == sampled.v[i];
        if (untouched) SampleTrajectory(shownAt);
        bool retarget = !untouched || trajectorySpeed!=display.animationSpeed || trajectoryBounce!=display.bounce;
        for (int i = 0; i < 6; ++i) retarget |= s[i]->target != trajectoryTargets[i];
        if (retarget) BuildTrajectory(shownAt);
        const bool done = TrajectoryIndex(shownAt) >= static_cast<double>(trajectory.size() - 1);
        if (done) {
            StopClock();
            pageFade.Snap(1);
            width.Snap(width.target);
            height.Snap(height.target);
            presence.Snap(presence.target);
            tuck.Snap(tuck.target);
            shape.Snap(shape.target);
            if (presence.x <= 0) { Show(false); return; }
        }
        Place();
    }
    // Playback bars are drawn on the compact capsule and the music page only;
    // the repaint timer runs just while they are visible.
    bool BarsShown() const {
        if (notice && notice->transient && notice->audioLevel)
            return visible && !suppressed && notice->presentationActive && tuck.target < 1 &&
                (!notice->audioPlaying || notice->audioPlaying());
        return visible && !suppressed && view.media.playing && !notice && tuck.target < 1 &&
               (!expanded ? !HasCompactWidgets() :
                page == IslandPage::Music && (!Workspace()||dynamic_island::HasExpandedWidget(display.expandedWidgets,dynamic_island::ExpandedWidget::Music)));
    }
    void Pulse() {
        if (!hwnd) return;
        if (BarsShown()) SetTimer(hwnd, BarsTimer, notice && notice->voiceCaption ? 30 : 80, nullptr);
        else KillTimer(hwnd, BarsTimer);
    }
    void Show(bool show) {
        if (!hwnd || visible == show) return;
        if(!show)StopClockFlip();
        visible = show;
        Pulse();
        if (!show) { DetachGlass(); window_layer::WantHidden(hwnd); ShowWindow(hwnd, SW_HIDE); return; }
        Place();
        if (AboveApplications())
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        else {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            desktop_level::Place(hwnd, true);
        }
    }
    bool Place() {
        if (!hwnd || !canvas || !visible) return false;
        MONITORINFO info{sizeof(info)};
        if (!ReadMonitor(info)) return false;
        if (!Draw()) return false;
        // Centre on the composed bitmap so every animation frame stays symmetric.
        const int center = info.rcWork.left + (info.rcWork.right - info.rcWork.left) / 2;
        // Tucked, the capsule slides up past the very top of the screen, leaving a
        // sliver that the pointer can reach to bring it back.
        const float mix=std::clamp(shape.x,0.f,1.f);
        const float floating=static_cast<float>(info.rcWork.top+Px(TopMargin));
        const float shown=floating+(static_cast<float>(info.rcMonitor.top)-floating)*mix;
        const int liveWidth = std::max(1, Px(drawnWidth)), liveHeight = std::max(1, Px(drawnHeight));
        const float tucked = static_cast<float>(info.rcMonitor.top + Px(TuckedSliver) - liveHeight);
        POINT at{center - liveWidth / 2,
                 static_cast<LONG>(std::lround(shown + (tucked - shown) * std::clamp(tuck.x, 0.f, 1.2f)))};
        const auto opacity = static_cast<BYTE>(std::lround(std::clamp(presence.x, 0.f, 1.f) * 255));
        if (!surface.Upload(bitmap, static_cast<unsigned>(liveWidth), static_cast<unsigned>(liveHeight)) ||
            !surface.Compose(hwnd, at, {liveWidth, liveHeight}, {}, opacity)) return false;
        SubmitGlass();
        return true;
    }

    static float RadiusFor(float h) {
        const float e = std::clamp((h - CompactHeight) / (ExpandedHeight - CompactHeight), 0.f, 1.f);
        return std::min(h / 2, h / 2 + (30 - h / 2) * e);
    }
    float Radius() const { return RadiusFor(height.x); }
    // The glass's copy of the trajectory: where Place puts the capsule at each
    // sample, in DIP from the window's current origin (lensOverride 2 fixes
    // that origin when the timeline is handed over).
    bool BuildKeyframes() {
        MONITORINFO info{sizeof(info)};
        RECT window{};
        if (!ReadMonitor(info) || !GetWindowRect(hwnd, &window)) return false;
        const float px = static_cast<float>(CanvasDpi()) / 96, dipScale = static_cast<float>(dpi) / 96, scale = ScaleFactor();
        const float center = static_cast<float>(info.rcWork.left + (info.rcWork.right - info.rcWork.left) / 2);
        const float floating=static_cast<float>(info.rcWork.top+Px(TopMargin));
        keyframes.clear();
        keyframes.reserve(trajectory.size());
        shapeFrames.clear();shapeFrames.reserve(trajectory.size());
        for (const auto& m : trajectory) {
            const float mix=std::clamp(m.x[5],0.f,1.f);
            const float shown=floating+(static_cast<float>(info.rcMonitor.top)-floating)*mix;
            const float w = std::clamp(m.x[0], 1.f, CanvasWidth), h = std::clamp(m.x[1], 1.f, CanvasHeight);
            const float tucked = static_cast<float>(info.rcMonitor.top + Px(TuckedSliver)) - h * px;
            const float y = shown + (tucked - shown) * std::clamp(m.x[3], 0.f, 1.2f);
            LumeMaterialKeyframe k{};
            k.lensX = (center - w * px / 2 - static_cast<float>(window.left)) / dipScale;
            k.lensY = (y - static_cast<float>(window.top)) / dipScale;
            k.lensWidth = w * scale;
            k.lensHeight = h * scale;
            k.opacity = std::clamp(m.x[2], 0.f, 1.f);
            k.cornerRadius = RadiusFor(h) * scale;
            keyframes.push_back(k);
            shapeFrames.push_back(mix);
        }
        return true;
    }
    // Glass follows the capsule's live size, radius and fade.
    void SubmitGlass() {
        if (!Glass() || !visible || suppressed) { DetachGlass(); return; }
        const float scale = ScaleFactor();
        LumeMaterialParameters p{};
        p.size = sizeof(p);
        p.owner = hwnd;
        p.frosted = display.material == IslandMaterial::Frosted;
        p.opticalProfile = 5u; // One continuous capsule-to-attached outline.
        p.islandShape=std::clamp(shape.x,0.f,1.f);
        p.lightForeground = 1;
        p.reducedMotion = Animations() ? 0u : 1u;
        p.cornerRadius = Radius() * scale;
        p.opacityEnabled = 1;
        p.opacity = std::clamp(presence.x, 0.f, 1.f);
        // Frosted glass disables interaction input in the material worker. Keep
        // a stable sentinel so pointer motion over the island cannot turn into
        // a stream of identical visual material updates while it is expanding.
        if (p.frosted) { p.contactX = -.62f; p.contactY = -.85f; }
        else MaterialContact(hwnd, width.x * scale, height.x * scale, dpi, p.contactX, p.contactY);
        const bool timeline = animating && trajectory.size() > 1 && p.reducedMotion == 0 &&
            (!keyframes.empty() || BuildKeyframes());
        if (timeline) {
            // Radius, opacity and shape are sampled from the immutable timeline
            // by the glass worker. Keep the scalar sentinel stable so frosted
            // glass does not wake that worker once per UI frame for values it
            // deliberately ignores.
            p.islandShape = 0;
            // Repeated unchanged while the motion runs; the glass plays it.
            const auto& k = keyframes.front();
            p.lensOverride = 2;
            p.lensX = k.lensX; p.lensY = k.lensY; p.lensWidth = k.lensWidth; p.lensHeight = k.lensHeight;
            p.opacity = k.opacity;
            p.cornerRadius = k.cornerRadius;
            p.timeline = keyframes.data();
            p.timelineCount = static_cast<std::uint32_t>(keyframes.size());
            p.timelineStepMs = TrajectoryStepMs;
            p.timelineStart = trajectoryStart;
            p.islandShapeTimeline=shapeFrames.data();
        }
        if (timeline && p.frosted && lastGlass.owner == p.owner &&
            lastGlass.timeline == p.timeline && lastGlass.timelineCount == p.timelineCount &&
            lastGlass.timelineStepMs == p.timelineStepMs && lastGlass.timelineStart == p.timelineStart &&
            lastGlass.islandShapeTimeline == p.islandShapeTimeline) {
            return;
        }
        SubmitMaterial(glass, p, lastGlass);
    }
    void DetachGlass() {
        glass.Detach();
        lastGlass = {};
    }

    // --- Drawing -------------------------------------------------------------

    float Expansion() const {
        const float full=notificationDetail?PageHeight:Workspace()&&page==IslandPage::Music?WorkspaceSize().height:ExpandedHeight;
        return std::clamp((height.x - CompactHeight) / (full - CompactHeight), 0.f, 1.f);
    }
    void FadeImage(const std::shared_ptr<const WidgetBitmap>& image, WidgetRect r, float radius, float alpha) {
        if (alpha <= .01f) return;
        if (Glass() && alpha < .5f) return;  // Glass has no solid colour to fade into.
        if (image) canvas->ImageBitmap(image, r, alpha);
        else if (Glass()) canvas->Rounded(r, radius, 0xffffff, .16f);
        else canvas->Rounded(r, radius, 0x2c2c30, 1);
        if (!Glass() && alpha < .99f) canvas->Rounded(r, radius, 0x000000, 1 - alpha);  // Background is black.
    }
    void Bars(float x, float bottom, float span, int count, float barWidth, float gap, float alpha) {
        const auto& media = view.media;
        // Other players publish no spectrum: animate a gentle synthetic beat instead.
        const bool live = std::any_of(media.spectrum.begin(), media.spectrum.end(), [](float v) { return v > .01f; });
        const float time = static_cast<float>(GetTickCount64() % 100000) / 1000.f;
        for (int i = 0; i < count; ++i) {
            const auto index = static_cast<std::size_t>(i) * media.spectrum.size() / static_cast<std::size_t>(count);
            const float synthetic = .45f + .35f * std::sin(time * (6.1f + 1.7f * static_cast<float>(i)) + static_cast<float>(i) * 1.9f) *
                                               std::sin(time * 2.3f + static_cast<float>(i));
            float level = !media.playing ? .2f : live ? std::clamp(media.spectrum[index] * 1.1f + media.energy * .15f, .15f, 1.f)
                                                      : std::clamp(synthetic, .18f, 1.f);
            const float bar = std::max(barWidth, span * level);
            canvas->Rounded({x + static_cast<float>(i) * (barWidth + gap), bottom - bar - (span - bar) / 2, barWidth, bar},
                            barWidth / 2, accent, alpha * (media.playing ? 1.f : .45f));
        }
    }
    // ---- lyrics ----
    // The island picks the line from the playback position, carried forward
    // from the last snapshot while playing, so lines change on time between
    // the (up to one second apart) snapshots. A change rolls the lines up: the
    // old one slides out above as the new one rises from below. A line wider
    // than its space moves sideways with the singing, so its end shows by the
    // time it is sung. Repainted by the bars timer, every frame while rolling.
    ULONGLONG mediaAt = 0;  // when view.media arrived
    static constexpr float RollMilliseconds = 420;
    int lyricIndex = -1, lyricFrom = -1;
    const void* lyricSet = nullptr;
    ULONGLONG lyricChanged = 0;
    bool fastBars = false;
    double Position() const {
        const auto& media = view.media;
        double position = media.position;
        if (media.playing && (media.positionKnown || (media.app.empty() && media.duration > 0)))
            position += static_cast<double>(GetTickCount64() - mediaAt) / 1000;
        return media.duration > 0 ? std::min(position, media.duration) : position;
    }
    // The line containing position; -1 before the first line and in gaps
    // more than three seconds past a line end.
    int CueAt(double position) const {
        const auto& cues = *view.media.lyrics;
        int index = -1;
        for (int i = 0; i < static_cast<int>(cues.size()); ++i) { if (cues[i].start > position + .05) break; index = i; }
        if (index < 0 || cues[index].text.empty() || position > cues[index].end + 3) return -1;
        return index;
    }
    void TrackLyric() {
        const void* set = view.media.lyrics.get();
        const int index = set ? CueAt(Position()) : -1;
        if (set != lyricSet) { lyricSet = set; lyricIndex = index; lyricFrom = -1; lyricChanged = 0; return; }
        if (index != lyricIndex) { lyricFrom = lyricIndex; lyricIndex = index; lyricChanged = GetTickCount64(); }
    }
    bool Rolling() const { return lyricChanged && GetTickCount64() - lyricChanged < static_cast<ULONGLONG>(RollMilliseconds); }
    float Roll() const {
        if (!Rolling()) return 1;
        const float t = std::clamp(static_cast<float>(GetTickCount64() - lyricChanged) / RollMilliseconds, 0.f, 1.f);
        return 1 - (1 - t) * (1 - t) * (1 - t);  // ease out
    }
    bool ShowLyric() {
        if (!view.media.playing || !view.media.lyrics) return false;
        TrackLyric();
        return lyricIndex >= 0 || (Rolling() && lyricFrom >= 0);
    }
    // One cue in a row: moved sideways by singing progress when too wide.
    void CueLine(int index, WidgetRect area, float size, std::uint32_t rgb, float alpha, bool current) {
        if (index < 0 || alpha <= .01f) return;
        const auto& cues = *view.media.lyrics;
        if (index >= static_cast<int>(cues.size())) return;
        const auto& cue = cues[index];
        const float textWidth = canvas->MeasureText(cue.text, size, WidgetTextWeight::Medium);
        if (textWidth <= area.width) {
            canvas->Text(cue.text, area, size, rgb, alpha, WidgetTextWeight::Medium);
            return;
        }
        float offset = 0;
        if (current) {
            const double length = std::max(1.0, cue.end - cue.start);
            const double progress = std::clamp((Position() - cue.start - .4) / (length * .85), 0.0, 1.0);
            offset = static_cast<float>(progress) * (textWidth - area.width + 12);
        }
        canvas->ScrollText(cue.text, area, size, offset, rgb, alpha);
    }
    // The current line in area, rolling up from the previous one. With next,
    // the following line shows dimmer in the row below and rolls up with it.
    void Lyric(WidgetRect area, float size, std::uint32_t rgb, float alpha, const WidgetRect* next = nullptr) {
        const float roll = Roll(), rise = area.height + 4;
        const float bottom = next ? next->y + next->height : area.y + area.height;
        if (canvas->PushClip({area.x, area.y - 2, area.width, bottom - area.y + 4})) {
            if (roll < 1) CueLine(lyricFrom, {area.x, area.y - rise * roll, area.width, area.height}, size, rgb, alpha * (1 - roll), false);
            const float from = next ? next->y - area.y : rise;
            CueLine(lyricIndex, {area.x, area.y + from * (1 - roll), area.width, area.height}, size, rgb,
                    alpha * (next ? .45f + .55f * roll : roll), true);
            if (next && lyricIndex >= 0)
                CueLine(lyricIndex + 1, {next->x, next->y + (next->height + 4) * (1 - roll), next->width, next->height}, size * .92f,
                        rgb, alpha * .42f * roll, false);
            canvas->PopClip();
        }
        // Smooth rolling: repaint every frame while lines move.
        if (!hwnd) return;
        if (Rolling() && !fastBars) { fastBars = true; SetTimer(hwnd, BarsTimer, 16, nullptr); }
        else if (!Rolling() && fastBars) { fastBars = false; Pulse(); }
    }
    void Compact(float w, float h, float alpha) {
        if(alpha<=.01f)return;
        if(HasCompactWidgets()){
            const float inset=12+dynamic_island::IslandShoulder(h)*std::clamp(shape.x,0.f,1.f);
            DrawCompact(*canvas,display.leftWidget,{inset,1,98,h-2},compactData,weather,performanceSample,view.english,alpha);
            DrawCompact(*canvas,display.rightWidget,{w-inset-98,1,98,h-2},compactData,weather,performanceSample,view.english,alpha,true);
            return;
        }
        if (!HasMedia()) return;
        // The shoulders belong to the outline, outside the compact content lane.
        const float attached=std::clamp(shape.x,0.f,1.f),inset=dynamic_island::IslandShoulder(h)*attached;
        const float art = h - 12, pad = (h - art) / 2;
        FadeImage(cover, {pad + 2 + inset, pad, art, art}, art * .3f, alpha);
        const float barsWidth = 4 * 3 + 3 * 2.5f;
        const float barsLeft=w-inset-pad-4-barsWidth;
        if(attached>.01f) {
            // Five short upright bars stay centered and readable even when paused.
            const auto& media=view.media;
            const bool live=std::any_of(media.spectrum.begin(),media.spectrum.end(),[](float v){return v>.01f;});
            const float time=static_cast<float>(GetTickCount64()%100000)/1000.f;
            for(int i=0;i<5;++i) {
                const float resting=.35f+.45f*(1-std::abs(static_cast<float>(i)-2)/2);
                const auto sample=static_cast<std::size_t>(i)*(media.spectrum.size()-1)/4;
                const float level=!media.playing?resting:live?std::clamp(media.spectrum[sample]+media.energy*.12f,0.f,1.f):
                    std::clamp(resting+.22f*std::sin(time*(4.5f+static_cast<float>(i))+.9f*static_cast<float>(i)),0.f,1.f);
                const float bar=5+11*level;
                canvas->Rounded({barsLeft+static_cast<float>(i)*4.25f,(h-bar)/2,2.5f,bar},1.f,
                                accent,alpha*attached*(media.playing?.82f:.66f));
            }
        }
        if(attached<.99f)Bars(barsLeft,h-pad,art*.82f,4,3,2.5f,alpha*(1-attached));
        const float left = pad + art + 10 + inset, right = w - inset - pad - barsWidth - 12;
        if (ShowLyric()) Lyric({left, (h - 18) / 2, right - left, 18}, 12, 0xffffff, .92f * alpha);
        else canvas->Text(FitWeightedText(*canvas, view.media.title, right - left, 12, WidgetTextWeight::Medium), {left, (h - 18) / 2, right - left, 18},
                     12, 0xffffff, .86f * alpha, WidgetTextWeight::Medium);
    }
    void Control(int id, float cx, float cy, float alpha) {
        // On glass a dark grey disc read as a black hole: lighten it like the
        // other hover highlights; on the black capsule it stays a soft grey.
        if (hoverHit == id) {
            if (Glass()) canvas->Rounded({cx - 20, cy - 18, 40, 36}, 18, 0xffffff, .14f * alpha);
            else canvas->Rounded({cx - 20, cy - 18, 40, 36}, 18, 0x2c2c2e, alpha);
        }
        if (id == 2) {
            if (view.media.playing) {
                canvas->Rounded({cx - 7, cy - 10, 5, 20}, 2, 0xffffff, alpha);
                canvas->Rounded({cx + 2, cy - 10, 5, 20}, 2, 0xffffff, alpha);
            } else Triangle(*canvas, cx + 1, cy, 18, 22, false, alpha);
            return;
        }
        const bool left = id == 1;
        Triangle(*canvas, cx - (left ? 6.f : -6.f), cy, 10, 15, left, alpha);
        Triangle(*canvas, cx + (left ? 6.f : -6.f), cy, 10, 15, left, alpha);
    }
    void Expanded(float w, float alpha) {
        if (alpha <= .01f) return;
        if (!HasMedia()) {
            canvas->Text(view.english ? L"Nothing playing" : L"暂无播放内容", {18, 54, w - 36, 24}, 16,
                         0xffffff, .88f * alpha, WidgetTextWeight::Semibold, WidgetTextAlign::Center);
            canvas->Text(view.english ? L"Play music to see controls and lyrics" : L"播放音乐后显示控制与歌词", {18, 84, w - 36, 20}, 12,
                         0xa1a1a6, alpha, WidgetTextWeight::Normal, WidgetTextAlign::Center);
            return;
        }
        const auto& media = view.media;
        FadeImage(cover, {18, 18, 58, 58}, 12, alpha);
        const float textWidth = w - 88 - 52;
        canvas->Text(FitWeightedText(*canvas, media.title, textWidth, 15, WidgetTextWeight::Semibold), {88, 22, textWidth, 22}, 15,
                     0xffffff, alpha, WidgetTextWeight::Semibold);
        const WidgetRect below{88, 66, textWidth, 18};
        if (ShowLyric()) Lyric({88, 46, textWidth, 20}, 12.5f, 0xe5e5ea, alpha, &below);
        else canvas->Text(FitWeightedText(*canvas, media.artist, textWidth, 12.5f, WidgetTextWeight::Normal), {88, 46, textWidth, 20}, 12.5f,
                     0xa1a1a6, alpha);
        Bars(w - 18 - 5 * 3 - 4 * 2.5f, 58, 26, 5, 3, 2.5f, alpha);
        const auto elapsed = Duration(static_cast<unsigned>(std::max(0.0, media.position)));
        const auto remaining = L"-" + Duration(static_cast<unsigned>(std::max(0.0, media.duration - media.position)));
        canvas->Text(elapsed, {18, 89, 42, 16}, 11, 0xffffff, .6f * alpha);
        canvas->Text(remaining, {w - 60, 89, 42, 16}, 11, 0xffffff, .6f * alpha, WidgetTextWeight::Normal,
                     WidgetTextAlign::Right);
        const float barLeft = 64, barWidth = w - 128;
        canvas->Rounded({barLeft, 95, barWidth, 4}, 2, 0xffffff, .22f * alpha);
        const float fraction = media.duration > 0 ? static_cast<float>(std::clamp(media.position / media.duration, 0.0, 1.0)) : 0;
        if (fraction > 0) canvas->Rounded({barLeft, 95, barWidth * fraction, 4}, 2, 0xffffff, .92f * alpha);
        const float cx = w / 2;
        Control(1, cx - 72, 128, alpha);
        Control(2, cx, 128, alpha);
        Control(3, cx + 72, 128, alpha);
    }
    // --- Calendar module ---------------------------------------------------------
    static constexpr std::uint32_t CalendarRed=0xff453a;
    static constexpr double MonthSeconds=.34,DaySeconds=.32,HoverSeconds=.14,PillSeconds=.2;
    void EnsureCalendar() {
        if(calendarYear)return;
        calendarYear=today.year;calendarMonth=today.month;calendarSelected=today;
    }
    void ResetCalendar() {
        calendarYear=0;EnsureCalendar();
        monthMotion.start=dayMotion.start=calendarHoverAt=calendarPillAt=-1;
        calendarHover=calendarHoverBefore=-1;calendarPill=false;
    }
    static float Progress(double start,double seconds) {
        return start<0?1.f:static_cast<float>(std::clamp((Seconds()-start)/seconds,0.,1.));
    }
    bool CalendarAnimating() const {
        const double now=Seconds();
        const auto live=[&](double start,double seconds){return start>=0&&now-start<seconds;};
        return live(monthMotion.start,MonthSeconds)||live(dayMotion.start,DaySeconds)||
            live(calendarHoverAt,HoverSeconds)||live(calendarPillAt,PillSeconds);
    }
    void CalendarTick() {
        if(!hwnd)return;
        if(!CalendarAnimating()){KillTimer(hwnd,CalendarTimer);if(!animating)Place();return;}
        if(!animating)Place();
    }
    double StartCalendarMotion() {
        if(!hwnd||!Animations())return -1;
        SetTimer(hwnd,CalendarTimer,15,nullptr);
        return Seconds();
    }
    void ShiftCalendarMonth(int step) {
        EnsureCalendar();
        const calendar::Date from{calendarYear,calendarMonth,1};
        int month=calendarMonth+step,year=calendarYear;
        while(month<1){month+=12;--year;}
        while(month>12){month-=12;++year;}
        if(year<1901||year>2099)return;
        calendarYear=year;calendarMonth=month;
        monthMotion={StartCalendarMotion(),step>0?1:-1,from};
        Place();
    }
    void SelectCalendarDay(calendar::Date day) {
        EnsureCalendar();
        if(day==calendarSelected)return;
        dayMotion={StartCalendarMotion(),day>calendarSelected?1:-1,calendarSelected};
        calendarSelected=day;
        Place();
    }
    void CalendarToday() {
        EnsureCalendar();
        if(calendarYear!=today.year||calendarMonth!=today.month){
            const calendar::Date from{calendarYear,calendarMonth,1};
            const int direction=calendar::Date{today.year,today.month,1}>from?1:-1;
            calendarYear=today.year;calendarMonth=today.month;
            monthMotion={StartCalendarMotion(),direction,from};
        }
        SelectCalendarDay(today);
        Place();
    }
    // Soft round pill with optional dimmed suffix; returns its width.
    float CalendarTag(float x,float y,const std::wstring& label,const std::wstring& suffix,bool hot,float alpha) {
        auto& c=*canvas;
        const float labelWidth=c.MeasureText(label,10.5f,WidgetTextWeight::Medium);
        const float suffixWidth=suffix.empty()?0:c.MeasureText(suffix,10.5f,WidgetTextWeight::Medium)+5;
        const float tagWidth=labelWidth+suffixWidth+16;
        c.Rounded({x,y,tagWidth,20},10,hot?CalendarRed:0xffffff,(hot?.16f:.08f)*alpha);
        c.Text(label,{x+8,y,labelWidth+2,20},10.5f,hot?0xff6961:0xffffff,alpha,WidgetTextWeight::Medium);
        if(!suffix.empty())c.Text(suffix,{x+8+labelWidth+5,y,suffixWidth,20},10.5f,0xffffff,.58f*alpha,WidgetTextWeight::Medium);
        return tagWidth;
    }
    void WeatherGlyph(float x,float y,float alpha) {
        auto& c=*canvas;
        const int code=weather.code;
        const bool clear=code<=1,rain=(code>=51&&code<=67)||(code>=80&&code<=82)||(code>=95&&code<=99),snow=(code>=71&&code<=77)||code==85||code==86;
        if(code<=2){
            if(weather.isDay)c.Ellipse({x+(clear?3.f:10.f),y+(clear?0.f:-1.f),clear?14.f:10.f,clear?14.f:10.f},0xffd60a,alpha);
            else c.AnnularSector(x+(clear?10.f:16.f),y+6,0,clear?6.f:5.f,200,220,0xf2f2f7,alpha);
            if(clear)return;
        }
        const std::uint32_t cloud=rain||snow||code>=45?0xc7c7cc:0xe5e5ea;
        c.Ellipse({x+2,y+4,10,10},cloud,alpha);
        c.Ellipse({x+7,y+1,12,12},cloud,alpha);
        c.Rounded({x,y+8,21,7},3.5f,cloud,alpha);
        if(rain)for(int i=0;i<3;++i)c.Line(x+5+static_cast<float>(i)*5,y+17,x+3+static_cast<float>(i)*5,y+21,0x64d2ff,alpha,1.5f);
        if(snow)for(int i=0;i<3;++i)c.Ellipse({x+4+static_cast<float>(i)*5,y+17,3,3},0xffffff,alpha);
    }
    std::wstring CalendarWeekday(calendar::Date d) const {
        static constexpr const wchar_t* zh[7]{L"星期日",L"星期一",L"星期二",L"星期三",L"星期四",L"星期五",L"星期六"};
        static constexpr const wchar_t* en[7]{L"Sunday",L"Monday",L"Tuesday",L"Wednesday",L"Thursday",L"Friday",L"Saturday"};
        const int index=std::clamp(calendar::Weekday(d),0,6);
        return view.english?en[index]:zh[index];
    }
    static int CalendarKey(calendar::Date d) { return d.year * 10000 + d.month * 100 + d.day; }
    const CalendarInfo& CachedCalendarInfo(calendar::Date d) {
        const int key = CalendarKey(d);
        const auto found = calendarInfo.find(key);
        if (found != calendarInfo.end()) return found->second;
        const auto lunar = calendar::LookupLunar(d);
        CalendarInfo info;
        info.lunarDate = text::Wide(lunar.lunarDate);
        if (!lunar.festivals.empty()) {
            info.mark = text::Wide(lunar.festivals.front());
            info.festival = true;
        } else if (!lunar.solarTerm.empty()) {
            info.mark = text::Wide(lunar.solarTerm);
        }
        if (calendarInfo.size() >= 256) calendarInfo.clear();
        return calendarInfo.emplace(key, std::move(info)).first->second;
    }
    std::pair<std::wstring, bool> CalendarMark(calendar::Date d) {
        const auto& info = CachedCalendarInfo(d);
        return {info.mark, info.festival};
    }
    void CalendarDayPane(calendar::Date d,float x,float y,float paneWidth,float alpha) {
        if(alpha<=.01f)return;
        auto& c=*canvas;
        const bool isToday=d==today;
        const auto weekday=CalendarWeekday(d);
        c.Text(isToday?(view.english?L"Today · "+weekday:L"今天 · "+weekday):weekday,{x,y,paneWidth,15},11,
            isToday?CalendarRed:0xffffff,(isToday?1.f:.58f)*alpha,WidgetTextWeight::Semibold);
        c.Text(std::to_wstring(d.day),{x-1,y+14,paneWidth,48},40,0xffffff,alpha,WidgetTextWeight::Bold);
        const auto& lunar=CachedCalendarInfo(d);
        const std::wstring week=view.english?L"Week "+std::to_wstring(IsoWeek(d)):L"第"+std::to_wstring(IsoWeek(d))+L"周";
        const std::wstring line=view.english||lunar.lunarDate.empty()?week:lunar.lunarDate+L" · "+week;
        c.Text(FitWeightedText(c,line,paneWidth,10.5f,WidgetTextWeight::Normal),{x,y+62,paneWidth,15},10.5f,0xffffff,.58f*alpha);
        if(isToday&&weather.available){
            WeatherGlyph(x,y+86,alpha);
            const auto temperature=std::to_wstring(weather.temperature)+L"°";
            const float tw=c.MeasureText(temperature,15,WidgetTextWeight::Semibold);
            c.Text(temperature,{x+28,y+82,tw+2,22},15,0xffffff,alpha,WidgetTextWeight::Semibold);
            const float rx=x+28+tw+7,rw=std::max(1.f,x+paneWidth-rx);
            c.Text(FitWeightedText(c,weather.condition,rw,10,WidgetTextWeight::Normal),{rx,weather.hasRange?y+80:y+86,rw,13},10,0xffffff,.58f*alpha);
            if(weather.hasRange)
                c.Text(std::to_wstring(weather.low)+L"° / "+std::to_wstring(weather.high)+L"°",{rx,y+92,rw,13},10,0xffffff,.58f*alpha);
        }
        const float tagY=y+ModuleHeight-20;
        c.PushClip({x,tagY,paneWidth+14,20});
        const auto mark=CalendarMark(d);
        if(isToday){
            if(!mark.first.empty())CalendarTag(x,tagY,(view.english?L"Today · ":L"今日 · ")+mark.first,{},mark.second,alpha);
            else for(int offset=1;offset<=60;++offset){
                const auto next=calendar::AddDays(d,offset);if(!next)break;
                const auto upcoming=CalendarMark(*next);if(upcoming.first.empty())continue;
                const std::wstring when=offset==1?(view.english?L"tomorrow":L"明天"):
                    view.english?L"in "+std::to_wstring(offset)+L" days":std::to_wstring(offset)+L" 天后";
                CalendarTag(x,tagY,upcoming.first,when,false,alpha);
                break;
            }
        }else{
            const auto days=CivilDays(d)-CivilDays(today);
            const auto count=std::to_wstring(days<0?-days:days);
            const std::wstring relative=days==1?(view.english?L"Tomorrow":L"明天"):days==-1?(view.english?L"Yesterday":L"昨天"):
                view.english?(days>0?L"In "+count+L" days":count+L" days ago"):count+(days>0?L" 天后":L" 天前");
            float at=x;
            if(!mark.first.empty())at+=CalendarTag(at,tagY,mark.first,{},mark.second,alpha)+5;
            CalendarTag(at,tagY,relative,{},false,alpha);
        }
        c.PopClip();
    }
    void CalendarHeader(int year,int month,float x,float y,float alpha) {
        if(alpha<=.01f)return;
        static constexpr const wchar_t* zh[12]{L"一月",L"二月",L"三月",L"四月",L"五月",L"六月",L"七月",L"八月",L"九月",L"十月",L"十一月",L"十二月"};
        static constexpr const wchar_t* en[12]{L"January",L"February",L"March",L"April",L"May",L"June",L"July",L"August",L"September",L"October",L"November",L"December"};
        auto& c=*canvas;
        const std::wstring name=view.english?en[month-1]:zh[month-1];
        const float nameWidth=c.MeasureText(name,13,WidgetTextWeight::Semibold);
        c.Text(name,{x,y,nameWidth+2,20},13,0xffffff,alpha,WidgetTextWeight::Semibold);
        c.Text(std::to_wstring(year),{x+nameWidth+4,y,44,20},13,0xffffff,.34f*alpha,WidgetTextWeight::Medium);
    }
    float CalendarHoverAlpha(int id) const {
        const float t=EaseOut(Progress(calendarHoverAt,HoverSeconds));
        if(id==calendarHover)return t;
        if(id==calendarHoverBefore)return 1-t;
        return 0;
    }
    void CalendarGrid(int year,int month,WidgetRect area,float alpha,bool interactive) {
        if(alpha<=.01f)return;
        auto& c=*canvas;
        const int first=(calendar::Weekday({year,month,1})+6)%7,days=calendar::DaysInMonth(year,month);
        const int rows=(first+days+6)/7;
        const float cellWidth=area.width/7,cellHeight=area.height/static_cast<float>(rows);
        for(int day=1;day<=days;++day){
            const int index=first+day-1,column=index%7,row=index/7;
            const calendar::Date date{year,month,day};
            const float cx=area.x+cellWidth*(static_cast<float>(column)+.5f),cy=area.y+cellHeight*(static_cast<float>(row)+.5f)-1;
            const bool isToday=date==today,selected=date==calendarSelected&&!isToday;
            const int id=CalendarDayHit+day;
            if(isToday)c.Ellipse({cx-9.5f,cy-9.5f,19,19},CalendarRed,alpha);
            else{
                const float hover=interactive?CalendarHoverAlpha(id):0;
                const float pop=selected?SpringSettle(Progress(dayMotion.start,DaySeconds)):1;
                const float radius=9.5f*(.6f+.4f*std::max(selected?pop:0.f,hover));
                const float fill=std::max(selected?.2f*std::min(1.f,EaseOut(Progress(dayMotion.start,DaySeconds))*1.2f):0.f,.1f*hover);
                if(fill>0)c.Ellipse({cx-radius,cy-radius,radius*2,radius*2},0xffffff,fill*alpha);
            }
            const bool weekend=column>=5;
            const bool past=date<today;
            const float shade=isToday?1.f:past?.34f:weekend?.58f:.96f;
            c.Text(std::to_wstring(day),{cx-cellWidth/2,cy-8,cellWidth,16},10.5f,0xffffff,shade*alpha,
                isToday?WidgetTextWeight::Bold:WidgetTextWeight::Medium,WidgetTextAlign::Center);
            if(const auto mark=CalendarMark(date);!mark.first.empty())
                c.Ellipse({cx-1.5f,cy+7,3,3},isToday?0xffffff:mark.second?CalendarRed:0xffffff,(isToday||mark.second?1.f:.58f)*alpha);
            if(interactive)workspaceHits.push_back({{cx-cellWidth/2,area.y+cellHeight*static_cast<float>(row),cellWidth,cellHeight},id,{}});
        }
    }
    void CalendarCard(WidgetRect card,float alpha) {
        if(alpha<=.01f)return;
        EnsureCalendar();
        if(display.calendarStyle==IslandCalendarStyle::ChineseAlmanac){
            if(!almanacReading||almanacReading->date!=calendarSelected)almanacReading=ReadIslandAlmanac(calendarSelected);
            const bool sun=calendarSelected==today&&weather.available&&!weather.failed;
            DrawIslandAlmanacCard(*canvas,card,*almanacReading,sun?weather.sunrise:L"",sun?weather.sunset:L"",view.english,alpha);
            if(calendarSelected!=today)workspaceHits.push_back({{card.x+51,card.y+54,106,96},CalendarTodayHit,{}});
            workspaceHits.push_back({card,CalendarAreaHit,{}});
            return;
        }
        auto& c=*canvas;
        const float paneWidth=std::min(104.f,card.width*.35f),gridX=card.x+paneWidth+14,gridWidth=card.x+card.width-gridX;
        // Hover transitions follow the shared hit id; record changes here so
        // the fade starts from the frame on which the pointer moved.
        const int hoverNow=hoverHit>=CalendarDayHit&&hoverHit<CalendarAreaHit?hoverHit:-1;
        if(hoverNow!=calendarHover){calendarHoverBefore=calendarHover;calendarHover=hoverNow;calendarHoverAt=StartCalendarMotion();}
        // Left: the selected day, sliding sideways when the selection changes.
        c.PushClip({card.x,card.y,paneWidth+14,card.height});
        {
            const float t=Progress(dayMotion.start,DaySeconds),slide=SpringSettle(t),fade=EaseOut(t);
            const float shift=static_cast<float>(dayMotion.direction)*10;
            if(t<1)CalendarDayPane(dayMotion.from,card.x-shift*fade,card.y,paneWidth,(1-fade)*alpha);
            CalendarDayPane(calendarSelected,card.x+shift*(1-slide),card.y,paneWidth,fade*alpha);
        }
        c.PopClip();
        // Right: month header, navigation and the month grid.
        const float monthT=Progress(monthMotion.start,MonthSeconds),monthSlide=SpringSettle(monthT),monthFade=EaseOut(monthT);
        const float monthShift=static_cast<float>(monthMotion.direction)*22;
        c.PushClip({gridX,card.y,gridWidth-46,20});
        if(monthT<1)CalendarHeader(monthMotion.from.year,monthMotion.from.month,gridX,card.y,(1-monthFade)*alpha);
        CalendarHeader(calendarYear,calendarMonth,gridX,card.y,monthFade*alpha);
        c.PopClip();
        const bool away=calendarSelected!=today||calendarYear!=today.year||calendarMonth!=today.month;
        if(away!=calendarPill){calendarPill=away;calendarPillAt=StartCalendarMotion();}
        const float pillT=EaseOut(Progress(calendarPillAt,PillSeconds)),pillShown=calendarPill?pillT:1-pillT;
        const std::wstring todayLabel=view.english?L"Today":L"今天";
        const float pillWidth=c.MeasureText(todayLabel,9.5f,WidgetTextWeight::Semibold)+14;
        const float right=gridX+gridWidth;
        if(pillShown>.01f){
            const float scale=.8f+.2f*(calendarPill?SpringSettle(Progress(calendarPillAt,PillSeconds)):1.f);
            const float pw=pillWidth*scale,ph=18*scale,px=right-46-pillWidth+(pillWidth-pw)/2,py=card.y+1+(18-ph)/2;
            const float hover=CalendarHoverAlpha(CalendarTodayHit);
            c.Rounded({px,py,pw,ph},ph/2,CalendarRed,(.14f+.1f*hover)*pillShown*alpha);
            c.Text(todayLabel,{px,py,pw,ph},9.5f*scale,CalendarRed,pillShown*alpha,WidgetTextWeight::Semibold,WidgetTextAlign::Center);
            if(calendarPill)workspaceHits.push_back({{right-46-pillWidth,card.y,pillWidth,20},CalendarTodayHit,{}});
        }
        for(int step=0;step<2;++step){
            const int id=step?CalendarNextHit:CalendarPrevHit;
            const float cx=right-32+static_cast<float>(step)*22,cy=card.y+10;
            const float hover=CalendarHoverAlpha(id);
            if(hover>0)c.Ellipse({cx-10,cy-10,20,20},0xffffff,.1f*hover*alpha);
            const float dir=step?1.f:-1.f;
            c.Line(cx-2*dir,cy-4,cx+2*dir,cy,0xffffff,(.58f+.42f*hover)*alpha,1.6f);
            c.Line(cx+2*dir,cy,cx-2*dir,cy+4,0xffffff,(.58f+.42f*hover)*alpha,1.6f);
            workspaceHits.push_back({{cx-11,cy-11,22,22},id,{}});
        }
        static constexpr const wchar_t* zhWeek=L"一二三四五六日";
        static constexpr const wchar_t* enWeek=L"MTWTFSS";
        const float cellWidth=gridWidth/7;
        for(int day=0;day<7;++day)
            c.Text(std::wstring(1,(view.english?enWeek:zhWeek)[day]),{gridX+cellWidth*static_cast<float>(day),card.y+25,cellWidth,13},8.5f,
                0xffffff,.34f*alpha,WidgetTextWeight::Medium,WidgetTextAlign::Center);
        const WidgetRect area{gridX,card.y+40,gridWidth,card.height-40};
        c.PushClip(area);
        if(monthT<1){
            c.Offset(viewSlide-monthShift*monthFade,0);
            CalendarGrid(monthMotion.from.year,monthMotion.from.month,area,(1-monthFade)*alpha,false);
        }
        c.Offset(viewSlide+monthShift*(1-monthSlide),0);
        CalendarGrid(calendarYear,calendarMonth,area,monthFade*alpha,true);
        c.Offset(viewSlide,0);
        c.PopClip();
        workspaceHits.push_back({card,CalendarAreaHit,{}});
    }
    // Today's to-dos, in the spirit of a Reminders widget: a header with a
    // completion ring, up to four rows (circle completes, title edits) and a
    // quiet "new" pill. Completed rows sink below open ones and are struck out.
    void TodoCard(WidgetRect card,float alpha) {
        if(alpha<=.01f)return;
        auto& c=*canvas;
        const float x=card.x,y=card.y,w=card.width;
        if (!todoCacheValid) {
            todoRows = todos.forDay ? todos.forDay(today) : std::vector<IslandTodo>{};
            std::stable_partition(todoRows.begin(),todoRows.end(),[](const auto& task){return !task.completed;});
            todoWritable = !todos.writable || todos.writable();
            todoCacheValid = true;
        }
        const auto& rows = todoRows;
        const auto done=static_cast<std::size_t>(std::count_if(rows.begin(),rows.end(),[](const auto& task){return task.completed;}));
        const bool writable=todoWritable;
        const std::wstring title=view.english?L"Today":L"今天";
        const float titleWidth=c.MeasureText(title,13,WidgetTextWeight::Semibold);
        c.Text(title,{x,y,titleWidth+2,20},13,0xffffff,alpha,WidgetTextWeight::Semibold);
        const std::wstring summary=rows.empty()?std::wstring{}:std::to_wstring(rows.size()-done)+(view.english?L" open":L" 项待办");
        c.Text(summary,{x+titleWidth+8,y+1,100,20},10.5f,0xffffff,.42f*alpha,WidgetTextWeight::Medium);
        // Completion ring: grey track, green arc for the completed share.
        const float rx=x+w-9,ry=y+10;
        if(!rows.empty())c.AnnularSector(rx,ry,6.5f,8.5f,0,360,0xffffff,.12f*alpha);
        if(!rows.empty()&&done)c.AnnularSector(rx,ry,6.5f,8.5f,0,360.f*static_cast<float>(done)/static_cast<float>(rows.size()),Green,alpha);
        if(rows.empty()){
            c.AnnularSector(x+w/2,y+58,13,14.5f,0,360,0xffffff,.22f*alpha);
            c.Line(x+w/2-5,y+58,x+w/2-1.5f,y+61.5f,0xffffff,.45f*alpha,1.8f);
            c.Line(x+w/2-1.5f,y+61.5f,x+w/2+5.5f,y+54.5f,0xffffff,.45f*alpha,1.8f);
            c.Text(view.english?L"Nothing due today":L"今天没有待办",{x,y+80,w,18},12,0xffffff,.82f*alpha,WidgetTextWeight::Medium,WidgetTextAlign::Center);
        }
        const std::size_t shown=std::min<std::size_t>(4,rows.size());
        for(std::size_t row=0;row<shown;++row){
            const auto& task=rows[row];
            const float top=y+28+static_cast<float>(row)*24;
            const int toggle=800+static_cast<int>(row),edit=810+static_cast<int>(row);
            const bool hot=hoverHit==toggle||hoverHit==edit;
            if(hot)c.Rounded({x-6,top-1,w+12,23},8,0xffffff,.07f*alpha);
            const std::uint32_t ring=task.color?task.color:0xffffff;
            const float cx=x+7,cy=top+10.5f;
            if(task.completed){
                c.AnnularSector(cx,cy,0,7,0,360,Green,alpha);
                c.Line(cx-3,cy,cx-.8f,cy+2.4f,0x0d2a1d,alpha,1.6f);
                c.Line(cx-.8f,cy+2.4f,cx+3.4f,cy-2.6f,0x0d2a1d,alpha,1.6f);
            }else{
                c.AnnularSector(cx,cy,5.6f,7,0,360,ring,(task.color?.95f:.5f)*alpha);
                if(hoverHit==toggle)c.AnnularSector(cx,cy,0,3,0,360,ring,.6f*alpha);
            }
            const float timeWidth=task.time.empty()?0:c.MeasureText(task.time,10,WidgetTextWeight::Medium)+8;
            const float textX=x+21,textWidth=std::max(1.f,w-21-timeWidth);
            const auto fitted=FitWeightedText(c,task.title,textWidth,11.5f,WidgetTextWeight::Medium);
            c.Text(fitted,{textX,top,textWidth,21},11.5f,0xffffff,(task.completed?.36f:.95f)*alpha,WidgetTextWeight::Medium);
            if(task.completed){
                const float strike=std::min(textWidth,c.MeasureText(fitted,11.5f,WidgetTextWeight::Medium));
                c.Line(textX,top+11,textX+strike,top+11,0xffffff,.36f*alpha,1);
            }
            if(timeWidth>0)c.Text(task.time,{x+w-timeWidth,top,timeWidth,21},10,0xffffff,(task.completed?.24f:.42f)*alpha,
                WidgetTextWeight::Medium,WidgetTextAlign::Right);
            if(writable){
                workspaceHits.push_back({{x-6,top-1,22,23},toggle,task});
                workspaceHits.push_back({{x+16,top-1,w-10,23},edit,task});
            }
        }
        const float footY=y+ModuleHeight-20;
        if(writable){
            const std::wstring add=view.english?L"New":L"新待办";
            const float addWidth=c.MeasureText(add,10.5f,WidgetTextWeight::Semibold)+28;
            const float addX=rows.empty()?x+(w-addWidth)/2:x;
            c.Rounded({addX,footY,addWidth,20},10,0x5b7bea,(hoverHit==720?.34f:.22f)*alpha);
            c.Line(addX+10,footY+10,addX+16,footY+10,0xaebfff,alpha,1.5f);
            c.Line(addX+13,footY+7,addX+13,footY+13,0xaebfff,alpha,1.5f);
            c.Text(add,{addX+20,footY,addWidth-22,20},10.5f,0xaebfff,alpha,WidgetTextWeight::Semibold);
            workspaceHits.push_back({{addX,footY,addWidth,20},720,{}});
        }
        if(rows.size()>shown)
            c.Text(view.english?L"+"+std::to_wstring(rows.size()-shown)+L" more":L"还有 "+std::to_wstring(rows.size()-shown)+L" 项",
                {x+w-90,footY,90,20},10,0xffffff,.38f*alpha,WidgetTextWeight::Medium,WidgetTextAlign::Right);
    }
    bool ClockFlipShown()const{
        return visible&&!suppressed&&expanded&&!notice&&!notificationDetail&&!sampleRendering&&tuck.target<1&&
            page==IslandPage::Music&&display.clockStyle==IslandClockStyle::Flip&&
            dynamic_island::HasExpandedWidget(display.expandedWidgets,dynamic_island::ExpandedWidget::Clock);
    }
    void StopClockFlip(){clockFlip.Stop();if(hwnd)KillTimer(hwnd,ClockFlipTimer);}
    void TickClockFlip(){
        const auto now=GetTickCount64();
        if(clockFlip.Observe(compactData.time,now,ClockFlipShown())){
            // A short-lived timer for this transition only; no idle polling or worker.
            if(!SetTimer(hwnd,ClockFlipTimer,16,nullptr))clockFlip.Stop();
        }else if(!clockFlip.Active(now))KillTimer(hwnd,ClockFlipTimer);
    }
    void ClockCard(WidgetRect card,float alpha) {
        const auto now=GetTickCount64();
        const bool flipping=ClockFlipShown()&&clockFlip.Active(now);
        DrawIslandClockCard(*canvas,card,compactData.time,view.english,display.clockStyle,alpha,
            flipping?&clockFlip.previous:nullptr,flipping?clockFlip.Progress(now):1.f);
    }
    void HorizontalCountdownCard(WidgetRect card,float alpha) {
        auto& c=*canvas;
        const float x=card.x,y=card.y,w=card.width,h=card.height;

        std::wstring count=L"—";
        std::wstring dateLabel=display.countdownCountUp?
            (view.english?L"Start date not set":L"未设置起始日"):
            (view.english?L"Target date not set":L"未设置目标日");
        std::wstring intervalLabel;
        if(display.countdownDate>0){
            if(const auto date=calendar::AddDays({1970,1,1},display.countdownDate)){
                const int delta=calendar::DaysSinceUnixEpoch(today)-display.countdownDate;
                const auto twoDigits=[](int value){return value<10?L"0"+std::to_wstring(value):std::to_wstring(value);};
                const auto displayDate=std::to_wstring(date->year)+L"-"+twoDigits(date->month)+L"-"+twoDigits(date->day);
                if(view.english){
                    dateLabel=(display.countdownCountUp?L"Start: ":L"Target: ")+displayDate;
                    if(display.countdownCountUp){
                        count=std::to_wstring(delta>=0?delta:-delta);
                        intervalLabel=delta>=0?count+L" days elapsed":count+L" days to start";
                    }else if(delta<=0){
                        count=std::to_wstring(-delta);intervalLabel=count+L" days remaining";
                    }else{count=L"0";intervalLabel=L"Target date reached";}
                }else{
                    constexpr wchar_t digits[]=L"〇一二三四五六七八九";
                    const auto chineseNumber=[&](int value){
                        if(value<10)return std::wstring(1,digits[value]);
                        return (value>=20?std::wstring(1,digits[value/10]):std::wstring{})+L"十"+
                            (value%10?std::wstring(1,digits[value%10]):std::wstring{});
                    };
                    std::wstring yearLabel;
                    for(const auto digit:std::to_wstring(date->year))yearLabel+=digits[digit-L'0'];
                    const auto dayLabel=date->day>20&&date->day<30?L"廿"+std::wstring(1,digits[date->day%10]):chineseNumber(date->day);
                    dateLabel=(display.countdownCountUp?L"起始日：":L"目标日：")+yearLabel+L"年"+
                        chineseNumber(date->month)+L"月"+dayLabel;
                    if(display.countdownCountUp){
                        count=std::to_wstring(delta>=0?delta:-delta);
                        intervalLabel=delta>=0?L"已经"+count+L"天":L"距离开始还有"+count+L"天";
                    }else if(delta<=0){
                        count=std::to_wstring(-delta);intervalLabel=L"还有"+count+L"天";
                    }else{count=L"0";intervalLabel=L"已到达目标日";}
                }
            }
        }
        const std::wstring title=display.countdownTitle.empty()?(view.english?L"Countdown":L"倒数日"):lume::text::Wide(display.countdownTitle);
        alpha*=display.countdownSolidAlpha/255.f;
        const auto tint=[&](unsigned white){
            const auto channel=[&](unsigned shift){return (((display.countdownSolidColor>>shift)&255)*(100-white)+255*white+50)/100;};
            return (channel(16)<<16)|(channel(8)<<8)|channel(0);
        };
        const auto background=tint(88),divider=tint(72);
        const auto color=display.countdownSolidColor;
        const auto brightest=std::max({(color>>16)&255,(color>>8)&255,color&255});
        const float shade=brightest>190?190.f/brightest:1.f;
        const auto ink=(static_cast<unsigned>(((color>>16)&255)*shade)<<16)|
            (static_cast<unsigned>(((color>>8)&255)*shade)<<8)|static_cast<unsigned>((color&255)*shade);
        c.Rounded(card,std::min(22.f,h*.16f),background,alpha);
        const float dividerX=x+w*.30f;
        c.Rounded({dividerX,y+h*.2f,1.5f,h*.6f},.75f,divider,alpha);
        const float numberWidth=dividerX-x-18.f;
        const float numberSize=std::min(54.f,numberWidth/(std::max<std::size_t>(1,count.size())*.62f));
        c.SerifText(count,{x+9,y+19,numberWidth,h*.52f},numberSize,ink,alpha,WidgetTextWeight::Bold,WidgetTextAlign::Center);
        c.Text(view.english?L"days":L"天",{x+9,y+h*.61f,numberWidth,22},13.f,ink,alpha,
            WidgetTextWeight::Medium,WidgetTextAlign::Center);

        const float detailX=dividerX+18.f;
        const float detailWidth=x+w-16.f-detailX;
        c.Text(FitWeightedText(c,title,detailWidth,20.f,WidgetTextWeight::Bold),
            {detailX,y+h*.20f,detailWidth,30},20.f,ink,alpha,WidgetTextWeight::Bold);
        c.Text(FitWeightedText(c,dateLabel,detailWidth,12.f,WidgetTextWeight::Normal),
            {detailX,y+h*.45f,detailWidth,22},12.f,ink,alpha*.8f,WidgetTextWeight::Normal);
        c.Text(FitWeightedText(c,intervalLabel,detailWidth,10.5f,WidgetTextWeight::Normal),
            {detailX,y+h*.65f,detailWidth,22},10.5f,ink,alpha*.8f,WidgetTextWeight::Normal);
    }
    void CountdownCard(WidgetRect card,float alpha) {
        if(display.countdownStyle!=IslandCountdownStyle::Standard){HorizontalCountdownCard(card,alpha);return;}
        auto& c=*canvas;
        const float x=card.x,y=card.y,w=card.width,h=card.height,header=40,footer=38;
        const float radius=std::clamp(std::max(22.f,std::min(w*.064705882353f,h*.061111111111f)),0.f,std::min(w,h)/2);
        // Draw each band once so translucent colors retain uniform opacity.
        if(c.PushClip({x,y,w,header})){
            c.Rounded(card,radius,display.countdownBackgroundColor,alpha*display.countdownBackgroundAlpha/255.f);c.PopClip();
        }
        if(c.PushClip({x,y+header,w,h-header-footer})){
            c.Rounded(card,radius,0xfafafa,alpha);c.PopClip();
        }
        if(c.PushClip({x,y+h-footer,w,footer})){
            c.Rounded(card,std::min(radius,footer*.5f),0xf4f3f1,alpha);c.PopClip();
        }
        const auto title=display.countdownTitle.empty()?std::wstring(view.english?L"Countdown":L"倒数日"):lume::text::Wide(display.countdownTitle);
        const auto background=display.countdownBackgroundColor;
        const auto headerInk=(((background>>16)&255)*299+((background>>8)&255)*587+(background&255)*114)>180000?0x262626u:0xffffffu;
        c.Text(FitWeightedText(c,title,w-16,15,WidgetTextWeight::Semibold),{x+8,y,w-16,header},15,headerInk,alpha,
            WidgetTextWeight::Semibold,WidgetTextAlign::Center);

        std::wstring number=L"—",dateLabel=view.english?L"Target: --":L"目标日：--";
        if(display.countdownDate>0){
            const auto target=calendar::AddDays({1970,1,1},display.countdownDate);
            if(target){
                const int days=display.countdownDate-calendar::DaysSinceUnixEpoch(today);
                number=std::to_wstring(std::max(0,days));
                dateLabel=(display.countdownCountUp?(view.english?L"Start: ":L"起始日："):
                    (view.english?L"Target: ":L"目标日："))+std::to_wstring(target->year)+L"-"+
                    std::to_wstring(target->month)+L"-"+std::to_wstring(target->day)+L" "+CalendarWeekday(*target);
                if(display.countdownCountUp){
                    const int delta=calendar::DaysSinceUnixEpoch(today)-display.countdownDate;
                    number=std::to_wstring(delta>=0?delta:-delta);
                }
                else number=std::to_wstring(std::max(0,display.countdownDate-calendar::DaysSinceUnixEpoch(today)));
            }
        }
        const float middleHeight=h-header-footer;
        const float numberWidth=c.MeasureText(number,94,WidgetTextWeight::Bold);
        const float numberSize=94*std::min(1.f,(w-24)/std::max(1.f,numberWidth));
        c.Text(number,{x+12,y+header,w-24,middleHeight},numberSize,display.countdownTextColor,alpha*display.countdownTextAlpha/255.f,
            WidgetTextWeight::Bold,WidgetTextAlign::Center);
        c.Text(FitWeightedText(c,dateLabel,w-16,11.5f,WidgetTextWeight::Normal),{x+8,y+h-footer+4,w-16,footer-7},11.5f,0x929292,alpha,
            WidgetTextWeight::Normal,WidgetTextAlign::Center);
    }
    static int WorkspaceEditAction(dynamic_island::ExpandedWidget widget) {
        return WorkspaceEditFirst+static_cast<int>(widget);
    }
    static bool Inside(WidgetRect rect,float x,float y){
        return x>=rect.x&&y>=rect.y&&x<rect.x+rect.width&&y<rect.y+rect.height;
    }
    WidgetRect EditHintRect() const {
        constexpr float hintWidth=74,hintHeight=34;
        const float left=std::clamp(static_cast<float>(editHintAnchor.x)+13,8.f,std::max(8.f,width.x-hintWidth-8));
        const float top=std::clamp(static_cast<float>(editHintAnchor.y)+14,14.f,std::max(14.f,height.x-hintHeight-8));
        return {left,top,hintWidth,hintHeight};
    }
    float EditHintProgress() const {
        return editHintAnimating?std::clamp(static_cast<float>((Seconds()-editHintAnimationAt)/EditHintAnimationSeconds),0.f,1.f):1.f;
    }
    void EditHint(float alpha) {
        if(!editWidget||!editHintVisible||!hoveredWidget||!expanded||page!=IslandPage::Music||notificationDetail)return;
        const auto target=EditHintRect();
        const float progress=EditHintProgress(),spring=SpringSettle(progress);
        const float scale=.78f+.22f*spring,opacity=EaseOut(progress)*alpha;
        const float w=target.width*scale,h=target.height*scale;
        const float x=target.x+(target.width-w)/2,y=target.y+(target.height-h)/2+(1-spring)*5;
        constexpr std::uint32_t bubble=0x22252b;
        canvas->Polygon({x+8,y+5,x+20,y+5,x+2,y-6},0x000000,.22f*opacity);
        canvas->Rounded({x+1,y+2,w,h},8,0x000000,.28f*opacity);
        canvas->Polygon({x+8,y+4,x+20,y+4,x+2,y-6},bubble,.98f*opacity);
        canvas->Rounded({x,y,w,h},8,bubble,.98f*opacity);
        canvas->Text(view.english?L"Edit":L"编辑",{x,y,w,h},11*scale,0xffffff,opacity,
            WidgetTextWeight::Medium,WidgetTextAlign::Center);
    }
    void ClearEditHint() {
        if(hwnd)KillTimer(hwnd,EditHintTimer);
        editHintVisible=false;
        editHintAnimating=false;
    }
    void UpdateEditHint(std::optional<dynamic_island::ExpandedWidget> widget,LPARAM l) {
        const float scale=96.f/static_cast<float>(CanvasDpi());
        const POINT position{static_cast<LONG>(GET_X_LPARAM(l)*scale),static_cast<LONG>(GET_Y_LPARAM(l)*scale)};
        if(widget==hoveredWidget){
            if(widget&&!editHintVisible)editHintAnchor=position;
            return;
        }
        ClearEditHint();
        hoveredWidget=widget;
        if(!widget||!editWidget||!expanded||Expansion()<.9f||notificationDetail||page!=IslandPage::Music)return;
        editHintAnchor=position;
        if(hwnd&&!SetTimer(hwnd,EditHintTimer,EditHintDelayMs,nullptr)){
            editHintVisible=true;
            editHintAnimationAt=Seconds();
        }
    }
    void WorkspaceView(float w,float,float alpha) {
        workspaceHits.clear();
        workspaceWidgets.clear();
        if(alpha<=.01f)return;
        using W=dynamic_island::ExpandedWidget;
        auto& c=*canvas;
        const auto text=[&](const std::wstring& value,WidgetRect r,float size=12,std::uint32_t color=0xffffff,float opacity=1.f){
            c.Text(FitWeightedText(c,value,r.width,size,WidgetTextWeight::Medium),r,size,color,alpha*opacity,WidgetTextWeight::Medium);
        };
        if(display.expandedWidgets.empty()){
            text(view.english?L"No enabled widgets":L"暂无启用的组件",{24,55,w-48,26},16);
            text(view.english?L"Enable music in island settings":L"可在灵动岛设置中启用音乐",{24,87,w-48,22},12,0xa1a1a6);
            return;
        }
        const float naturalWidth=WorkspaceNaturalWidth();
        // Lay out the cards once at their settled fit. Scaling every glyph by
        // the width spring made icons and text repeatedly shrink/grow during
        // the shell's bounce, with a fresh raster on every animation frame.
        const float layoutWidth=WorkspaceSize().width;
        const float rowScale=std::max(.01f,std::min(1.f,layoutWidth/naturalWidth));
        const float savedSlide=viewSlide;
        // Place() centers integer-pixel HWND widths. Mirror that arithmetic so
        // a one-pixel change in the shell cannot shift the stationary content
        // by half a pixel and alternate its antialiasing from frame to frame.
        const float px=static_cast<float>(CanvasDpi())/96;
        const float centered=static_cast<float>(Px(w)/2-Px(layoutWidth)/2)/px;
        viewSlide=(savedSlide+centered)/rowScale;
        c.Scale(rowScale);c.Offset(viewSlide,0);
        float nextX=WorkspacePad;
        for(std::size_t index=0;index<display.expandedWidgets.size();++index){
            const auto kind=display.expandedWidgets[index];
            const float cardWidth=WidgetWidth(kind),cardHeight=WidgetHeight(kind),x=nextX,y=WorkspaceTop+(WorkspaceHeight()-cardHeight)/2;
            nextX+=cardWidth+WorkspaceGap;
            const WidgetRect card{x,y,cardWidth,cardHeight};
            workspaceWidgets.push_back({card,kind});
            if(kind==W::Countdown){CountdownCard(card,alpha);continue;}
            if(kind==W::CalendarWeather){CalendarCard(card,alpha);continue;}
            if(kind==W::Clock){ClockCard(card,alpha);continue;}
            if(kind==W::System){DrawIslandPerformanceCard(c,card,performanceSample,compactData,view.english,alpha);continue;}
            c.PushClip(card);
            const float inner=cardWidth-24,cx=x+cardWidth/2;
            if(kind==W::Music){
                if(HasMedia()){
                    FadeImage(cover,{x,y+4,52,52},11,alpha);
                    c.Text(FitWeightedText(c,view.media.title,cardWidth-64,13,WidgetTextWeight::Semibold),{x+64,y+11,cardWidth-64,20},13,
                        0xffffff,alpha,WidgetTextWeight::Semibold);
                    if(ShowLyric())Lyric({x+64,y+33,cardWidth-64,18},11,0xe5e5ea,alpha);
                    else text(view.media.artist,{x+64,y+33,cardWidth-64,18},11,0xffffff,.58f);
                    const float progress=view.media.duration>0?static_cast<float>(std::clamp(Position()/view.media.duration,0.,1.)):0;
                    const float barX=x+38,barWidth=cardWidth-76;
                    c.Rounded({barX,y+85,barWidth,3},1.5f,0xffffff,.16f*alpha);
                    if(progress>0)c.Rounded({barX,y+85,barWidth*progress,3},1.5f,0xffffff,.92f*alpha);
                    c.Text(Duration(static_cast<unsigned>(std::max(0.,Position()))),{x,y+79,34,14},9.5f,0xffffff,.34f*alpha);
                    c.Text(L"-"+Duration(static_cast<unsigned>(std::max(0.,view.media.duration-Position()))),{x+cardWidth-34,y+79,34,14},9.5f,
                        0xffffff,.34f*alpha,WidgetTextWeight::Normal,WidgetTextAlign::Right);
                    for(int id=1;id<=3;++id){
                        const float at=cx+static_cast<float>(id-2)*56;
                        Control(id,at,y+124,alpha);
                        workspaceHits.push_back({{at-20,y+106,40,36},id,{}});
                    }
                }else{
                    c.AnnularSector(cx,y+46,0,23,0,360,0xffffff,.07f*alpha);
                    c.Line(cx+4,y+32,cx+4,y+54,0xffffff,.7f*alpha,2);
                    c.Line(cx+4,y+32,cx+12,y+36,0xffffff,.7f*alpha,2);
                    c.AnnularSector(cx,y+55,0,5,0,360,0xffffff,.7f*alpha);
                    c.Text(view.english?L"Nothing playing":L"暂无播放内容",{x,y+86,cardWidth,20},13,0xffffff,alpha,
                        WidgetTextWeight::Semibold,WidgetTextAlign::Center);
                    c.Text(view.english?L"Play a song to begin":L"播放音乐后显示控制",{x,y+109,cardWidth,18},11,0xffffff,.45f*alpha,
                        WidgetTextWeight::Normal,WidgetTextAlign::Center);
                }
            }else if(kind==W::Focus){
                c.Text(Duration(view.focusing?view.remaining:view.finished?0:1500),{x+10,y+23,cardWidth-20,30},24,0xffffff,alpha,WidgetTextWeight::Bold,WidgetTextAlign::Center);
                text(view.focusing?(view.english?L"in focus":L"专注中"):view.finished?(view.english?L"well done":L"专注完成"):(view.english?L"a little focus":L"一段专注"),
                    {x+13,y+53,inner,17},9,0xe9a16b,.9f);
                const float progress=view.focusing?static_cast<float>(view.remaining)/1500.f:view.finished?0.f:1.f;
                const float arm=cardWidth-36,angle=(-18-24*(1-progress))*3.14159265f/180;
                const float pivotX=x+17,pivotY=y+112;
                c.Line(pivotX,pivotY,x+cardWidth-15,pivotY+2,0xb57e4d,.5f*alpha,2);
                c.Line(pivotX,pivotY,pivotX+std::cos(angle)*arm,pivotY+std::sin(angle)*arm,0xe8b885,.85f*alpha,2);
                c.AnnularSector(pivotX,pivotY,0,4,0,360,0xe2b36f,alpha);
                c.AnnularSector(pivotX+std::cos(angle)*arm,pivotY+std::sin(angle)*arm,0,2.5f,0,360,0xffd6ad,alpha);
                const int action=view.focusing?701:700;
                const WidgetRect control{x+12,y+128,inner,26};
                if(hoverHit==action)c.Rounded(control,13,0xffffff,.07f*alpha);
                if(view.focusing)c.Rounded({cx-5,y+136,10,10},2,0xffffff,.85f*alpha);
                else Triangle(c,cx,y+141,10,12,false,.9f*alpha);
                workspaceHits.push_back({control,action,{}});
            }else if(kind==W::Todos){c.PopClip();TodoCard(card,alpha);continue;
            }
            c.PopClip();
        }
        for(auto& hit:workspaceHits){
            hit.rect.x=hit.rect.x*rowScale+savedSlide+centered;hit.rect.y*=rowScale;
            hit.rect.width*=rowScale;hit.rect.height*=rowScale;
        }
        for(auto& hit:workspaceWidgets){
            hit.rect.x=hit.rect.x*rowScale+savedSlide+centered;hit.rect.y*=rowScale;
            hit.rect.width*=rowScale;hit.rect.height*=rowScale;
        }
        viewSlide=savedSlide;c.Scale(1);c.Offset(savedSlide,0);
    }
    void VoiceBars(float x, float cy, float alpha, bool wide = false) {
        if (!notice || !notice->audioLevel) return;
        const float reading = notice->audioLevel();
        const float level = std::isfinite(reading) ? std::clamp(reading, 0.f, 1.f) : 0.f;
        constexpr float gains[]{.55f, 1.f, .78f, .42f, .82f, .61f, .95f, .5f, .88f, .58f};
        constexpr std::uint32_t colors[]{0x9bdfbd,0xbbe9cf,0xccebdc,0xe2eee5,0xdaf0e4,
            0xf4f0dc,0xf0dfce,0xf2d4b9,0xf4c9aa,0xe7bb9a};
        for (unsigned bar = 0; bar < (wide ? 10u : 4u); ++bar) {
            const float barHeight = 3.f + 25.f * level * gains[bar];
            canvas->Rounded({x + static_cast<float>(bar) * (wide ? 4.f : 5.f), cy - barHeight / 2, wide ? 2.5f : 3.f, barHeight},
                1.25f, wide ? colors[bar] : 0x83e6c5, .92f * alpha);
        }
    }
    void NoticeView(float w, float h, float alpha) {
        if (!notice || alpha <= .01f) return;
        auto& c = *canvas;
        const float cy = h / 2;
        if (notice->transient && notice->voiceCaption) {
            if (!notice->icon || !c.ImageBitmap(notice->icon, {18, cy - 22, 44, 44}, alpha))
                c.AnnularSector(40, cy, 0, 22, 0, 360, 0x83e6c5, alpha);
            constexpr float left = 76;
            const float textWidth = std::max(0.f, w - left - 74);
            c.Text(FitWeightedText(c, notice->time, textWidth, 11.5f, WidgetTextWeight::Normal),
                {left, cy - 21, textWidth, 16}, 11.5f, 0xffffff, .55f * alpha);
            const auto caption = notice->voiceCaption();
            const float size = caption.size() == 1 ? 20.f : 15.f;
            c.Text(FitWeightedText(c, caption, textWidth, size, WidgetTextWeight::Normal),
                {left, cy - 4, textWidth, 25}, size, 0xffffff, alpha, WidgetTextWeight::Normal);
            VoiceBars(w - 57, cy, alpha, true);
            return;
        }
        // Bundled icons retain their own transparent silhouette. Todo notices
        // and unavailable assets keep the existing bell fallback.
        if (!notice->transient || !notice->icon || !c.ImageBitmap(notice->icon, {17, cy - 17, 34, 34}, alpha)) {
            c.AnnularSector(34, cy, 0, 17, 0, 360, 0xff9f0a, alpha);
            c.AnnularSector(34, cy - 1.5f, 4.2f, 6.2f, -90, 180, 0xffffff, alpha);
            c.Rounded({27.8f, cy - 1.5f, 12.4f, 6}, 1.5f, 0xffffff, alpha);
            c.Rounded({26, cy + 3.5f, 16, 2}, 1, 0xffffff, alpha);
            c.AnnularSector(34, cy + 7.2f, 0, 1.8f, 0, 360, 0xffffff, alpha);
        }
        const bool voice = notice->transient && static_cast<bool>(notice->audioLevel);
        if (voice) {
            // Use the voice's current PCM envelope, not the music player's
            // transport state or a clock-driven imitation of an audio signal.
            VoiceBars(61, cy, alpha);
        }
        const float left = voice ? 90.f : 62.f, right = w - 20;
        const auto heading = notice->transient ? notice->time :
            (view.english ? L"To-do reminder" : L"待办提醒") + (notice->time.empty() ? std::wstring{} : L" · " + notice->time);
        c.Text(FitWeightedText(c, heading, right - left, 11.5f, WidgetTextWeight::Normal), {left, cy - 21, right - left, 16}, 11.5f,
               0xffffff, .55f * alpha);
        c.Text(FitWeightedText(c, notice->title, right - left, 14.5f, WidgetTextWeight::Semibold), {left, cy - 4, right - left, 22}, 14.5f,
               0xffffff, alpha, WidgetTextWeight::Semibold);
    }
    float NotificationViewportHeight() const {
        return PageHeight - 80;
    }
    void NotificationDetailView(float w, float h, float alpha) {
        if (!notice || alpha <= .01f) return;
        auto& c = *canvas;
        if (!notice->icon || !c.ImageBitmap(notice->icon, {20, 18, 28, 28}, alpha))
            c.AnnularSector(34, 32, 0, 13, 0, 360, 0xff9f0a, alpha);
        const bool voice = static_cast<bool>(notice->audioLevel);
        if (voice) VoiceBars(58, 32, alpha);
        const float headingX = voice ? 86.f : 58.f;
        c.Text(view.english ? L"Notification" : L"通知详情", {headingX, 21, w - headingX - 58, 22}, 12,
               0xffffff, .55f * alpha);
        const float closeX = w - 42;
        c.Rounded({closeX, 18, 26, 26}, 13, 0xffffff, (hoverHit == 410 ? .16f : .08f) * alpha);
        c.Line(closeX + 9, 27, closeX + 17, 35, 0xffffff, .7f * alpha, 1.5f);
        c.Line(closeX + 17, 27, closeX + 9, 35, 0xffffff, .7f * alpha, 1.5f);
        // Keep one final-width layout throughout the spring animation, so
        // glyphs do not reflow or create new text layouts on every frame.
        constexpr float textWidth = PageWidth - 40;
        const float titleHeight = c.MeasureWrappedText(notice->time, textWidth, 17, WidgetTextWeight::Semibold);
        const float bodyHeight = c.MeasureWrappedText(notice->title, textWidth, 14);
        notificationContentHeight = titleHeight + (notice->title.empty() ? 0.f : 8.f + bodyHeight);
        const float viewportHeight = NotificationViewportHeight();
        const float limit = std::max(0.f, notificationContentHeight - viewportHeight);
        notificationScroll = std::clamp(notificationScroll, 0.f, limit);
        c.PushClip({20, 60, std::max(0.f, w - 40), std::min(viewportHeight, std::max(0.f, h - 80))});
        c.WrappedText(notice->time, {20, 60 - notificationScroll, textWidth, titleHeight}, 17,
                      0xffffff, alpha, WidgetTextWeight::Semibold);
        c.WrappedText(notice->title, {20, 68 + titleHeight - notificationScroll, textWidth, bodyHeight}, 14,
                      0xffffff, .82f * alpha);
        c.PopClip();
        if (limit > 0) {
            const float thumb = std::max(16.f, viewportHeight * viewportHeight / notificationContentHeight);
            c.Rounded({w - 10, 60, 3, viewportHeight}, 1.5f, 0xffffff, .08f * alpha);
            c.Rounded({w - 10, 60 + (viewportHeight - thumb) * notificationScroll / limit, 3, thumb},
                      1.5f, 0xffffff, .4f * alpha);
        }
    }
    void OpenNotificationDetail() {
        if (!notice || !notice->transient || notice->activate || notice->voiceCaption || notificationDetail) return;
        notificationDetail = true;
        notificationScroll = notificationContentHeight = 0;
        // Preserve the capsule's existing delivery timer and hover grace period.
        // Opening the reminder does not create another delivery or history entry.
        Expand(true);
    }
    bool ShowNotice(const std::string& id, const std::wstring& title, const std::wstring& time) {
        // In the desktop layer windows may cover the island, so the reminder
        // would count as delivered unseen; the normal popup takes it instead.
        if (!hwnd || suppressed || window_layer::Suppressed(hwnd)) return false;
        if(window_layer::enabled && window_layer::ReadContext().foreignForeground)return false;
        const Notice next{id, title, time, false, {}, 0, {}};
        reminders.erase(std::remove_if(reminders.begin(), reminders.end(), [&](const Notice& n) { return n.id == id; }), reminders.end());
        reminders.insert(reminders.begin(), next);
        if (reminders.size() > static_cast<std::size_t>(MaxReminders)) reminders.resize(static_cast<std::size_t>(MaxReminders));
        // Several reminders due together are shown one after another.
        if (notice && (notice->transient || notice->id != id)) {
            if (queued.size() < static_cast<std::size_t>(MaxReminders)) queued.push_back(next);
            return true;
        }
        notice = next;
        SyncNoticeLayer();
        if (expanded) Expand(false);
        SetTimer(hwnd, NoticeTimer, NoticeMilliseconds, nullptr);
        Activity(NoticeMilliseconds);
        Animate();
        return true;
    }
    const char* NotificationBlockReason() const {
        if (!hwnd || !prefs.enabled) return "stopped";
        if (suppressed || window_layer::Suppressed(hwnd)) return "suppressed";
        MONITORINFO monitor{sizeof(monitor)}; RECT area{};
        if(ReadMonitor(monitor))area=monitor.rcWork;else GetWindowRect(hwnd,&area);
        // Use the target screen, including while the idle capsule is tucked
        // above its edge. Global BUSY can describe a different monitor.
        const bool protectedOnTarget=window_layer::Protected(area,window_layer::ReadContext(),true);
        if(protectedOnTarget)return "fullscreen";
        if(notificationAllowed)return notificationAllowed()?"ready":"unavailable";
        const auto desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
        if(!desktop)return "locked";
        wchar_t name[80]{};DWORD bytes=0;
        const bool present=GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&bytes)&&_wcsicmp(name,L"Default")==0;
        CloseDesktop(desktop);
        if(!present)return "locked";
        QUERY_USER_NOTIFICATION_STATE state=QUNS_NOT_PRESENT;
        if(FAILED(SHQueryUserNotificationState(&state)))return "system-unavailable";
        if(AllowIslandNotificationState(state,protectedOnTarget))return "ready";
        switch(state){
        case QUNS_NOT_PRESENT:return "not-present";
        case QUNS_RUNNING_D3D_FULL_SCREEN:return "exclusive-fullscreen";
        case QUNS_PRESENTATION_MODE:return "presentation";
        case QUNS_QUIET_TIME:return "quiet-time";
        default:return "system-unavailable";
        }
    }
    bool NotificationsAllowed() const {
        const auto* reason=NotificationBlockReason();
        if(notificationDisplayStatus!=reason){
            notificationDisplayStatus=reason;
            diag::Log("island-notifications",std::string("display-gate=")+reason);
        }
        return notificationDisplayStatus=="ready";
    }
    void SyncNoticeLayer() {
        const bool next = notice && notice->transient;
        if (transientLayer == next) return;
        transientLayer = next;
        if (!hwnd) return;
        window_layer::ConfigureIsland(hwnd, AboveApplications(), transientLayer || display.hideFullscreen);
        if (AboveApplications()) {
            RemovePropW(hwnd, lume::window_ids::DesktopWidgetProp);
            desktop_level::Forget(hwnd);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        } else {
            SetPropW(hwnd, lume::window_ids::DesktopWidgetProp, reinterpret_cast<HANDLE>(1));
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            desktop_level::Place(hwnd, false);
        }
    }
    void ArmNotice() {
        SyncNoticeLayer();
        if (!notice || !hwnd) return;
        const UINT duration = notice->transient ? notice->durationMs : NoticeMilliseconds;
        SetTimer(hwnd, NoticeTimer, duration, nullptr);
        PresentNotice(true);
        Activity(duration);
        Animate();
        if(notice&&notice->transient)diag::Log("island-notifications","capsule-start type="+
            (notice->coalesceKey.empty()?std::string("reminder"):notice->coalesceKey)+" durationMs="+std::to_string(duration));
    }
    void PresentNotice(bool present) {
        if (!notice || !notice->transient || notice->presentationActive == present) return;
        notice->presentationActive = present;
        if (notice->presented) notice->presented(present);
        Pulse();
    }
    bool ShowNotification(const IslandNotification& value) {
        if (!NotificationsAllowed() || value.id.empty() || value.id.size() > 256 || value.title.empty()) return false;
        if(value.coalesceKey.size()>32)return false;
        Notice next{value.id, value.body.substr(0, 512), value.title.substr(0, 128), true, value.activate,
            GetTickCount64() + 60000, notificationIcons.Load(value.icon),value.coalesceKey,
            std::clamp(value.durationMs, 1000u, 60000u), value.presented, false, value.audioLevel, value.audioPlaying, value.voiceCaption};
        if (value.immediate && (!notice || notice->id != next.id)) {
            // Timed voice reminders show at their scheduled instant. Preserve
            // ordinary content to resume afterwards, without replaying a voice.
            if (notice) {
                PresentNotice(false);
                if (!notice->presented && queued.size() < static_cast<std::size_t>(MaxReminders))
                    queued.push_front(*notice);
                notice.reset();
            }
            notificationDetail = false;
            notificationScroll = notificationContentHeight = 0;
            notice = std::move(next);
            if (expanded) Expand(false);
            ArmNotice();
            return true;
        }
        const auto sameGroup=[&](const Notice& item){return item.transient&&!next.coalesceKey.empty()&&item.coalesceKey==next.coalesceKey;};
        if(notice&&sameGroup(*notice)&&!notificationDetail){
            queued.erase(std::remove_if(queued.begin(),queued.end(),sameGroup),queued.end());
            PresentNotice(false);
            notice=std::move(next);ArmNotice();return true;
        }
        const auto group=std::find_if(queued.begin(),queued.end(),sameGroup);
        if(group!=queued.end()){*group=std::move(next);return true;}
        if (notice && notice->transient && notice->id == next.id) {
            next.expiresAt = notice->expiresAt;
            // Refreshing text for the same visible notice retains its lease.
            next.presented = notice->presented;
            next.presentationActive = notice->presentationActive;
            next.audioLevel = notice->audioLevel;
            next.audioPlaying = notice->audioPlaying;
            next.voiceCaption = notice->voiceCaption;
            notice = std::move(next); Place(); return true;
        }
        const auto duplicate = std::find_if(queued.begin(), queued.end(), [&](const Notice& item) {
            return item.transient && item.id == next.id;
        });
        if (duplicate != queued.end()) {
            next.expiresAt = duplicate->expiresAt; *duplicate = std::move(next); return true;
        }
        if (notice) {
            if (queued.size() >= static_cast<std::size_t>(MaxReminders)) return false;
            queued.push_back(std::move(next)); return true;
        }
        notice = std::move(next);
        if (expanded) Expand(false);
        ArmNotice();
        return notice && notice->transient;
    }
    void ClearNotifications() {
        queued.erase(std::remove_if(queued.begin(), queued.end(), [](const Notice& item) { return item.transient; }), queued.end());
        if (notice && notice->transient) DismissNotice();
    }
    void DismissNotice(bool all = false) {
        if (!notice) return;
        PresentNotice(false);
        if (notificationDetail) {
            notificationDetail = false;
            notificationScroll = notificationContentHeight = 0;
            Expand(false);
        }
        if(notice->transient)diag::Log("island-notifications","capsule-end type="+
            (notice->coalesceKey.empty()?std::string("reminder"):notice->coalesceKey));
        notice.reset();
        if (hwnd) KillTimer(hwnd, NoticeTimer);
        if (all) queued.clear();
        while (!queued.empty() && queued.front().transient &&
               (queued.front().expiresAt <= GetTickCount64() || !NotificationsAllowed())) queued.pop_front();
        if (!queued.empty() && hwnd && !suppressed) {
            notice = queued.front();
            queued.pop_front();
            ArmNotice();
            return;
        }
        SyncNoticeLayer();
        Activity(LeaveDelay);
        Animate();
    }
    std::vector<IslandPage> Pages() const {
        std::vector<IslandPage> pages;
        if (Workspace() || HasMedia() || reminders.empty()) pages.push_back(IslandPage::Music);
        if (!reminders.empty()) pages.push_back(IslandPage::Reminder);
        return pages;
    }
    void Dots(float w, float h, float alpha) {
        const auto pages = Pages();
        if (pages.size() < 2 || alpha <= .01f) return;
        // Pages run left to right, so the dots do too.
        float x = w / 2 - (static_cast<float>(pages.size()) * 10 + 6) / 2;
        for (std::size_t i = 0; i < pages.size(); ++i) {
            const bool current = pages[i] == page;
            const float dot = current ? 14.f : 5.f;
            canvas->Rounded({x, h - 9, dot, 5}, 2.5f, 0xffffff, alpha * (current ? .85f : .3f));
            x += dot + 5;
        }
    }
    // Reminders shown this session, with their live todo state: complete one
    // with its circle, open it in the calendar by clicking the row.
    void ReminderPage(float w, float, float alpha) {
        if (alpha <= .01f) return;
        auto& c = *canvas;
        c.AnnularSector(31, 27, 0, 9, 0, 360, 0xff9f0a, alpha);
        c.Rounded({27.5f, 23, 7, 6}, 2, 0xffffff, alpha);
        c.Rounded({26, 28, 10, 1.6f}, .8f, 0xffffff, alpha);
        c.Text(L"提醒", {48, 16, 120, 22}, 15, 0xffffff, alpha, WidgetTextWeight::Semibold);
        c.Text(L"清空", {w - 66, 19, 44, 18}, 12, 0xffffff, (hoverHit == 540 ? .9f : .5f) * alpha, WidgetTextWeight::Normal,
               WidgetTextAlign::Right);
        reminderRows.clear();
        const std::size_t rows = std::min<std::size_t>(reminders.size(), 3);
        for (std::size_t i = 0; i < rows; ++i) {
            const auto& entry = reminders[i];
            const auto live = todos.find ? todos.find(entry.id) : std::optional<IslandTodo>{};
            const float y = 44 + static_cast<float>(i) * 44;
            const WidgetRect row{14, y, w - 28, 41};
            reminderRows.push_back(row);
            const std::uint32_t tint = live && live->color ? live->color : 0xffffff;
            c.Rounded(row, 10, tint, (live && live->color ? (live->completed ? .16f : .28f) : .07f) * alpha);
            if (hoverHit == 520 + static_cast<int>(i) || hoverHit == 500 + static_cast<int>(i)) c.Rounded(row, 10, 0xffffff, .08f * alpha);
            const WidgetRect check{24, y + 11.5f, 18, 18};
            const bool done = live && live->completed;
            if (!live) c.AnnularSector(check.x + 9, check.y + 9, 7.6f, 9, 0, 360, 0xffffff, .25f * alpha);
            else if (done) {
                c.AnnularSector(check.x + 9, check.y + 9, 0, 9, 0, 360, Green, alpha);
                c.Line(check.x + 5, check.y + 9.5f, check.x + 8, check.y + 12.5f, 0xffffff, alpha, 1.8f);
                c.Line(check.x + 8, check.y + 12.5f, check.x + 13.5f, check.y + 6, 0xffffff, alpha, 1.8f);
            } else c.AnnularSector(check.x + 9, check.y + 9, 7.6f, 9, 0, 360, 0xffffff, .75f * alpha);
            const auto title = live ? live->title : entry.title;
            const float textWidth = w - 58 - 28;
            const auto fitted = FitWeightedText(c, title, textWidth, 13.5f, WidgetTextWeight::Semibold);
            c.Text(fitted, {52, y + 3, textWidth, 19}, 13.5f, 0xffffff, (done ? .5f : .95f) * alpha, WidgetTextWeight::Semibold);
            if (done) c.Line(52, y + 13, 52 + std::min(textWidth, c.MeasureText(fitted, 13.5f, WidgetTextWeight::Semibold)), y + 15,
                             0xffffff, .45f * alpha, 1);
            std::wstring detail = live ? (live->time.empty() ? entry.time : live->time) : L"已删除";
            if (live && !live->notes.empty()) detail += (detail.empty() ? L"" : L" · ") + live->notes;
            c.Text(FitWeightedText(c, detail, textWidth, 11.5f, WidgetTextWeight::Normal), {52, y + 22, textWidth, 16}, 11.5f, 0xffffff,
                   .55f * alpha);
        }
        if (reminders.size() > rows)
            c.Text(L"还有 " + std::to_wstring(reminders.size() - rows) + L" 条提醒", {22, 177, w - 44, 14}, 10.5f, 0xffffff,
                   .45f * alpha);
    }
    void SetPage(IslandPage next, int direction = 0) {
        if (next == page) return;
        if(next!=IslandPage::Music){ClearEditHint();hoveredWidget.reset();}
        if (!direction) {
            const auto pages = Pages();
            const auto from = std::find(pages.begin(), pages.end(), page), to = std::find(pages.begin(), pages.end(), next);
            direction = to >= from ? 1 : -1;
        }
        pageDirection = direction;
        page = next;
        SyncPerformance();
        pageFade.Snap(0);
        pageFade.target = 1;
        Animate();
    }
    void Wheel(int delta) {
        if (notificationDetail) {
            const float limit = std::max(0.f, notificationContentHeight - NotificationViewportHeight());
            notificationScroll = std::clamp(notificationScroll - static_cast<float>(delta) * 42.f / WHEEL_DELTA, 0.f, limit);
            if (!animating) Place();
            return;
        }
        if (notice && notice->transient && !notice->activate) { OpenNotificationDetail(); return; }
        // Over the calendar the wheel turns months; one step per gesture burst.
        if(expanded&&Workspace()&&hoverHit>CalendarDayHit&&hoverHit<=CalendarAreaHit){
            const double now=Seconds();
            if(now-calendarWheelAt>=.28){
                calendarWheelAt=now;
                if(display.calendarStyle==IslandCalendarStyle::ChineseAlmanac){
                    EnsureCalendar();
                    if(const auto next=calendar::AddDays(calendarSelected,delta<0?1:-1);next&&next->year>=1900&&next->year<=2100)
                        SelectCalendarDay(*next);
                }else ShiftCalendarMonth(delta<0?1:-1);
            }
            return;
        }
        const auto pages = Pages();
        if (pages.empty()) return;
        if (!expanded) {
            // Scrolling the capsule opens straight onto the next page.
            page = pages.size() > 1 && delta < 0 ? pages[1] : pages.front();
            Expand(true);
            return;
        }
        auto it = std::find(pages.begin(), pages.end(), page);
        const auto index = it == pages.end() ? 0 : static_cast<std::ptrdiff_t>(it - pages.begin());
        const auto count = static_cast<std::ptrdiff_t>(pages.size());
        SetPage(pages[static_cast<std::size_t>(((index + (delta < 0 ? 1 : -1)) % count + count) % count)], delta < 0 ? 1 : -1);
    }
    bool Draw() {
        const float w = std::clamp(width.x, 1.f, CanvasWidth), h = std::clamp(height.x, 1.f, CanvasHeight);
        drawnWidth = w;
        drawnHeight = h;
        if(!expanded&&width.Settled()&&height.Settled()){
            canvasWidth=480;canvasHeight=260;
        }
        if(expanded&&Workspace()&&!notificationDetail){
            const auto size=WorkspaceSize();
            canvasWidth=std::max(canvasWidth,std::min(CanvasWidth,std::ceil(size.width*1.18f)));
            canvasHeight=std::max(canvasHeight,std::min(CanvasHeight,std::ceil(size.height*1.18f)));
        }
        if (!canvas->Begin(canvasWidth,canvasHeight,CanvasDpi())) return false;
        if (view.media.artwork != coverKey) {
            coverKey = view.media.artwork;
            cover = DecodeArtwork(coverKey);
            accent = Accent(cover);
        }
        const float e = Expansion();
        const float radius = Radius();
        if (shape.x>0) {
            canvas->Rounded({0,0,w,h},0,0x000000,Glass()?1.f/255:1.f);
        } else if (Glass()) {
            // Same as the calendar widget: the material alone, plus a near-invisible
            // fill that keeps blank glass hit-testable.
            canvas->Rounded({0, 0, w, h}, radius, 0x000000, 1.f / 255);
        } else {
            canvas->Rounded({0, 0, w, h}, radius, 0x2a2a2e, 1);
            canvas->Rounded({.75f, .75f, w - 1.5f, h - 1.5f}, radius - .75f, 0x000000, 1);
        }
        if (notificationDetail && notice) {
            const float detailAlpha = std::clamp((h - NoticeHeight) / (PageHeight - NoticeHeight), 0.f, 1.f);
            NoticeView(w, h, 1 - detailAlpha);
            NotificationDetailView(w, h, detailAlpha);
            return FinishShape();
        }
        if (notice && !expanded) { NoticeView(w, h, std::clamp((h - CompactHeight) / (NoticeHeight - CompactHeight), 0.f, 1.f)); return FinishShape(); }
        // A tucked island exposes only its empty edge, never artwork or lyrics.
        const float revealed=1-std::clamp(tuck.x*1.4f,0.f,1.f);
        Compact(w, h, std::clamp(1 - e * 3, 0.f, 1.f)*revealed);
        const float openingReveal=std::clamp((e-.55f)/.45f,0.f,1.f);
        // The shell may overshoot and contract again. Content reveals once;
        // reusing that oscillation as opacity made dark print pulse on paper.
        if(Workspace())workspaceReveal=expanded?std::max(workspaceReveal,openingReveal):std::min(workspaceReveal,openingReveal);
        else workspaceReveal=openingReveal;
        const float content = workspaceReveal * std::clamp(pageFade.x, 0.f, 1.f)*revealed;
        // The new page slides in sideways while it fades in.
        const float slide = (1 - std::clamp(pageFade.x, 0.f, 1.f)) * 56 * static_cast<float>(pageDirection);
        canvas->PushClip({0, 0, w, h});
        viewSlide = slide;
        canvas->Offset(slide, 0);
        switch (page) {
        case IslandPage::Music: if(Workspace())WorkspaceView(w,h,content);
            else Expanded(w,content);
            break;
        case IslandPage::Reminder: ReminderPage(w, h, content); break;
        }
        canvas->Offset(0, 0);
        if(page==IslandPage::Music)EditHint(content);
        canvas->PopClip();
        if (e > .9f) Dots(w, h, content);
        return FinishShape();
    }

    bool FinishShape() {
        if(!canvas->Finish(bitmap))return false;
        const auto scale=static_cast<float>(CanvasDpi())/96.f;
        if(shape.x<=0)return true;
        const float w=drawnWidth*scale,h=drawnHeight*scale;
        const float radius=Radius()*scale,mix=std::clamp(shape.x,0.f,1.f);
        return shapeMask.Apply(bitmap,w,h,radius,mix,scale);
    }

    // --- Fullscreen and games ------------------------------------------------

    // Returns true when an exclusive fullscreen game, a presentation, or a
    // window covering this monitor is in front. QUNS_BUSY alone is not used: it
    // also reports fullscreen windows on other monitors.
    bool FullscreenInFront() const {
        RECT area{}; MONITORINFO monitorInfo{sizeof(monitorInfo)};
        if(hwnd)GetWindowRect(hwnd,&area);
        if(IsRectEmpty(&area)&&ReadMonitor(monitorInfo))area=monitorInfo.rcMonitor;
        return window_layer::balancedIdle || window_layer::Protected(area,window_layer::ReadContext(),display.hideFullscreen)||window_layer::Suppressed(hwnd);
    }
    void CheckForeground() {
        const bool hasNotifications = (notice && notice->transient) ||
            std::any_of(queued.begin(), queued.end(), [](const Notice& item) { return item.transient; });
        if (hasNotifications && !NotificationsAllowed()) ClearNotifications();
        const bool next = FullscreenInFront();
        if (next == suppressed) return;
        suppressed = next;
        if (suppressed) { PresentNotice(false); expanded = hoverExpanded = notificationDetail = false; notice.reset(); queued.clear(); SyncNoticeLayer(); WatchOutside(false); SyncPerformance(); Settle(); }  // Leave at once, no animation over a game.
        else Animate();
        Pulse();
    }
    static void CALLBACK ForegroundEvent(HWINEVENTHOOK hook, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
        for(const auto current:instances)
            if(current->foregroundHook==hook&&current->hwnd)PostMessageW(current->hwnd,ForegroundMessage,0,0);
    }

    // --- Lifecycle -----------------------------------------------------------

    bool Start(const widget::Preferences& next, const IslandDisplay& options) {
        prefs = next;
        if (hwnd && display.topmost!=options.topmost) Stop();
        display = options;
        todoCacheValid = false;
        calendarInfo.clear();
        if (hwnd) {
            window_layer::ConfigureIsland(hwnd,AboveApplications(),transientLayer || display.hideFullscreen);
            SyncPerformance();
            CheckForeground();Animate();return true;
        }
        WNDCLASSEXW wc{sizeof(wc)};
        wc.hInstance = instance;
        wc.lpfnWndProc = Procedure;
        wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        wc.lpszClassName = ClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        expanded = hoverExpanded = hovered = visible = false;
        InvalidateDisplayTiming();
        const DWORD style = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (display.topmost ? WS_EX_TOPMOST : 0);
        MONITORINFO target{sizeof(target)};
        ReadMonitor(target);
        hwnd = CreateWindowExW(style, ClassName, L"Lume", WS_POPUP,
            target.rcWork.left+(target.rcWork.right-target.rcWork.left)/2,target.rcWork.top,1,1,nullptr,nullptr,instance,this);
        if (!hwnd) return false;
        if(!window_layer::Register(hwnd,window_layer::Kind::Island,nullptr,display.topmost,display.hideFullscreen)){Stop();return false;}
        DragAcceptFiles(hwnd,TRUE);
        compactData.Refresh();compactData.files=static_cast<unsigned>(heldFiles.size());
        dpi = GetDpiForWindow(hwnd);
        if (!dpi) dpi = 96;
        canvas = std::make_unique<WidgetCanvas>();
        if (!display.topmost) SetPropW(hwnd, lume::window_ids::DesktopWidgetProp, reinterpret_cast<HANDLE>(1));
        if (!SetTimer(hwnd, TickTimer, 1000, nullptr)) { Stop(); return false; }
        SyncPerformance();
        instances.push_back(this);
        foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundEvent, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        suppressed = FullscreenInFront();
        Targets();
        width.Snap(width.target);
        height.Snap(height.target);
        shape.Snap(shape.target);
        presence.Snap(0);
        tuck.Snap(0);
        Activity();
        Animate();
        return true;
    }
    bool CompactPerformance() const {
        using W=dynamic_island::CompactWidget;
        const auto needsSample=[](W value){return value==W::Network||value==W::Cpu||value==W::Memory||value==W::CpuMemory;};
        return needsSample(display.leftWidget)||needsSample(display.rightWidget);
    }
    bool ExpandedPerformance() const {
        return dynamic_island::HasExpandedWidget(display.expandedWidgets,dynamic_island::ExpandedWidget::System);
    }
    // Only visible meters consume samples. The sampler reacquires a fresh
    // baseline after suspension; hidden expanded cards do not keep it warm.
    void SyncPerformance() {
        const bool want = preview != nullptr || (hwnd && (CompactPerformance()||ExpandedPerformance()));
        if (want) {
            performance.SetActive(preview!=nullptr||(window_layer::ContentVisible(hwnd)&&
                (expanded?(!notificationDetail&&page==IslandPage::Music&&ExpandedPerformance()):(!notice&&CompactPerformance()))));
            // A preview may have started sampling before the HWND existed.
            performanceTarget = hwnd;
            if (!performance.Running()) {
                performanceSample.reset();
                performance.Start();
            } else performanceSample = performance.Latest();
        } else {
            performanceTarget = nullptr;
            if (performance.Running()) performance.Stop();
            performanceSample.reset();
        }
    }
    void Stop() {
        ClearNotifications();
        notificationDetail = false;
        notificationScroll = notificationContentHeight = 0;
        expanded = false;
        // Join before destroying the HWND; late posted messages are ignored.
        performanceTarget = nullptr;
        performance.Stop();
        performanceSample.reset();
        StopClock();
        InvalidateDisplayTiming();
        WatchOutside(false);
        DetachGlass();
        if (foregroundHook) { UnhookWinEvent(foregroundHook); foregroundHook = nullptr; }
        instances.erase(std::remove(instances.begin(),instances.end(),this),instances.end());
        if (hwnd) {
            const auto old = hwnd;
            hwnd = nullptr;
            if (GetCapture() == old) ReleaseCapture();
            DestroyWindow(old);
        }
        surface.Release();
        shapeMask.Reset();
        canvas.reset();
        preview.reset();
        notificationIcons.Clear();
        notificationDisplayStatus.clear();
        bitmap = {};
        canvasWidth=480;canvasHeight=260;
        expanded = hoverExpanded = hovered = visible = false;
        pressed = hoverHit = -1;
    }
    static LRESULT CALLBACK OutsideProc(int code, WPARAM w, LPARAM l) {
        if (code == HC_ACTION &&
            (w == WM_LBUTTONDOWN || w == WM_RBUTTONDOWN || w == WM_MBUTTONDOWN)) {
            const POINT point = reinterpret_cast<const MSLLHOOKSTRUCT*>(l)->pt;
            for(const auto current:instances){
                RECT rect{};
                if(current->watchingOutside&&current->hwnd&&!current->menuOpen&&GetWindowRect(current->hwnd,&rect)&&!PtInRect(&rect,point))
                    PostMessageW(current->hwnd,OutsideMessage,0,0);
            }
        }
        return CallNextHookEx(nullptr, code, w, l);
    }
    // The hook exists only while the panel is open and is removed as soon as it
    // collapses, so normal use (and games, which force a collapse) never pay for it.
    void WatchOutside(bool watch) {
        watchingOutside=watch;
        if (watch && !outsideHook) outsideHook = SetWindowsHookExW(WH_MOUSE_LL, OutsideProc, instance, 0);
        if (!watch && outsideHook&&std::none_of(instances.begin(),instances.end(),[](const auto current){return current->watchingOutside;})) {
            UnhookWindowsHookEx(outsideHook);outsideHook=nullptr;
        }
    }
    void Expand(bool next, bool byHover = false) {
        if (!next && notificationDetail) { DismissNotice(); return; }
        if (next && notice && notice->transient && !notice->activate && !notificationDetail) { OpenNotificationDetail(); return; }
        if (!hwnd || expanded == next) return;
        if(!next){ClearEditHint();hoveredWidget.reset();}
        StopClockFlip();GetLocalTime(&compactData.time);clockFlip.Observe(compactData.time,GetTickCount64(),false);
        if (next && notice && notice->transient && !notificationDetail) ClearNotifications();
        expanded = next;
        if (next) {
            ResetCalendar();
            const auto pages = Pages();
            if (pages.empty()) { expanded = false; return; }
            if (std::find(pages.begin(),pages.end(),page)==pages.end()) page=pages.front();
            pageFade.Snap(1);
        } else if (HasMedia()) page = IslandPage::Music;
        hoverExpanded = next && byHover;
        SyncPerformance();
        WatchOutside(next);
        if (!next) Activity(LeaveDelay, false);
        KillTimer(hwnd, CollapseTimer);
        Animate();
        if (act) act(dynamic_island::Action::ExpansionChanged);
    }

    // --- Input -----------------------------------------------------------------

    int Hit(LPARAM l) const {
        const float scale = 96.f / static_cast<float>(CanvasDpi());
        const float x = static_cast<float>(GET_X_LPARAM(l)) * scale, y = static_cast<float>(GET_Y_LPARAM(l)) * scale;
        if (x < 0 || x >= width.x || y < 0 || y >= height.x) return -1;
        if(dynamic_island::MorphShapeDistance(x,y,width.x,height.x,Radius(),shape.x)>0)return -1;
        if (notice && !expanded) return 400;
        if(!expanded){
            if(display.leftWidget!=dynamic_island::CompactWidget::None&&x<120)return 600;
            if(display.rightWidget!=dynamic_island::CompactWidget::None&&x>width.x-120)return 601;
        }
        if (notificationDetail) {
            if (Expansion() < .9f) return 412;
            if (y >= 18 && y < 44 && x >= width.x - 42 && x < width.x - 16) return 410;
            return 412;
        }
        if (!expanded || Expansion() < .9f) return 0;
        if (page == IslandPage::Reminder) {
            if (y < 40 && x >= width.x - 76) return 540;
            for (std::size_t i = 0; i < reminderRows.size(); ++i) {
                const auto& r = reminderRows[i];
                if (y >= r.y && y < r.y + r.height && x >= r.x && x < r.x + r.width)
                    return (x < 48 ? 500 : 520) + static_cast<int>(i);
            }
            return 299;
        }
        if(editWidget&&editHintVisible&&hoveredWidget&&Inside(EditHintRect(),x,y))
            return WorkspaceEditAction(*hoveredWidget);
        if(Workspace()){
            for(const auto& hit:workspaceHits){const auto& r=hit.rect;
                if(x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height)return hit.action;
            }
            for(const auto& hit:workspaceWidgets){const auto& r=hit.rect;
                if(editWidget&&hit.widget==dynamic_island::ExpandedWidget::Countdown&&Inside(r,x,y))return WorkspaceEditAction(hit.widget);
            }
            return 0;
        }
        if (!HasMedia()) return 0;
        if (y >= 110 && y < 146) {
            const float cx = width.x / 2;
            if (std::abs(x - (cx - 72)) < 22) return 1;
            if (std::abs(x - cx) < 22) return 2;
            if (std::abs(x - (cx + 72)) < 22) return 3;
        }
        return y < 104 ? 0 : -1;
    }
    std::optional<dynamic_island::ExpandedWidget> WidgetAt(LPARAM l) const {
        if(!editWidget||!expanded||Expansion()<.9f||notificationDetail||page!=IslandPage::Music)return {};
        const float scale=96.f/static_cast<float>(CanvasDpi());
        const float x=static_cast<float>(GET_X_LPARAM(l))*scale,y=static_cast<float>(GET_Y_LPARAM(l))*scale;
        if(x<0||x>=width.x||y<0||y>=height.x||dynamic_island::MorphShapeDistance(x,y,width.x,height.x,Radius(),shape.x)>0)return {};
        if(editHintVisible&&hoveredWidget&&Inside(EditHintRect(),x,y))return hoveredWidget;
        if(!Workspace())return dynamic_island::ExpandedWidget::Music;
        for(const auto& hit:workspaceWidgets){const auto& r=hit.rect;
            if(x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height)return hit.widget;
        }
        return {};
    }
    void FileMenu() {
        POINT at{};GetCursorPos(&at);
        std::vector<LumeWidgetMenu::Action> actions;
        actions.push_back({view.english?L"Copy all files":L"复制全部暂存文件",1,!heldFiles.empty()});
        actions.push_back({view.english?L"Clear file tray":L"清空暂存（不删除文件）",2,!heldFiles.empty()});
        for(std::size_t i=0;i<heldFiles.size();++i)actions.push_back({std::filesystem::path(heldFiles[i]).filename().wstring(),static_cast<UINT>(100+i),true});
        if(heldFiles.empty())actions.push_back({view.english?L"Drop files onto the island":L"将文件拖到灵动岛即可暂存",0,false});
        menuOpen=true;const auto command=LumeWidgetMenu::ShowItems(hwnd,at,actions);menuOpen=false;
        if(command==2){heldFiles.clear();compactData.files=0;Activity();Animate();return;}
        std::vector<std::wstring> chosen;
        if(command==1)chosen=heldFiles;
        else if(command>=100&&command<100+heldFiles.size())chosen.push_back(heldFiles[command-100]);
        if(chosen.empty())return;
        std::size_t chars=1;for(const auto& path:chosen)chars+=path.size()+1;
        const auto memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DROPFILES)+chars*sizeof(wchar_t));if(!memory)return;
        auto* data=static_cast<DROPFILES*>(GlobalLock(memory));if(!data){GlobalFree(memory);return;}
        data->pFiles=sizeof(DROPFILES);data->fWide=TRUE;
        auto* dest=reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(data)+sizeof(DROPFILES));
        for(const auto& path:chosen){std::copy(path.begin(),path.end(),dest);dest+=path.size()+1;}
        GlobalUnlock(memory);
        bool transferred=false;
        if(OpenClipboard(hwnd)){if(EmptyClipboard())transferred=SetClipboardData(CF_HDROP,memory)!=nullptr;CloseClipboard();}
        if(!transferred)GlobalFree(memory);
        Activity();
    }
    void Click(int hit) {
        if(hit>=WorkspaceEditFirst&&hit<WorkspaceEditFirst+7){
            if(editWidget)editWidget(static_cast<dynamic_island::ExpandedWidget>(hit-WorkspaceEditFirst),hwnd);
            return;
        }
        if(hit>CalendarDayHit&&hit<=CalendarDayHit+31){EnsureCalendar();SelectCalendarDay({calendarYear,calendarMonth,hit-CalendarDayHit});return;}
        if(hit==CalendarPrevHit||hit==CalendarNextHit){ShiftCalendarMonth(hit==CalendarNextHit?1:-1);return;}
        if(hit==CalendarTodayHit){CalendarToday();return;}
        if(hit==CalendarAreaHit)return;
        if(hit==720){if(todos.edit&&(!todos.writable||todos.writable()))todos.edit({},today);return;}
        if(hit==700||hit==701){if(act)act(hit==700?dynamic_island::Action::Focus:dynamic_island::Action::CancelFocus);return;}
        if(hit>=810&&hit<814){
            const auto found=std::find_if(workspaceHits.begin(),workspaceHits.end(),[&](const auto& value){return value.action==hit;});
            if(found!=workspaceHits.end()&&found->todo&&todos.edit&&(!todos.writable||todos.writable()))todos.edit(found->todo->id,today);
            return;
        }
        if(hit>=800&&hit<804){
            const auto found=std::find_if(workspaceHits.begin(),workspaceHits.end(),[&](const auto& value){return value.action==hit;});
            if(found==workspaceHits.end()||!found->todo||!todos.toggle||(todos.writable&&!todos.writable()))return;
            const auto id=found->todo->id;
            // Resolve the current task again; store notifications can replace a row.
            const auto rows=todos.forDay?todos.forDay(today):std::vector<IslandTodo>{};
            const auto task=std::find_if(rows.begin(),rows.end(),[&](const auto& value){return value.id==id;});
            if(task!=rows.end()){const auto current=*task;todos.toggle(current);Place();}
            return;
        }
        if(hit==600||hit==601){
            using W=dynamic_island::CompactWidget;
            const auto choice=hit==600?display.leftWidget:display.rightWidget;
            if(choice==W::Files){FileMenu();return;}
            // Readouts share the capsule's normal expand action. Opening a
            // settings manager requires the explicit context-menu command.
            Expand(!expanded);
            return;
        }
        if (hit == 400) {
            if (notice && notice->transient) {
                if (notice->activate) {
                    const auto activate = notice->activate;
                    DismissNotice();
                    activate();
                } else OpenNotificationDetail();
                return;
            }
            // The reminder page lists every reminder, including any still queued.
            DismissNotice(true);
            if (!reminders.empty()) {
                page = IslandPage::Reminder;
                pageFade.Snap(1);
                Expand(true);
            }
            return;
        }
        if (hit == 410) { DismissNotice(); return; }
        if (hit == 412) return;
        if (hit == 540) { reminders.clear(); Expand(false); Animate(); return; }
        if (hit >= 500 && hit < 520 + MaxReminders) {
            const auto index = static_cast<std::size_t>(hit >= 520 ? hit - 520 : hit - 500);
            if (index >= reminders.size()) return;
            const auto id = reminders[index].id;
            if (hit >= 520) { if (todos.openTask) todos.openTask(id); return; }
            if (const auto live = todos.find ? todos.find(id) : std::optional<IslandTodo>{}; live && todos.toggle) {
                todos.toggle(*live);
                Place();
            }
            return;
        }
        if (hit == 0) { Expand(!expanded); return; }
        if (hit == 299) return;
        if (!act) return;
        using dynamic_island::Action;
        switch (hit) {
        case 1: act(Action::Previous); break;
        case 2: act(Action::Toggle); break;
        case 3: act(Action::Next); break;
        default: break;
        }
    }
    void Menu(POINT at,bool editCountdown) {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return;
        menuOpen = true;
        KillTimer(hwnd, CollapseTimer);
        const bool frosted = display.material == IslandMaterial::Frosted, none = display.material == IslandMaterial::None;
        const UINT command = LumeWidgetMenu::ShowIsland(hwnd, at, frosted, none, editCountdown);
        menuOpen = false;
        Gdiplus::GdiplusShutdown(token);
        using dynamic_island::Action;
        switch (command) {
        case 7: if (act) act(Action::MaterialNone); break;
        case 8: if (act) act(Action::MaterialLiquid); break;
        case 9: if (act) act(Action::MaterialFrosted); break;
        case 11: if (act) act(Action::Settings); break;
        case 12: if(editWidget)editWidget(dynamic_island::ExpandedWidget::Countdown,hwnd);break;
        case 1: if (act) act(Action::Close); break;  // May stop this window.
        default:
            if (hwnd && expanded && !hovered) SetTimer(hwnd, CollapseTimer, 3500, nullptr);
            break;
        }
    }
    void SyncHoverExpansion() {
        if(!hwnd)return;
        KillTimer(hwnd,HoverTimer);
        if(display.hoverExpand&&hovered&&!expanded&&!suppressed&&!menuOpen&&(!notice||!notice->transient))
            SetTimer(hwnd,HoverTimer,SnapIslandHoverDelay(display.hoverDelayMs),nullptr);
    }
    void Hover(bool inside) {
        if (inside == hovered) return;
        hovered = inside;
        if (inside) {
            KillTimer(hwnd, CollapseTimer);
            SyncHoverExpansion();
        } else {
            KillTimer(hwnd, HoverTimer);
            hoverHit = -1;hoveredWidget.reset();ClearEditHint();
            // Collapse after leaving: quickly when opened by hover, otherwise after a pause.
            if (expanded) SetTimer(hwnd, CollapseTimer, hoverExpanded ? 450 : 3500, nullptr);
            Activity(LeaveDelay, false);
        }
        Animate();
    }

    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, w, l);
        if(message==desktop_level::Message()) {
            self->CheckForeground();
            if(!self->suppressed&&!window_layer::Suppressed(hwnd)) {
                self->Animate();
                if(!self->AboveApplications()&&IsWindowVisible(hwnd))desktop_level::Place(hwnd,false);
            }
            return 0;
        }
        if (!self->AboveApplications()) {
            if (message == WM_WINDOWPOSCHANGING) desktop_level::Changing(l);
        }
        switch (message) {
        case WM_DROPFILES: {
            const auto drop=reinterpret_cast<HDROP>(w);
            if(!self->suppressed&&!window_layer::Suppressed(hwnd)&&
                (self->display.leftWidget==dynamic_island::CompactWidget::Files||self->display.rightWidget==dynamic_island::CompactWidget::Files)){
                const UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);
                for(UINT i=0;i<count&&i<64&&self->heldFiles.size()<32;++i){
                    const UINT length=DragQueryFileW(drop,i,nullptr,0);if(!length||length>32760)continue;
                    std::wstring path(length+1,L'\0');DragQueryFileW(drop,i,path.data(),length+1);path.resize(length);
                    if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)continue;
                    if(std::find(self->heldFiles.begin(),self->heldFiles.end(),path)==self->heldFiles.end())self->heldFiles.push_back(std::move(path));
                }
                self->compactData.files=static_cast<unsigned>(self->heldFiles.size());self->Activity();self->Animate();
            }
            DragFinish(drop);return 0;
        }
        case WM_CANCELMODE:
            if(window_layer::Suppressed(hwnd)) {
                self->suppressed=true;
                self->PresentNotice(false);
                self->notice.reset();self->queued.clear();self->SyncNoticeLayer();
                self->expanded=self->hoverExpanded=self->notificationDetail=false;self->WatchOutside(false);
                self->DetachGlass();self->clock.Pause();self->animating=false;
            }
            return 0;
        case ForegroundMessage: self->CheckForeground(); return 0;
        case PerformanceMessage:
            if (!self->performanceTarget.load()) return 0;  // a late sample after shutdown
            self->performanceSample = self->performance.Latest();
            // Warming a collapsed expanded-widget is data-only. Compact
            // meters and the visible performance page still repaint normally.
            if (self->visible && !self->animating &&
                (self->expanded ? (!self->notificationDetail&&self->page==IslandPage::Music&&self->ExpandedPerformance()) :
                                  (!self->notice&&self->CompactPerformance()))) self->Place();
            return 0;
        case FrameMessage:
            self->clock.Consumed();
            if (self->animating) self->Frame();
            return 0;
        case WM_MOUSEWHEEL: self->Wheel(GET_WHEEL_DELTA_WPARAM(w)); return 0;
        // Tilting the wheel or swiping sideways on a touchpad: right moves to the next page.
        case WM_MOUSEHWHEEL: self->Wheel(-GET_WHEEL_DELTA_WPARAM(w)); return 0;
        case OutsideMessage: if (!self->menuOpen) self->Expand(false); return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_TIMER:
            switch (w) {
            case TickTimer:
                if (window_layer::balancedIdle) {
                    self->SyncPerformance();
                    if (self->act) self->act(dynamic_island::Action::Tick);
                    return 0;
                }
                self->SyncPerformance();
                self->compactData.Refresh();self->compactData.files=static_cast<unsigned>(self->heldFiles.size());
                self->TickClockFlip();
                if(self->visible&&!self->animating)self->Place();
                self->CheckForeground();
                if (const auto now = Today(); now != self->today) {
                    self->today = now;
                    self->todoCacheValid = false;
                    self->calendarInfo.clear();
                    if (self->visible && !self->animating) self->Place();
                }
                // Other topmost windows may have covered the island.
                if (self->act) self->act(dynamic_island::Action::Tick);
                return 0;
            case AnimationTimer: self->Frame(); return 0;
            case CalendarTimer: self->CalendarTick(); return 0;
            case ClockFlipTimer:
                if(!self->ClockFlipShown()){self->StopClockFlip();return 0;}
                if(!self->clockFlip.Active(GetTickCount64()))self->StopClockFlip();
                if(!self->animating)self->Place();
                return 0;
            case BarsTimer:
                if (!self->BarsShown()) {
                    // Commit the final caption even when audio could not start.
                    if (self->notice && self->notice->voiceCaption && !self->animating) self->Place();
                    KillTimer(hwnd, BarsTimer);
                }
                else if (!self->animating) self->Place();  // A running animation already redraws.
                return 0;
            case HoverTimer:
                KillTimer(hwnd, HoverTimer);
                if (self->display.hoverExpand&&self->hovered&&!self->suppressed&&!self->menuOpen&&
                    (!self->notice||!self->notice->transient)) self->Expand(true, true);
                return 0;
            case EditHintTimer:
                if(self->editHintAnimating){
                    if(self->EditHintProgress()>=1){
                        self->editHintAnimating=false;
                        KillTimer(hwnd,EditHintTimer);
                    }
                    if(!self->animating)self->Place();
                    return 0;
                }
                KillTimer(hwnd,EditHintTimer);
                if(self->hovered&&self->hoveredWidget&&self->editWidget&&self->expanded&&!self->suppressed&&
                   !self->menuOpen&&!self->notificationDetail&&self->page==IslandPage::Music&&self->Expansion()>=.9f){
                    self->editHintVisible=true;
                    self->editHintAnimating=true;
                    self->editHintAnimationAt=Seconds();
                    if(!SetTimer(hwnd,EditHintTimer,16,nullptr))self->editHintAnimating=false;
                    if(!self->animating)self->Place();
                }
                return 0;
            case CollapseTimer:
                KillTimer(hwnd, CollapseTimer);
                if (!self->hovered) self->Expand(false);
                return 0;
            case NoticeTimer:
                KillTimer(hwnd, NoticeTimer);
                if (self->hovered && self->notice) SetTimer(hwnd, NoticeTimer, 2500, nullptr);  // Keep the capsule while it is being read.
                else self->DismissNotice();
                return 0;
            case IdleTimer:
                KillTimer(hwnd, IdleTimer);
                self->Animate();
                return 0;
            default: break;
            }
            break;
        case WM_LBUTTONDOWN:
            self->pressed = self->Hit(l);
            if (self->pressed >= 0) SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE: {
            self->Hover(true);
            const auto widget=self->WidgetAt(l);
            const bool changed=widget!=self->hoveredWidget;
            self->UpdateEditHint(widget,l);
            const auto hit = self->Hit(l);
            SetCursor(LoadCursorW(nullptr,hit>=WorkspaceEditFirst&&hit<WorkspaceEditFirst+7?IDC_HAND:IDC_ARROW));
            if (changed||self->hoverHit != hit) {
                self->hoverHit = hit;
                if (!self->animating) self->Place();
            }
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            return 0;
        }
        case WM_MOUSELEAVE: self->Hover(false); return 0;
        case WM_LBUTTONUP: {
            const auto hit = self->pressed;
            const bool clicked = GetCapture() == hwnd && hit >= 0 && self->Hit(l) == hit;
            self->pressed = -1;
            if (GetCapture() == hwnd) ReleaseCapture();
            if (clicked) self->Click(hit);
            return 0;
        }
        case WM_CAPTURECHANGED: self->pressed = -1; return 0;
        case WM_CONTEXTMENU: return 0;
        case WM_RBUTTONUP: {
            self->hoverHit = self->Hit(l);
            const auto widget=self->WidgetAt(l);
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(hwnd, &point);
            self->Menu(point,widget==dynamic_island::ExpandedWidget::Countdown);
            return 0;
        }
        case WM_DPICHANGED:
            self->InvalidateDisplayTiming();
            self->dpi = HIWORD(w);
            if(self->canvas)self->Animate();
            return 0;
        case WM_DISPLAYCHANGE:
            self->InvalidateDisplayTiming();
            self->Place();
            return 0;
        case WM_SETTINGCHANGE:
            self->InvalidateDisplayTiming();
            break;
        case WM_CLOSE: if (self->act) self->act(dynamic_island::Action::Close); return 0;
        case WM_NCDESTROY:
            if (!self->display.topmost) desktop_level::Forget(hwnd);
            RemovePropW(hwnd, lume::window_ids::DesktopWidgetProp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (self->hwnd == hwnd) self->hwnd = nullptr;
            break;
        default: break;
        }
        return DefWindowProcW(hwnd, message, w, l);
    }
};
IslandWindow::IslandWindow(HINSTANCE i, Act act, std::function<bool()> allow, EditWidget edit)
    : impl_(std::make_unique<Impl>(i, std::move(act), std::move(allow), std::move(edit))) {}
IslandWindow::~IslandWindow() = default;
std::optional<performance::Snapshot> IslandWindow::PerformanceReading() const { return impl_->performanceSample; }
bool IslandWindow::Start(const widget::Preferences& p, const IslandDisplay& d) { return impl_->Start(p, d); }
void IslandWindow::SetMonitor(const IslandMonitor& monitor) {
    if(impl_->targetMonitor&&*impl_->targetMonitor==monitor)return;
    impl_->targetMonitor=monitor;
    impl_->InvalidateDisplayTiming();
    if(!impl_->hwnd)return;
    impl_->DetachGlass();
    impl_->StopClock();
    RECT current{};GetWindowRect(impl_->hwnd,&current);
    SetWindowPos(impl_->hwnd,nullptr,monitor.work.left+(monitor.work.right-monitor.work.left-(current.right-current.left))/2,
        monitor.work.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    impl_->dpi=GetDpiForWindow(impl_->hwnd);if(!impl_->dpi)impl_->dpi=96;
    impl_->CheckForeground();impl_->Activity();impl_->Place();impl_->Animate();
}
void IslandWindow::Stop() { impl_->Stop(); }
bool IslandWindow::SetDisplay(const IslandDisplay& d) {
    if (!impl_->hwnd) { impl_->display = d; return true; }
    const auto previous = impl_->display;
    impl_->StopClockFlip();
    const bool startAutoHide = d.autoHide && !previous.autoHide;
    if (!impl_->Start(impl_->prefs, d)) {
        // A layering change destroys the window before creating the new one:
        // bring the previous window back so the island does not vanish.
        if (!impl_->hwnd) impl_->Start(impl_->prefs, previous);
        else impl_->display = previous;
        return false;
    }
    if (startAutoHide || d.shape!=previous.shape || d.size!=previous.size || d.material!=previous.material)
        impl_->Activity();  // Show changed appearance briefly before tucking again.
    if(d.hoverExpand!=previous.hoverExpand||d.hoverDelayMs!=previous.hoverDelayMs){
        impl_->SyncHoverExpansion();
        if(!d.hoverExpand&&impl_->hoverExpanded){
            impl_->hoverExpanded=false;
            if(impl_->expanded&&!impl_->hovered)SetTimer(impl_->hwnd,CollapseTimer,3500,nullptr);
        }
    }
    return true;
}
void IslandWindow::Update(IslandPresentation view) {
    const bool had = impl_->HasMedia();
    const auto oldId = impl_->view.media.id;
    const bool oldPlaying = impl_->view.media.playing;
    impl_->view = std::move(view);
    impl_->mediaAt = GetTickCount64();
    if (!impl_->hwnd) return;
    impl_->Pulse();
    // A new track or a play/pause change briefly brings a tucked island out.
    if (oldId != impl_->view.media.id || oldPlaying != impl_->view.media.playing) impl_->Activity();
    if (had != impl_->HasMedia()) impl_->Animate();  // Grow from idle to compact or hide.
    else if (impl_->visible && !impl_->animating && !IsIconic(impl_->hwnd)) impl_->Place();
}
void IslandWindow::Expand(bool expanded) { impl_->Expand(expanded); }
void IslandWindow::SetTodoSource(IslandTodoSource source) { impl_->todos = std::move(source); impl_->todoCacheValid = false; }
void IslandWindow::SetWeather(dynamic_island::Weather value) {
    impl_->weather = std::move(value);
    // A running animation consumes the latest data on its next display frame.
    if (impl_->hwnd && impl_->visible && !impl_->animating) impl_->Place();
}
void IslandWindow::RefreshTodos() {
    impl_->todoCacheValid = false;
    if (impl_->hwnd && impl_->visible && !impl_->animating && impl_->expanded && (impl_->page == IslandPage::Reminder || impl_->Workspace())) impl_->Place();
}
IslandPage IslandWindow::Page() const { return impl_->page; }
bool IslandWindow::ShowNotice(const std::string& id, const std::wstring& title, const std::wstring& time) {
    return impl_->ShowNotice(id, title, time);
}
bool IslandWindow::ShowNotification(const IslandNotification& value) { return impl_->ShowNotification(value); }
void IslandWindow::ClearNotifications() { impl_->ClearNotifications(); }
HWND IslandWindow::Handle() const { return impl_->hwnd; }
bool IslandWindow::Expanded() const { return impl_->expanded; }
bool IslandWindow::Shown() const { return impl_->hwnd && impl_->visible; }
WidgetBitmap IslandWindow::Frame() const { return impl_->bitmap; }
IslandPreviewFrame IslandWindow::Preview(const IslandDisplay& display,bool expanded,float advanceSeconds) {
    if(!impl_->preview) {
        impl_->preview=std::make_unique<Impl>(impl_->instance,Act{});
        auto& sample=impl_->preview->view.media;
        sample.available=true;sample.playing=true;sample.title=L"Forest";sample.artist=L"Lume";
        sample.position=35;sample.duration=180;sample.energy=.35f;
        sample.spectrum={.3f,.45f,.7f,.5f,.8f,.6f,.4f,.55f,.7f,.45f,.6f,.35f,.5f,.8f,.65f,.4f};
        wchar_t executable[32768]{};
        if(GetModuleFileNameW(nullptr,executable,32768)) {
            const auto file=std::filesystem::path(executable).parent_path()/L"ui/assets/island-preview/cover.png";
            std::ifstream input(file,std::ios::binary|std::ios::ate);
            if(input&&input.tellg()>0&&input.tellg()<100000) {
                const auto size=static_cast<DWORD>(input.tellg());std::vector<BYTE> bytes(size);input.seekg(0);
                if(input.read(reinterpret_cast<char*>(bytes.data()),size)) {
                    DWORD count=0;
                    if(CryptBinaryToStringA(bytes.data(),size,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&count)) {
                        std::string encoded(count,'\0');
                        if(CryptBinaryToStringA(bytes.data(),size,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,encoded.data(),&count)) {
                            if(!encoded.empty()&&encoded.back()=='\0')encoded.pop_back();
                            sample.artwork="data:image/png;base64,"+encoded;
                        }
                    }
                }
            }
        }
    }
    auto& p=*impl_->preview;
    impl_->compactData.Refresh();impl_->compactData.files=static_cast<unsigned>(impl_->heldFiles.size());
    impl_->SyncPerformance();impl_->performanceSample=impl_->performance.Latest();
    p.compactData=impl_->compactData;p.weather=impl_->weather;p.performanceSample=impl_->performanceSample;p.view.english=impl_->view.english;
    wchar_t time[16]{};swprintf_s(time,L"%02u:%02u",p.compactData.time.wHour,p.compactData.time.wMinute);
    p.view.time=time;p.today=Today();p.todos.forDay=impl_->todos.forDay;
    p.view.remaining=impl_->view.remaining;p.view.focusing=impl_->view.focusing;p.view.finished=impl_->view.finished;
    if(!p.canvas)p.canvas=std::make_unique<WidgetCanvas>();
    p.display=display;p.dpi=96;p.page=IslandPage::Music;p.mediaAt=GetTickCount64();
    p.expanded=expanded;p.hovered=false;p.suppressed=false;p.pageFade.Snap(1);p.tuck.Snap(0);
    const bool first=!p.sampleRendering;
    p.sampleRendering=true;
    p.Targets();p.previewScale.target=Scale(display.size);
    if(first||advanceSeconds<=0||!Impl::Animations()) {
        p.width.Snap(p.width.target);p.height.Snap(p.height.target);p.shape.Snap(p.shape.target);p.previewScale.Snap(p.previewScale.target);
    } else {
        const float advance=std::clamp(advanceSeconds,.001f,.05f);
        const int steps=static_cast<int>(std::ceil(advance/(Impl::TrajectoryStepMs/1000)));
        const float dt=advance/static_cast<float>(steps);
        const auto width=MotionParameters(0,display),height=MotionParameters(1,display),shape=MotionParameters(5,display);
        for(int i=0;i<steps;++i) {
            p.width.Step(dt,width.stiffness,width.damping);p.height.Step(dt,height.stiffness,height.damping);
            p.shape.Step(dt,shape.stiffness,shape.damping);p.previewScale.Step(dt,shape.stiffness,shape.damping);
        }
    }
    const bool moving=!p.width.Settled()||!p.height.Settled()||
        std::abs(p.shape.x-p.shape.target)>.001f||std::abs(p.shape.v)>.005f||
        std::abs(p.previewScale.x-p.previewScale.target)>.001f||std::abs(p.previewScale.v)>.005f;
    if(!moving){p.width.Snap(p.width.target);p.height.Snap(p.height.target);p.shape.Snap(p.shape.target);p.previewScale.Snap(p.previewScale.target);}
    if(!p.Draw())return {};
    IslandPreviewFrame result;result.width=static_cast<unsigned>(p.Px(p.drawnWidth));result.height=static_cast<unsigned>(p.Px(p.drawnHeight));
    result.cornerRadius=p.Radius()*p.ScaleFactor();result.accent=p.accent;
    result.shapeMix=std::clamp(p.shape.x,0.f,1.f);result.topOffset=8*p.ScaleFactor()*(1-result.shapeMix);
    result.animating=moving;
    if(result.width>p.bitmap.width||result.height>p.bitmap.height)return {};
    result.pixels.width=result.width;result.pixels.height=result.height;
    result.pixels.pixels.resize(static_cast<std::size_t>(result.width)*result.height);
    for(unsigned y=0;y<result.height;++y)
        std::copy_n(p.bitmap.pixels.data()+static_cast<std::size_t>(y)*p.bitmap.width,result.width,
                    result.pixels.pixels.data()+static_cast<std::size_t>(y)*result.width);
    return result;
}
void IslandWindow::ReleasePreview(){impl_->preview.reset();impl_->SyncPerformance();}
std::wstring IslandWindow::CompactReadings(){
    impl_->compactData.Refresh();impl_->compactData.files=static_cast<unsigned>(impl_->heldFiles.size());
    impl_->performanceSample=impl_->performance.Latest();
    auto rows=nlohmann::json::array();
    for(int i=1;i<static_cast<int>(dynamic_island::CompactWidget::Count);++i){
        const auto value=ReadCompact(static_cast<dynamic_island::CompactWidget>(i),impl_->compactData,impl_->weather,impl_->performanceSample,impl_->view.english);
        rows.push_back({{"id",i},{"text",text::Utf8(value.text)},{"secondary",text::Utf8(value.secondary)},
            {"available",value.available},{"progress",value.progress?nlohmann::json(*value.progress):nlohmann::json(nullptr)}});
    }
    const auto result=rows.dump(-1,' ',true);return {result.begin(),result.end()};
}
void IslandWindow::Settle() { impl_->Settle(); }
}
namespace lume::platform::windows {
std::shared_ptr<const WidgetBitmap> DecodeMediaArtwork(const std::string& value) { return DecodeArtwork(value); }
}
