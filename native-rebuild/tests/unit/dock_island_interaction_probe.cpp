// Manual joint probe: an idle Dock and a continuously expanding plain island
// on the same/other display. No user settings or pointer input are changed.
#include "platform/windows/dynamic_island/island_window.h"
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/launchpad_window.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/text_util.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace lume;
using namespace lume::platform::windows;
using Json = nlohmann::json;

namespace {
constexpr UINT IslandFrame = WM_APP + 0x6e4;
double Now() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Samples {
    std::vector<double> values;
    void Add(double value) { values.push_back(value); }
    Json Report(double budget) const {
        if (values.empty()) return Json{{"count", 0}};
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](double fraction) {
            return sorted[static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1))];
        };
        const auto over = [&](double threshold) {
            return std::count_if(sorted.begin(), sorted.end(), [&](double value) { return value > threshold; });
        };
        return {{"count", sorted.size()}, {"p50_ms", percentile(.5)}, {"p95_ms", percentile(.95)},
                {"p99_ms", percentile(.99)}, {"max_ms", sorted.back()}, {"over_budget", over(budget)},
                {"over_1_6_period", over(budget * 1.6)}, {"over_16_ms", over(16)}, {"over_32_ms", over(32)}};
    }
};
struct Metrics {
    Samples allDispatch, islandDispatch, islandFrameDispatch, islandFrameBeginGap, islandFrameEndGap, dockDispatch;
    std::map<unsigned, unsigned> dockMessages;
    unsigned duplicateSizes = 0;
    double dockTotal = 0;
    void Pump(unsigned milliseconds, HWND island, HWND dockIcons, HWND dockBar) {
        const auto until = GetTickCount64() + milliseconds;
        double previousBegin = 0, previousEnd = 0;
        SIZE previousSize{};
        do {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                const double start = Now();
                DispatchMessageW(&message);
                const double end = Now(), spent = end - start;
                allDispatch.Add(spent);
                if (message.hwnd == island) {
                    islandDispatch.Add(spent);
                    if (message.message == IslandFrame) {
                        islandFrameDispatch.Add(spent);
                        if (previousBegin) islandFrameBeginGap.Add(start - previousBegin);
                        if (previousEnd) islandFrameEndGap.Add(end - previousEnd);
                        previousBegin = start; previousEnd = end;
                        RECT bounds{};
                        GetWindowRect(island, &bounds);
                        const SIZE size{bounds.right - bounds.left, bounds.bottom - bounds.top};
                        if (size.cx == previousSize.cx && size.cy == previousSize.cy) ++duplicateSizes;
                        previousSize = size;
                    }
                }
                if (message.hwnd && (message.hwnd == dockIcons || message.hwnd == dockBar)) {
                    dockDispatch.Add(spent); dockTotal += spent; ++dockMessages[message.message];
                }
                if (GetTickCount64() >= until) break;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
        } while (GetTickCount64() < until);
    }
    Json Report(double budget) const {
        Json messages = Json::object();
        for (const auto& [message, count] : dockMessages) messages[std::to_string(message)] = count;
        return {{"all_dispatch", allDispatch.Report(budget)}, {"island_dispatch", islandDispatch.Report(budget)},
                {"island_frame_dispatch", islandFrameDispatch.Report(budget)},
                {"island_frame_begin_gap", islandFrameBeginGap.Report(budget)},
                {"island_frame_end_gap", islandFrameEndGap.Report(budget)}, {"duplicate_size_frames", duplicateSizes},
                {"dock_dispatch", dockDispatch.Report(budget)}, {"dock_dispatch_total_ms", dockTotal}, {"dock_messages", messages}};
    }
};
double RefreshHz(const IslandMonitor& monitor) {
    DEVMODEW mode{}; mode.dmSize = sizeof(mode);
    return EnumDisplaySettingsW(monitor.device.c_str(), ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1 ?
        static_cast<double>(mode.dmDisplayFrequency) : 60.0;
}
Json ExternalDocks() {
    Json result = Json::array();
    EnumWindows([](HWND hwnd, LPARAM context) -> BOOL {
        DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId() || !IsWindowVisible(hwnd) || !window_ids::IsClass(hwnd, window_ids::DockIconsClass)) return TRUE;
        RECT bounds{}; GetWindowRect(hwnd, &bounds);
        reinterpret_cast<Json*>(context)->push_back({{"pid", pid}, {"bounds", {bounds.left, bounds.top, bounds.right, bounds.bottom}}});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
Json GlassState(HWND owner) {
    struct Search { HWND owner; HWND glass = nullptr; } search{owner};
    EnumWindows([](HWND hwnd, LPARAM context) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(context);
        if (GetPropW(hwnd, window_ids::GlassSurfaceOwnerProp) == reinterpret_cast<HANDLE>(value.owner)) {
            value.glass = hwnd; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return {{"owner_visible", owner && IsWindowVisible(owner) != FALSE}, {"exists", search.glass != nullptr},
            {"visible", search.glass && IsWindowVisible(search.glass) != FALSE}};
}
std::shared_ptr<const WidgetBitmap> Icon() {
    auto bitmap = std::make_shared<WidgetBitmap>();
    bitmap->width = bitmap->height = 64;
    bitmap->pixels.assign(64 * 64, 0xff58a6ff);
    return bitmap;
}
std::vector<dock::Item> Items() {
    std::vector<dock::Item> items;
    for (unsigned i = 0; i < 10; ++i) {
        dock::Item item; item.kind = dock::Kind::Pinned;
        item.name = "Probe icon " + std::to_string(i); item.path = "C:\\LumeProbe\\" + std::to_string(i) + ".exe";
        item.orderIndex = i; items.push_back(std::move(item));
    }
    return items;
}
void Inputs(IslandWindow& island) {
    IslandPresentation state; state.time = L"10:08"; state.date = L"2026-11-08";
    island.Update(state);
    dynamic_island::Weather weather;
    weather.enabled = weather.available = weather.hasRange = true;
    weather.temperature = 18; weather.high = 22; weather.low = 12;
    weather.city = L"Probe"; weather.condition = L"多云"; weather.code = 3;
    island.SetWeather(weather);
    IslandTodoSource todos;
    todos.forDay = [](calendar::Date) {
        return std::vector<IslandTodo>{{"a", L"同屏 Dock 和灵动岛连续展开测试", L"10:08", false, false, 0x30d158, L"保持现有视觉"},
                                      {"b", L"复测另一块显示器", L"11:00", true, false, 0x0a84ff, L""}};
    };
    todos.hasTodos = [](calendar::Date) { return true; };
    todos.writable = [] { return true; }; island.SetTodoSource(std::move(todos));
}
Json Run(const IslandMonitor& monitor, const std::string& mode, unsigned rounds) {
    const auto externalBefore = ExternalDocks();
    const HINSTANCE module = GetModuleHandleW(nullptr);
    const auto icon = Icon();
    DockWindowPorts ports;
    ports.icon = [icon](const dock::Item&) { return icon; };
    ports.slotsName = L"Local\\LumeDockIslandProbeSlots-" + std::to_wstring(GetCurrentProcessId());
    DockWindow dockWindow(module, std::move(ports));
    std::unique_ptr<LaunchpadWindow> preparedLaunchpad;
    Metrics warmup;
    if (mode != "off") {
        DockAppearance look; look.transparent = mode == "transparent"; look.frosted = mode == "frosted"; look.bottomOffset = 120;
        if (!dockWindow.Show(Items(), look)) throw std::runtime_error("probe Dock failed to show");
        const auto deadline = GetTickCount64() + 4000;
        do { warmup.Pump(100, nullptr, dockWindow.Handle(), dockWindow.BarHandle()); }
        while (mode != "transparent" && dockWindow.MaterialState() == WidgetMaterialState::Starting && GetTickCount64() < deadline);
        warmup.Pump(1000, nullptr, dockWindow.Handle(), dockWindow.BarHandle());
        if (std::getenv("LUME_DOCK_ISLAND_PREPARE")) {
            LaunchpadPorts launchpadPorts;
            launchpadPorts.entries = [] {
                std::vector<dock::Entry> entries;
                for (unsigned i = 0; i < 20; ++i) {
                    dock::Entry entry; entry.path = "C:\\LumeProbe\\" + std::to_string(i) + ".exe";
                    entry.name = "Prepared probe entry " + std::to_string(i); entries.push_back(std::move(entry));
                }
                return entries;
            };
            launchpadPorts.filter = [](const std::vector<dock::Entry>& entries, const std::string&) { return entries; };
            launchpadPorts.icon = [icon](const std::string&) { return icon; };
            preparedLaunchpad = std::make_unique<LaunchpadWindow>(module, std::move(launchpadPorts));
            if (!preparedLaunchpad->Prepare(dockWindow.ScreenBounds(), mode == "frosted"))
                throw std::runtime_error("probe launchpad failed to prepare");
            warmup.Pump(1500, nullptr, dockWindow.Handle(), dockWindow.BarHandle());
        }
    }
    IslandWindow island(module, {});
    IslandDisplay display; display.autoHide = false; display.topmost = true; display.hideFullscreen = false;
    display.material = IslandMaterial::None; display.shape = IslandShape::Standard; display.size = IslandSize::Standard;
    display.bounce = IslandBounce::Bouncy;
    display.expandedWidgets = {dynamic_island::ExpandedWidget::CalendarWeather, dynamic_island::ExpandedWidget::Todos};
    widget::Preferences prefs; prefs.enabled = true;
    island.SetMonitor(monitor); Inputs(island);
    if (!island.Start(prefs, display)) throw std::runtime_error("probe island failed to show");
    island.Settle();
    warmup.Pump(800, island.Handle(), dockWindow.Handle(), dockWindow.BarHandle());
    const double hz = RefreshHz(monitor), period = 1000.0 / hz;
    const RECT dockBounds = dockWindow.ScreenBounds();
    Metrics measurements; Samples expandCall, collapseCall;
    const double measuredStart = Now();
    for (unsigned round = 0; round < rounds; ++round) {
        double start = Now(); island.Expand(true); expandCall.Add(Now() - start);
        measurements.Pump(420, island.Handle(), dockWindow.Handle(), dockWindow.BarHandle());
        start = Now(); island.Expand(false); collapseCall.Add(Now() - start);
        measurements.Pump(420, island.Handle(), dockWindow.Handle(), dockWindow.BarHandle());
    }
    Json result = measurements.Report(period);
    result["measured_start_ms"] = measuredStart;
    result["measured_end_ms"] = Now();
    result["external_docks_before"] = externalBefore;
    result["external_docks_after"] = ExternalDocks();
    result["dock"] = mode;
    result["dock_material_state"] = static_cast<int>(dockWindow.MaterialState());
    result["dock_material_ready"] = dockWindow.MaterialState() == WidgetMaterialState::Ready;
    result["dock_glass"] = GlassState(dockWindow.BarHandle());
    result["launchpad_prepared"] = preparedLaunchpad != nullptr;
    if (preparedLaunchpad) {
        result["launchpad_material_state"] = static_cast<int>(preparedLaunchpad->MaterialState());
        result["launchpad_material_ready"] = preparedLaunchpad->MaterialState() == WidgetMaterialState::Ready;
        result["launchpad_glass"] = GlassState(preparedLaunchpad->Handle());
    }
    result["island_material"] = "none";
    result["island_monitor"] = text::Utf8(monitor.device);
    result["island_primary"] = monitor.primary; result["refresh_hz"] = hz; result["frame_budget_ms"] = period;
    result["dock_bounds"] = {dockBounds.left, dockBounds.top, dockBounds.right, dockBounds.bottom};
    result["rounds"] = rounds; result["expand_call"] = expandCall.Report(period); result["collapse_call"] = collapseCall.Report(period);
    island.Stop(); if (preparedLaunchpad) preparedLaunchpad->Stop(); dockWindow.Stop();
    warmup.Pump(150, nullptr, nullptr, nullptr);
    return result;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "usage: lume-dock-island-interaction-probe <output-directory>\n"; return 2; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 3;
    try {
        unsigned rounds = 6;
        if (const auto text = std::getenv("LUME_DOCK_ISLAND_ROUNDS")) {
            const int parsed = std::atoi(text); if (parsed >= 1 && parsed <= 40) rounds = static_cast<unsigned>(parsed);
        }
        const auto monitors = ConnectedIslandMonitors();
        if (monitors.empty()) throw std::runtime_error("no connected monitors");
        const std::filesystem::path root(argv[1]); std::filesystem::create_directories(root);
        Json report{{"external_docks", ExternalDocks()}, {"cases", Json::array()},
            {"note", "Manual probe: UI-message gaps and dispatch cost, not actual presented GPU frames. No pointer input, user settings or permissions changed."}};
        const std::string onlyMode = std::getenv("LUME_DOCK_ISLAND_MODE") ? std::getenv("LUME_DOCK_ISLAND_MODE") : "";
        const std::string onlyMonitor = std::getenv("LUME_DOCK_ISLAND_MONITOR") ? std::getenv("LUME_DOCK_ISLAND_MONITOR") : "";
        for (const auto& monitor : monitors) for (const auto* mode : {"off", "transparent", "frosted", "liquid"}) {
            if (!onlyMode.empty() && onlyMode != mode) continue;
            if (!onlyMonitor.empty() && onlyMonitor != text::Utf8(monitor.device)) continue;
            const auto result = Run(monitor, mode, rounds);
            report["cases"].push_back(result); std::ofstream(root / "report.json") << report.dump(2);
            std::cout << result.dump() << '\n';
        }
        report["external_docks_after"] = ExternalDocks();
        std::ofstream(root / "report.json") << report.dump(2);
        CoUninitialize(); return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; CoUninitialize(); return 1;
    }
}
