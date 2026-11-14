#include "platform/windows/dynamic_island/island_window.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <cstdlib>
#include <string>
#include <vector>

using namespace lume;
using namespace lume::platform::windows;
using Json = nlohmann::json;

namespace {
constexpr UINT FrameMessage = WM_APP + 0x6e4;

double Now() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Samples {
    std::vector<double> values;
    void Add(double value) { values.push_back(value); }
    Json Report() const {
        if (values.empty()) return Json{{"count", 0}};
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](double p) {
            return sorted[static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1))];
        };
        const auto count = [&](double threshold) {
            return std::count_if(sorted.begin(), sorted.end(), [threshold](double value) { return value > threshold; });
        };
        return {{"count", sorted.size()}, {"p50_ms", percentile(.50)}, {"p95_ms", percentile(.95)},
                {"p99_ms", percentile(.99)}, {"max_ms", sorted.back()}, {"over_8_ms", count(8)},
                {"over_16_ms", count(16)}, {"over_32_ms", count(32)}};
    }
};

struct PumpResult {
    unsigned frames = 0;
};

PumpResult Pump(unsigned milliseconds, Samples& dispatch, Samples& frameDispatch) {
    const auto end = GetTickCount64() + milliseconds;
    PumpResult result;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            const double start = Now();
            DispatchMessageW(&message);
            const double spent = Now() - start;
            dispatch.Add(spent);
            if (message.message == FrameMessage) {
                frameDispatch.Add(spent);
                ++result.frames;
            }
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    } while (GetTickCount64() < end);
    return result;
}

