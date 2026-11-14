// Explicit before/after probe; no user settings, screen capture or pointer input.
#include "platform/windows/dynamic_island/island_window.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <gdiplus.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

using namespace lume;
using namespace lume::platform::windows;
using Json = nlohmann::json;
namespace {
double Now() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Samples {
    std::vector<double> values;
    void Add(double value) { values.push_back(value); }
    Json Report() const {
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        if (sorted.empty()) return Json{{"count", 0}};
        const auto percentile = [&](double p) { return sorted[static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1))]; };
        return {{"count", sorted.size()}, {"p50_ms", percentile(.5)}, {"p95_ms", percentile(.95)},
                {"max_ms", sorted.back()}, {"over_8_ms", std::count_if(sorted.begin(), sorted.end(), [](double v) { return v > 8; })},
                {"over_16_ms", std::count_if(sorted.begin(), sorted.end(), [](double v) { return v > 16; })}};
    }
};
std::string Hash(const WidgetBitmap& frame) {
    std::uint64_t value = 14695981039346656037ull;
    for (auto pixel : frame.pixels) for (unsigned shift : {0u, 8u, 16u, 24u}) {
        value ^= (pixel >> shift) & 255u;
        value *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}
bool Save(const WidgetBitmap& frame, const std::filesystem::path& path) {
    if (frame.pixels.empty()) return false;
    Gdiplus::Bitmap image(frame.width, frame.height, static_cast<INT>(frame.width * 4), PixelFormat32bppPARGB,
                         reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.pixels.data())));
    const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    return image.Save(path.c_str(), &png) == Gdiplus::Ok;
}
void Inputs(IslandWindow& island) {
    IslandPresentation state;
    state.media.available = true;
    state.media.playing = false;
    state.media.title = L"Interaction probe";
    state.media.artist = L"Lume";
    state.media.duration = 180;
    state.media.position = 45;
    state.time = L"10:08";
    state.date = L"2026-11-08";
    island.Update(state);
    dynamic_island::Weather weather;
    weather.enabled = weather.available = weather.hasRange = true;
    weather.temperature = 18;
    weather.high = 22;
    weather.low = 12;
    weather.city = L"Lume";
    weather.condition = L"Cloudy";
    weather.code = 3;
    island.SetWeather(weather);
    IslandTodoSource todos;
    todos.forDay = [](calendar::Date) {
        return std::vector<IslandTodo>{{"probe-a", L"Check smooth motion", L"10:08", false, false, 0x30d158, L""},
                                       {"probe-b", L"Keep existing pixels", L"11:00", true, false, 0x0a84ff, L""}};
    };
    todos.hasTodos = [](calendar::Date) { return true; };
    todos.writable = [] { return true; };
    island.SetTodoSource(std::move(todos));
}
Json Previews(const std::filesystem::path& root) {
    // Preview has an intentionally moving demo song. Use static weather/todo
    // widgets for byte-for-byte comparisons; real HWND music below is paused.
    IslandWindow island(GetModuleHandleW(nullptr), {});
    Inputs(island);
    Json frames = Json::array();
    Samples rendering;
    for (const auto shape : {IslandShape::Standard, IslandShape::Capsule})
      for (const auto size : {IslandSize::Small, IslandSize::Standard, IslandSize::Large})
        for (const auto material : {IslandMaterial::None, IslandMaterial::Liquid, IslandMaterial::Frosted}) {
            IslandDisplay display;
            display.autoHide = false;
            display.shape = shape;
            display.size = size;
            display.material = material;
            display.leftWidget = dynamic_island::CompactWidget::Weather;
            display.rightWidget = dynamic_island::CompactWidget::TemperatureRange;
            display.expandedWidgets = {dynamic_island::ExpandedWidget::CalendarWeather, dynamic_island::ExpandedWidget::Todos};
            display.animationSpeed = static_cast<IslandAnimationSpeed>(static_cast<int>(size));
            display.bounce = static_cast<IslandBounce>(static_cast<int>(size));
            island.ReleasePreview();
            const auto prefix = std::to_string(static_cast<int>(shape)) + "-" + std::to_string(static_cast<int>(size)) + "-" +
                                std::to_string(static_cast<int>(material));
            const auto capture = [&](bool expanded, float advance, const std::string& label, bool image) {
                const auto start = Now();
                const auto frame = island.Preview(display, expanded, advance);
                rendering.Add(Now() - start);
                if (!frame.width || !frame.height || frame.pixels.pixels.empty()) throw std::runtime_error("empty preview frame");
                frames.push_back({{"case", prefix + "-" + label}, {"width", frame.width}, {"height", frame.height},
                    {"radius", frame.cornerRadius}, {"shape", frame.shapeMix}, {"top", frame.topOffset},
                    {"animating", frame.animating}, {"hash", Hash(frame.pixels)}});
                if (image && !Save(frame.pixels, root / (prefix + "-" + label + ".png"))) throw std::runtime_error("preview save failed");
            };
            capture(false, 0, "compact", size == IslandSize::Standard && material == IslandMaterial::None);
            for (int step = 0; step < 6; ++step) capture(true, .04f, "open-" + std::to_string(step), step == 2 && size == IslandSize::Standard && material == IslandMaterial::None);
            capture(true, 0, "expanded", size == IslandSize::Standard);
            for (int step = 0; step < 6; ++step) capture(false, .04f, "close-" + std::to_string(step), false);
        }
    island.ReleasePreview();
    return {{"frames", frames}, {"render", rendering.Report()}};
}
void ValidFrame(const IslandPreviewFrame& frame) {
    if (!frame.width || !frame.height || frame.pixels.width != frame.width || frame.pixels.height != frame.height ||
        frame.pixels.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height ||
        !std::isfinite(frame.cornerRadius) || !std::isfinite(frame.shapeMix) || !std::isfinite(frame.topOffset) ||
        std::none_of(frame.pixels.pixels.begin(), frame.pixels.pixels.end(), [](std::uint32_t value) { return value != 0; }))
        throw std::runtime_error("empty or invalid matrix frame");
}
Json MotionMatrix() {
    Json cases = Json::array();
    unsigned totalFrames = 0;
    for (const auto shape : {IslandShape::Standard, IslandShape::Capsule})
      for (const auto speed : {IslandAnimationSpeed::Slow, IslandAnimationSpeed::Normal, IslandAnimationSpeed::Fast})
        for (const auto bounce : {IslandBounce::Subtle, IslandBounce::Normal, IslandBounce::Bouncy}) {
            IslandWindow island(GetModuleHandleW(nullptr), {}), reference(GetModuleHandleW(nullptr), {});
            Inputs(island);
            Inputs(reference);
            IslandDisplay display;
            display.autoHide = false;
            display.shape = shape;
            display.size = IslandSize::Small;
            display.animationSpeed = speed;
            display.bounce = bounce;
            display.leftWidget = dynamic_island::CompactWidget::Weather;
            display.rightWidget = dynamic_island::CompactWidget::TemperatureRange;
            display.expandedWidgets = {dynamic_island::ExpandedWidget::CalendarWeather, dynamic_island::ExpandedWidget::Todos};
            Json frames = Json::array();
            const auto capture = [&](bool expanded, float advance, const std::string& phase) {
                const auto frame = island.Preview(display, expanded, advance);
                ValidFrame(frame);
                ++totalFrames;
                frames.push_back({{"phase", phase}, {"width", frame.width}, {"height", frame.height},
                    {"radius", frame.cornerRadius}, {"shape", frame.shapeMix}, {"top", frame.topOffset},
                    {"animating", frame.animating}, {"hash", Hash(frame.pixels)}});
                return frame;
            };
            capture(false, 0, "initial");
            for (int step = 0; step < 4; ++step) capture(true, .016f, "open");
            for (int step = 0; step < 3; ++step) capture(false, .016f, "reverse");
            display.shape = shape == IslandShape::Standard ? IslandShape::Capsule : IslandShape::Standard;
            display.size = IslandSize::Large;
            for (int step = 0; step < 4; ++step) capture(true, .016f, "morph-and-grow");
            display.size = IslandSize::Small;
            for (int step = 0; step < 3; ++step) capture(false, .016f, "reverse-and-shrink");
            display.shape = shape;
            display.size = IslandSize::Standard;
            IslandPreviewFrame final;
            unsigned settleFrames = 0;
            do { final = capture(true, .05f, "settle-open"); ++settleFrames; } while (final.animating && settleFrames < 180);
            const auto expectedOpen = reference.Preview(display, true);
            ValidFrame(expectedOpen);
            const auto equal = [](const IslandPreviewFrame& a, const IslandPreviewFrame& b) {
                return a.width == b.width && a.height == b.height && a.cornerRadius == b.cornerRadius &&
                    a.shapeMix == b.shapeMix && a.topOffset == b.topOffset && a.pixels.pixels == b.pixels.pixels;
            };
            if (final.animating || !equal(final, expectedOpen)) throw std::runtime_error("matrix opening did not converge to original settled geometry/pixels");
            settleFrames = 0;
            do { final = capture(false, .05f, "settle-close"); ++settleFrames; } while (final.animating && settleFrames < 180);
            const auto expectedClosed = reference.Preview(display, false);
            ValidFrame(expectedClosed);
            if (final.animating || !equal(final, expectedClosed)) throw std::runtime_error("matrix closing did not converge to original settled geometry/pixels");
            cases.push_back({{"shape", static_cast<int>(shape)}, {"speed", static_cast<int>(speed)},
                             {"bounce", static_cast<int>(bounce)}, {"converged", true}, {"frames", frames}});
        }
    return {{"case_count", cases.size()}, {"frame_count", totalFrames}, {"cases", cases}};
}
void Pump(unsigned duration, Samples& dispatch, Samples& frameDispatch) {
    const auto end = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            const double start = Now();
            DispatchMessageW(&message);
            const double spent = Now() - start;
            dispatch.Add(spent);
            if (message.message == WM_APP + 0x6e4) frameDispatch.Add(spent);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}
