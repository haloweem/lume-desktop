// Opt-in acceptance probe. Local silence, real audio host, no manager or accounts.
#include "apps/shell/music_desktop_session.h"
#include "apps/shell/music_session.h"
#include "platform/windows/music_host_client.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>
#include <objbase.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace lume;
using Json = nlohmann::json;
namespace fs = std::filesystem;
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
constexpr UINT Wake = WM_APP + 71;
constexpr int PhaseSeconds = 10;

void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void Write(std::ofstream& stream, const Json& record) {
    stream << record.dump() << '\n';
    stream.flush();
    Check(stream.good(), "report write failed");
}
Json Parse(const std::wstring& text) { return Json::parse(text.begin(), text.end()); }
ULONGLONG Ticks(FILETIME value) {
    return (static_cast<ULONGLONG>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
ULONGLONG Cpu(HANDLE process, ULONGLONG* born = nullptr) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    Check(GetProcessTimes(process, &creation, &exit, &kernel, &user) != FALSE, "GetProcessTimes failed");
    if (born) *born = Ticks(creation);
    return Ticks(kernel) + Ticks(user);
}

struct Process {
    Handle handle{nullptr, CloseHandle};
    DWORD parent = 0;
    ULONGLONG born = 0;
    fs::path image;
};
struct Processes {
    std::map<DWORD, Process> tracked;
    void Add(DWORD pid, DWORD parent) {
        Handle handle(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
            FALSE, pid), CloseHandle);
        Check(handle != nullptr, "cannot inspect own process tree");
        ULONGLONG born = 0;
        Cpu(handle.get(), &born);
        if (parent && born < tracked.at(parent).born) return;
        wchar_t image[32768]{};
        DWORD size = static_cast<DWORD>(std::size(image));
        Check(QueryFullProcessImageNameW(handle.get(), 0, image, &size) != FALSE, "process image query failed");
        tracked.emplace(pid, Process{std::move(handle), parent, born, fs::path(image)});
    }
    Processes() { Add(GetCurrentProcessId(), 0); }
    void Discover() {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0), CloseHandle);
        Check(snapshot.get() != INVALID_HANDLE_VALUE, "process enumeration failed");
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        std::vector<PROCESSENTRY32W> entries;
        Check(Process32FirstW(snapshot.get(), &entry) != FALSE, "process enumeration empty");
        do { entries.push_back(entry); } while (Process32NextW(snapshot.get(), &entry));
        Check(GetLastError() == ERROR_NO_MORE_FILES, "process enumeration incomplete");
        // Retained process handles keep identity stable through shutdown.
        bool added = false;
        do {
            added = false;
            for (const auto& item : entries) {
                if (tracked.count(item.th32ProcessID) || !tracked.count(item.th32ParentProcessID)) continue;
                const auto before = tracked.size();
                Add(item.th32ProcessID, item.th32ParentProcessID);
                added = added || tracked.size() != before;
            }
        } while (added);
    }
    Json Sample() {
        Discover();
        Json items = Json::array();
        ULONGLONG cpu = 0, privateBytes = 0, workingSet = 0;
        unsigned children = 0;
        for (const auto& [pid, process] : tracked) {
            const auto wait = WaitForSingleObject(process.handle.get(), 0);
            Check(wait == WAIT_TIMEOUT || wait == WAIT_OBJECT_0, "process wait failed");
            const bool alive = wait == WAIT_TIMEOUT;
            const auto ticks = Cpu(process.handle.get());
            cpu += ticks; // Include final CPU of exited children, never subtract it.
            Json item{{"pid", pid}, {"parentPid", process.parent}, {"image", process.image.u8string()},
                {"creation100ns", process.born}, {"alive", alive}, {"cpu100ns", ticks}};
            if (alive) {
                PROCESS_MEMORY_COUNTERS_EX memory{};
                memory.cb = sizeof(memory);
                Check(GetProcessMemoryInfo(process.handle.get(),
                    reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE,
                    "GetProcessMemoryInfo failed");
                privateBytes += memory.PrivateUsage;
                workingSet += memory.WorkingSetSize;
                item["privateBytes"] = memory.PrivateUsage;
                item["workingSetBytes"] = memory.WorkingSetSize;
                if (pid != GetCurrentProcessId()) ++children;
            } else {
                DWORD code = 0;
                Check(GetExitCodeProcess(process.handle.get(), &code) != FALSE, "exit code query failed");
                item["exitCode"] = code;
            }
            items.push_back(std::move(item));
        }
        return {{"tickMs", GetTickCount64()}, {"cpu100ns", cpu}, {"privateBytes", privateBytes},
            {"workingSetBytes", workingSet}, {"liveChildren", children}, {"processes", std::move(items)}};
    }
};