HWND GlassWindow(HWND owner) {
    struct Search { HWND owner; HWND glass; } search{owner, nullptr};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(value);
        if (GetPropW(window, L"LumeGlassSurfaceOwner") == reinterpret_cast<HANDLE>(search.owner)) {
            search.glass = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.glass;
}

Json GlassState(HWND owner) {
    const HWND glass = GlassWindow(owner);
    return {{"exists", glass != nullptr}, {"visible", glass && IsWindowVisible(glass) != FALSE},
            {"owner_visible", IsWindowVisible(owner) != FALSE}, {"glass", reinterpret_cast<std::uintptr_t>(glass)}};
}

void Inputs(IslandWindow& island, bool uv = false) {
    IslandPresentation state;
    state.media.available = true;
    state.media.playing = false;
    state.media.id = "glass-probe";
    state.media.title = L"Paused glass probe";
    state.media.artist = L"Lume";
    state.media.duration = 180;
    state.media.position = 45;
    state.time = L"10:08";
    state.date = L"2026-11-08";
    state.english = true;
    island.Update(state);

    dynamic_island::Weather weather;
    weather.enabled = weather.available = weather.hasRange = true;
    weather.temperature = 18;
    weather.high = 22;
    weather.low = 12;
    weather.city = L"Lume";
    weather.condition = L"Cloudy";
    weather.code = 3;
    if (uv) weather.uv = 3.5;
    island.SetWeather(weather);

    IslandTodoSource todos;
    todos.forDay = [](calendar::Date) {
        return std::vector<IslandTodo>{{"glass-a", L"Glass expand workload", L"10:08", false, false, 0x30d158, L""},
                                       {"glass-b", L"Keep the existing visual", L"11:00", true, false, 0x0a84ff, L""}};
    };
    todos.hasTodos = [](calendar::Date) { return true; };
    todos.writable = [] { return true; };
    island.SetTodoSource(std::move(todos));
}

void Drain(unsigned milliseconds, Samples& dispatch, Samples& frames) {
    Pump(milliseconds, dispatch, frames);
}

Json RunCase(const IslandMonitor& monitor, IslandMaterial material, IslandSize size,
             const std::vector<dynamic_island::ExpandedWidget>& widgets) {
    IslandWindow island(GetModuleHandleW(nullptr), {});
    IslandDisplay display;
    display.autoHide = false;
    display.topmost = true;
    display.hideFullscreen = false;
    display.material = material;
    display.shape = IslandShape::Standard;
    display.size = size;
    display.bounce = IslandBounce::Normal;
    display.animationSpeed = IslandAnimationSpeed::Normal;
    display.expandedWidgets = widgets;

    widget::Preferences preferences;
    preferences.enabled = true;
    if (!island.Start(preferences, display)) throw std::runtime_error("glass probe island failed to start");
    island.SetMonitor(monitor);
    Inputs(island);
    island.Settle();

    Samples dispatch, frameDispatch, expandCall, collapseCall;
    Drain(1200, dispatch, frameDispatch); // material attach and first presented frame
    const auto beforeGlass = GlassState(island.Handle());
    unsigned glassVisibleTransitions = 0;
    unsigned glassPresentTransitions = 0;
    unsigned totalFrames = 0;
    const int rounds = [] {
        if (const char* value = std::getenv("LUME_GLASS_PROBE_ROUNDS")) {
            const int parsed = std::atoi(value);
            if (parsed >= 1 && parsed <= 100) return parsed;
        }
        return 20;
    }();
    for (int round = 0; round < rounds; ++round) {
        const double openStart = Now();
        island.Expand(true);
        expandCall.Add(Now() - openStart);
        const auto open = Pump(260, dispatch, frameDispatch);
        totalFrames += open.frames;
        const auto openGlass = GlassState(island.Handle());
        glassPresentTransitions += openGlass["exists"].get<bool>() ? 1u : 0u;
        glassVisibleTransitions += openGlass["visible"].get<bool>() ? 1u : 0u;

        const double closeStart = Now();
        island.Expand(false);
        collapseCall.Add(Now() - closeStart);
        const auto close = Pump(260, dispatch, frameDispatch);
        totalFrames += close.frames;
        const auto closeGlass = GlassState(island.Handle());
        glassPresentTransitions += closeGlass["exists"].get<bool>() ? 1u : 0u;
        glassVisibleTransitions += closeGlass["visible"].get<bool>() ? 1u : 0u;
    }
    island.Settle();
    const auto afterGlass = GlassState(island.Handle());
    const auto result = Json{
        {"monitor", monitor.device}, {"material", material == IslandMaterial::Frosted ? "frosted" : "none"},
        {"size", size == IslandSize::Large ? "large" : "standard"},
        {"layout", widgets.size() == 3 ? "three" : "default"},
        {"monitor_bounds", {monitor.bounds.left, monitor.bounds.top, monitor.bounds.right, monitor.bounds.bottom}},
        {"monitor_work", {monitor.work.left, monitor.work.top, monitor.work.right, monitor.work.bottom}},
        {"before_glass", beforeGlass}, {"after_glass", afterGlass}, {"glass_present_transitions", glassPresentTransitions},
        {"glass_visible_transitions", glassVisibleTransitions}, {"transition_count", rounds * 2}, {"frames", totalFrames},
        {"expand_call", expandCall.Report()}, {"collapse_call", collapseCall.Report()},
        {"dispatch", dispatch.Report()}, {"frame_dispatch", frameDispatch.Report()}
    };
    island.Stop();
    return result;
}

// Opt-in hover fixture. The default direct-Expand matrix below stays unchanged.
// This uses the real HoverTimer/CollapseTimer and the same UI thread for both
// displays, without sending pointer input to another process or moving it.
unsigned EnvironmentNumber(const char* name, unsigned fallback, unsigned low, unsigned high) {
    const char* value = std::getenv(name);
    if (!value) return fallback;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    return end != value && !*end && parsed >= low && parsed <= high ? static_cast<unsigned>(parsed) : fallback;
}

const char* MessageName(UINT message, WPARAM parameter, bool island = true) {
    if (message == FrameMessage) return "FrameMessage";
    if (message == WM_MOUSEMOVE) return "WM_MOUSEMOVE";
    if (message == WM_MOUSELEAVE) return "WM_MOUSELEAVE";
    if (message == WM_TIMER) {
        if (!island) return "WM_TIMER";
        switch (parameter) {
        case 1: return "WM_TIMER/TickTimer";
        case 2: return "WM_TIMER/AnimationTimer";
        case 3: return "WM_TIMER/HoverTimer";
        case 4: return "WM_TIMER/CollapseTimer";
        case 5: return "WM_TIMER/BarsTimer";
        case 6: return "WM_TIMER/IdleTimer";
        case 7: return "WM_TIMER/NoticeTimer";
        case 8: return "WM_TIMER/CalendarTimer";
        default: return "WM_TIMER/other";
        }
    }
    return "other";
}

double ThreadCpuMillis() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1;
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return static_cast<double>(k.QuadPart + u.QuadPart) / 10000.;
}

struct HoverSlot {
    IslandMonitor monitor;
    std::unique_ptr<IslandWindow> island;
    unsigned ticks = 0, expansionChanges = 0;
    double frameStart = 0, frameEnd = 0;
    Samples frameGaps, frameEndGaps;
};

