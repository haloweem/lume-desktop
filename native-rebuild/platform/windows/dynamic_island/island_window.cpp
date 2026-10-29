#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "platform/windows/text_util.h"
#include "platform/windows/dynamic_island/island_weather_page.h"
#include "platform/windows/performance/performance_monitor.h"
#include "platform/windows/performance/performance_view.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include "platform/windows/widget_ui/layered_surface.h"
#include "platform/windows/widget_ui/text_fit.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/dock/frame_clock.h"
#include "core/calendar/lunar_calendar.h"
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

namespace lume::platform::windows {
namespace {
constexpr wchar_t ClassName[] = L"LumeWindowsDynamicIsland";
constexpr UINT_PTR TickTimer = 1, AnimationTimer = 2, HoverTimer = 3, CollapseTimer = 4, BarsTimer = 5, IdleTimer = 6, NoticeTimer = 7;
constexpr UINT ForegroundMessage = WM_APP + 0x6e2, OutsideMessage = WM_APP + 0x6e3, FrameMessage = WM_APP + 0x6e4,
               PerformanceMessage = WM_APP + 0x6e5;
// Base sizes in DIP before the size option scales them.
constexpr float IdleWidth = 120, IdleHeight = 30;
constexpr float CompactWidth = 236, CompactHeight = 34;
constexpr float HoverGrowWidth = 12, HoverGrowHeight = 3;
constexpr float ExpandedWidth = 372, ExpandedHeight = 156;
constexpr float PageWidth = 404, PageHeight = 208;  // Calendar and todo pages.
constexpr float NoticeWidth = 340, NoticeHeight = 62;
constexpr UINT NoticeMilliseconds = 9000;
constexpr int MaxReminders = 10;
// The canvas keeps this fixed size during animations, so no bitmap or render
// target is reallocated per frame; each frame presents only the live area.
constexpr float CanvasWidth = 420, CanvasHeight = 216;
constexpr std::uint32_t Green = 0x30d158, Blue = 0x0a84ff;
constexpr float TopMargin = 8;
// Auto-hide: how long without activity before tucking, and what stays visible.
constexpr ULONGLONG IdleDelay = 3000, LeaveDelay = 1200;
constexpr float TuckedSliver = 6;
constexpr std::size_t MaxArtworkBytes = 2 * 1024 * 1024;
using Microsoft::WRL::ComPtr;

float Scale(IslandSize size) { return size == IslandSize::Small ? .86f : size == IslandSize::Large ? 1.16f : 1.f; }

calendar::Date Today() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    return {now.wYear, now.wMonth, now.wDay};
}
std::wstring Wide(std::string_view text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring value(static_cast<std::size_t>(std::max(count, 0)), L'\0');
    if (count > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), value.data(), count);
    return value;
}
// A day's festival name (the calendar widget's data); holiday spans are not repeated.
std::wstring Festival(const calendar::LunarDetails& lunar) {
    if (!lunar.festivals.empty()) return Wide(lunar.festivals.front());
    return {};
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
}  // namespace

struct IslandWindow::Impl {
    HINSTANCE instance;
    Act act;
    HWND hwnd = nullptr;
    unsigned dpi = 96;
    widget::Preferences prefs;
    IslandDisplay display;
    IslandPresentation view;
    std::unique_ptr<WidgetCanvas> canvas;
    LayeredSurface surface;
    WidgetBitmap bitmap;
    std::shared_ptr<const WidgetBitmap> cover;
    std::string coverKey;
    std::uint32_t accent = 0x30d158;
    bool expanded = false, hoverExpanded = false, hovered = false, suppressed = false, visible = false;
    // A maximized window is in front on the island's monitor: tuck away from its
    // tabs and title bar; hovering the sliver still brings the island out.
    bool maximized = false;
    int pressed = -1, hoverHit = -1;
    Spring width, height, presence, tuck;  // tuck: 0 shown, 1 against the screen top.
    Spring pageFade;  // 0 right after a page change, 1 settled.
    IslandPage page = IslandPage::Music;
    bool pageChosen = false;  // The user picked the page since the island last opened.
    calendar::Date month = Today(), selected = Today(), today = Today();
    IslandTodoSource todos;
    dynamic_island::Weather weather;  // Projection of the weather component.
    // The performance page samples only while it is open: the worker posts
    // here, and the UI thread reads the latest snapshot.
    std::atomic<HWND> performanceTarget{nullptr};
    PerformanceMonitor performance{[this] {
        if (const HWND target = performanceTarget.load()) PostMessageW(target, PerformanceMessage, 0, 0);
    }};
    std::optional<performance::Snapshot> performanceSample;
    bool performanceFailed = false;
    // id "msg:<exe>" marks a chat message: app names the program, target is
    // its window (activated on click). Chat messages never join reminders.
    struct Notice { std::string id; std::wstring title, time, app; HWND target = nullptr; std::uint32_t color = 0xff9f0a; };
    std::optional<Notice> notice;
    std::vector<Notice> reminders;  // Newest first, at most MaxReminders.
    std::deque<Notice> queued;      // Notices waiting for the current one to finish.
    int pageDirection = 1;          // +1 slides in from the right, -1 from the left.
    std::vector<IslandTodo> todoRows;
    std::vector<WidgetRect> todoChecks;
    std::vector<WidgetRect> reminderRows;
    FrameClock clock;
    double lastSeconds = 0;
    float drawnWidth = 0, drawnHeight = 0;
    ULONGLONG activeUntil = 0;
    HWINEVENTHOOK foregroundHook = nullptr;
    HHOOK outsideHook = nullptr;  // Only while expanded: a click elsewhere collapses.
    bool menuOpen = false, animating = false;
    WidgetMaterialRuntime glass;
    LumeMaterialParameters lastGlass{};
    static inline Impl* active = nullptr;

    Impl(HINSTANCE i, Act a) : instance(i), act(std::move(a)) {}
    ~Impl() { Stop(); }

    float ScaleFactor() const { return Scale(display.size); }
    // Effective DPI folds the size option into the canvas, so layouts stay in base DIP.
    unsigned CanvasDpi() const { return static_cast<unsigned>(std::lround(static_cast<float>(dpi) * ScaleFactor())); }
    int Px(float value) const { return static_cast<int>(std::lround(value * static_cast<float>(CanvasDpi()) / 96.f)); }
    bool HasMedia() const { return view.media.available; }
    bool Glass() const { return display.material != IslandMaterial::None; }
    bool Wanted() const { return !suppressed; }  // The idle pill always shows.

