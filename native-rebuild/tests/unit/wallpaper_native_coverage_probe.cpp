// Manual probe: production window enumeration, an external fixture process,
// real mpv and isolated preferences. Never switches the input desktop.
#include "apps/shell/wallpaper_session.h"
#include "core/wallpaper/library.h"
#include "core/wallpaper/playback_preferences.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/mpv_process_controller.h"
#include "platform/windows/text_util.h"
#include "vendor/nlohmann/json.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Json = nlohmann::json;
using Session = lume::shell::WallpaperSession;
using Action = lume::ui::WallpaperAction;
namespace fs = std::filesystem;
constexpr UINT CoverCommand = WM_APP + 37;
Json evidence{{"checks", Json::array()}, {"cursor_observations", Json::array()}};
void Check(bool value, const char* label) {
    evidence["checks"].push_back({{"check", label}, {"passed", value}});
    std::cout << (value ? "PASS: " : "FAIL: ") << label << std::endl;
    if (!value) throw std::runtime_error(label);
}
void Pump(DWORD duration = 5) {
    const auto until = GetTickCount64() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 2, QS_ALLINPUT);
    } while (GetTickCount64() < until);
}
bool Wait(const std::function<bool()>& ready, DWORD timeout = 10000) {
    const auto until = GetTickCount64() + timeout;
    do { Pump(); if (ready()) return true; } while (GetTickCount64() < until);
    return false;
}
std::wstring DesktopName(HDESK desktop) {
    wchar_t name[256]{}; DWORD bytes = 0;
    return desktop && GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &bytes) ? name : L"";
}
std::wstring InputDesktopName() {
    const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    const auto name = DesktopName(desktop); if (desktop) CloseDesktop(desktop); return name;
}
Json CursorObservation(const char* phase) {
    CURSORINFO cursor{sizeof(cursor)}; SetLastError(0);
    const bool available = GetCursorInfo(&cursor) != FALSE; const auto error = GetLastError();
    Json threads = Json::array(); const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) do {
            if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
            GUITHREADINFO gui{sizeof(gui)};
            if (GetGUIThreadInfo(entry.th32ThreadID, &gui)) threads.push_back({{"thread", entry.th32ThreadID},
                {"capture", reinterpret_cast<std::uintptr_t>(gui.hwndCapture)},
                {"focus", reinterpret_cast<std::uintptr_t>(gui.hwndFocus)}});
        } while (Thread32Next(snapshot, &entry));
        CloseHandle(snapshot);
    }
    return {{"phase", phase}, {"available", available}, {"error", error}, {"flags", cursor.flags},
        {"input_desktop", lume::text::Utf8(InputDesktopName())}, {"own_gui_threads", threads},
        {"note", "Read-only observation; an inactive desktop cannot establish game cursor visibility."}};
}
Json ConsoleObservation() {
    const auto window = GetConsoleWindow(); DWORD owner = 0;
    if (window) GetWindowThreadProcessId(window, &owner);
    std::array<DWORD, 64> processes{}; SetLastError(ERROR_SUCCESS);
    const auto count = GetConsoleProcessList(processes.data(), static_cast<DWORD>(processes.size()));
    const auto error = count ? ERROR_SUCCESS : GetLastError();
    Json attached = Json::array();
    if (count <= processes.size()) for (DWORD i = 0; i < count; ++i) attached.push_back(processes[i]);
    return {{"window", reinterpret_cast<std::uintptr_t>(window)}, {"window_process", owner},
        {"attached_count", count}, {"attached_processes", attached}, {"query_error", error}};
}
std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\""; std::size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'\"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'\"') result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'\"';
}
struct Child {
    PROCESS_INFORMATION process{}; HANDLE job = nullptr;
    Child(const std::vector<std::wstring>& arguments, const std::wstring& desktop) {
        wchar_t executable[32768]{}; Check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "find probe executable");
        std::wstring command = Quote(executable); for (const auto& argument : arguments) command += L" " + Quote(argument);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.lpDesktop = const_cast<wchar_t*>(desktop.c_str());
        job = CreateJobObjectW(nullptr, nullptr); Check(job != nullptr, "create fixture lifetime job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            // These fixtures write JSON, not terminal output. A detached
            // console process avoids creating its own hidden conhost, which
            // would legitimately remain alive until the scenario exits.
            !CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | DETACHED_PROCESS,
                nullptr, nullptr, &startup, &process) || !AssignProcessToJobObject(job, process.hProcess) ||
            ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
            if (process.hProcess) { TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 2000); }
            if (process.hThread) CloseHandle(process.hThread); if (process.hProcess) CloseHandle(process.hProcess);
            CloseHandle(job); job = nullptr; throw std::runtime_error("start fixture on private desktop");
        }
        evidence["spawned_processes"].push_back({{"pid", process.dwProcessId}, {"detached_console", true}});
    }
    ~Child() {
        if (job) CloseHandle(job);
        if (process.hProcess) { WaitForSingleObject(process.hProcess, 2000); CloseHandle(process.hProcess); }
        if (process.hThread) CloseHandle(process.hThread);
    }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
};
LRESULT CALLBACK CoverProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == CoverCommand) {
        const auto displays = lume::platform::windows::EnumerateMpvDisplays();
        if (w >= displays.size()) return 0;
        if (l == SW_HIDE || l == SW_MINIMIZE) ShowWindow(window, static_cast<int>(l));
        else {
            const auto& bounds = displays[static_cast<std::size_t>(w)].bounds;
            ShowWindow(window, SW_RESTORE);
            SetWindowPos(window, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        return 1;
    }
    if (message == WM_CLOSE) { DestroyWindow(window); PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
int CoverFixture() {
    WNDCLASSW type{}; type.hInstance = GetModuleHandleW(nullptr); type.lpfnWndProc = CoverProcedure;
    type.lpszClassName = L"LumeWallpaperCoverageFixture";
    if (!RegisterClassW(&type)) return 2;
    if (!CreateWindowExW(0, type.lpszClassName, L"Private wallpaper coverage fixture", WS_POPUP,
        0, 0, 640, 360, nullptr, nullptr, type.hInstance, nullptr)) return 3;
    MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
std::vector<DWORD> Players(HWND parent) {
    std::vector<DWORD> result;
    EnumChildWindows(parent, [](HWND window, LPARAM value)->BOOL {
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid); auto& ids = *reinterpret_cast<std::vector<DWORD>*>(value);
        if (pid != GetCurrentProcessId() && std::find(ids.begin(), ids.end(), pid) == ids.end()) ids.push_back(pid);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result)); return result;
}
std::vector<DWORD> Descendants() {
    std::vector<DWORD> result{GetCurrentProcessId()};
    struct Entry { DWORD pid; DWORD parent; std::wstring name; };
    std::vector<Entry> processes;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (snapshot == INVALID_HANDLE_VALUE) return {MAXDWORD};
    PROCESSENTRY32W process{}; process.dwSize = sizeof(process);
    if (Process32FirstW(snapshot, &process)) do { processes.push_back({process.th32ProcessID, process.th32ParentProcessID, process.szExeFile}); }
        while (Process32NextW(snapshot, &process));
    CloseHandle(snapshot);
    for (std::size_t i = 0; i < result.size(); ++i) for (const auto& item : processes)
        if (item.parent == result[i] && std::find(result.begin(), result.end(), item.pid) == result.end()) result.push_back(item.pid);
    result.erase(result.begin());
    std::vector<DWORD> running;
    Json observations = Json::array();
    for (const auto pid : result) {
        const auto entry = std::find_if(processes.begin(), processes.end(), [pid](const Entry& value) { return value.pid == pid; });
        const auto handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
        const auto error = handle ? ERROR_SUCCESS : GetLastError();
        const auto state = handle ? WaitForSingleObject(handle, 0) : WAIT_FAILED;
        if (handle) CloseHandle(handle);
        // Toolhelp may retain a terminated process while another owner still
        // holds a process handle. Require actual running state, and keep
        // inaccessible descendants as failures rather than assuming cleanup.
        const bool alive = handle ? state != WAIT_OBJECT_0 : error != ERROR_INVALID_PARAMETER;
        if (alive) running.push_back(pid);
        observations.push_back({{"pid", pid}, {"parent", entry->parent}, {"image", lume::text::Utf8(entry->name)},
            {"wait_state", state}, {"open_error", error}, {"running_or_unverified", alive}});
    }
    evidence["descendant_observations"] = std::move(observations);
    return running;
}
struct Probe {
    HANDLE pipe = INVALID_HANDLE_VALUE; DWORD pid = 0; std::string buffered; std::uint64_t sequence = 800000;
    ~Probe() { Close(); }
    void Close() { if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; pid = 0; buffered.clear(); }
    bool Follow(HWND parent) {
        const auto children = Players(parent); if (children.size() != 1) return false;
        if (pid == children.front() && pipe != INVALID_HANDLE_VALUE) return true;
        Close(); WIN32_FIND_DATAW entry{};
        const auto pattern = L"\\\\.\\pipe\\lume-native-mpv-" + std::to_wstring(GetCurrentProcessId()) + L"-*";
        const auto finder = FindFirstFileW(pattern.c_str(), &entry); if (finder == INVALID_HANDLE_VALUE) return false;
        do {
            const auto name = L"\\\\.\\pipe\\" + std::wstring(entry.cFileName);
            const auto candidate = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (candidate == INVALID_HANDLE_VALUE) continue;
            ULONG owner = 0;
            if (GetNamedPipeServerProcessId(candidate, &owner) && owner == children.front()) { pipe = candidate; pid = owner; break; }
            CloseHandle(candidate);
        } while (FindNextFileW(finder, &entry)); FindClose(finder); return pipe != INVALID_HANDLE_VALUE;
    }
    bool Transfer(bool write, void* data, DWORD bytes, DWORD& count) {
        OVERLAPPED operation{}; operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); if (!operation.hEvent) return false;
        BOOL success = write ? WriteFile(pipe, data, bytes, &count, &operation) : ReadFile(pipe, data, bytes, &count, &operation);
        if (!success && GetLastError() == ERROR_IO_PENDING) {
            const auto until = GetTickCount64() + 1500;
            while (WaitForSingleObject(operation.hEvent, 0) == WAIT_TIMEOUT && GetTickCount64() < until) Pump();
            if (WaitForSingleObject(operation.hEvent, 0) == WAIT_OBJECT_0) success = GetOverlappedResult(pipe, &operation, &count, FALSE);
            else { CancelIoEx(pipe, &operation); DWORD ignored = 0; GetOverlappedResult(pipe, &operation, &ignored, TRUE); }
        }
        CloseHandle(operation.hEvent); return success != FALSE;
    }
    Json Property(const char* property) {
        if (pipe == INVALID_HANDLE_VALUE) return {};
        const auto id = ++sequence;
        auto request = Json{{"command", Json::array({"get_property", property})}, {"request_id", id}}.dump() + '\n';
        DWORD count = 0; if (!Transfer(true, request.data(), static_cast<DWORD>(request.size()), count) || count != request.size()) return {};
        const auto until = GetTickCount64() + 2000;
        while (GetTickCount64() < until) {
            const auto newline = buffered.find('\n');
            if (newline == std::string::npos) {
                std::array<char, 4096> bytes{};
                if (!Transfer(false, bytes.data(), static_cast<DWORD>(bytes.size()), count) || !count) return {};
                buffered.append(bytes.data(), count); if (buffered.size() > 65536) return {}; continue;
            }
            const auto reply = Json::parse(buffered.substr(0, newline), nullptr, false); buffered.erase(0, newline + 1);
            if (reply.is_object() && reply.value("request_id", std::uint64_t{0}) == id)
                return reply.value("error", std::string{}) == "success" ? reply.value("data", Json{}) : Json{};
        }
        return {};
    }
};
struct Playback {
    HWND parent = nullptr; std::unique_ptr<Session> session; Probe probe; std::uint64_t display = 0;
    Playback(const fs::path& executable, const fs::path& video, const fs::path& root,
        const lume::platform::windows::MpvDisplay& monitor) : display(monitor.id) {
        fs::create_directories(root); lume::wallpaper::Library library(root.u8string());
        Check(library.Load().ok, "load isolated coverage library"); const auto imported = library.ImportLocal(video.u8string());
        Check(imported.ok, "import only the supplied local fixture");
        lume::platform::windows::LocalSettingsFileStore files; lume::wallpaper::PlaybackPreferencesStore store(files);
        lume::wallpaper::PlaybackPreferences preferences; preferences.pauseFullscreen = true;
        preferences.displays.push_back({monitor.id, imported.id, false, 0, true});
        Check(store.Save((root / "preferences.json").u8string(), preferences), "save isolated per-display preferences");
        parent = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Private wallpaper mpv parent",
            WS_POPUP, -30000, -30000, 640, 360, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(parent != nullptr, "create owned off-screen playback parent"); ShowWindow(parent, SW_SHOWNOACTIVATE);
        try {
            lume::shell::WallpaperSessionOptions options; options.root = root.u8string(); options.executable = executable.wstring();
            options.verification = true; options.verificationParent = parent; options.verificationRecoveryMilliseconds = 6000;
            options.verificationFullscreen = [monitor](std::uint64_t id) {
                return id == monitor.id && lume::shell::detail::WallpaperDisplayCoveredNative(monitor.bounds);
            };
            session = std::make_unique<Session>(GetModuleHandleW(nullptr), std::move(options), [](const auto&) {});
            if (!session->Restore()) throw std::runtime_error("restore coverage session");
        } catch (...) {
            session.reset(); DestroyWindow(parent); parent = nullptr; throw;
        }
    }
    ~Playback() { Stop(); if (parent) DestroyWindow(parent); }
    void Stop() { probe.Close(); if (session) session->Shutdown(); }
    bool Paused(bool expected) { return probe.Follow(parent) && probe.Property("pause") == Json(expected); }
    void Send(Action action) { lume::ui::WallpaperPageCommand command; command.action = action; command.display = display; session->HandlePage(command); }
    bool Advancing() {
        if (!Paused(false)) return false;
        const auto first = probe.Property("time-pos"); Pump(317); const auto last = probe.Property("time-pos");
        return first.is_number() && last.is_number() && first != last;
    }
};
int Scenario(const fs::path& executable, const fs::path& video, const fs::path& output) {
    Check(evidence["console"]["window"] == 0 && evidence["console"]["attached_count"] == 0,
        "private scenario starts without an attached console");
    const auto inputBefore = InputDesktopName(); evidence["cursor_observations"].push_back(CursorObservation("scenario-before"));
    Check(!inputBefore.empty(), "read the input desktop before the scenario");
    const auto desktop = DesktopName(GetThreadDesktop(GetCurrentThreadId()));
    Check(!desktop.empty() && desktop != inputBefore, "scenario belongs to an inactive private desktop");
    const auto monitors = lume::platform::windows::EnumerateMpvDisplays(); Check(!monitors.empty(), "enumerate physical monitor bounds");
    evidence["displays_tested"] = (std::min)(std::size_t{2}, monitors.size());
    evidence["multi_display_skipped"] = monitors.size() < 2;
    std::vector<std::unique_ptr<Playback>> playback;
    for (std::size_t i = 0; i < (std::min)(std::size_t{2}, monitors.size()); ++i)
        playback.push_back(std::make_unique<Playback>(executable, video, output / ("display-" + std::to_string(i)), monitors[i]));
    for (const auto& player : playback) Check(Wait([&] { return player->Paused(false); }), "isolated real player starts unpaused");
    {
        Child cover({L"--cover-fixture"}, L"WinSta0\\" + desktop); HWND window = nullptr;
        Check(Wait([&] {
            struct Search { DWORD pid; HWND window; } search{cover.process.dwProcessId, nullptr};
            EnumWindows([](HWND candidate, LPARAM value)->BOOL {
                auto& result = *reinterpret_cast<Search*>(value); DWORD pid = 0; GetWindowThreadProcessId(candidate, &pid);
                wchar_t name[64]{}; GetClassNameW(candidate, name, 64);
                if (pid == result.pid && wcscmp(name, L"LumeWallpaperCoverageFixture") == 0) { result.window = candidate; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&search)); window = search.window; return window != nullptr;
        }), "find the ordinary covering window from a different process");
        const auto command = [&](std::size_t monitor, int show) {
            DWORD_PTR result = 0;
            Check(SendMessageTimeoutW(window, CoverCommand, monitor, show, SMTO_ABORTIFHUNG, 1500, &result) && result == 1,
                "bounded fixture coverage command completes");
        };
        for (std::size_t i = 0; i < playback.size(); ++i) {
            const auto original = Players(playback[i]->parent); command(i, SW_SHOW);
            Check(Wait([&] { return lume::shell::detail::WallpaperDisplayCoveredNative(monitors[i].bounds); }), "production enumeration sees external full-monitor coverage");
            Check(Wait([&] { return playback[i]->Paused(true); }), "actual mpv pauses behind the covering window");
            for (std::size_t other = 0; other < playback.size(); ++other) if (other != i)
                Check(!lume::shell::detail::WallpaperDisplayCoveredNative(monitors[other].bounds) && playback[other]->Paused(false),
                    "the other physical display remains uncovered and playing");
            command(i, SW_MINIMIZE);
            Check(Wait([&] { return !lume::shell::detail::WallpaperDisplayCoveredNative(monitors[i].bounds) && playback[i]->Paused(false); }),
                "minimizing the external window restores playback through native enumeration");
            Check(Players(playback[i]->parent) == original && playback[i]->Advancing(), "short coverage preserves PID and the returned timeline advances");
            command(i, SW_HIDE);
        }
        command(0, SW_SHOW); Check(Wait([&] { return playback[0]->Paused(true); }), "long coverage first confirms actual pause");
        const auto previous = Players(playback[0]->parent);
        Check(Wait([&] { return Players(playback[0]->parent).empty(); }, 12000), "long native coverage parks the owned decoder");
        playback[0]->probe.Close(); command(0, SW_HIDE);
        Check(Wait([&] { return playback[0]->Paused(false); }), "hiding long coverage recreates one playing decoder");
        Check(Players(playback[0]->parent) != previous && playback[0]->Advancing(), "the replacement PID has a progressing real timeline");
        playback[0]->Send(Action::Pause); Check(Wait([&] { return playback[0]->Paused(true); }), "manual pause reaches the actual player");
        command(0, SW_SHOW); Pump(200); command(0, SW_HIDE); Pump(2200);
        Check(playback[0]->Paused(true), "return from native coverage preserves manual pause");
        playback[0]->Send(Action::Resume); Check(Wait([&] { return playback[0]->Paused(false); }) && playback[0]->Advancing(), "explicit manual resume restores progress");
        DWORD_PTR result = 0; SendMessageTimeoutW(window, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 1500, &result);
        Check(WaitForSingleObject(cover.process.hProcess, 2000) == WAIT_OBJECT_0, "external coverage fixture exits normally");
    }
    for (const auto& player : playback) { player->Stop(); Check(Players(player->parent).empty() && !GetWindow(player->parent, GW_CHILD), "session releases its decoder and child host"); }
    Check(Wait([] { return Descendants().empty(); }, 5000), "scenario leaves no fixture or player descendants");
    evidence["cursor_observations"].push_back(CursorObservation("scenario-after"));
    Check(InputDesktopName() == inputBefore, "the input desktop remains unchanged");
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    evidence["process_id"] = GetCurrentProcessId(); evidence["console"] = ConsoleObservation();
    if (argc == 2 && wcscmp(argv[1], L"--cover-fixture") == 0) return CoverFixture();
    const bool scenario = argc == 5 && wcscmp(argv[1], L"--scenario") == 0;
    if ((!scenario && argc != 4) || (scenario && argc != 5)) {
        std::cerr << "Usage: wallpaper-native-coverage-probe mpv.exe local-fixture.mp4 output-directory\n"; return 2;
    }
    const int offset = scenario ? 2 : 1;
    const auto executable = fs::absolute(argv[offset]), video = fs::absolute(argv[offset + 1]), output = fs::absolute(argv[offset + 2]);
    HDESK desktop = nullptr; DWORD code = 1;
    try {
        Check(fs::is_regular_file(executable) && fs::is_regular_file(video), "mpv and supplied local fixture exist");
        fs::create_directories(output);
        if (scenario) code = static_cast<DWORD>(Scenario(executable, video, output));
        else {
            const auto inputBefore = InputDesktopName(); evidence["cursor_observations"].push_back(CursorObservation("runner-before"));
            Check(!inputBefore.empty(), "read the input desktop before the runner");
            const auto name = L"LumeWallpaperCoverage-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
            desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr); Check(desktop != nullptr, "create private non-input desktop");
            {
                Child child({L"--scenario", executable.wstring(), video.wstring(), output.wstring()}, L"WinSta0\\" + name);
                Check(WaitForSingleObject(child.process.hProcess, 150000) == WAIT_OBJECT_0, "coverage scenario finishes within its process timeout");
                Check(GetExitCodeProcess(child.process.hProcess, &code) != FALSE, "read private scenario result");
            }
            evidence["cursor_observations"].push_back(CursorObservation("runner-after"));
            Check(InputDesktopName() == inputBefore, "runner never switches the input desktop");
        }
    } catch (const std::exception& error) { evidence["error"] = error.what(); std::cerr << "FAIL: " << error.what() << '\n'; code = 1; }
    if (desktop) CloseDesktop(desktop);
    evidence["exit_code"] = code;
    evidence["scope"] = "Private desktop, production EnumWindows, external ordinary windows and real mpv. Not exclusive-D3D, actual monitor-off, Explorer Show Desktop, GPU-present or game-cursor acceptance.";
    std::ofstream(output / (scenario ? "scenario-evidence.json" : "runner-evidence.json")) << evidence.dump(2);
    return static_cast<int>(code);
}