struct HoverTrace {
    struct Event {
        double at, ms, cpu, startGap, endGap;
        HWND window;
        UINT message;
        WPARAM parameter;
        int owner;
        unsigned round;
        const char* phase;
        bool sent;
        char slowClass[64]{};
    };
    std::vector<HoverSlot>& slots;
    std::vector<Event> events;
    std::map<std::pair<UINT, WPARAM>, Samples> messages;
    Samples dispatch, frameDispatch, dispatchCpu, frameCpu;
    double started = Now();
    ULONGLONG startedTick = GetTickCount64();
    unsigned round = 0, ignoredQueuedLeaves = 0, droppedEvents = 0;
    const char* phase = "warmup";
    HWND held = nullptr;
    explicit HoverTrace(std::vector<HoverSlot>& values) : slots(values) { events.reserve(32768); }
    int Owner(HWND window) const {
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (slots[i].island->Handle() == window) return static_cast<int>(i);
        return -1;
    }
    void Phase(const char* next) {
        phase = next;
        for (auto& slot : slots) slot.frameStart = slot.frameEnd = 0;
    }
    void Record(HWND window, UINT message, WPARAM parameter, double begin, double end, double cpu, bool sent) {
        const double spent = end - begin;
        const int owner = Owner(window);
        double startGap = 0, endGap = 0;
        dispatch.Add(spent);
        if (cpu >= 0) dispatchCpu.Add(cpu);
        messages[{message, message == WM_TIMER ? parameter : 0}].Add(spent);
        if (message == FrameMessage && owner >= 0) {
            frameDispatch.Add(spent);
            if (cpu >= 0) frameCpu.Add(cpu);
            auto& slot = slots[static_cast<std::size_t>(owner)];
            if (slot.frameStart) { startGap = begin - slot.frameStart; slot.frameGaps.Add(startGap); }
            if (slot.frameEnd) { endGap = end - slot.frameEnd; slot.frameEndGaps.Add(endGap); }
            slot.frameStart = begin; slot.frameEnd = end;
        }
        if (events.size() < 100000) {
            Event event{begin - started, spent, cpu, startGap, endGap, window, message, parameter,
                owner, round, phase, sent};
            if (spent > 40 && window) GetClassNameA(window, event.slowClass, 64);
            events.push_back(event);
        } else ++droppedEvents;
    }
    void Send(HWND window, UINT message, WPARAM parameter = 0, LPARAM data = 0) {
        const double cpuBegin = ThreadCpuMillis(), begin = Now();
        SendMessageW(window, message, parameter, data);
        const double end = Now(), cpuEnd = ThreadCpuMillis();
        Record(window, message, parameter, begin, end,
            cpuBegin >= 0 && cpuEnd >= cpuBegin ? cpuEnd - cpuBegin : -1, true);
    }
    void PumpFor(unsigned milliseconds) {
        const auto deadline = GetTickCount64() + milliseconds;
        do {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                // TrackMouseEvent observes the real pointer, which deliberately
                // stays elsewhere. Suppress its queued leave only while this
                // owned fixture emulates a resting pointer; explicit sent
                // WM_MOUSELEAVE still runs the real product leave path.
                if (message.message == WM_MOUSELEAVE && message.hwnd == held) { ++ignoredQueuedLeaves; continue; }
                TranslateMessage(&message);
                const double cpuBegin = ThreadCpuMillis(), begin = Now();
                DispatchMessageW(&message);
                const double end = Now(), cpuEnd = ThreadCpuMillis();
                Record(message.hwnd, message.message, message.wParam, begin, end,
                    cpuBegin >= 0 && cpuEnd >= cpuBegin ? cpuEnd - cpuBegin : -1, false);
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
        } while (GetTickCount64() < deadline);
    }
    Json Encode(const Event& event) const {
        return {{"at_ms", event.at}, {"dispatch_ms", event.ms}, {"thread_cpu_ms", event.cpu}, {"frame_begin_gap_ms", event.startGap},
            {"frame_end_gap_ms", event.endGap}, {"hwnd", reinterpret_cast<std::uintptr_t>(event.window)},
            {"message", event.message}, {"message_name", MessageName(event.message, event.parameter, event.owner >= 0)},
            {"wparam", static_cast<std::uint64_t>(event.parameter)}, {"island_index", event.owner},
            {"round", event.round}, {"phase", event.phase}, {"sent", event.sent}, {"slow_window_class", event.slowClass}};
    }
    Json Save(const std::filesystem::path& root) const {
        std::ofstream stream(root / "dispatches.jsonl");
        Json slow = Json::array(), byMessage = Json::array();
        for (const auto& event : events) {
            const auto encoded = Encode(event);
            stream << encoded.dump() << '\n';
            if (event.ms > 40) slow.push_back(encoded);
        }
        for (const auto& entry : messages) byMessage.push_back({
            {"message", entry.first.first}, {"wparam", static_cast<std::uint64_t>(entry.first.second)},
            {"message_name", MessageName(entry.first.first, entry.first.second, false)}, {"dispatch", entry.second.Report()}});
        return {{"dispatch", dispatch.Report()}, {"frame_dispatch", frameDispatch.Report()},
            {"dispatch_thread_cpu", dispatchCpu.Report()}, {"frame_thread_cpu", frameCpu.Report()},
            {"start_steady_ms", started}, {"start_tick_ms", startedTick},
            {"messages", byMessage}, {"slow_dispatches_over_40_ms", slow},
            {"trace_events", events.size()}, {"dropped_trace_events", droppedEvents},
            {"ignored_queued_leave_messages", ignoredQueuedLeaves}};
    }
};