Json Runtime(const std::filesystem::path& root) {
    Json cases = Json::array();
    for (const auto shape : {IslandShape::Standard, IslandShape::Capsule}) {
        IslandWindow island(GetModuleHandleW(nullptr), {});
        IslandDisplay display;
        display.autoHide = false;
        display.topmost = true;
        display.hideFullscreen = false;
        display.shape = shape;
        display.expandedWidgets = {dynamic_island::ExpandedWidget::Music, dynamic_island::ExpandedWidget::CalendarWeather,
                                   dynamic_island::ExpandedWidget::Todos};
        widget::Preferences preferences;
        preferences.enabled = true;
        if (!island.Start(preferences, display)) throw std::runtime_error("runtime island failed to start");
        Inputs(island);
        island.Settle();
        Samples dispatch, frameDispatch, expand, hover;
        Pump(100, dispatch, frameDispatch);
        for (int round = 0; round < 4; ++round) {
            for (const bool expanded : {true, false}) {
                const auto start = Now();
                island.Expand(expanded);
                expand.Add(Now() - start);
                Pump(350, dispatch, frameDispatch);
            }
        }
        island.Expand(true);
        island.Settle();
        SendMessageW(island.Handle(), WM_MOUSEMOVE, 0, MAKELPARAM(30, 30));
        island.Settle();
        const auto prefix = "runtime-" + std::to_string(static_cast<int>(shape));
        if (!Save(island.Frame(), root / (prefix + ".png"))) throw std::runtime_error("runtime save failed");
        RECT rect{};
        GetClientRect(island.Handle(), &rect);
        for (int round = 0; round < 3; ++round) for (int y = 1; y < 5; ++y) for (int x = 1; x < 17; ++x) {
            const auto start = Now();
            SendMessageW(island.Handle(), WM_MOUSEMOVE, 0, MAKELPARAM(rect.right * x / 18, rect.bottom * y / 6));
            hover.Add(Now() - start);
        }
        cases.push_back({{"shape", static_cast<int>(shape)}, {"expand", expand.Report()}, {"hover", hover.Report()},
                         {"dispatch", dispatch.Report()}, {"animation_dispatch", frameDispatch.Report()}});
        island.Stop();
        Pump(10, dispatch, frameDispatch);
    }
    return cases;
}
Json DisplayEvents() {
    BOOL motionBefore = TRUE;
    const bool motionRead = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &motionBefore, 0) != FALSE;
    Json cases = Json::array();
    for (const auto shape : {IslandShape::Standard, IslandShape::Capsule}) {
        IslandWindow island(GetModuleHandleW(nullptr), {});
        IslandDisplay display;
        display.autoHide = false;
        display.hideFullscreen = false;
        display.topmost = true;
        display.shape = shape;
        widget::Preferences preferences;
        preferences.enabled = true;
        if (!island.Start(preferences, display)) throw std::runtime_error("display-event island failed to start");
        Inputs(island);
        island.Settle();
        const auto screens = ConnectedIslandMonitors();
        if (screens.empty()) throw std::runtime_error("no connected monitors");
        for (std::size_t screen = 0; screen < screens.size(); ++screen) {
            island.SetMonitor(screens[screen]);
            island.SetMonitor(screens[screen]); // The same-screen fast path must remain usable.
            island.Settle();
            const HWND hwnd = island.Handle();
            const unsigned dpi = GetDpiForWindow(hwnd);
            if (!dpi) throw std::runtime_error("display-event window has no DPI");
            RECT rect{};
            GetWindowRect(hwnd, &rect);
            const UINT messages[] = {WM_DISPLAYCHANGE, WM_SETTINGCHANGE, WM_DPICHANGED};
            for (const auto message : messages) {
                if (message == WM_DPICHANGED)
                    SendMessageW(hwnd, message, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
                else if (message == WM_DISPLAYCHANGE)
                    SendMessageW(hwnd, message, 32, MAKELPARAM(screens[screen].bounds.right - screens[screen].bounds.left,
                                                             screens[screen].bounds.bottom - screens[screen].bounds.top));
                else SendMessageW(hwnd, message, SPI_GETCLIENTAREAANIMATION, 0);
                Samples dispatch, frameDispatch;
                island.Expand(true);
                Pump(20, dispatch, frameDispatch);
                island.Expand(false);
                Pump(20, dispatch, frameDispatch);
                island.Expand(true);
                island.Settle();
                const auto open = island.Frame();
                RECT openRect{};
                const bool openRectRead = GetWindowRect(hwnd, &openRect) != FALSE;
                const auto canvasDpi = static_cast<unsigned>(std::lround(static_cast<double>(dpi) * 1.2));
                const auto px = [canvasDpi](float dip) {
                    return static_cast<int>(std::lround(dip * static_cast<float>(canvasDpi) / 96.f));
                };
                const int openWidth = static_cast<int>(openRect.right - openRect.left);
                const int openHeight = static_cast<int>(openRect.bottom - openRect.top);
                if (island.Handle() != hwnd || !IsWindow(hwnd) || !island.Expanded() || open.pixels.empty() || !openRectRead ||
                    std::abs(openWidth - px(372)) > 1 || std::abs(openHeight - px(156)) > 1)
                    throw std::runtime_error("display event broke opening after quick reversal");
                // Ensure the compact target is the ordinary paused-media size,
                // independent of whether the real pointer happens to be nearby.
                SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0);
                island.Expand(false);
                island.Settle();
                const auto closed = island.Frame();
                RECT closedRect{};
                const bool closedRectRead = GetWindowRect(hwnd, &closedRect) != FALSE;
                const int closedWidth = static_cast<int>(closedRect.right - closedRect.left);
                const int closedHeight = static_cast<int>(closedRect.bottom - closedRect.top);
                if (island.Handle() != hwnd || island.Expanded() || closed.pixels.empty() || GetDpiForWindow(hwnd) != dpi ||
                    !closedRectRead || std::abs(closedWidth - px(236)) > 1 || std::abs(closedHeight - px(34)) > 1)
                    throw std::runtime_error("display event broke closing or changed actual DPI");
                cases.push_back({{"shape", static_cast<int>(shape)}, {"monitor", screen}, {"dpi", dpi},
                    {"canvas_dpi", canvasDpi}, {"message", message}, {"open_width", openWidth}, {"open_height", openHeight},
                    {"closed_width", closedWidth}, {"closed_height", closedHeight}, {"passed", true}});
            }
        }
        island.Stop();
    }
    BOOL motionAfter = TRUE;
    const bool motionStillRead = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &motionAfter, 0) != FALSE;
    if (motionRead != motionStillRead || (motionRead && motionBefore != motionAfter))
        throw std::runtime_error("system animation preference changed during read-only probe");
    return {{"cases", cases}, {"case_count", cases.size()}, {"animation_preference_read", motionRead},
            {"animation_preference", motionBefore != FALSE}, {"animation_preference_unchanged", true}};
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "usage: lume-island-interaction-probe <output-directory>\n"; return 2; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) { CoUninitialize(); return 4; }
    int result = 0;
    try {
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root);
        const auto start = Now();
        Json report;
        report["preview"] = Previews(root);
        report["runtime"] = Runtime(root);
        report["motion_matrix"] = MotionMatrix();
        report["display_events"] = DisplayEvents();
        report["elapsed_ms"] = Now() - start;
        std::ofstream(root / "report.json") << report.dump(2);
        std::cout << "frames=" << report["preview"]["frames"].size() << " elapsed_ms=" << report["elapsed_ms"]
                  << " preview=" << report["preview"]["render"].dump() << " runtime=" << report["runtime"].dump() << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    return result;
}