    void Targets() {
        if ((!HasMedia() && page == IslandPage::Music) || (reminders.empty() && page == IslandPage::Reminder)) page = IslandPage::Calendar;
        // Media drops out briefly between tracks or while a player restarts its
        // session. The Calendar page above is only a fallback: once media is back
        // the island shows music again unless the user picked a page since it opened.
        else if (HasMedia() && page == IslandPage::Calendar && !pageChosen) page = IslandPage::Music;
        if (notice && !expanded) { width.target = NoticeWidth; height.target = NoticeHeight; }
        else if (expanded && page == IslandPage::Music) { width.target = ExpandedWidth; height.target = ExpandedHeight; }
        else if (expanded) { width.target = PageWidth; height.target = PageHeight; }
        else if (HasMedia()) {
            width.target = CompactWidth + (hovered ? HoverGrowWidth : 0);
            height.target = CompactHeight + (hovered ? HoverGrowHeight : 0);
        } else {
            width.target = IdleWidth + (hovered ? HoverGrowWidth * .5f : 0);
            height.target = IdleHeight + (hovered ? HoverGrowHeight * .5f : 0);
        }
        presence.target = Wanted() ? 1.f : 0.f;
        const bool idle = display.autoHide && GetTickCount64() >= activeUntil;
        tuck.target = (idle || maximized) && !hovered && !expanded && !notice ? 1.f : 0.f;
    }
    // Keeps the island out for a while after something happens, then tucks it.
    void Activity(ULONGLONG hold = IdleDelay) {
        activeUntil = std::max(activeUntil, GetTickCount64() + hold);
        if (!hwnd || !display.autoHide) return;
        SetTimer(hwnd, IdleTimer, static_cast<UINT>(activeUntil - GetTickCount64()) + 20, nullptr);
        Animate();
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
    }
    // The motion is sampled ahead (every TrajectoryStepMs, from the current
    // state to rest) whenever it starts or its targets change. Each frame reads
    // it at the moment the frame reaches the screen, and the glass is handed
    // the same curve, so the capsule's glass follows it on every refresh even
    // when this thread runs a frame late (it repeated a size, then jumped).
    static constexpr float TrajectoryStepMs = 2;
    struct Motion { float x[5], v[5]; };
    std::vector<Motion> trajectory;
    LONGLONG trajectoryStart = 0;
    float trajectoryTargets[5]{};
    Motion sampled{};
    std::vector<LumeMaterialKeyframe> keyframes;
    std::array<Spring*, 5> Springs() { return {&width, &height, &presence, &tuck, &pageFade}; }
    static LONGLONG QpcFrequency() {
        static const LONGLONG frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart; }();
        return frequency;
    }
    // The refresh this frame is shown on: the next vblank of the island's own
    // display, as the glass counts it. (DWM's timing info follows the fastest
    // monitor, 180 Hz next to a 100 Hz island, and was off by up to 8 ms.)
    LONGLONG NextDisplayQpc() const {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        MONITORINFOEXW monitor{};
        monitor.cbSize = sizeof(monitor);
        const double hz = GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor) &&
                EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1
            ? mode.dmDisplayFrequency : 60.0;
        const LONGLONG period = static_cast<LONGLONG>(static_cast<double>(QpcFrequency()) / hz);
        const LONGLONG vblank = clock.LastVBlank();
        if (!vblank || now.QuadPart - vblank > 4 * period) return now.QuadPart + period;
        return vblank + std::max<LONGLONG>(1, (now.QuadPart - vblank + period - 1) / period) * period;
    }
    bool Resting(const Motion& m) const {
        const auto s = const_cast<Impl*>(this)->Springs();
        for (int i : {0, 1, 3, 4})
            if (std::abs(m.x[i] - s[i]->target) >= .05f || std::abs(m.v[i]) >= .05f) return false;
        return std::abs(m.x[2] - s[2]->target) < .02f;
    }
    void BuildTrajectory(LONGLONG start) {
        auto s = Springs();
        trajectory.clear();
        keyframes.clear();
        trajectoryStart = start;
        Spring work[5];
        Motion m{};
        for (int i = 0; i < 5; ++i) { work[i] = *s[i]; m.x[i] = work[i].x; m.v[i] = work[i].v; trajectoryTargets[i] = work[i].target; }
        trajectory.push_back(m);
        sampled = m;
        static constexpr float stiffness[5] = {210, 260, 300, 170, 260}, damping[5] = {21, 24, 34, 24, 30};
        for (int n = 0; n < 2000 && !Resting(m); ++n) {
            for (int i = 0; i < 5; ++i) work[i].Step(TrajectoryStepMs / 1000, stiffness[i], damping[i]);
            work[2].x = std::clamp(work[2].x, 0.f, 1.f);
            for (int i = 0; i < 5; ++i) { m.x[i] = work[i].x; m.v[i] = work[i].v; }
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
        for (int i = 0; i < 5; ++i) {
            s[i]->x = sampled.x[i] = a.x[i] + (b.x[i] - a.x[i]) * f;
            s[i]->v = sampled.v[i] = a.v[i] + (b.v[i] - a.v[i]) * f;
        }
    }
    void Frame() {
        const LONGLONG shownAt = NextDisplayQpc();
        auto s = Springs();
        // Something snapped a spring since the last sample: start again from it.
        bool untouched = !trajectory.empty();
        for (int i = 0; i < 5 && untouched; ++i) untouched = s[i]->x == sampled.x[i] && s[i]->v == sampled.v[i];
        if (untouched) SampleTrajectory(shownAt);
        bool retarget = !untouched;
        for (int i = 0; i < 5; ++i) retarget |= s[i]->target != trajectoryTargets[i];
        if (retarget) BuildTrajectory(shownAt);
        const bool done = TrajectoryIndex(shownAt) >= static_cast<double>(trajectory.size() - 1);
        if (done) {
            StopClock();
            pageFade.Snap(1);
            width.Snap(width.target);
            height.Snap(height.target);
            presence.Snap(presence.target);
            tuck.Snap(tuck.target);
            if (presence.x <= 0) { Show(false); return; }
        }
        Place();
    }
    // Playback bars are drawn on the compact capsule and the music page only;
    // the repaint timer runs just while they are visible.
    bool BarsShown() const {
        return visible && !suppressed && view.media.playing && !notice && tuck.target < 1 &&
               (!expanded || page == IslandPage::Music);
    }
    void Pulse() {
        if (!hwnd) return;
        if (BarsShown()) SetTimer(hwnd, BarsTimer, 80, nullptr);
        else KillTimer(hwnd, BarsTimer);
    }
    void Show(bool show) {
        if (!hwnd || visible == show) return;
        visible = show;
        Pulse();
        if (!show) { DetachGlass(); ShowWindow(hwnd, SW_HIDE); return; }
        Place();
        if (display.topmost)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        else {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            desktop_level::Place(hwnd, true);
        }
    }
    bool Place() {
        if (!hwnd || !canvas || !visible) return false;
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info)) return false;
        if (!Draw()) return false;
        // Centre on the composed bitmap so every animation frame stays symmetric.
        const int center = info.rcWork.left + (info.rcWork.right - info.rcWork.left) / 2;
        // Tucked, the capsule slides up past the very top of the screen, leaving a
        // sliver that the pointer can reach to bring it back.
        const float shown = static_cast<float>(info.rcWork.top + Px(TopMargin));
        const int liveWidth = std::max(1, Px(drawnWidth)), liveHeight = std::max(1, Px(drawnHeight));
        const float tucked = static_cast<float>(info.rcMonitor.top + Px(TuckedSliver) - liveHeight);
        POINT at{center - liveWidth / 2,
                 static_cast<LONG>(std::lround(shown + (tucked - shown) * std::clamp(tuck.x, 0.f, 1.2f)))};
        const auto opacity = static_cast<BYTE>(std::lround(std::clamp(presence.x, 0.f, 1.f) * 255));
        if (!surface.Upload(bitmap) || !surface.Compose(hwnd, at, {liveWidth, liveHeight}, {}, opacity)) return false;
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
        if (!GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &info) || !GetWindowRect(hwnd, &window)) return false;
        const float px = static_cast<float>(CanvasDpi()) / 96, dipScale = static_cast<float>(dpi) / 96, scale = ScaleFactor();
        const float center = static_cast<float>(info.rcWork.left + (info.rcWork.right - info.rcWork.left) / 2);
        const float shown = static_cast<float>(info.rcWork.top + Px(TopMargin));
        keyframes.clear();
        keyframes.reserve(trajectory.size());
        for (const auto& m : trajectory) {
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
        p.opticalProfile = 1;  // The calendar widget's profile.
        p.lightForeground = 1;
        p.reducedMotion = Animations() ? 0u : 1u;
        p.cornerRadius = Radius() * scale;
        p.opacityEnabled = 1;
        p.opacity = std::clamp(presence.x, 0.f, 1.f);
        MaterialContact(hwnd, width.x * scale, height.x * scale, dpi, p.contactX, p.contactY);
        if (animating && trajectory.size() > 1 && p.reducedMotion == 0 && (!keyframes.empty() || BuildKeyframes())) {
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
        }
        SubmitMaterial(glass, p, lastGlass);
    }
    void DetachGlass() {
        glass.Detach();
        lastGlass = {};
    }

    // --- Drawing -------------------------------------------------------------

    float Expansion() const {
        return std::clamp((height.x - CompactHeight) / (ExpandedHeight - CompactHeight), 0.f, 1.f);
    }
    void FadeImage(const std::shared_ptr<const WidgetBitmap>& image, WidgetRect r, float radius, float alpha) {
        if (alpha <= .01f) return;
        if (Glass() && alpha < .5f) return;  // Glass has no solid colour to fade into.
        if (image) canvas->ImageBitmap(image, r);
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
        if (media.playing) position += static_cast<double>(GetTickCount64() - mediaAt) / 1000;
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
        if (alpha <= .01f || !HasMedia()) return;
        const float art = h - 12, pad = (h - art) / 2;
        FadeImage(cover, {pad + 2, pad, art, art}, art * .3f, alpha);
        const float barsWidth = 4 * 3 + 3 * 2.5f;
        Bars(w - pad - 4 - barsWidth, h - pad, art * .82f, 4, 3, 2.5f, alpha);
        const float left = pad + art + 10, right = w - pad - barsWidth - 12;
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
        if (alpha <= .01f || !HasMedia()) return;
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
    void NoticeView(float w, float h, float alpha) {
        if (!notice || alpha <= .01f) return;
        auto& c = *canvas;
        if (!notice->app.empty()) {
            // A chat program: its colour and initial in a disc.
            const float my = h / 2;
            c.AnnularSector(34, my, 0, 17, 0, 360, notice->color, alpha);
            c.Text(notice->app.substr(0, 1), {17, my - 11, 34, 22}, 14, 0xffffff, alpha, WidgetTextWeight::Semibold, WidgetTextAlign::Center);
            const float l = 62, r = w - 20;
            c.Text(notice->app + L" · 新消息", {l, my - 21, r - l, 16}, 11.5f, 0xffffff, .55f * alpha);
            c.Text(FitWeightedText(c, notice->title, r - l, 14.5f, WidgetTextWeight::Semibold), {l, my - 4, r - l, 22}, 14.5f,
                   0xffffff, alpha, WidgetTextWeight::Semibold);
            return;
        }
        // Bell in an orange disc, like a system reminder.
        const float cy = h / 2;
        c.AnnularSector(34, cy, 0, 17, 0, 360, 0xff9f0a, alpha);
        c.AnnularSector(34, cy - 1.5f, 4.2f, 6.2f, -90, 180, 0xffffff, alpha);
        c.Rounded({27.8f, cy - 1.5f, 12.4f, 6}, 1.5f, 0xffffff, alpha);
        c.Rounded({26, cy + 3.5f, 16, 2}, 1, 0xffffff, alpha);
        c.AnnularSector(34, cy + 7.2f, 0, 1.8f, 0, 360, 0xffffff, alpha);
        const float left = 62, right = w - 20;
        c.Text(L"待办提醒" + (notice->time.empty() ? std::wstring{} : L" · " + notice->time), {left, cy - 21, right - left, 16}, 11.5f,
               0xffffff, .55f * alpha);
        c.Text(FitWeightedText(c, notice->title, right - left, 14.5f, WidgetTextWeight::Semibold), {left, cy - 4, right - left, 22}, 14.5f,
               0xffffff, alpha, WidgetTextWeight::Semibold);
    }
    bool ShowNotice(const std::string& id, const std::wstring& title, const std::wstring& time) {
        // In the desktop layer windows may cover the island, so the reminder
        // would count as delivered unseen; the normal popup takes it instead.
        if (!hwnd || suppressed || !display.topmost) return false;
        const Notice next{id, title, time};
        reminders.erase(std::remove_if(reminders.begin(), reminders.end(), [&](const Notice& n) { return n.id == id; }), reminders.end());
        reminders.insert(reminders.begin(), next);
        if (reminders.size() > static_cast<std::size_t>(MaxReminders)) reminders.resize(static_cast<std::size_t>(MaxReminders));
        // Several reminders due together are shown one after another.
        if (notice && notice->id != id) {
            if (queued.size() < static_cast<std::size_t>(MaxReminders)) queued.push_back(next);
            return true;
        }
        notice = next;
        if (expanded) Expand(false);
        SetTimer(hwnd, NoticeTimer, NoticeMilliseconds, nullptr);
        Activity(NoticeMilliseconds);
        Animate();
        return true;
    }
    // QQ, WeChat and similar programs flash their taskbar button for a new
    // message (Windows does not hand their notification text to other apps).
    // The shell hook reports the flash; the island names the program and
    // opens its window on click. A flash repeats, so once per program per 8 s.
    struct ChatApp { const wchar_t* exe; const wchar_t* name; std::uint32_t color; };
    static const ChatApp* Chat(HWND window) {
        static constexpr ChatApp apps[]{
            {L"weixin.exe", L"微信", 0x07c160}, {L"wechat.exe", L"微信", 0x07c160}, {L"wxwork.exe", L"企业微信", 0x2b7bf6},
            {L"qq.exe", L"QQ", 0x12b7f5}, {L"tim.exe", L"TIM", 0x2c8cf0}, {L"dingtalk.exe", L"钉钉", 0x1677ff},
            {L"feishu.exe", L"飞书", 0x3370ff}, {L"lark.exe", L"飞书", 0x3370ff}};
        DWORD pid = 0;
        if (!window || !GetWindowThreadProcessId(window, &pid) || !pid) return nullptr;
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return nullptr;
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        const bool named = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
        CloseHandle(process);
        if (!named) return nullptr;
        const wchar_t* file = wcsrchr(path, L'\\');
        file = file ? file + 1 : path;
        for (const auto& app : apps) if (_wcsicmp(file, app.exe) == 0) return &app;
        return nullptr;
    }
    std::map<std::wstring, ULONGLONG> chatShown;
    void ChatFlash(HWND window) {
        if (!display.messages || !hwnd) return;
        const auto* app = Chat(window);
        if (!app) return;
        // Logged so a report shows whether the program signalled at all.
        diag::Log("island", std::string("chat flash ") + text::Utf8(app->exe) + (suppressed ? " (fullscreen)" : "") +
            (GetForegroundWindow() == window ? " (foreground)" : ""));
        if (suppressed) return;
        // The flash of a window the user is looking at is not news.
        if (GetForegroundWindow() == window) return;
        const auto now = GetTickCount64();
        auto& last = chatShown[app->name];
        if (last && now - last < 8000) return;
        last = now;
        wchar_t title[256]{};
        GetWindowTextW(window, title, 256);
        // The window title is the chat for detached chat windows; the main
        // window is titled with the program itself.
        std::wstring text = title;
        if (text.empty() || text == app->name || _wcsicmp(text.c_str(), L"QQ") == 0 || text == L"WeChat")
            text = L"你有新的消息";
        Notice next{std::string("msg:") + std::to_string(reinterpret_cast<std::uintptr_t>(window)), text, {}, app->name, window, app->color};
        if (notice && notice->id != next.id) {
            if (queued.size() < static_cast<std::size_t>(MaxReminders)) queued.push_back(next);
            return;
        }
        notice = next;
        if (expanded) Expand(false);
        SetTimer(hwnd, NoticeTimer, NoticeMilliseconds, nullptr);
        Activity(NoticeMilliseconds);
        Animate();
    }
    static void OpenChat(HWND window) {
        if (!IsWindow(window)) return;
        if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
        else if (!IsWindowVisible(window)) ShowWindow(window, SW_SHOW);
        // The island never activates, so Windows may refuse a plain request.
        if (!SetForegroundWindow(window)) SwitchToThisWindow(window, TRUE);
    }
    void DismissNotice(bool all = false) {
        if (!notice) return;
        notice.reset();
        if (hwnd) KillTimer(hwnd, NoticeTimer);
        if (all) queued.clear();
        if (!queued.empty() && hwnd && !suppressed) {
            notice = queued.front();
            queued.pop_front();
            SetTimer(hwnd, NoticeTimer, NoticeMilliseconds, nullptr);
            Activity(NoticeMilliseconds);
            Animate();
            return;
        }
        Activity(LeaveDelay);
        Animate();
    }
    std::vector<IslandPage> Pages() const {
        std::vector<IslandPage> pages;
        if (HasMedia()) pages.push_back(IslandPage::Music);
        pages.push_back(IslandPage::Calendar);
        pages.push_back(IslandPage::Weather);
        pages.push_back(IslandPage::Performance);
        pages.push_back(IslandPage::Todo);
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
    void Chevron(float cx, float cy, bool next, bool twice, float alpha) {
        const float d = next ? 1.f : -1.f;
        for (int i = 0; i < (twice ? 2 : 1); ++i) {
            const float x = cx + (twice ? (static_cast<float>(i) * 5 - 2.5f) * d : 0);
            canvas->Line(x - 2.5f * d, cy - 4, x + 1.5f * d, cy, 0xffffff, alpha, 1.5f);
            canvas->Line(x + 1.5f * d, cy, x - 2.5f * d, cy + 4, 0xffffff, alpha, 1.5f);
        }
    }
    // Calendar navigation buttons: prev year, prev month, next month, next year.
    WidgetRect NavButton(int index, float w) const { return {w - 132 + static_cast<float>(index) * 26, 14, 24, 24}; }
    struct GridGeometry { float left, top, cell, row; int rows, offset; };
    GridGeometry Grid(float w) const {
        const int first = calendar::Weekday({month.year, month.month, 1});
        const int offset = (first + 6) % 7;  // Monday first, like the split calendar.
        const int days = calendar::DaysInMonth(month.year, month.month);
        const int rows = (offset + days + 6) / 7;
        const float left = 176, top = 66, cell = (w - 24 - left) / 7;
        return {left, top, cell, std::min(22.f, (PageHeight - 12 - top) / static_cast<float>(rows)), rows, offset};
    }
    void CalendarPage(float w, float alpha) {
        if (alpha <= .01f) return;
        static constexpr const wchar_t* Weekdays[] = {L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"};
        static constexpr const wchar_t* Short[] = {L"一", L"二", L"三", L"四", L"五", L"六", L"日"};
        auto& c = *canvas;
        // Left: today, like the split calendar's summary column.
        c.Text(Weekdays[calendar::Weekday(today)], {24, 20, 130, 18}, 13, Blue, alpha, WidgetTextWeight::Semibold);
        c.Text(std::to_wstring(today.day), {20, 34, 140, 64}, 56, 0xffffff, alpha, WidgetTextWeight::Bold);
        c.Text(std::to_wstring(today.year) + L"年" + std::to_wstring(today.month) + L"月", {24, 100, 130, 18}, 12,
               0xffffff, .6f * alpha);
        // Festivals of the shown month: upcoming ones first, then the latest past ones.
        std::vector<std::pair<int, std::wstring>> festivals;
        const int days = calendar::DaysInMonth(month.year, month.month);
        for (int d = 1; d <= days; ++d) {
            auto name = Festival(calendar::LookupLunar({month.year, month.month, d}));
            if (!name.empty()) festivals.emplace_back(d, std::move(name));
        }
        std::vector<std::pair<int, std::wstring>> shown;
        for (const auto& f : festivals) if (calendar::Date{month.year, month.month, f.first} >= today && shown.size() < 3) shown.push_back(f);
        for (auto it = festivals.rbegin(); it != festivals.rend() && shown.size() < 3; ++it)
            if (calendar::Date{month.year, month.month, it->first} < today) shown.push_back(*it);
        std::sort(shown.begin(), shown.end());
        float y = 126;
        for (const auto& [day, name] : shown) {
            const auto date = std::to_wstring(month.month) + L"月" + std::to_wstring(day) + L"日";
            c.Text(date, {24, y, 70, 18}, 12, Blue, alpha, WidgetTextWeight::Semibold);
            c.Text(FitWeightedText(c, name, 66, 12, WidgetTextWeight::Normal), {88, y, 66, 18}, 12, 0xffffff, .88f * alpha);
            y += 20;
        }
        c.Line(162, 18, 162, PageHeight - 18, 0xffffff, .14f * alpha, 1);
        // Right: month grid.
        c.Text(std::to_wstring(month.year) + L"年" + std::to_wstring(month.month) + L"月", {176, 16, 120, 22}, 15,
               0xffffff, alpha, WidgetTextWeight::Semibold);
        for (int i = 0; i < 4; ++i) {
            const auto r = NavButton(i, w);
            if (hoverHit == 10 + i) c.Rounded(r, 12, 0xffffff, .14f * alpha);
            Chevron(r.x + r.width / 2, r.y + r.height / 2, i >= 2, i == 0 || i == 3, .8f * alpha);
        }
        const auto g = Grid(w);
        for (int i = 0; i < 7; ++i)
            c.Text(Short[i], {g.left + g.cell * static_cast<float>(i), 46, g.cell, 16}, 11, 0xffffff, (i >= 5 ? .4f : .55f) * alpha,
                   WidgetTextWeight::Medium, WidgetTextAlign::Center);
        for (int d = 1; d <= days; ++d) {
            const int index = g.offset + d - 1;
            const float x = g.left + g.cell * static_cast<float>(index % 7), top = g.top + g.row * static_cast<float>(index / 7);
            const calendar::Date date{month.year, month.month, d};
            const bool isToday = date == today, isSelected = date == selected && !isToday;
            const float cx = x + g.cell / 2, cy = top + g.row / 2 - 1;
            if (isToday) c.AnnularSector(cx, cy, 0, 10, 0, 360, Blue, alpha);
            else if (isSelected) c.AnnularSector(cx, cy, 9, 10, 0, 360, 0xffffff, .55f * alpha);
            else if (hoverHit == 100 + d) c.AnnularSector(cx, cy, 0, 10, 0, 360, 0xffffff, .12f * alpha);
            const bool weekend = index % 7 >= 5;
            c.Text(std::to_wstring(d), {x, cy - 8, g.cell, 16}, 12, 0xffffff, (isToday ? 1.f : weekend ? .5f : .9f) * alpha,
                   isToday ? WidgetTextWeight::Bold : WidgetTextWeight::Medium, WidgetTextAlign::Center);
            // Dots: festivals in red, days with todos in the accent colour (shared store).
            const bool festival = !Festival(calendar::LookupLunar(date)).empty();
            const bool hasTodo = todos.hasTodos && todos.hasTodos(date);
            const float dotY = cy + 9;
            if (festival && hasTodo) {
                c.AnnularSector(cx - 2.5f, dotY, 0, 1.4f, 0, 360, Blue, alpha);
                c.AnnularSector(cx + 2.5f, dotY, 0, 1.4f, 0, 360, Green, alpha);
            } else if (festival) c.AnnularSector(cx, dotY, 0, 1.4f, 0, 360, isToday ? 0xffffff : Blue, alpha);
            else if (hasTodo) c.AnnularSector(cx, dotY, 0, 1.4f, 0, 360, isToday ? 0xffffff : Green, alpha);
        }
    }
    void LoadTodos() {
        todoRows = todos.forDay ? todos.forDay(selected) : std::vector<IslandTodo>{};
    }
    void TodoPage(float w, float h, float alpha) {
        if (alpha <= .01f) return;
        auto& c = *canvas;
        const auto title = selected == today ? std::wstring(L"今日待办")
                                             : std::to_wstring(selected.month) + L"月" + std::to_wstring(selected.day) + L"日 待办";
        c.Text(title, {22, 16, 200, 22}, 15, 0xffffff, alpha, WidgetTextWeight::Semibold);
        const auto done = std::count_if(todoRows.begin(), todoRows.end(), [](const auto& t) { return t.completed; });
        if (!todoRows.empty())
            c.Text(std::to_wstring(done) + L"/" + std::to_wstring(todoRows.size()) + L" 已完成", {w - 140, 19, 112, 18}, 12,
                   0xffffff, .5f * alpha, WidgetTextWeight::Normal, WidgetTextAlign::Right);
        todoChecks.clear();
        if (todoRows.empty()) {
            c.Text(L"没有待办", {0, h / 2 - 16, w, 22}, 14, 0xffffff, .7f * alpha, WidgetTextWeight::Medium, WidgetTextAlign::Center);
            c.Text(L"在日历或今日待办中添加，这里会同步显示", {0, h / 2 + 8, w, 18}, 11.5f, 0xffffff, .42f * alpha,
                   WidgetTextWeight::Normal, WidgetTextAlign::Center);
            return;
        }
        const std::size_t rows = std::min<std::size_t>(todoRows.size(), 5);
        for (std::size_t i = 0; i < rows; ++i) {
            const auto& todo = todoRows[i];
            const float y = 48 + static_cast<float>(i) * 29;
            const WidgetRect check{22, y + 4, 18, 18};
            todoChecks.push_back(check);
            if (todo.color) {
                c.Rounded({14, y, w - 40, 26}, 8, todo.color, (todo.completed ? .2f : .34f) * alpha);
                c.Rounded({14, y + 4, 3, 18}, 1.5f, todo.color, (todo.completed ? .55f : 1.f) * alpha);
            }
            if (hoverHit == 200 + static_cast<int>(i)) c.Rounded({14, y, w - 40, 26}, 8, 0xffffff, .1f * alpha);
            if (todo.completed) {
                c.AnnularSector(check.x + 9, check.y + 9, 0, 9, 0, 360, Green, alpha);
                c.Line(check.x + 5, check.y + 9.5f, check.x + 8, check.y + 12.5f, 0xffffff, alpha, 1.8f);
                c.Line(check.x + 8, check.y + 12.5f, check.x + 13.5f, check.y + 6, 0xffffff, alpha, 1.8f);
            } else c.AnnularSector(check.x + 9, check.y + 9, 7.6f, 9, 0, 360, 0xffffff, .7f * alpha);
            const float timeWidth = todo.time.empty() ? 0.f : 74.f;
            const float textWidth = w - 52 - 28 - timeWidth;
            c.Text(FitWeightedText(c, todo.title, textWidth, 13, WidgetTextWeight::Medium), {50, y + 4, textWidth, 18}, 13, 0xffffff,
                   (todo.completed ? .45f : .95f) * alpha, WidgetTextWeight::Medium);
            if (todo.completed) {
                const float strike = std::min(textWidth, c.MeasureText(FitWeightedText(c, todo.title, textWidth, 13, WidgetTextWeight::Medium), 13, WidgetTextWeight::Medium));
                c.Line(50, y + 13, 50 + strike, y + 13, 0xffffff, .4f * alpha, 1);
            }
            if (timeWidth > 0)
                c.Text(todo.time, {w - 28 - timeWidth, y + 5, timeWidth, 16}, 11, 0xffffff, .5f * alpha, WidgetTextWeight::Normal,
                       WidgetTextAlign::Right);
        }
        if (todoRows.size() > rows)
            c.Text(L"还有 " + std::to_wstring(todoRows.size() - rows) + L" 项 · 点击打开日历", {22, h - 24, w - 50, 16}, 11,
                   0xffffff, .45f * alpha);
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
        pageChosen = true;
        if (next == page) return;
        if (!direction) {
            const auto pages = Pages();
            const auto from = std::find(pages.begin(), pages.end(), page), to = std::find(pages.begin(), pages.end(), next);
            direction = to >= from ? 1 : -1;
        }
        pageDirection = direction;
        page = next;
        if (page == IslandPage::Todo) LoadTodos();
        SyncPerformance();
        pageFade.Snap(0);
        pageFade.target = 1;
        Animate();
    }
    void Wheel(int delta) {
        const auto pages = Pages();
        if (!expanded) {
            // Scrolling the capsule opens straight onto the next page.
            pageChosen = true;
            page = pages.size() > 1 && delta < 0 ? pages[1] : pages.front();
            if (page == IslandPage::Todo) LoadTodos();
            Expand(true);
            return;
        }
        auto it = std::find(pages.begin(), pages.end(), page);
        const auto index = it == pages.end() ? 0 : static_cast<std::ptrdiff_t>(it - pages.begin());
        const auto count = static_cast<std::ptrdiff_t>(pages.size());
        SetPage(pages[static_cast<std::size_t>(((index + (delta < 0 ? 1 : -1)) % count + count) % count)], delta < 0 ? 1 : -1);
    }
    void StepMonth(int months) {
        int total = month.year * 12 + (month.month - 1) + months;
        total = std::clamp(total, 1 * 12, 9999 * 12 + 11);
        month = {total / 12, total % 12 + 1, 1};
        Place();
    }
    bool Draw() {
        const float w = std::clamp(width.x, 1.f, CanvasWidth), h = std::clamp(height.x, 1.f, CanvasHeight);
        drawnWidth = w;
        drawnHeight = h;
        if (!canvas->Begin(CanvasWidth, CanvasHeight, CanvasDpi())) return false;
        if (view.media.artwork != coverKey) {
            coverKey = view.media.artwork;
            cover = DecodeArtwork(coverKey);
            accent = Accent(cover);
        }
        const float e = Expansion();
        const float radius = Radius();
        if (Glass()) {
            // Same as the calendar widget: the material alone, plus a near-invisible
            // fill that keeps blank glass hit-testable.
            canvas->Rounded({0, 0, w, h}, radius, 0x000000, 1.f / 255);
        } else {
            canvas->Rounded({0, 0, w, h}, radius, 0x2a2a2e, 1);
            canvas->Rounded({.75f, .75f, w - 1.5f, h - 1.5f}, radius - .75f, 0x000000, 1);
        }
        if (notice && !expanded) { NoticeView(w, h, std::clamp((h - CompactHeight) / (NoticeHeight - CompactHeight), 0.f, 1.f)); return canvas->Finish(bitmap); }
        Compact(w, h, std::clamp(1 - e * 3, 0.f, 1.f));
        const float content = std::clamp((e - .55f) / .45f, 0.f, 1.f) * std::clamp(pageFade.x, 0.f, 1.f);
        // The new page slides in sideways while it fades in.
        const float slide = (1 - std::clamp(pageFade.x, 0.f, 1.f)) * 56 * static_cast<float>(pageDirection);
        canvas->PushClip({0, 0, w, h});
        canvas->Offset(slide, 0);
        switch (page) {
        case IslandPage::Music: Expanded(w, content); break;
        case IslandPage::Calendar: CalendarPage(w, content); break;
        case IslandPage::Weather: DrawIslandWeatherPage(*canvas, weather, w, h, content); break;
        case IslandPage::Performance: DrawPerformance(*canvas, w, h, performanceSample, performanceFailed, content); break;
        case IslandPage::Todo: TodoPage(w, h, content); break;
        case IslandPage::Reminder: ReminderPage(w, h, content); break;
        }
        canvas->Offset(0, 0);
        canvas->PopClip();
        if (e > .9f) Dots(w, h, content);
        return canvas->Finish(bitmap);
    }

    // --- Fullscreen and games ------------------------------------------------

    // Returns true when an exclusive fullscreen game, a presentation, or a
    // window covering this monitor is in front. QUNS_BUSY alone is not used: it
    // also reports fullscreen windows on other monitors.
    bool FullscreenInFront() const {
        if (!display.hideFullscreen) return false;
        QUERY_USER_NOTIFICATION_STATE state{};
        if (SUCCEEDED(SHQueryUserNotificationState(&state)) &&
            (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE))
            return true;
        const HWND foreground = GetForegroundWindow();
        if (!foreground || foreground == hwnd || !IsWindowVisible(foreground) || IsIconic(foreground)) return false;
        wchar_t name[64]{};
        GetClassNameW(foreground, name, static_cast<int>(std::size(name)));
        for (const auto* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
            if (wcscmp(name, shell) == 0) return false;
        DWORD process = 0;
        GetWindowThreadProcessId(foreground, &process);
        if (process == GetCurrentProcessId()) return false;
        const HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) != monitor) return false;
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(monitor, &info);
        RECT bounds{};
        if (FAILED(DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))))
            GetWindowRect(foreground, &bounds);
        return bounds.left <= info.rcMonitor.left && bounds.top <= info.rcMonitor.top &&
               bounds.right >= info.rcMonitor.right && bounds.bottom >= info.rcMonitor.bottom;
    }
    // True when a maximized (not fullscreen) window of another app is in front on
    // the island's monitor. Only a topmost island can cover it.
    bool MaximizedInFront() const {
        if (!display.topmost) return false;
        const HWND foreground = GetForegroundWindow();
        if (!foreground || foreground == hwnd || !IsWindowVisible(foreground) || IsIconic(foreground) ||
            !IsZoomed(foreground))
            return false;
        DWORD process = 0;
        GetWindowThreadProcessId(foreground, &process);
        if (process == GetCurrentProcessId()) return false;
        return MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL) == MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
    void CheckForeground() {
        // Maximizing does not change the foreground window; the 1 s tick catches it.
        if (const bool zoomed = MaximizedInFront(); zoomed != maximized) {
            maximized = zoomed;
            if (!suppressed) Animate();
        }
        const bool next = FullscreenInFront();
        if (next == suppressed) return;
        suppressed = next;
        if (suppressed) { expanded = hoverExpanded = false; notice.reset(); queued.clear(); WatchOutside(false); Settle(); }  // Leave at once, no animation over a game.
        else Animate();
        Pulse();
    }
    static void CALLBACK ForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
        if (active && active->hwnd) PostMessageW(active->hwnd, ForegroundMessage, 0, 0);
    }

    // --- Lifecycle -----------------------------------------------------------

    bool Start(const widget::Preferences& next, const IslandDisplay& options) {
        prefs = next;
        if (hwnd && options.topmost != display.topmost) Stop();
        display = options;
        if (hwnd) { Animate(); return true; }
        WNDCLASSEXW wc{sizeof(wc)};
        wc.hInstance = instance;
        wc.lpfnWndProc = Procedure;
        wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        wc.lpszClassName = ClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        expanded = hoverExpanded = hovered = visible = false;
        const DWORD style = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (display.topmost ? WS_EX_TOPMOST : 0);
        hwnd = CreateWindowExW(style, ClassName, L"Lume", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
        if (!hwnd) return false;
        dpi = GetDpiForWindow(hwnd);
        if (!dpi) dpi = 96;
        canvas = std::make_unique<WidgetCanvas>();
        if (!display.topmost) SetPropW(hwnd, lume::window_ids::DesktopWidgetProp, reinterpret_cast<HANDLE>(1));
        if (!SetTimer(hwnd, TickTimer, 1000, nullptr)) { Stop(); return false; }
        // Taskbar-button flashes of chat programs (best effort).
        RegisterShellHookWindow(hwnd);
        active = this;
        foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundEvent, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        suppressed = FullscreenInFront();
        maximized = MaximizedInFront();
        Targets();
        width.Snap(width.target);
        height.Snap(height.target);
        presence.Snap(0);
        tuck.Snap(0);
        Activity();
        Animate();
        return true;
    }
    // Samples once a second while the performance page is open; stopping
    // joins the worker, so no notification outlives the window.
    void SyncPerformance() {
        const bool want = hwnd && expanded && page == IslandPage::Performance;
        if (want && !performance.Running()) {
            performanceTarget = hwnd;
            performanceSample.reset();
            performanceFailed = !performance.Start();
        } else if (!want && performanceTarget.load()) {
            performanceTarget = nullptr;
            performance.Stop();
            performanceSample.reset();
            performanceFailed = false;
        }
    }
    void Stop() {
        expanded = false;
        SyncPerformance();
        StopClock();
        WatchOutside(false);
        DetachGlass();
        if (foregroundHook) { UnhookWinEvent(foregroundHook); foregroundHook = nullptr; }
        if (active == this) active = nullptr;
        if (hwnd) {
            const auto old = hwnd;
            hwnd = nullptr;
            if (GetCapture() == old) ReleaseCapture();
            DeregisterShellHookWindow(old);
            DestroyWindow(old);
        }
        surface.Release();
        canvas.reset();
        bitmap = {};
        expanded = hoverExpanded = hovered = visible = false;
        pressed = hoverHit = -1;
    }
    static LRESULT CALLBACK OutsideProc(int code, WPARAM w, LPARAM l) {
        if (code == HC_ACTION && active && active->hwnd && !active->menuOpen &&
            (w == WM_LBUTTONDOWN || w == WM_RBUTTONDOWN || w == WM_MBUTTONDOWN)) {
            RECT rect{};
            const POINT point = reinterpret_cast<const MSLLHOOKSTRUCT*>(l)->pt;
            if (GetWindowRect(active->hwnd, &rect) && !PtInRect(&rect, point))
                PostMessageW(active->hwnd, OutsideMessage, 0, 0);
        }
        return CallNextHookEx(nullptr, code, w, l);
    }
    // The hook exists only while the panel is open and is removed as soon as it
    // collapses, so normal use (and games, which force a collapse) never pay for it.
    void WatchOutside(bool watch) {
        if (watch && !outsideHook) outsideHook = SetWindowsHookExW(WH_MOUSE_LL, OutsideProc, instance, 0);
        if (!watch && outsideHook) { UnhookWindowsHookEx(outsideHook); outsideHook = nullptr; }
    }
    void Expand(bool next, bool byHover = false) {
        if (!hwnd || expanded == next) return;
        expanded = next;
        if (next) { today = Today(); if (page != IslandPage::Calendar) month = {today.year, today.month, 1}; pageFade.Snap(1); }
        else { month = {today.year, today.month, 1}; selected = today; pageChosen = false; if (HasMedia()) page = IslandPage::Music; }
        if (next && page == IslandPage::Todo) LoadTodos();
        hoverExpanded = next && byHover;
        SyncPerformance();
        WatchOutside(next);
        if (!next) Activity(LeaveDelay);
        KillTimer(hwnd, CollapseTimer);
        Animate();
        if (act) act(dynamic_island::Action::ExpansionChanged);
    }

    // --- Input -----------------------------------------------------------------

    int Hit(LPARAM l) const {
        const float scale = 96.f / static_cast<float>(CanvasDpi());
        const float x = static_cast<float>(GET_X_LPARAM(l)) * scale, y = static_cast<float>(GET_Y_LPARAM(l)) * scale;
        if (x < 0 || x >= width.x || y < 0 || y >= height.x) return -1;
        if (notice && !expanded) return 400;
        if (!expanded || Expansion() < .9f) return 0;
        if (page == IslandPage::Calendar) {
            for (int i = 0; i < 4; ++i) {
                const auto r = NavButton(i, width.x);
                if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) return 10 + i;
            }
            const auto g = Grid(width.x);
            if (x >= g.left && x < g.left + g.cell * 7 && y >= g.top && y < g.top + g.row * static_cast<float>(g.rows)) {
                const int index = static_cast<int>((y - g.top) / g.row) * 7 + static_cast<int>((x - g.left) / g.cell);
                const int day = index - g.offset + 1;
                if (day >= 1 && day <= calendar::DaysInMonth(month.year, month.month)) return 100 + day;
            }
            return x < 162 ? 300 : 299;  // Left column opens the calendar; blank does nothing.
        }
        if (page == IslandPage::Reminder) {
            if (y < 40 && x >= width.x - 76) return 540;
            for (std::size_t i = 0; i < reminderRows.size(); ++i) {
                const auto& r = reminderRows[i];
                if (y >= r.y && y < r.y + r.height && x >= r.x && x < r.x + r.width)
                    return (x < 48 ? 500 : 520) + static_cast<int>(i);
            }
            return 299;
        }
        if (page == IslandPage::Todo) {
            for (std::size_t i = 0; i < todoChecks.size(); ++i)
                if (y >= todoChecks[i].y - 4 && y < todoChecks[i].y + 22 && x < width.x - 24) return 200 + static_cast<int>(i);
            return 300;
        }
        if (page == IslandPage::Weather) return weather.available?299:560;
        if (page == IslandPage::Performance) return 299;
        if (y >= 110 && y < 146) {
            const float cx = width.x / 2;
            if (std::abs(x - (cx - 72)) < 22) return 1;
            if (std::abs(x - cx) < 22) return 2;
            if (std::abs(x - (cx + 72)) < 22) return 3;
        }
        return y < 104 ? 0 : -1;
    }
    void Click(int hit) {
        if (hit == 400 && notice && notice->target) {
            const auto target = notice->target;
            DismissNotice();
            OpenChat(target);
            return;
        }
        if (hit == 400) {
            // The reminder page lists every reminder, including any still queued.
            DismissNotice(true);
            if (!reminders.empty()) {
                page = IslandPage::Reminder;
                pageFade.Snap(1);
                Expand(true);
            }
            return;
        }
        if (hit == 540) { reminders.clear(); SetPage(IslandPage::Calendar, -1); return; }
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
        if (hit >= 10 && hit < 14) { StepMonth(hit == 10 ? -12 : hit == 11 ? -1 : hit == 12 ? 1 : 12); return; }
        if (hit > 100 && hit < 140) {
            // Selecting a day shows its todos, read from the same store as the calendar.
            selected = {month.year, month.month, hit - 100};
            SetPage(IslandPage::Todo);
            return;
        }
        if (hit >= 200 && hit < 210) {
            const auto index = static_cast<std::size_t>(hit - 200);
            if (index < todoRows.size()) {
                if (todoRows[index].projected) { if (todos.openCalendar) todos.openCalendar(); }
                else if (todos.toggle) todos.toggle(todoRows[index]);
            }
            return;
        }
        if (hit == 300) { if (todos.openCalendar) todos.openCalendar(); return; }
        if (hit == 560) { if (act) act(dynamic_island::Action::WeatherSettings); return; }
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
    void Menu(POINT at) {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return;
        menuOpen = true;
        KillTimer(hwnd, CollapseTimer);
        const bool todoPage = expanded && page == IslandPage::Todo;
        std::optional<IslandTodo> row;
        if (todoPage && hoverHit >= 200 && hoverHit < 210 && static_cast<std::size_t>(hoverHit - 200) < todoRows.size())
            row = todoRows[static_cast<std::size_t>(hoverHit - 200)];
        const auto day = selected;
        const bool frosted = display.material == IslandMaterial::Frosted, none = display.material == IslandMaterial::None;
        const UINT command = todoPage
            ? LumeWidgetMenu::ShowIslandTodo(hwnd, at, row.has_value(), !todos.writable || todos.writable(), frosted, none)
            : LumeWidgetMenu::ShowIsland(hwnd, at, frosted, none, expanded, display.messages);
        if (command == 50 || command == 51 || command == 52) {
            menuOpen = false;
            Gdiplus::GdiplusShutdown(token);
            if (command == 52) { if (todos.edit) todos.edit({}, day); }
            else if (row && command == 50) { if (todos.edit) todos.edit(row->id, day); }
            else if (row && todos.remove) todos.remove(*row);
            return;
        }
        menuOpen = false;
        Gdiplus::GdiplusShutdown(token);
        using dynamic_island::Action;
        switch (command) {
        case 2: Expand(!expanded); break;
        case 12: if (act) act(Action::ToggleMessages); break;
        case 7: if (act) act(Action::MaterialNone); break;
        case 8: if (act) act(Action::MaterialLiquid); break;
        case 9: if (act) act(Action::MaterialFrosted); break;
        case 10: if (act) act(Action::Reset); break;
        case 11: if (act) act(Action::Settings); break;
        case 1: if (act) act(Action::Close); break;  // May stop this window.
        default:
            if (hwnd && expanded && !hovered) SetTimer(hwnd, CollapseTimer, 3500, nullptr);
            break;
        }
    }
    void Hover(bool inside) {
        if (inside == hovered) return;
        hovered = inside;
        if (inside) {
            KillTimer(hwnd, CollapseTimer);
            if (display.hoverExpand && HasMedia() && !expanded) SetTimer(hwnd, HoverTimer, 320, nullptr);
        } else {
            KillTimer(hwnd, HoverTimer);
            hoverHit = -1;
            // Collapse after leaving: quickly when opened by hover, otherwise after a pause.
            if (expanded) SetTimer(hwnd, CollapseTimer, hoverExpanded ? 450 : 3500, nullptr);
            Activity(LeaveDelay);
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
        if (!self->display.topmost) {
            if (message == WM_WINDOWPOSCHANGING) desktop_level::Changing(l);
            if (message == desktop_level::Message()) {
                if (IsWindowVisible(hwnd)) desktop_level::Place(hwnd, false);
                return 0;
            }
        }
        static const UINT shellHook = RegisterWindowMessageW(L"SHELLHOOK");
        if (shellHook && message == shellHook) {
            if ((w & 0x7fff) == HSHELL_REDRAW && (w & HSHELL_HIGHBIT)) self->ChatFlash(reinterpret_cast<HWND>(l));
            return 0;
        }
        switch (message) {
        case ForegroundMessage: self->CheckForeground(); return 0;
        case PerformanceMessage:
            if (!self->performanceTarget.load()) return 0;  // a late sample after the page closed
            self->performanceSample = self->performance.Latest();
            self->performanceFailed = !self->performanceSample && !self->performance.Running();
            if (self->visible && self->expanded && self->page == IslandPage::Performance) self->Place();
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
                self->CheckForeground();
                if (const auto now = Today(); now != self->today) {
                    self->today = now;
                    if (!self->expanded) self->month = {now.year, now.month, 1}, self->selected = now;
                    if (self->visible) self->Place();
                }
                // Other topmost windows may have covered the island.
                if (self->visible && self->display.topmost)
                    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                if (self->act) self->act(dynamic_island::Action::Tick);
                return 0;
            case AnimationTimer: self->Frame(); return 0;
            case BarsTimer:
                if (!self->BarsShown()) KillTimer(hwnd, BarsTimer);
                else if (!self->animating) self->Place();  // A running animation already redraws.
                return 0;
            case HoverTimer:
                KillTimer(hwnd, HoverTimer);
                if (self->hovered) self->Expand(true, true);
                return 0;
            case CollapseTimer:
                KillTimer(hwnd, CollapseTimer);
                if (!self->hovered) self->Expand(false);
                return 0;
            case NoticeTimer:
                KillTimer(hwnd, NoticeTimer);
                if (self->hovered) SetTimer(hwnd, NoticeTimer, 2500, nullptr);  // Keep while read.
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
            const auto hit = self->Hit(l);
            if (self->hoverHit != hit) {
                self->hoverHit = hit;
                self->Place();
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
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(hwnd, &point);
            self->Menu(point);
            return 0;
        }
        case WM_DPICHANGED: self->dpi = HIWORD(w); self->Place(); return 0;
        case WM_DISPLAYCHANGE: self->Place(); return 0;
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
IslandWindow::IslandWindow(HINSTANCE i, Act act) : impl_(std::make_unique<Impl>(i, std::move(act))) {}
IslandWindow::~IslandWindow() = default;
bool IslandWindow::Start(const widget::Preferences& p, const IslandDisplay& d) { return impl_->Start(p, d); }
void IslandWindow::Stop() { impl_->Stop(); }
bool IslandWindow::SetDisplay(const IslandDisplay& d) {
    if (!impl_->hwnd) { impl_->display = d; return true; }
    const auto previous = impl_->display;
    const bool startAutoHide = d.autoHide && !previous.autoHide;
    if (!impl_->Start(impl_->prefs, d)) {
        // A layering change destroys the window before creating the new one:
        // bring the previous window back so the island does not vanish.
        if (!impl_->hwnd) impl_->Start(impl_->prefs, previous);
        else impl_->display = previous;
        return false;
    }
    if (startAutoHide) impl_->Activity();  // Stay out briefly, then tuck.
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
    else if (impl_->visible && !IsIconic(impl_->hwnd)) impl_->Place();
}
void IslandWindow::Expand(bool expanded) { impl_->Expand(expanded); }
void IslandWindow::SetTodoSource(IslandTodoSource source) { impl_->todos = std::move(source); }
void IslandWindow::SetWeather(dynamic_island::Weather value) {
    impl_->weather = std::move(value);
    if (impl_->hwnd && impl_->visible && impl_->expanded) impl_->Place();
}
void IslandWindow::RefreshTodos() {
    if (impl_->page == IslandPage::Todo) impl_->LoadTodos();
    if (impl_->hwnd && impl_->visible && impl_->expanded) impl_->Place();
}
IslandPage IslandWindow::Page() const { return impl_->page; }
bool IslandWindow::ShowNotice(const std::string& id, const std::wstring& title, const std::wstring& time) {
    return impl_->ShowNotice(id, title, time);
}
HWND IslandWindow::Handle() const { return impl_->hwnd; }
bool IslandWindow::Expanded() const { return impl_->expanded; }
bool IslandWindow::Shown() const { return impl_->hwnd && impl_->visible; }
WidgetBitmap IslandWindow::Frame() const { return impl_->bitmap; }
void IslandWindow::Settle() { impl_->Settle(); }
}
namespace lume::platform::windows {
std::shared_ptr<const WidgetBitmap> DecodeMediaArtwork(const std::string& value) { return DecodeArtwork(value); }
}