Json RunHover(const std::vector<IslandMonitor>& monitors, const std::filesystem::path& root) {
    const bool dual = std::getenv("LUME_GLASS_PROBE_HOVER_DUAL") != nullptr;
    const unsigned monitorNumber = EnvironmentNumber("LUME_GLASS_PROBE_HOVER_MONITOR",
        static_cast<unsigned>(monitors.size()), 1, static_cast<unsigned>(monitors.size()));
    const unsigned rounds = EnvironmentNumber("LUME_GLASS_PROBE_ROUNDS", 8, 1, 100);
    const unsigned delay = SnapIslandHoverDelay(EnvironmentNumber("LUME_GLASS_PROBE_HOVER_DELAY_MS", 650, 100, 1000));
    const unsigned hold = EnvironmentNumber("LUME_GLASS_PROBE_HOVER_HOLD_MS", 800, 200, 5000);
    const unsigned leave = EnvironmentNumber("LUME_GLASS_PROBE_HOVER_LEAVE_MS", 1200, 500, 5000);
    IslandDisplay display;
    display.screen = 0; display.autoHide = display.topmost = display.hoverExpand = true;
    display.hoverDelayMs = delay; display.material = IslandMaterial::None;
    display.shape = IslandShape::Standard; display.size = IslandSize::Standard;
    display.bounce = IslandBounce::Bouncy; display.animationSpeed = IslandAnimationSpeed::Normal;
    display.leftWidget = dynamic_island::CompactWidget::Uv;
    display.rightWidget = dynamic_island::CompactWidget::None;
    display.expandedWidgets = {dynamic_island::ExpandedWidget::CalendarWeather, dynamic_island::ExpandedWidget::Todos};
    std::vector<HoverSlot> slots;
    slots.reserve(dual ? std::min<std::size_t>(2, monitors.size()) : 1);
    for (std::size_t index = 0; index < monitors.size(); ++index) {
        if (dual ? index >= 2 : index + 1 != monitorNumber) continue;
        slots.emplace_back(); auto& slot = slots.back(); slot.monitor = monitors[index];
        auto* counters = &slot;
        slot.island = std::make_unique<IslandWindow>(GetModuleHandleW(nullptr), [counters](dynamic_island::Action action) {
            if (action == dynamic_island::Action::Tick) ++counters->ticks;
            if (action == dynamic_island::Action::ExpansionChanged) ++counters->expansionChanges;
        });
        widget::Preferences preferences; preferences.enabled = true;
        if (!slot.island->Start(preferences, display)) throw std::runtime_error("hover probe island failed to start");
        slot.island->SetMonitor(slot.monitor); Inputs(*slot.island, true); slot.island->Settle();
    }
    HoverTrace trace(slots); trace.PumpFor(1200);
    Json cycles = Json::array();
    for (unsigned round = 0; round < rounds; ++round) for (std::size_t index = 0; index < slots.size(); ++index) {
        trace.round = round; trace.Phase("hover-delay-and-open");
        auto& slot = slots[index]; const HWND window = slot.island->Handle();
        RECT client{}; GetClientRect(window, &client);
        trace.held = window;
        trace.Send(window, WM_MOUSEMOVE, 0, MAKELPARAM(std::max(1L, client.right / 2), std::max(1L, client.bottom / 2)));
        trace.PumpFor(delay + hold);
        const bool opened = slot.island->Expanded();
        trace.Phase("leave-and-collapse"); trace.held = nullptr;
        trace.Send(window, WM_MOUSELEAVE); trace.PumpFor(leave);
        cycles.push_back({{"round", round}, {"island_index", index}, {"opened_by_hover", opened},
            {"collapsed_after_leave", !slot.island->Expanded()}});
    }
    Json windows = Json::array();
    for (auto& slot : slots) {
        windows.push_back({{"monitor", slot.monitor.device}, {"bounds", {slot.monitor.bounds.left, slot.monitor.bounds.top,
            slot.monitor.bounds.right, slot.monitor.bounds.bottom}}, {"dpi", GetDpiForWindow(slot.island->Handle())},
            {"tick_actions", slot.ticks}, {"expansion_changed_actions", slot.expansionChanges},
            {"frame_begin_gap", slot.frameGaps.Report()}, {"frame_end_gap", slot.frameEndGaps.Report()}});
    }
    const auto timing = trace.Save(root);
    for (auto& slot : slots) slot.island->Stop();
    return {{"mode", "synthetic-hover"}, {"pid", GetCurrentProcessId()}, {"ui_thread", GetCurrentThreadId()},
        {"simultaneous_islands", slots.size()}, {"rounds_per_island", rounds}, {"hover_delay_ms", delay},
        {"hold_after_delay_ms", hold}, {"leave_pump_ms", leave}, {"material", "none"}, {"shape", "standard"},
        {"size", "standard"}, {"animation_speed", "normal"}, {"bounce", "bouncy"}, {"expanded_layout", 53},
        {"left_widget", "uv"}, {"right_widget", "none"}, {"auto_hide", true}, {"hover_expand", true},
        {"topmost", true}, {"hide_fullscreen", display.hideFullscreen}, {"windows", windows},
        {"cycles", cycles}, {"timing", timing},
        {"note", "Own HWND mouse messages and real hover/collapse timers; no physical pointer movement, settings or permission changes. Queued physical-pointer leave is ignored only during synthetic hold. Frame gaps include intentional hover/collapse dwell and are not presented-frame loss. Thread CPU is an accounting delta, so a short dispatch can show zero. Trace metadata is stored after timed calls; JSON is written only after measurement. Fixed sample media/weather/two todos. No DynamicIslandSession notifications, manager WebView preview, DockSession, real todo-store or other production Shell work."}};
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "usage: lume-island-glass-interaction-probe <output-directory>\n";
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    try {
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root);
        const auto monitors = ConnectedIslandMonitors();
        if (monitors.empty()) throw std::runtime_error("no connected monitors");
        if (std::getenv("LUME_GLASS_PROBE_HOVER")) {
            const auto report = RunHover(monitors, root);
            std::ofstream(root / "report.json") << report.dump(2);
            std::cout << report.dump() << '\n';
            CoUninitialize(); return 0;
        }
        const std::vector<dynamic_island::ExpandedWidget> defaults{dynamic_island::ExpandedWidget::Music};
        const std::vector<dynamic_island::ExpandedWidget> configured{
            dynamic_island::ExpandedWidget::CalendarWeather, dynamic_island::ExpandedWidget::Todos};
        const std::vector<dynamic_island::ExpandedWidget> three{
            dynamic_island::ExpandedWidget::Music, dynamic_island::ExpandedWidget::CalendarWeather,
            dynamic_island::ExpandedWidget::Todos};
        Json cases = Json::array();
        const bool focus = std::getenv("LUME_GLASS_PROBE_FOCUS") != nullptr;
        const std::string focusMaterial = std::getenv("LUME_GLASS_PROBE_MATERIAL") ?
            std::getenv("LUME_GLASS_PROBE_MATERIAL") : "frosted";
        for (std::size_t monitorIndex = 0; monitorIndex < monitors.size(); ++monitorIndex) {
            if (focus && monitorIndex + 1 != monitors.size()) continue;
            const auto& monitor = monitors[monitorIndex];
            for (const auto material : {IslandMaterial::None, IslandMaterial::Frosted}) {
                if (focus && ((focusMaterial == "none" && material != IslandMaterial::None) ||
                              (focusMaterial != "none" && material != IslandMaterial::Frosted))) continue;
                for (const auto size : {IslandSize::Standard, IslandSize::Large}) {
                    if (focus && size != IslandSize::Standard) continue;
                    const bool configuredLayout = std::getenv("LUME_GLASS_PROBE_CONFIGURED_LAYOUT") != nullptr;
                    cases.push_back(RunCase(monitor, material, size, focus ? (configuredLayout ? configured : three) : defaults));
                    if (!focus) cases.push_back(RunCase(monitor, material, size, three));
                }
            }
        }
        const Json report{{"case_count", cases.size()}, {"monitors", monitors.size()}, {"cases", cases},
                          {"note", "No pointer input, user settings, or system permissions were changed."}};
        std::ofstream(root / "report.json") << report.dump(2);
        std::cout << report.dump() << '\n';
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        CoUninitialize();
        return 1;
    }
}