struct Evidence {
    std::atomic<unsigned> plays{0}, pauses{0}, closes{0};
    std::atomic<double> position{0}, duration{0};
};
// Observes matching ACKs only; every request/event still uses the real pipe client.
class ObservedHost final : public shell::IMusicPlaybackHost {
public:
    ObservedHost(const fs::path& executable, Evidence& evidence)
        : client_({executable.wstring(), 1000}), evidence_(evidence) {}
    bool Start(EventHandler handler) override {
        return client_.Start([this, handler = std::move(handler)](std::string frame) {
            event_ = music::DecodeHostEvent(frame);
            handler(std::move(frame));
        });
    }
    bool Send(std::string frame) override { return Send(std::move(frame), {}); }
    bool Send(std::string frame, const std::function<bool()>& cancelled) override {
        const auto command = music::DecodeHostCommand(frame);
        event_.reset();
        const bool sent = client_.Send(std::move(frame), cancelled);
        if (sent && command && event_ && event_->session == command->session &&
            event_->sequence == command->sequence && event_->error.empty()) {
            evidence_.position = event_->position;
            evidence_.duration = event_->duration;
            if (command->action == "play" && event_->playing) ++evidence_.plays;
            if (command->action == "pause" && !event_->playing && event_->status == "paused") ++evidence_.pauses;
            if (command->action == "close" && event_->status == "closed") ++evidence_.closes;
        }
        return sent;
    }
    void Stop() override { client_.Stop(); }
    bool Running() const noexcept override { return client_.Running(); }
private:
    music::MusicHostClient client_;
    Evidence& evidence_;
    std::optional<music::HostEvent> event_;
};

void Silence(const fs::path& path) {
    constexpr std::uint32_t bytesPerSecond = 48000 * 2 * 2, bytes = bytesPerSecond * 120;
    std::ofstream wav(path, std::ios::binary);
    const auto little = [&](std::uint32_t value, int count) {
        for (int i = 0; i < count; ++i) wav.put(static_cast<char>((value >> (8 * i)) & 255));
    };
    wav.write("RIFF", 4); little(36 + bytes, 4); wav.write("WAVEfmt ", 8);
    little(16, 4); little(1, 2); little(2, 2); little(48000, 4);
    little(bytesPerSecond, 4); little(4, 2); little(16, 2);
    wav.write("data", 4); little(bytes, 4);
    const std::array<char, 8192> zeros{};
    for (std::uint32_t remaining = bytes; remaining;) {
        const auto count = std::min(remaining, static_cast<std::uint32_t>(zeros.size()));
        wav.write(zeros.data(), count);
        remaining -= count;
    }
    wav.close();
    Check(wav.good(), "silent WAV write failed");
}

void Pump(DWORD milliseconds, const std::function<void()>& sync) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            Check(message.message != WM_QUIT, "unexpected quit");
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        sync();
        const auto now = GetTickCount64();
        if (now >= deadline) return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(deadline - now), QS_ALLINPUT);
    } while (true);
}
HWND OwnWindow(const wchar_t* className) {
    struct Search { const wchar_t* name; HWND found = nullptr; } search{className};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM context) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(context);
        wchar_t name[128]{};
        GetClassNameW(hwnd, name, static_cast<int>(std::size(name)));
        if (std::wstring(name) == value.name) value.found = hwnd;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

