#include "apps/shell/desktop_application.h"
#include "core/ui/page_bridge.h"
#include "process_memory_snapshot.h"
#include "vendor/nlohmann/json.hpp"

#include <objbase.h>
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using lume::shell::DesktopApplication;
using lume::shell::DesktopApplicationOptions;
using lume::ui::PageCommand;
using json = nlohmann::json;

void Check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

void Pump(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}

class ProcessTreeAccounting final {
public:
    ProcessTreeAccounting() {
        job_ = CreateJobObjectW(nullptr, nullptr);
        Check(job_ != nullptr && AssignProcessToJobObject(job_, GetCurrentProcess()) != FALSE,
              "create accounting job");
    }
    ~ProcessTreeAccounting() { if (job_) CloseHandle(job_); }
    ProcessTreeAccounting(const ProcessTreeAccounting&) = delete;

    json Snapshot(const char* phase, int round) const {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION memory{};
        const bool timeOk = QueryInformationJobObject(
            job_, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) != FALSE;
        const DWORD timeError = timeOk ? ERROR_SUCCESS : GetLastError();
        const bool memoryOk = QueryInformationJobObject(
            job_, JobObjectExtendedLimitInformation, &memory, sizeof(memory), nullptr) != FALSE;
        const DWORD memoryError = memoryOk ? ERROR_SUCCESS : GetLastError();
        json result{{"schemaVersion", 1}, {"phase", phase}, {"round", round},
            {"pid", GetCurrentProcessId()}, {"tickMs", GetTickCount64()},
            {"source", "QueryInformationJobObject"},
            {"coverage", "probe_and_all_future_descendants_including_nested_icon_jobs"},
            {"complete", timeOk && memoryOk}, {"timeError", timeError}, {"memoryError", memoryError},
            {"limitFlags", nullptr}, {"totalProcesses", nullptr}, {"activeProcesses", nullptr},
            {"terminatedByLimit", nullptr}, {"kernelTime100ns", nullptr}, {"userTime100ns", nullptr},
            {"peakProcessCommitBytes", nullptr}, {"peakJobCommitBytes", nullptr}};
        if (timeOk) {
            result["totalProcesses"] = accounting.TotalProcesses;
            result["activeProcesses"] = accounting.ActiveProcesses;
            result["terminatedByLimit"] = accounting.TotalTerminatedProcesses;
            result["kernelTime100ns"] = accounting.TotalKernelTime.QuadPart;
            result["userTime100ns"] = accounting.TotalUserTime.QuadPart;
        }
        if (memoryOk) {
            result["limitFlags"] = memory.BasicLimitInformation.LimitFlags;
            result["peakProcessCommitBytes"] = memory.PeakProcessMemoryUsed;
            result["peakJobCommitBytes"] = memory.PeakJobMemoryUsed;
        }
        return result;
    }
private:
    HANDLE job_ = nullptr;
};

struct Probe {
    DesktopApplication* app = nullptr;
    std::filesystem::path phasePath;
    const ProcessTreeAccounting* accounting = nullptr;
    DWORD dwell = 4000;
    int rounds = 1;
    int round = 0;
    int stage = 0;
    ULONGLONG stageAt = 0;
    bool failed = false;

