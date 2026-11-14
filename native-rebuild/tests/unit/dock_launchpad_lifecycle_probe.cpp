// Manual lifecycle/latency probe. Uses synthetic entries/backgrounds, leaves
// application preferences and the physical pointer untouched. Run the same
// executable beside old/new LumeGlass.dll files for comparable measurements.
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/launchpad_window.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/text_util.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lume;
using namespace lume::platform::windows;
using Json = nlohmann::json;
namespace {
constexpr UINT DockFrame = WM_APP + 0x7c1, LaunchpadFrame = WM_APP + 0x7a5;
double Now() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
template<class T> T Resolve(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name); T result = nullptr;
    static_assert(sizeof(result) == sizeof(address)); std::memcpy(&result, &address, sizeof(result)); return result;
}
struct Samples {
    std::vector<double> values;
    void Add(double value) { values.push_back(value); }
    Json Report(double period) const {
        if (values.empty()) return {{"count", 0}};
        auto sorted = values; std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](double p) { return sorted[static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1))]; };
        return {{"count", sorted.size()}, {"p50_ms", percentile(.5)}, {"p95_ms", percentile(.95)},
                {"max_ms", sorted.back()}, {"over_1_6_period", std::count_if(sorted.begin(), sorted.end(),
                    [&](double value) { return value > period * 1.6; })}};
    }
};
struct FrameMetrics {
    Samples dispatch, dockFrames, launchpadFrames, dockGaps, launchpadGaps;
    std::function<bool()> dockAnimating, launchpadAnimating;
    HWND syntheticHover = nullptr;
    double dockPrior = 0, launchpadPrior = 0;
    void BeginAnimationSegment() { dockPrior = 0; launchpadPrior = 0; }
    void Pump(unsigned milliseconds) {
        const auto deadline = GetTickCount64() + milliseconds;
        do {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                // Fixture-only: a leave notice from the unmoved physical cursor
                // must not cancel the explicitly sent synthetic hover below.
                if (message.hwnd == syntheticHover && message.message == WM_MOUSELEAVE) continue;
                const bool dockRunning = dockAnimating && dockAnimating();
                const bool launchpadRunning = launchpadAnimating && launchpadAnimating();
                const double begin = Now(); TranslateMessage(&message); DispatchMessageW(&message);
                const double spent = Now() - begin; dispatch.Add(spent);
                if (message.message == DockFrame) {
                    dockFrames.Add(spent);
                    if (dockRunning && dockPrior) dockGaps.Add(begin - dockPrior);
                    dockPrior = begin;
                }
                if (message.message == LaunchpadFrame) {
                    launchpadFrames.Add(spent);
                    if (launchpadRunning && launchpadPrior) launchpadGaps.Add(begin - launchpadPrior);
                    launchpadPrior = begin;
                }
                if (!dockAnimating || !dockAnimating()) dockPrior = 0;
                if (!launchpadAnimating || !launchpadAnimating()) launchpadPrior = 0;
                if (GetTickCount64() >= deadline) break;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
        } while (GetTickCount64() < deadline);
    }
    bool Until(const std::function<bool()>& ready, unsigned timeout) {
        const auto deadline = GetTickCount64() + timeout;
        do { Pump(1); if (ready()) return true; } while (GetTickCount64() < deadline);
        return ready();
    }
    Json Report(double period) const {
        return {{"dispatch", dispatch.Report(period)}, {"dock_frames", dockFrames.Report(period)},
                {"launchpad_frames", launchpadFrames.Report(period)}, {"dock_frame_gaps", dockGaps.Report(period)},
                {"launchpad_frame_gaps", launchpadGaps.Report(period)}};
    }
};
LRESULT CALLBACK StableFocus(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR reference) {
    // Fixture-only: unrelated user activity must not dismiss a measured opening.
    if (message == WM_ACTIVATEAPP && !w) { ++*reinterpret_cast<unsigned*>(reference); return 0; }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, StableFocus, id);
    return DefSubclassProc(window, message, w, l);
}
struct Surface {
    HWND hwnd = nullptr; DWORD worker = 0; bool visible = false;
    Json Report() const { return {{"window", reinterpret_cast<std::uintptr_t>(hwnd)}, {"worker", worker}, {"visible", visible}}; }
};
Surface SurfaceFor(HWND owner) {
    struct Search { HWND owner; Surface surface; } search{owner, {}};
    EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(parameter);
        if (GetPropW(hwnd, window_ids::GlassSurfaceOwnerProp) != reinterpret_cast<HANDLE>(search.owner)) return TRUE;
        search.surface.hwnd = hwnd; search.surface.worker = GetWindowThreadProcessId(hwnd, nullptr);
        search.surface.visible = IsWindowVisible(hwnd) != FALSE; return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.surface;
}
Json Resources(LumeMaterialStatisticsFn statistics, const char* phase) {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    DWORD handles = 0; GetProcessHandleCount(GetCurrentProcess(), &handles);
    unsigned threads = 0;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) do { if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++threads; } while (Thread32Next(snapshot, &entry));
        CloseHandle(snapshot);
    }
    LumeMaterialStatisticsData gpu{}; gpu.size = sizeof(gpu); const bool measured = statistics && statistics(&gpu);
    return {{"phase", phase}, {"t_ms", Now()}, {"private_bytes", memory.PrivateUsage}, {"working_set_bytes", memory.WorkingSetSize},
            {"user", GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)}, {"gdi", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)},
            {"handles", handles}, {"threads", threads}, {"material_statistics_available", measured},
            {"material_users", gpu.users}, {"d3d_devices", gpu.devices}, {"captures", gpu.captures}};
}
std::string Sha256(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD bytes = 0, objectBytes = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &bytes, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0); return {};
    }
    std::vector<unsigned char> object(objectBytes); std::string result;
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) >= 0) {
        std::ifstream input(path, std::ios::binary); std::vector<unsigned char> block(64 * 1024); bool good = input.good();
        while (good && input) {
            input.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()));
            const auto count = input.gcount();
            if (count > 0 && BCryptHashData(hash, block.data(), static_cast<ULONG>(count), 0) < 0) good = false;
        }
        unsigned char digest[32]{};
        if (good && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0) {
            std::ostringstream encoded; encoded << std::hex << std::setfill('0');
            for (const auto byte : digest) encoded << std::setw(2) << static_cast<unsigned>(byte);
            result = encoded.str();
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0); return result;
}
LRESULT CALLBACK Background(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT bounds{}; GetClientRect(window, &bounds);
        const auto brush = CreateSolidBrush(RGB(38, 64, 90)); FillRect(dc, &bounds, brush); DeleteObject(brush);
        EndPaint(window, &paint); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
std::vector<HWND> Backgrounds(HINSTANCE instance) {
    WNDCLASSW type{}; type.hInstance = instance; type.lpfnWndProc = Background; type.lpszClassName = L"LumeLifecycleProbeBackground";
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("background registration failed");
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    std::vector<HWND> windows;
    for (int i = 0; i < 3; ++i) {
        const HWND window = CreateWindowExW(WS_EX_NOACTIVATE, type.lpszClassName, L"Synthetic lifecycle background", WS_POPUP,
            monitor.rcWork.left + 200 + i * 120, monitor.rcWork.top + 250 + i * 60, 560, 420, nullptr, nullptr, instance, nullptr);
        if (!window) throw std::runtime_error("background creation failed");
        ShowWindow(window, SW_SHOWNOACTIVATE); UpdateWindow(window); windows.push_back(window);
    }
    return windows;
}
std::shared_ptr<const WidgetBitmap> Icon() {
    auto icon = std::make_shared<WidgetBitmap>(); icon->width = icon->height = 64;
    icon->pixels.assign(64 * 64, 0xff62a4dc); return icon;
}
std::vector<dock::Entry> Entries() {
    std::vector<dock::Entry> entries;
    for (unsigned i = 0; i < 40; ++i) {
        dock::Entry entry; entry.path = "C:\\LumeLifecycleProbe\\" + std::to_string(i) + ".exe";
        entry.name = "Lifecycle entry " + std::to_string(i); entries.push_back(std::move(entry));
    }
    return entries;
}
struct Opening { Json report; double start = 0, finish = 0; DWORD worker = 0; };
Opening Open(LaunchpadWindow& pad, RECT dock, bool frosted, FrameMetrics& metrics, const char* kind) {
    Opening opening; const Surface previous = SurfaceFor(pad.Handle());
    metrics.BeginAnimationSegment();
    opening.start = Now(); const bool shown = pad.Show(dock, frosted); const double callMs = Now() - opening.start;
    double firstVisible = -1, firstReady = -1;
    bool settled = false;
    const bool completed = shown && metrics.Until([&] {
        const double elapsed = Now() - opening.start; const auto surface = SurfaceFor(pad.Handle());
        if (firstVisible < 0 && pad.Visible() && surface.visible) firstVisible = elapsed;
        if (firstReady < 0 && pad.MaterialState() == WidgetMaterialState::Ready) firstReady = elapsed;
        settled = pad.Visible() && !pad.Animating();
        return settled && firstVisible >= 0 && firstReady >= 0;
    }, 1800);
    const auto current = SurfaceFor(pad.Handle()); opening.finish = Now(); opening.worker = current.worker;
    opening.report = {{"kind", kind}, {"start_ms", opening.start}, {"finish_ms", opening.finish}, {"call_ms", callMs},
        {"completed", completed}, {"settled", settled}, {"surface_visible_ms", firstVisible}, {"material_ready_ms", firstReady},
        {"previous_surface", previous.Report()}, {"surface", current.Report()},
        {"warning", "Visible observations can reuse a surface during reversal; new submissions are checked separately in worker timing."}};
    return opening;
}
bool Close(LaunchpadWindow& pad, FrameMetrics& metrics) {
    metrics.BeginAnimationSegment();
    pad.Hide();
    return metrics.Until([&] { return !pad.Visible() && !pad.Animating() && !IsWindowVisible(pad.Handle()); }, 1800);
}
Json Hover(DockWindow& dock, FrameMetrics& metrics, std::size_t index) {
    RECT layer{}; GetWindowRect(dock.Handle(), &layer);
    const RECT icon = dock.IconBounds(index);
    const LPARAM point = MAKELPARAM((icon.left + icon.right) / 2 - layer.left, (icon.top + icon.bottom) / 2 - layer.top);
    metrics.BeginAnimationSegment(); metrics.syntheticHover = dock.Handle();
    const double begin = Now(); SendMessageW(dock.Handle(), WM_MOUSEMOVE, 0, point);
    const double hoverCall = Now() - begin;
    const bool hoverSettled = metrics.Until([&] { return !dock.Animating(); }, 1200);
    const float expanded = dock.ScaleOf(index);
    metrics.syntheticHover = nullptr; metrics.BeginAnimationSegment();
    const double leave = Now(); SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0);
    const double leaveCall = Now() - leave;
    const bool leaveSettled = metrics.Until([&] { return !dock.Animating(); }, 1200);
    const float restored = dock.ScaleOf(index);
    return {{"index", index}, {"hover_call_ms", hoverCall}, {"leave_call_ms", leaveCall},
        {"hover_settled", hoverSettled}, {"leave_settled", leaveSettled}, {"expanded_scale", expanded}, {"restored_scale", restored},
        {"completed", hoverSettled && leaveSettled && expanded > 1.1f && std::abs(restored - 1.f) < .05f}};
}
void AddSubmission(Opening& opening, const std::filesystem::path& prefix) {
    opening.report["first_new_material_submission_ms"] = nullptr;
    if (!opening.worker) return;
    auto path = prefix; path += L"." + std::to_wstring(opening.worker); std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        double at = 0, read = 0;
        if (std::sscanf(line.c_str(), "T t=%lf read=%lf", &at, &read) != 2 || read < opening.start || at > opening.finish + 500) continue;
        const auto opacityAt = line.find("opacity=");
        if (opacityAt == std::string::npos || std::strtod(line.c_str() + opacityAt + 8, nullptr) <= .001) continue;
        opening.report["first_new_material_submission_ms"] = at - opening.start;
        opening.report["submission_evidence"] = path.u8string(); return;
    }
}
unsigned Setting(const char* name, unsigned fallback, unsigned maximum) {
    const auto value = std::getenv(name); if (!value) return fallback;
    const int parsed = std::atoi(value); return parsed >= 1 && parsed <= static_cast<int>(maximum) ? static_cast<unsigned>(parsed) : fallback;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "usage: lume-dock-launchpad-lifecycle-probe <output-directory>\n"; return 2; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    HMODULE module = nullptr;
    try {
        const auto root = std::filesystem::absolute(argv[1]); std::filesystem::create_directories(root);
        const auto prefix = root / L"material.log";
        SetEnvironmentVariableW(L"LUME_GLASS_LOG", prefix.c_str()); SetEnvironmentVariableW(L"LUME_GLASS_TIMING", L"1");
        wchar_t executable[32768]{}; const auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768) throw std::runtime_error("executable path unavailable");
        const auto dll = std::filesystem::path(executable).parent_path() / L"LumeGlass.dll";
        module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) throw std::runtime_error("LumeGlass.dll missing beside the probe");
        const auto statistics = Resolve<LumeMaterialStatisticsFn>(module, "LumeMaterialStatistics");
        if (!statistics) throw std::runtime_error("material statistics export unavailable");
        const unsigned batches = Setting("LUME_LIFECYCLE_BATCHES", 5, 20), cycles = Setting("LUME_LIFECYCLE_CYCLES", 12, 100);
        const bool frosted = !std::getenv("LUME_LIFECYCLE_LIQUID");
        const bool transparent = std::getenv("LUME_LIFECYCLE_TRANSPARENT_DOCK") != nullptr;
        BOOL animations = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
        DEVMODEW mode{}; mode.dmSize = sizeof(mode); EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode);
        const double period = 1000.0 / (mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency : 60);
        Json report{{"dll", dll.u8string()}, {"dll_sha256", Sha256(dll)}, {"material", frosted ? "frosted" : "liquid"},
            {"transparent_dock", transparent}, {"batches", batches}, {"cycles_per_batch", cycles},
            {"system_animations_enabled", animations != FALSE}, {"refresh_period_ms", period},
            {"reduced_motion_coverage", "No injection API exists; system preference is read and left unchanged."},
            {"resource_samples", Json::array()}, {"batches_measured", Json::array()}, {"openings", Json::array()}, {"dock_hovers", Json::array()}};
        const auto save = [&] { std::ofstream(root / L"report.json") << report.dump(2); };
        FrameMetrics metrics;
        report["resource_samples"].push_back(Resources(statistics, "before_objects"));
        const auto icon = Icon();
        DockWindowPorts dockPorts; dockPorts.icon = [icon](const dock::Item&) { return icon; };
        dockPorts.slotsName = L"Local\\LumeLifecycleSlots-" + std::to_wstring(GetCurrentProcessId());
        std::vector<dock::Item> items;
        for (unsigned i = 0; i < 10; ++i) { dock::Item item; item.name = "Probe"; item.path = "C:\\LumeLifecycleProbe\\" + std::to_string(i); item.orderIndex = i; items.push_back(std::move(item)); }
        const double dockConstruct = Now();
        DockWindow dockWindow(GetModuleHandleW(nullptr), std::move(dockPorts));
        const double dockConstructMs = Now() - dockConstruct;
        metrics.dockAnimating = [&] { return dockWindow.Animating(); };
        DockAppearance look; look.frosted = frosted; look.transparent = transparent; look.bottomOffset = 120;
        const double dockStart = Now(); const bool dockShown = dockWindow.Show(items, look);
        const double dockCall = Now() - dockStart;
        if (!dockShown) throw std::runtime_error("Dock failed to show");
        double iconsVisible = -1, barVisible = -1, materialReady = -1, glassVisible = -1;
        const bool dockReady = metrics.Until([&] {
            const double elapsed = Now() - dockStart;
            if (iconsVisible < 0 && IsWindowVisible(dockWindow.Handle())) iconsVisible = elapsed;
            if (barVisible < 0 && IsWindowVisible(dockWindow.BarHandle())) barVisible = elapsed;
            if (materialReady < 0 && dockWindow.MaterialState() == WidgetMaterialState::Ready) materialReady = elapsed;
            if (glassVisible < 0 && SurfaceFor(dockWindow.BarHandle()).visible) glassVisible = elapsed;
            return iconsVisible >= 0 && barVisible >= 0 && (transparent || (materialReady >= 0 && glassVisible >= 0));
        }, 3000);
        report["dock_first_show"] = {{"construct_call_ms", dockConstructMs}, {"call_ms", dockCall}, {"completed", dockReady},
            {"icons_visible_ms", iconsVisible}, {"bar_visible_ms", barVisible}, {"material_ready_ms", materialReady},
            {"glass_visible_ms", glassVisible}, {"material_required", !transparent}};
        metrics.Pump(400);
        LaunchpadPorts ports; ports.entries = Entries;
        ports.filter = [](const std::vector<dock::Entry>& entries, const std::string&) { return entries; };
        ports.icon = [icon](const std::string&) { return icon; };
        unsigned ignoredFocusLoss = 0;
        LaunchpadWindow pad(GetModuleHandleW(nullptr), std::move(ports));
        metrics.launchpadAnimating = [&] { return pad.Animating(); };
        auto backgrounds = Backgrounds(GetModuleHandleW(nullptr)); metrics.Pump(100);
        const RECT anchor = dockWindow.ScreenBounds();
        if (!pad.Prepare(anchor, frosted)) throw std::runtime_error("Launchpad prepare failed");
        if (!SetWindowSubclass(pad.Handle(), StableFocus, 1, reinterpret_cast<DWORD_PTR>(&ignoredFocusLoss)))
            throw std::runtime_error("fixture focus subclass failed");
        const bool warmed = metrics.Until([&] { return pad.MaterialState() == WidgetMaterialState::Ready; }, 3000);
        report["initial_prewarm_ready"] = warmed; report["initial_hidden_surface"] = SurfaceFor(pad.Handle()).Report();
        report["resource_samples"].push_back(Resources(statistics, "hidden_with_backgrounds"));
        for (const auto background : backgrounds) DestroyWindow(background); backgrounds.clear();
        metrics.Pump(600); report["resource_samples"].push_back(Resources(statistics, "hidden_after_backgrounds_closed"));
        std::vector<Opening> openings;
        openings.push_back(Open(pad, anchor, frosted, metrics, "first_after_prepare"));
        report["first_close_completed"] = Close(pad, metrics);
        metrics.Pump(400); report["resource_samples"].push_back(Resources(statistics, "after_first_close"));
        unsigned failedHovers = 0, failedPreparations = 0;
        for (unsigned batch = 0; batch < batches; ++batch) {
            FrameMetrics batchMetrics; unsigned failedOpenings = 0, failedClosures = 0;
            batchMetrics.dockAnimating = [&] { return dockWindow.Animating(); };
            batchMetrics.launchpadAnimating = [&] { return pad.Animating(); };
            const auto batchStart = Now();
            for (unsigned cycle = 0; cycle < cycles; ++cycle) {
                if (cycle % 3 == 0) {
                    auto hover = Hover(dockWindow, batchMetrics, (batch + cycle) % 10);
                    if (!hover["completed"].get<bool>()) ++failedHovers;
                    hover["batch"] = batch; hover["cycle"] = cycle; report["dock_hovers"].push_back(std::move(hover));
                }
                auto opening = Open(pad, anchor, frosted, batchMetrics, "repeat");
                if (!opening.report["completed"].get<bool>()) ++failedOpenings;
                openings.push_back(std::move(opening));
                if (cycle % 4 == 3) {
                    batchMetrics.BeginAnimationSegment();
                    pad.Hide(); batchMetrics.Pump(55);
                    auto reversed = Open(pad, anchor, frosted, batchMetrics, "closing_reversal");
                    if (!reversed.report["completed"].get<bool>()) ++failedOpenings;
                    openings.push_back(std::move(reversed));
                }
                if (!Close(pad, batchMetrics)) ++failedClosures;
                batchMetrics.Until([&] { return pad.MaterialState() == WidgetMaterialState::Ready; }, 2500);
                if (cycle % 6 == 5 && !pad.Prepare(anchor, frosted)) ++failedPreparations;
            }
            batchMetrics.Pump(250);
            Json batchReport = batchMetrics.Report(period);
            batchReport["index"] = batch; batchReport["duration_ms"] = Now() - batchStart;
            batchReport["failed_openings"] = failedOpenings; batchReport["failed_closures"] = failedClosures;
            batchReport["hidden_owner_visible"] = IsWindowVisible(pad.Handle()) != FALSE;
            batchReport["hidden_material_ready"] = pad.MaterialState() == WidgetMaterialState::Ready;
            report["batches_measured"].push_back(batchReport);
            auto resource = Resources(statistics, "after_batch"); resource["index"] = batch;
            report["resource_samples"].push_back(resource); save();
        }
        RemoveWindowSubclass(pad.Handle(), StableFocus, 1);
        pad.Stop(); dockWindow.Stop(); metrics.Pump(800);
        report["resource_samples"].push_back(Resources(statistics, "after_stop"));
        const auto beforeUsers = report["resource_samples"].front()["material_users"].get<unsigned>();
        const auto afterUsers = report["resource_samples"].back()["material_users"].get<unsigned>();
        report["all_material_leases_released"] = afterUsers == beforeUsers;
        report["all_material_captures_released"] = report["resource_samples"].back()["captures"] == report["resource_samples"].front()["captures"];
        report["all_material_devices_released"] = report["resource_samples"].back()["d3d_devices"] == report["resource_samples"].front()["d3d_devices"];
        unsigned failedOpenings = 0, failedClosures = report["first_close_completed"].get<bool>() ? 0 : 1;
        for (const auto& opening : openings) if (!opening.report["completed"].get<bool>()) ++failedOpenings;
        for (const auto& batch : report["batches_measured"]) failedClosures += batch["failed_closures"].get<unsigned>();
        bool allStatistics = true;
        for (const auto& resource : report["resource_samples"]) allStatistics = allStatistics && resource["material_statistics_available"].get<bool>();
        report["ignored_external_focus_loss"] = ignoredFocusLoss;
        report["failures"] = {{"openings", failedOpenings}, {"closures", failedClosures}, {"hover_sequences", failedHovers}, {"preparations", failedPreparations}};
        const bool passed = dockReady && warmed && !failedOpenings && !failedClosures && !failedHovers && !failedPreparations && allStatistics &&
            report["all_material_leases_released"].get<bool>() && report["all_material_captures_released"].get<bool>() && report["all_material_devices_released"].get<bool>();
        report["passed"] = passed;
        report["notes"] = "Frame-message timings are not actual GPU present cadence. Frame gaps include every interval while the corresponding native animation clock remains running in one Show/Hide segment, with no upper cutoff. First new material submission is parsed from a worker's post-Show timing with positive logged opacity; t is recorded after its vblank wait and is a conservative completion observation. Private/working-set growth can be caches or driver pools; inspect batch trends and final lease/capture counts before calling it a leak.";
        for (auto& opening : openings) { AddSubmission(opening, prefix); report["openings"].push_back(opening.report); }
        save();
        std::cout << Json{{"dll_sha256", report["dll_sha256"]}, {"openings", openings.size()},
            {"batches", report["batches_measured"].size()}, {"passed", passed}, {"failures", report["failures"]},
            {"leases_released", report["all_material_leases_released"]}, {"report", (root / L"report.json").u8string()}}.dump() << '\n';
        FreeLibrary(module); module = nullptr; CoUninitialize(); return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; if (module) FreeLibrary(module); CoUninitialize(); return 1;
    }
}