void Run(const fs::path& directory, const fs::path& host, Json& report) {
    Silence(directory / L"silence.wav");
    Processes processes;
    Evidence evidence;
    std::ofstream samples(directory / L"samples.jsonl");
    shell::MusicSessionOptions options;
    options.executable = host.wstring();
    options.createHost = [&] { return std::make_unique<ObservedHost>(host, evidence); };
    const auto thread = GetCurrentThreadId();
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    options.wake = [thread] { PostThreadMessageW(thread, Wake, 0, 0); };
    shell::MusicSession music(std::move(options));
    unsigned commands = 0;
    bool commandOk = true;
    shell::MusicDesktopSessionOptions desktopOptions;
    desktopOptions.path = (directory / L"music-widget.json").u8string();
    shell::MusicDesktopSession desktop(GetModuleHandleW(nullptr), std::move(desktopOptions),
        [&](const ui::MusicPageCommand& command) {
            ++commands;
            commandOk = commandOk && command.action == ui::MusicAction::Toggle &&
                music.HandlePage(command).has_value();
        }, [] {});
    const auto sync = [&] {
        if (music.DrainNotification()) desktop.Update(music.Snapshot());
        Check(commandOk, "desktop command was not routed to MusicSession");
    };
    const auto wait = [&](const std::function<bool()>& ready) {
        const auto end = GetTickCount64() + 10000;
        while (!ready() && GetTickCount64() < end) Pump(25, sync);
        if (!ready()) report["failedSessionState"] = Parse(music.State());
        Check(ready(), "timed out waiting for real host/desktop transition");
    };
    const auto enable = [&](bool value) {
        const auto response = desktop.HandleMessage(value ?
            LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":true})" :
            LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":false})");
        Check(response.has_value(), "desktop setting rejected");
        const auto state = Parse(*response);
        Check(state.at("saved") == true && state.at("enabled") == value &&
            state.at("running") == value, "isolated desktop setting did not apply");
    };
    const auto toggle = [&] {
        const auto window = OwnWindow(L"LumeNativeDesktopWidget");
        Check(window && IsWindowVisible(window), "own music widget missing");
        const auto before = commands;
        // Target only our HWND. Never send global media keys to the user's player.
        SendMessageW(window, WM_KEYDOWN, VK_SPACE, 0);
        Check(commands == before + 1 && commandOk, "native desktop toggle did not dispatch");
    };
    const auto phase = [&](const char* name, bool hostExpected, bool playing, bool widget) {
        std::cout << "PHASE " << name << " (" << PhaseSeconds << "s)\n" << std::flush;
        auto first = processes.Sample();
        const auto start = first.at("tickMs").get<ULONGLONG>();
        const double position = evidence.position.load();
        double privateSum = 0, workingSum = 0;
        ULONGLONG privatePeak = 0, workingPeak = 0;
        Json last;
        for (int second = 0; second <= PhaseSeconds; ++second) {
            const auto due = start + static_cast<ULONGLONG>(second) * 1000;
            const auto now = GetTickCount64();
            if (due > now) Pump(static_cast<DWORD>(due - now), sync);
            auto record = second == 0 ? first : processes.Sample();
            Check((record.at("liveChildren").get<unsigned>() != 0) == hostExpected,
                "unexpected live child count");
            Check(music.HostRunning() == hostExpected && music.Snapshot().playing == playing &&
                desktop.Running() == widget, "phase state changed unexpectedly");
            if (hostExpected) {
                unsigned hosts = 0;
                for (const auto& [pid, process] : processes.tracked)
                    if (process.parent == GetCurrentProcessId()) {
                        Check(fs::equivalent(process.image, host), "child is not the requested audio host");
                        if (WaitForSingleObject(process.handle.get(), 0) == WAIT_TIMEOUT) ++hosts;
                    }
                Check(hosts == 1, "expected exactly one live audio host");
            }
            const auto memory = record.at("privateBytes").get<ULONGLONG>();
            const auto working = record.at("workingSetBytes").get<ULONGLONG>();
            privateSum += static_cast<double>(memory);
            workingSum += static_cast<double>(working);
            privatePeak = std::max(privatePeak, memory);
            workingPeak = std::max(workingPeak, working);
            record["phase"] = name;
            record["second"] = second;
            record["hostPositionSeconds"] = evidence.position.load();
            record["sessionPositionSeconds"] = music.Snapshot().position;
            Write(samples, record);
            last = std::move(record);
        }
        const auto elapsed = last.at("tickMs").get<ULONGLONG>() - start;
        const double cpu = static_cast<double>(last.at("cpu100ns").get<ULONGLONG>() -
            first.at("cpu100ns").get<ULONGLONG>()) / (static_cast<double>(elapsed) * 100);
        const double advanced = evidence.position.load() - position;
        report["phases"].push_back({{"phase", name}, {"elapsedMs", elapsed}, {"samples", PhaseSeconds + 1},
            {"cpuPercentOneCore", cpu}, {"cpuPercentMachine", cpu / report.at("logicalProcessors").get<double>()},
            {"meanPrivateBytes", privateSum / (PhaseSeconds + 1)}, {"peakPrivateBytes", privatePeak},
            {"meanWorkingSetBytes", workingSum / (PhaseSeconds + 1)}, {"peakWorkingSetBytes", workingPeak},
            {"hostPositionDeltaSeconds", advanced}, {"liveChildren", last.at("liveChildren")}});
        Check(!playing || advanced >= 7, "real audio output position did not advance");
        Check(playing || std::abs(advanced) < 0.05, "host moved during a non-playing phase");
        Check(Parse(music.State()).at("error") == "", "music session reported a playback error");
    };

    Check(desktop.Restore(), "desktop restore/SMTC initialization failed");
    Check(!desktop.Running() && !music.HasWork(), "fresh sessions are not idle");
    report["desktopRestored"] = Parse(desktop.State());
    phase("idle", false, false, false);
    music::Track track;
    track.id = "resource-probe-local-silence";
    track.title = "Local silent WAV resource fixture";
    track.source = (directory / L"silence.wav").u8string();
    Check(music.PlayTracks({track}, track.id, true), "local fixture rejected");
    wait([&] { return evidence.plays >= 1 && evidence.position >= 0.5; });
    Check(std::abs(evidence.duration.load() - 120) < 0.1, "real host did not decode the WAV duration");
    phase("playing", true, true, false);
    enable(true);
    Pump(1000, sync);
    phase("desktop_widget_enabled", true, true, true);
    toggle();
    wait([&] { return evidence.pauses >= 1 && !music.Snapshot().playing; });
    const double paused = evidence.position.load();
    enable(false);
    Check(!OwnWindow(L"LumeNativeDesktopWidget"), "disabled widget window survived");
    phase("paused_disabled", true, false, false);
    enable(true);
    desktop.Update(music.Snapshot());
    toggle();
    wait([&] { return evidence.plays >= 2 && evidence.position > paused + 0.5; });
    Check(evidence.position < paused + 3, "host kept advancing during the ten-second pause");
    report["pauseResume"] = {{"desktopToggleCommands", commands}, {"pauseAck", evidence.pauses.load()},
        {"playAck", evidence.plays.load()}, {"pausedPositionSeconds", paused},
        {"resumedPositionSeconds", evidence.position.load()}};
    const auto window = OwnWindow(L"LumeNativeDesktopWidget");
    SendMessageW(window, WM_CLOSE, 0, 0);
    Pump(250, sync);
    Check(!IsWindow(window) && !desktop.Running() && Parse(desktop.State()).at("enabled") == false,
        "native widget close failed");
    report["nativeWidgetCloseVerified"] = true;
    desktop.Shutdown();
    music.Shutdown();
    Check(evidence.closes == 1 && !music.HasWork(), "host close ACK or worker shutdown missing");
    Check(!OwnWindow(L"LumeNativeDesktopWidget") && !OwnWindow(L"LumeMusicMediaDispatch") &&
        Parse(desktop.State()).at("smtc").at("registered") == false, "desktop/SMTC window survived shutdown");
    phase("shutdown", false, false, false);
    report["shutdown"] = processes.Sample();
    report["shutdown"]["hostCloseAck"] = evidence.closes.load();
    report["shutdown"]["noMusicWork"] = !music.HasWork();
    report["shutdown"]["desktopState"] = Parse(desktop.State());
    Check(report["shutdown"].at("liveChildren") == 0, "child process survived shutdown");
    for (const auto& process : report["shutdown"].at("processes"))
        if (process.at("pid") != GetCurrentProcessId())
            Check(process.at("exitCode") == 0, "child process exited with an error");
}
} // namespace

