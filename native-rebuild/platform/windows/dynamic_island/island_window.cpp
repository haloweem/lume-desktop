#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/dynamic_island/compact_widgets.h"
#include "vendor/nlohmann/json.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include "core/dynamic_island/island_shape.h"
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
constexpr UINT_PTR TickTimer = 1, AnimationTimer = 2, HoverTimer = 3, CollapseTimer = 4, BarsTimer = 5, IdleTimer = 6, NoticeTimer = 7;
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
constexpr int MaxReminders = 10;
// The canvas keeps this fixed size during animations, so no bitmap or render
// target is reallocated per frame; each frame presents only the live area.
// Leave room for the strongest bounce on the 404 x 208 reminder panel.
constexpr float CanvasWidth = 480, CanvasHeight = 260;
constexpr std::uint32_t Green = 0x30d158;
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

std::vector<IslandMonitor> ConnectedIslandMonitors() {
    std::vector<IslandMonitor> monitors;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{
        MONITORINFOEXW info{};info.cbSize=sizeof(info);
        if(GetMonitorInfoW(monitor,&info)&&!IsRectEmpty(&info.rcMonitor)&&!IsRectEmpty(&info.rcWork))
            reinterpret_cast<std::vector<IslandMonitor>*>(data)->push_back({info.szDevice,info.rcMonitor,info.rcWork,(info.dwFlags&MONITORINFOF_PRIMARY)!=0});
        return TRUE;
    },reinterpret_cast<LPARAM>(&monitors));
    // Keep numbering deterministic when Windows changes its enumeration order.
    std::sort(monitors.begin(),monitors.end(),[](const auto& a,const auto& b){return a.device<b.device;});
    if(monitors.size()>64)monitors.resize(64);
    return monitors;
}

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
    std::unique_ptr<Impl> preview;
    std::shared_ptr<const WidgetBitmap> cover;
    std::string coverKey;
    std::uint32_t accent = 0x30d158;
    bool expanded = false, hoverExpanded = false, hovered = false, suppressed = false, visible = false;
    int pressed = -1, hoverHit = -1;
    Spring width, height, presence, tuck, shape; // shape: 0 capsule, 1 attached.
    Spring pageFade;  // 0 right after a page change, 1 settled.
    Spring previewScale;
    bool sampleRendering=false;
    IslandPage page = IslandPage::Music;
    calendar::Date today = Today();
    IslandTodoSource todos;
    dynamic_island::Weather weather;  // Projection of the weather component.
    CompactData compactData;
    std::vector<std::wstring> heldFiles;
    // Keep samples warm for the window's lifetime: the worker posts
    // here, and the UI thread reads the latest snapshot.
    std::atomic<HWND> performanceTarget{nullptr};
    PerformanceMonitor performance{[this] {
        if (const HWND target = performanceTarget.load()) PostMessageW(target, PerformanceMessage, 0, 0);
    }};
    std::optional<performance::Snapshot> performanceSample;
    // id "msg:<exe>" marks a chat message: app names the program, target is
    // its window (activated on click). Chat messages never join reminders.
    struct Notice { std::string id; std::wstring title, time, app; HWND target = nullptr; std::uint32_t color = 0xff9f0a; };
    std::optional<Notice> notice;
    std::vector<Notice> reminders;  // Newest first, at most MaxReminders.
    std::deque<Notice> queued;      // Notices waiting for the current one to finish.
    int pageDirection = 1;          // +1 slides in from the right, -1 from the left.
    std::vector<WidgetRect> reminderRows;
    FrameClock clock;
    double lastSeconds = 0;
    float drawnWidth = 0, drawnHeight = 0;
    ULONGLONG activeUntil = 0;
    HWINEVENTHOOK foregroundHook = nullptr;
    bool watchingOutside=false;
    bool menuOpen = false, animating = false;
    WidgetMaterialRuntime glass;
    LumeMaterialParameters lastGlass{};
    static inline std::vector<Impl*> instances;
    static inline HHOOK outsideHook=nullptr;  // Shared only while at least one island is expanded.

    Impl(HINSTANCE i, Act a) : instance(i), act(std::move(a)) {}
    ~Impl() { Stop(); }

    float ScaleFactor() const { return sampleRendering?previewScale.x:Scale(display.size); }
    // Effective DPI folds the size option into the canvas, so layouts stay in base DIP.
    unsigned CanvasDpi() const { return static_cast<unsigned>(std::lround(static_cast<float>(dpi) * ScaleFactor())); }
    int Px(float value) const { return static_cast<int>(std::lround(value * static_cast<float>(CanvasDpi()) / 96.f)); }
    bool HasMedia() const { return view.media.available; }
    bool ReadMonitor(MONITORINFO& info) const {
        if(targetMonitor){info.rcMonitor=targetMonitor->bounds;info.rcWork=targetMonitor->work;info.dwFlags=targetMonitor->primary?MONITORINFOF_PRIMARY:0;return true;}
        return GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&info)!=FALSE;
    }
    bool Glass() const { return display.material != IslandMaterial::None; }
    bool Attached() const {return display.shape==IslandShape::Standard;}
    bool Wanted() const { return !suppressed; }  // The idle pill always shows.

    void Targets() {
        shape.target=Attached()?1.f:0.f;
        if (reminders.empty() && page == IslandPage::Reminder) page = IslandPage::Music;
        if (!HasMedia() && page == IslandPage::Music && !reminders.empty()) page = IslandPage::Reminder;
        if (notice && !expanded) { width.target = NoticeWidth; height.target = NoticeHeight; }
        else if (expanded && page == IslandPage::Reminder) { width.target = PageWidth; height.target = PageHeight; }
        else if (expanded) { width.target = ExpandedWidth; height.target = ExpandedHeight; }
        else if (HasMedia()) {
            width.target = CompactWidth + (hovered ? HoverGrowWidth : 0);
            height.target = CompactHeight + (hovered ? HoverGrowHeight : 0);
        } else {
            width.target = IdleWidth + (hovered ? HoverGrowWidth * .5f : 0);
            height.target = IdleHeight + (hovered ? HoverGrowHeight * .5f : 0);
        }
        if(!expanded&&!notice&&(display.leftWidget!=dynamic_island::CompactWidget::None||display.rightWidget!=dynamic_island::CompactWidget::None)){
            width.target=HasMedia()?360.f:260.f;height.target=36.f;
        }
        presence.target = Wanted() ? 1.f : 0.f;
        const bool idle = display.autoHide && GetTickCount64() >= activeUntil;
        tuck.target = idle && !hovered && !expanded && !notice ? 1.f : 0.f;
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
            p.islandShapeTimeline=shapeFrames.data();
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
        if(alpha<=.01f)return;
        if(display.leftWidget!=dynamic_island::CompactWidget::None||display.rightWidget!=dynamic_island::CompactWidget::None){
            const float inset=12+dynamic_island::IslandShoulder(h)*std::clamp(shape.x,0.f,1.f);
            DrawCompact(*canvas,display.leftWidget,{inset,1,98,h-2},compactData,weather,performanceSample,view.english,alpha);
            DrawCompact(*canvas,display.rightWidget,{w-inset-98,1,98,h-2},compactData,weather,performanceSample,view.english,alpha,true);
            if(HasMedia()){
                const float center=w/2;FadeImage(cover,{center-16,(h-24)/2,24,24},6,alpha);
                Bars(center+14,h/2+7,14,3,2,2,alpha);
            }
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
        if (!hwnd || suppressed || window_layer::Suppressed(hwnd)) return false;
        if(window_layer::enabled && window_layer::ReadContext().foreignForeground)return false;
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
        if (HasMedia() || reminders.empty()) pages.push_back(IslandPage::Music);
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
        if (!canvas->Begin(CanvasWidth, CanvasHeight, CanvasDpi())) return false;
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
        if (notice && !expanded) { NoticeView(w, h, std::clamp((h - CompactHeight) / (NoticeHeight - CompactHeight), 0.f, 1.f)); return FinishShape(); }
        // A tucked island exposes only its empty edge, never artwork or lyrics.
        const float revealed=1-std::clamp(tuck.x*1.4f,0.f,1.f);
        Compact(w, h, std::clamp(1 - e * 3, 0.f, 1.f)*revealed);
        const float content = std::clamp((e - .55f) / .45f, 0.f, 1.f) * std::clamp(pageFade.x, 0.f, 1.f)*revealed;
        // The new page slides in sideways while it fades in.
        const float slide = (1 - std::clamp(pageFade.x, 0.f, 1.f)) * 56 * static_cast<float>(pageDirection);
        canvas->PushClip({0, 0, w, h});
        canvas->Offset(slide, 0);
        switch (page) {
        case IslandPage::Music: Expanded(w, content); break;
        case IslandPage::Reminder: ReminderPage(w, h, content); break;
        }
        canvas->Offset(0, 0);
        canvas->PopClip();
        if (e > .9f) Dots(w, h, content);
        return FinishShape();
    }

    bool FinishShape() {
        if(!canvas->Finish(bitmap))return false;
        if(shape.x<=0)return true;
        const float scale=static_cast<float>(CanvasDpi())/96.f;
        const float w=drawnWidth*scale,h=drawnHeight*scale;
        const float radius=Radius()*scale,mix=std::clamp(shape.x,0.f,1.f);
        const unsigned liveW=std::min(bitmap.width,static_cast<unsigned>(std::ceil(w)));
        const unsigned liveH=std::min(bitmap.height,static_cast<unsigned>(std::ceil(h)));
        for(unsigned y=0;y<liveH;++y)for(unsigned x=0;x<liveW;++x) {
            auto& pixel=bitmap.pixels[static_cast<std::size_t>(y)*bitmap.width+x];
            if(!pixel)continue;
            const float distance=dynamic_island::MorphShapeDistance(static_cast<float>(x)+.5f,static_cast<float>(y)+.5f,w,h,radius,mix,scale);
            if(distance<=-.708f)continue;
            if(distance>=.708f){pixel=0;continue;}
            unsigned coverage=0;
            for(int sy=0;sy<8;++sy)for(int sx=0;sx<8;++sx)
                coverage+=dynamic_island::MorphShapeDistance(static_cast<float>(x)+(static_cast<float>(sx)+.5f)/8,
                    static_cast<float>(y)+(static_cast<float>(sy)+.5f)/8,w,h,radius,mix,scale)<=0?1u:0u;
            std::uint32_t next=0;
            for(unsigned shift:{0u,8u,16u,24u})next|=((((pixel>>shift)&255)*coverage+32)/64)<<shift;
            pixel=next;
        }
        return true;
    }

    // --- Fullscreen and games ------------------------------------------------

    // Returns true when an exclusive fullscreen game, a presentation, or a
    // window covering this monitor is in front. QUNS_BUSY alone is not used: it
    // also reports fullscreen windows on other monitors.
    bool FullscreenInFront() const {
        RECT area{}; MONITORINFO monitorInfo{sizeof(monitorInfo)};
        if(hwnd)GetWindowRect(hwnd,&area);
        if(IsRectEmpty(&area)&&ReadMonitor(monitorInfo))area=monitorInfo.rcMonitor;
        return window_layer::Protected(area,window_layer::ReadContext(),display.hideFullscreen)||window_layer::Suppressed(hwnd);
    }
    void CheckForeground() {
        const bool next = FullscreenInFront();
        if (next == suppressed) return;
        suppressed = next;
        if (suppressed) { expanded = hoverExpanded = false; notice.reset(); queued.clear(); WatchOutside(false); Settle(); }  // Leave at once, no animation over a game.
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
        if (hwnd) {
            window_layer::ConfigureIsland(hwnd,display.topmost,display.hideFullscreen);
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
        // Taskbar-button flashes of chat programs (best effort).
        RegisterShellHookWindow(hwnd);
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
    // Rates need consecutive samples. Keep them warm across page changes.
    void SyncPerformance() {
        using W=dynamic_island::CompactWidget;
        const auto needsSample=[](W value){return value==W::Network||value==W::Cpu||value==W::Memory||value==W::CpuMemory;};
        const bool want = preview != nullptr || (hwnd && (needsSample(display.leftWidget)||needsSample(display.rightWidget)));
        if (want && !performance.Running()) {
            performanceTarget = hwnd;
            performanceSample.reset();
            performance.Start();
        } else if (!want && performance.Running()) {
            performanceTarget = nullptr;
            performance.Stop();
            performanceSample.reset();
        }
    }
    void Stop() {
        expanded = false;
        // Join before destroying the HWND; late posted messages are ignored.
        performanceTarget = nullptr;
        performance.Stop();
        performanceSample.reset();
        StopClock();
        WatchOutside(false);
        DetachGlass();
        if (foregroundHook) { UnhookWinEvent(foregroundHook); foregroundHook = nullptr; }
        instances.erase(std::remove(instances.begin(),instances.end(),this),instances.end());
        if (hwnd) {
            const auto old = hwnd;
            hwnd = nullptr;
            if (GetCapture() == old) ReleaseCapture();
            DeregisterShellHookWindow(old);
            DestroyWindow(old);
        }
        surface.Release();
        canvas.reset();
        preview.reset();
        bitmap = {};
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
        if (!hwnd || expanded == next) return;
        expanded = next;
        if (next) {
            const auto pages = Pages();
            if (pages.empty()) { expanded = false; return; }
            if (std::find(pages.begin(),pages.end(),page)==pages.end()) page=pages.front();
            pageFade.Snap(1);
        } else if (HasMedia()) page = IslandPage::Music;
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
        if(dynamic_island::MorphShapeDistance(x,y,width.x,height.x,Radius(),shape.x)>0)return -1;
        if (notice && !expanded) return 400;
        if(!expanded){
            if(display.leftWidget!=dynamic_island::CompactWidget::None&&x<120)return 600;
            if(display.rightWidget!=dynamic_island::CompactWidget::None&&x>width.x-120)return 601;
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
        if (!HasMedia()) return 0;
        if (y >= 110 && y < 146) {
            const float cx = width.x / 2;
            if (std::abs(x - (cx - 72)) < 22) return 1;
            if (std::abs(x - cx) < 22) return 2;
            if (std::abs(x - (cx + 72)) < 22) return 3;
        }
        return y < 104 ? 0 : -1;
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
        if(hit==600||hit==601){
            using W=dynamic_island::CompactWidget;
            const auto choice=hit==600?display.leftWidget:display.rightWidget;
            if(choice==W::Files){FileMenu();return;}
            // Readouts share the capsule's normal expand action. Opening a
            // settings manager requires the explicit context-menu command.
            Expand(!expanded);
            return;
        }
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
    void Menu(POINT at) {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return;
        menuOpen = true;
        KillTimer(hwnd, CollapseTimer);
        const bool frosted = display.material == IslandMaterial::Frosted, none = display.material == IslandMaterial::None;
        const UINT command = LumeWidgetMenu::ShowIsland(hwnd, at, frosted, none, expanded, display.messages,
                                                       HasMedia() || !reminders.empty());
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
    void SyncHoverExpansion() {
        if(!hwnd)return;
        KillTimer(hwnd,HoverTimer);
        if(display.hoverExpand&&hovered&&!expanded&&!suppressed&&!menuOpen)
            SetTimer(hwnd,HoverTimer,std::clamp(display.hoverDelayMs,100u,1000u),nullptr);
    }
    void Hover(bool inside) {
        if (inside == hovered) return;
        hovered = inside;
        if (inside) {
            KillTimer(hwnd, CollapseTimer);
            SyncHoverExpansion();
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
        if(message==desktop_level::Message()) {
            self->CheckForeground();
            if(!self->suppressed&&!window_layer::Suppressed(hwnd)) {
                self->Animate();
                if(!self->display.topmost&&IsWindowVisible(hwnd))desktop_level::Place(hwnd,false);
            }
            return 0;
        }
        if (!self->display.topmost) {
            if (message == WM_WINDOWPOSCHANGING) desktop_level::Changing(l);
        }
        static const UINT shellHook = RegisterWindowMessageW(L"SHELLHOOK");
        if (shellHook && message == shellHook) {
            if ((w & 0x7fff) == HSHELL_REDRAW && (w & HSHELL_HIGHBIT)) self->ChatFlash(reinterpret_cast<HWND>(l));
            return 0;
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
                self->expanded=self->hoverExpanded=false;self->WatchOutside(false);
                self->DetachGlass();self->clock.Pause();self->animating=false;
            }
            return 0;
        case ForegroundMessage: self->CheckForeground(); return 0;
        case PerformanceMessage:
            if (!self->performanceTarget.load()) return 0;  // a late sample after shutdown
            self->performanceSample = self->performance.Latest();
            if (self->visible && !self->expanded) self->Place();
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
                self->compactData.Refresh();self->compactData.files=static_cast<unsigned>(self->heldFiles.size());
                if(!self->expanded&&self->visible)self->Place();
                self->CheckForeground();
                if (const auto now = Today(); now != self->today) {
                    self->today = now;
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
                if (self->display.hoverExpand&&self->hovered&&!self->suppressed&&!self->menuOpen) self->Expand(true, true);
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
        case WM_DPICHANGED: self->dpi = HIWORD(w); if(self->canvas)self->Animate(); return 0;
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
std::optional<performance::Snapshot> IslandWindow::PerformanceReading() const { return impl_->performanceSample; }
bool IslandWindow::Start(const widget::Preferences& p, const IslandDisplay& d) { return impl_->Start(p, d); }
void IslandWindow::SetMonitor(const IslandMonitor& monitor) {
    if(impl_->targetMonitor&&*impl_->targetMonitor==monitor)return;
    impl_->targetMonitor=monitor;
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
    else if (impl_->visible && !IsIconic(impl_->hwnd)) impl_->Place();
}
void IslandWindow::Expand(bool expanded) { impl_->Expand(expanded); }
void IslandWindow::SetTodoSource(IslandTodoSource source) { impl_->todos = std::move(source); }
void IslandWindow::SetWeather(dynamic_island::Weather value) {
    impl_->weather = std::move(value);
    if (impl_->hwnd && impl_->visible && !impl_->expanded) impl_->Place();
}
void IslandWindow::RefreshTodos() {
    if (impl_->hwnd && impl_->visible && impl_->expanded && impl_->page == IslandPage::Reminder) impl_->Place();
}
IslandPage IslandWindow::Page() const { return impl_->page; }
bool IslandWindow::ShowNotice(const std::string& id, const std::wstring& title, const std::wstring& time) {
    return impl_->ShowNotice(id, title, time);
}
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