    void Phase(const char* name) {
        stageAt = GetTickCount64();
        const auto temporary = std::filesystem::path(phasePath.wstring() + L".tmp");
        {
            std::ofstream output(temporary);
            output << json{{"phase", name}, {"round", round}, {"pid", GetCurrentProcessId()},
                {"tickMs", stageAt}}.dump();
            Check(output.good(), "write phase marker");
        }
        Check(MoveFileExW(temporary.c_str(), phasePath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE, "publish phase marker");
        std::cout << "PHASE " << name << " round=" << round << " tick=" << stageAt << std::endl;
    }

    void Observe(const char* name) {
        Phase(name);
        Pump(dwell);
        Phase("diagnostic");
        Check(WriteProcessMemorySnapshot(phasePath.parent_path() / "memory-snapshots.jsonl", name, round),
              "write product memory snapshot");
        std::ofstream jobs(phasePath.parent_path() / "job-snapshots.jsonl", std::ios::app);
        jobs << accounting->Snapshot(name, round).dump() << '\n';
        Check(jobs.good(), "write product job snapshot");
    }
};

Probe* active = nullptr;

void DisableComponents(DesktopApplication& app) {
    app.Dispatch(PageCommand::ClockDisable);
    app.Dispatch(PageCommand::CalendarDisable);
    app.Dispatch(PageCommand::WeatherDisable);
    app.Dispatch(PageCommand::AlmanacDisable);
    app.Dispatch(PageCommand::PerformanceDisable);
    app.Dispatch(PageCommand::OrganizerDisable);
}

void EnableComponents(DesktopApplication& app) {
    app.Dispatch(PageCommand::ClockEnable);
    app.Dispatch(PageCommand::CalendarEnable);
    app.Dispatch(PageCommand::WeatherEnable);
    app.Dispatch(PageCommand::AlmanacEnable);
    app.Dispatch(PageCommand::PerformanceEnable);
    app.Dispatch(PageCommand::OrganizerEnable);
}

void CALLBACK Drive(HWND, UINT, UINT_PTR timer, DWORD) {
    if (!active || !active->app) return;
    auto& probe = *active;
    const auto elapsed = GetTickCount64() - probe.stageAt;
    if (probe.stage == 0) {
        // Run starts hidden. This is the only initial disabled endpoint, before
        // any manager WebView or component is opened.
        probe.stage = 10;
        probe.Observe("initial_disabled");
        probe.round = 1;
        EnableComponents(*probe.app);
        probe.Observe("components_only_before");
        probe.app->Open();
        probe.stage = 1;
        probe.stageAt = GetTickCount64();
        return;
    }
    if (probe.stage == 10) return;
    if (probe.stage == 1 && probe.app->ManagerReady()) {
        probe.stage = 11;
        probe.Observe("manager_open");
        probe.stage = 2;
        return;
    }
    if (probe.stage == 2 && elapsed >= probe.dwell) {
        ShowWindow(probe.app->ManagerHandle(), SW_MINIMIZE);
        probe.stage = 12;
        probe.Observe("manager_minimized");
        probe.stage = 3;
        return;
    }
    if (probe.stage == 3 && elapsed >= probe.dwell) {
        ShowWindow(probe.app->ManagerHandle(), SW_RESTORE);
        probe.stage = 13;
        probe.Observe("manager_restored");
        probe.stage = 4;
        return;
    }
    if (probe.stage == 4 && elapsed >= probe.dwell) {
        probe.app->CloseManager();
        probe.stage = 5;
        probe.stageAt = GetTickCount64();
        return;
    }
    if (probe.stage == 5 && !probe.app->ManagerHandle()) {
        probe.stage = 15;
        probe.Observe("components_only_after_close");
        DisableComponents(*probe.app);
        probe.Observe("component_disabled");
        if (probe.round < probe.rounds) {
            ++probe.round;
            EnableComponents(*probe.app);
            probe.Observe("components_only_before");
            probe.app->Open();
            probe.stage = 1;
        } else {
            probe.stage = 6;
        }
        probe.stageAt = GetTickCount64();
        return;
    }
    if (probe.stage == 6 && elapsed >= probe.dwell) {
        KillTimer(nullptr, timer);
        probe.app->Exit();
    }
}

int Run(const std::filesystem::path& phasePath, int rounds, DWORD dwell, DWORD cooldown) {
    Check(std::filesystem::is_directory(phasePath.parent_path()), "measurement directory");
    const auto config = phasePath.parent_path() / "probe-config";
    std::filesystem::create_directories(config / "wallpaper");
    ProcessTreeAccounting accounting;
    DesktopApplicationOptions options;
    options.clockPreferencesPath = (config / "clock.json").u8string();
    options.calendarPreferencesPath = (config / "calendar.json").u8string();
    options.performancePreferencesPath = (config / "performance.json").u8string();
    options.almanacPreferencesPath = (config / "almanac.json").u8string();
    options.weatherPreferencesPath = (config / "weather.json").u8string();
    options.organizerPreferencesPath = (config / "organizer.json").u8string();
    options.desktopIconsPreferencesPath = (config / "icons.json").u8string();
    options.minimizePreferencesPath = (config / "minimize.json").u8string();
    options.applicationSettings.preferencesPath = (config / "application.json").u8string();
    options.accountOptions.storageDirectory = (config / "account").u8string();
    options.updateNotification.statePath = (config / "updates.json").u8string();
    options.wallpaperOptions.root = (config / "wallpaper").u8string();
    options.wallpaperOptions.executable = L"";
    options.verificationDesktopIcons = [] { return std::vector<lume::platform::windows::DesktopIconsTarget>{}; };
    options.verification = true;
    options.startHidden = true;
    options.onMessage = [](const std::wstring&) {};

    DesktopApplication app(GetModuleHandleW(nullptr), std::move(options));
    Probe probe{&app, phasePath, &accounting, dwell, rounds};
    active = &probe;
    DisableComponents(app);
    const auto timer = SetTimer(nullptr, 0, 50, Drive);
    Check(timer != 0, "create product phase timer");
    const int result = app.Run();
    KillTimer(nullptr, timer);
    active = nullptr;
    Check(result == 0, "desktop application run");
    Check(probe.stage == 6, "all product lifecycle phases completed");
    if (cooldown) {
        probe.Phase("cooldown");
        Pump(cooldown);
        probe.Phase("diagnostic");
        Check(WriteProcessMemorySnapshot(phasePath.parent_path() / "memory-snapshots.jsonl", "cooldown", rounds),
              "write product cooldown snapshot");
        std::ofstream jobs(phasePath.parent_path() / "job-snapshots.jsonl", std::ios::app);
        jobs << accounting.Snapshot("cooldown", rounds).dump() << '\n';
    }
    probe.Phase("complete");
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc != 6) return 2;
    try {
        const int rounds = std::stoi(argv[2]);
        const int dwell = std::stoi(argv[3]);
        const int cooldown = std::stoi(argv[5]);
        if (rounds < 1 || rounds > 100 || dwell < 2000 || dwell > 30000 || cooldown < 0 || cooldown > 600 ||
            std::string(argv[4]) != "product-combined") return 2;
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(com)) return 1;
        const int result = Run(std::filesystem::u8path(argv[1]), rounds, static_cast<DWORD>(dwell),
            static_cast<DWORD>(cooldown) * 1000);
        CoUninitialize();
        return result;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