int main() {
    int argc = 0;
    auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc != 3) {
        if (argv) LocalFree(argv);
        std::cerr << "Usage: lume-music-resource-probe <new-output-directory> <LumeMusicHost.exe>\n";
        return 1;
    }
    const fs::path requested(argv[1]), executable(argv[2]);
    LocalFree(argv);
    fs::path directory;
    Json report;
    bool apartment = false, created = false;
    int result = 1;
    try {
        const auto host = fs::canonical(executable);
        Check(fs::is_regular_file(host) && _wcsicmp(host.filename().c_str(), L"LumeMusicHost.exe") == 0,
            "an existing LumeMusicHost.exe is required");
        directory = fs::absolute(requested);
        Check(fs::create_directory(directory), "output must be a new directory with an existing parent");
        created = true;
        fs::current_path(directory); // The real host inherits only this isolated working directory.
        report = {{"status", "running"}, {"scope", "MusicSession + MusicDesktopSession/Controls + real LumeMusicHost"},
            {"fixture", "120-second local silent PCM WAV, 48000 Hz, stereo, 16 bit; NOT online playback"},
            {"managerWebViewIncluded", false}, {"accountsOrProvidersUsed", false},
            {"settingsPath", (directory / L"music-widget.json").u8string()}, {"host", host.u8string()},
            {"logicalProcessors", GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)}, {"phaseSeconds", PhaseSeconds},
            {"measurement", "Probe (including instrumentation) plus observed descendants. CPU: kernel+user deltas; "
                "100% one-core = one logical CPU. Memory: private commit and summed working sets (shared pages can "
                "count twice). No system audio services, DWM/GPU or resource thresholds. Process snapshots every "
                "second may miss short-lived descendants. Shutdown samples retain stopped session objects."},
            {"phases", Json::array()}};
        Check(report.at("logicalProcessors").get<unsigned>() > 0, "logical processor count unavailable");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        apartment = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        Check(apartment, "COM initialization failed");
        Run(directory, host, report);
        report["status"] = "passed";
        result = 0;
    } catch (const std::exception& error) {
        report["status"] = "failed";
        report["error"] = error.what();
        std::cerr << "FAIL: " << error.what() << '\n';
    }
    if (apartment) CoUninitialize();
    if (created) {
        try {
            std::ofstream summary(directory / L"summary.json");
            Write(summary, report);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            result = 1;
        }
        std::cout << (result == 0 ? "MUSIC_RESOURCE_PROBE_PASS: " : "MUSIC_RESOURCE_PROBE_FAIL: ")
            << directory.u8string() << '\n';
    }
    return result;
}
