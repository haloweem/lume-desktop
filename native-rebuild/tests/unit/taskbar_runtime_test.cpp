#include "platform/windows/taskbar/taskbar_runtime.h"
#include "platform/windows/taskbar/taskbar_core_process.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

using namespace lume::taskbar;
using namespace lume::platform::windows;
namespace {
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); } }
struct CrashFixtureState {
    DWORD child = 0, createdLow = 0, createdHigh = 0, memberAtCreation = 0;
};
HANDLE observedJob = nullptr, observedProcess = nullptr;
bool memberAtCreation = false, failObservedWait = false;
CrashFixtureState* crashFixture = nullptr;
HANDLE WINAPI ObserveCreateJob(LPSECURITY_ATTRIBUTES attributes, LPCWSTR name) {
    observedJob = CreateJobObjectW(attributes, name); return observedJob;
}
BOOL WINAPI ObserveCreateProcess(LPCWSTR application, LPWSTR command,
    LPSECURITY_ATTRIBUTES processAttributes, LPSECURITY_ATTRIBUTES threadAttributes,
    BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory,
    LPSTARTUPINFOW startup, LPPROCESS_INFORMATION created) {
    const BOOL result = CreateProcessW(application, command, processAttributes, threadAttributes,
        inherit, flags, environment, directory, startup, created);
    if (result) {
        BOOL member = FALSE;
        memberAtCreation = IsProcessInJob(created->hProcess, observedJob, &member) && member;
        observedProcess = created->hProcess;
        if (crashFixture) {
            FILETIME birth{}, end{}, kernel{}, user{};
            GetProcessTimes(created->hProcess, &birth, &end, &kernel, &user);
            crashFixture->createdLow = birth.dwLowDateTime;
            crashFixture->createdHigh = birth.dwHighDateTime;
            crashFixture->memberAtCreation = memberAtCreation;
            InterlockedExchange(reinterpret_cast<volatile LONG*>(&crashFixture->child),
                                static_cast<LONG>(created->dwProcessId));
            // Die at the first instruction after the kernel creates the still
            // suspended child, before any caller-side job assignment can run.
            TerminateProcess(GetCurrentProcess(), 93);
            ExitProcess(94);
        }
    }
    return result;
}
DWORD WINAPI ObserveWait(HANDLE handle, DWORD milliseconds) {
    if (failObservedWait && handle == observedProcess) { SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
    return WaitForSingleObject(handle, milliseconds);
}
TaskbarCoreProcessApi ObservedApi() { return {ObserveCreateJob, ObserveCreateProcess, ObserveWait}; }
std::filesystem::path FixtureExecutable() {
    wchar_t path[32768]{};
    Check(GetModuleFileNameW(nullptr, path, 32768) != 0, "locate isolated process fixture");
    return path;
}
int CrashImmediatelyAfterChildCreation(const char* name) {
    const std::string text(name); const std::wstring wide(text.begin(), text.end());
    const auto mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wide.c_str());
    if (!mapping) return 3;
    crashFixture = static_cast<CrashFixtureState*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CrashFixtureState)));
    if (!crashFixture) return 4;
    std::atomic_bool cancel{false};
    TaskbarCoreProcess core(cancel, [](bool, TaskbarCoreLaunch& launch) {
        launch.executable = FixtureExecutable(); launch.arguments = L"--taskbar-kernel-fixture";
        return Error::None;
    }, {}, ObservedApi());
    core.Start();
    return 5;
}
bool AtomicProcessRegression() {
    bool passed = true;
    const auto expect = [&](bool value, const char* text) {
        if (!value) { passed = false; std::cerr << "FAIL: " << text << '\n'; }
    };
    std::atomic_bool cancel{false};
    TaskbarCoreTimings timings; timings.startTimeoutMs = 0; timings.readySettleMs = 0;
    timings.waitSliceMs = 1; timings.stopTimeoutMs = 20;
    HANDLE retained = nullptr;
    {
        int launches = 0;
        TaskbarCoreProcess core(cancel, [&](bool, TaskbarCoreLaunch& launch) {
            ++launches; launch.executable = FixtureExecutable(); launch.arguments = L"--taskbar-kernel-fixture";
            return Error::None;
        }, timings, ObservedApi());
        expect(core.Start() == Error::CoreStart && core.HasProcess(), "non-graphical child is created but never ready");
        expect(memberAtCreation, "child belongs to its specific job at the instant CreateProcessW returns");
        Check(DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(core.ExitSignal()),
            GetCurrentProcess(), &retained, SYNCHRONIZE, FALSE, 0), "retain kernel fixture wait handle");
        failObservedWait = true;
        expect(core.Start() == Error::CoreStart && launches == 1 && core.HasProcess(),
               "failed liveness wait cannot discard ownership and launch a replacement");
        expect(core.Stop(false) == Error::CoreStop && launches == 1 && core.HasProcess(),
               "failed exit wait cannot acknowledge shutdown or clear live process ownership");
        failObservedWait = false;
    }
    expect(WaitForSingleObject(retained, 3000) == WAIT_OBJECT_0, "non-graphical fixture is contained by final job close");
    CloseHandle(retained);
    const auto name = L"Local\\Lume.Taskbar.AtomicFixture." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(GetTickCount64());
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(CrashFixtureState), name.c_str());
    Check(mapping != nullptr, "isolated crash fixture mapping");
    auto* state = static_cast<CrashFixtureState*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CrashFixtureState)));
    Check(state != nullptr, "isolated crash fixture view"); *state = {};
    HANDLE child = nullptr;
    {
        TaskbarCoreTimings ownerTimings; ownerTimings.startTimeoutMs = 3000;
        TaskbarCoreProcess owner(cancel, [&](bool, TaskbarCoreLaunch& launch) {
            launch.executable = FixtureExecutable(); launch.arguments = L"--taskbar-atomic-owner-crash " + name;
            return Error::None;
        }, ownerTimings);
        expect(owner.Start() == Error::CoreExited, "fixture owner crashes immediately after suspended child creation");
        expect(state->child != 0 && state->memberAtCreation,
               "abrupt owner death cannot fall between child creation and job ownership");
        if (state->child) {
            child = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, state->child);
            if (child) {
                FILETIME birth{}, end{}, kernel{}, user{};
                Check(GetProcessTimes(child, &birth, &end, &kernel, &user), "verify crash fixture child identity");
                const bool same = birth.dwLowDateTime == state->createdLow && birth.dwHighDateTime == state->createdHigh;
                expect(!same || WaitForSingleObject(child, 1000) == WAIT_OBJECT_0,
                       "crashing owner leaves no suspended child alive");
                if (!same) { CloseHandle(child); child = nullptr; }
            } else expect(GetLastError() == ERROR_INVALID_PARAMETER, "terminated crash fixture child is absent");
        }
        // The outer test job also contains all descendants, so a red regression
        // cannot leak its deliberately orphaned suspended child into the desktop.
    }
    if (child) {
        expect(WaitForSingleObject(child, 3000) == WAIT_OBJECT_0, "outer test job also cleans up a failing crash fixture");
        CloseHandle(child);
    }
    UnmapViewOfFile(state); CloseHandle(mapping);
    return passed;
}
class Files final : public lume::settings::IFileStore {
public:
    mutable std::mutex mutex;
    std::map<std::string, std::string> data;
    bool Read(const std::string& p, std::string& out) const override {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = data.find(p);
        if (it == data.end()) return false;
        out = it->second; return true;
    }
    bool Exists(const std::string& p) const override {
        std::lock_guard<std::mutex> lock(mutex); return data.count(p) != 0;
    }
    bool Write(const std::string& p, const std::string& out) override {
        std::lock_guard<std::mutex> lock(mutex); data[p] = out; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (!data.count(from)) return false;
        data[to] = data.at(from); data.erase(from); return true;
    }
    bool Remove(const std::string& p) override {
        std::lock_guard<std::mutex> lock(mutex); data.erase(p); return true;
    }
};
struct Shared {
    std::atomic_int acquired{0}, applied{0}, released{0}, active{0}, starts{0}, stops{0};
    std::atomic_bool autoHide{false}, blockStart{false}, failAutoHide{false};
};
class Port final : public ITaskbarPort {
public:
    Port(Shared& state, const std::atomic_bool& cancel) : shared(state), cancel(cancel) {}
    Shared& shared;
    const std::atomic_bool& cancel;
    bool core = false;
    Error Acquire(bool, Mode& mode) override { ++shared.acquired; mode = Mode::Windows11X64; return Error::None; }
    void Release() override { ++shared.released; }
    Error ReadAutoHide(bool& value) override { ++shared.applied; value = shared.autoHide; return Error::None; }
    Error SetAutoHide(bool value) override {
        ++shared.applied;
        if (shared.failAutoHide) return Error::ApplyAutoHide;
        shared.autoHide = value; return Error::None;
    }
    Error ReadClassic(std::vector<Appearance>&) override { Check(false, "Windows 11 never applies Win10 accent"); return Error::None; }
    Error MakeTransparent(const Appearance&) override { return Error::ApplyAppearance; }
    RestoreResult RestoreAppearance(const Appearance&) override { return RestoreResult::Failed; }
    Error StartCore() override {
        ++shared.starts; ++shared.active;
        while (shared.blockStart && !cancel) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        --shared.active;
        if (cancel) return Error::Cancelled;
        core = true; return Error::None;
    }
    bool CoreAlive() const override { return core; }
    Error StopCore(bool) override { ++shared.stops; core = false; return Error::None; }
};
// Win10 classic accent: Explorer can rewrite it without any event, so the
// worker must keep reapplying while transparency is running.
class ClassicPort final : public ITaskbarPort {
public:
    explicit ClassicPort(std::atomic_int& writes) : writes(writes) {}
    std::atomic_int& writes;
    bool autoHide = false;
    Error Acquire(bool, Mode& mode) override { mode = Mode::Classic; return Error::None; }
    void Release() override {}
    Error ReadAutoHide(bool& value) override { value = autoHide; return Error::None; }
    Error SetAutoHide(bool value) override { autoHide = value; return Error::None; }
    Error ReadClassic(std::vector<Appearance>& out) override {
        Appearance a; a.window = 1; a.process = 2; a.processCreated = 3; a.accent = 0;
        out = {a}; return Error::None;
    }
    Error MakeTransparent(const Appearance&) override { ++writes; return Error::None; }
    RestoreResult RestoreAppearance(const Appearance&) override { return RestoreResult::Restored; }
    Error StartCore() override { Check(false, "classic mode never starts the core"); return Error::CoreStart; }
    bool CoreAlive() const override { return false; }
    Error StopCore(bool) override { return Error::None; }
};
template<class Predicate> bool Wait(Predicate p, int milliseconds = 3000) {
    const auto start = GetTickCount64();
    while (!p()) {
        if (GetTickCount64() - start > static_cast<unsigned>(milliseconds)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}
struct Resources { DWORD handles = 0, gdi = 0, user = 0, threads = 0; SIZE_T commit = 0; };
LRESULT CALLBACK RejectWatchWindow(HWND window, UINT message, WPARAM w, LPARAM l) {
    return message == WM_NCCREATE ? FALSE : DefWindowProcW(window, message, w, l);
}
Resources Sample() {
    Resources r;
    GetProcessHandleCount(GetCurrentProcess(), &r.handles);
    r.gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    r.user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    r.commit = memory.PrivateUsage;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    if (snapshot != INVALID_HANDLE_VALUE) {
        for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
            if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++r.threads;
        CloseHandle(snapshot);
    }
    return r;
}
struct FixtureWindow {
    UINT closeDelay = 1;
    bool closing = false;
};
LRESULT CALLBACK CoreFixtureWindow(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* fixture = reinterpret_cast<FixtureWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        fixture = static_cast<FixtureWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(fixture));
    }
    if (message == WM_CLOSE && fixture) {
        if (!fixture->closing) { fixture->closing = true; SetTimer(window, 1, fixture->closeDelay, nullptr); }
        return 0;
    }
    if (message == WM_TIMER && w == 1) { DestroyWindow(window); return 0; }
    if (message == WM_TIMER && w == 2) { PostQuitMessage(9); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
int RunCoreFixture(bool neverReady) {
    FixtureWindow fixture; fixture.closeDelay = 350;
    WNDCLASSEXW type{}; type.cbSize = sizeof(type); type.hInstance = GetModuleHandleW(nullptr);
    type.lpfnWndProc = CoreFixtureWindow;
    type.lpszClassName = neverReady ? L"LumeNotReadyCoreFixture" : L"TrayWindow";
    if (!RegisterClassExW(&type)) return 2;
    const auto window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName,
        L"Lume isolated lifecycle fixture", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, &fixture);
    if (!window || !SetTimer(window, 2, 10000, nullptr)) return 3;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (IsWindow(window)) DestroyWindow(window);
    return static_cast<int>(message.wParam);
}
bool ProcessLifecycleRegression() {
    wchar_t path[32768]{};
    Check(GetModuleFileNameW(nullptr, path, 32768) != 0, "locate isolated core fixture executable");
    std::atomic_bool cancel{false};
    TaskbarCoreTimings timings; timings.startTimeoutMs = 2000; timings.readySettleMs = 0;
    timings.waitSliceMs = 10; timings.stopTimeoutMs = 80;
    bool passed = true;
    const auto expect = [&](bool value, const char* text) {
        if (!value) { passed = false; std::cerr << "FAIL: " << text << '\n'; }
    };
    HANDLE containedChild = nullptr;
    {
        int launches = 0;
        TaskbarCoreProcess core(cancel, [&](bool, TaskbarCoreLaunch& launch) {
            ++launches; launch.executable = path; launch.arguments = L"--taskbar-core-fixture never-ready";
            return Error::None;
        }, timings);
        expect(core.Start() == Error::CoreStart && core.Alive() && !core.Ready(),
               "real core fixture stays alive without ready window");
        expect(core.Start() == Error::CoreStart && launches == 1,
               "retry cannot report a live but never-ready core as successfully started");
        Check(DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(core.ExitSignal()),
                              GetCurrentProcess(), &containedChild, SYNCHRONIZE, FALSE, 0) != FALSE,
              "retain only a wait handle for verifying job containment");
    }
    expect(WaitForSingleObject(containedChild, 3000) == WAIT_OBJECT_0,
           "job destruction terminates the never-ready child");
    CloseHandle(containedChild);
    {
        int launches = 0;
        TaskbarCoreProcess core(cancel, [&](bool, TaskbarCoreLaunch& launch) {
            ++launches; launch.executable = path; launch.arguments = L"--taskbar-core-fixture delayed-close";
            return Error::None;
        }, timings);
        const auto started = core.Start();
        if (started != Error::None) {
            DWORD code = 0;
            GetExitCodeProcess(reinterpret_cast<HANDLE>(core.ExitSignal()), &code);
            std::cerr << "delayed-close fixture start=" << ErrorName(started)
                      << " alive=" << core.Alive() << " exit=" << code << '\n';
            const auto child = GetProcessId(reinterpret_cast<HANDLE>(core.ExitSignal()));
            EnumWindows([](HWND window, LPARAM wanted) -> BOOL {
                DWORD owner = 0; GetWindowThreadProcessId(window, &owner);
                if (owner == static_cast<DWORD>(wanted)) {
                    wchar_t name[128]{}; GetClassNameW(window, name, 128);
                    std::wcerr << L"fixture hwnd=" << window << L" class=" << name << L'\n';
                }
                return TRUE;
            }, static_cast<LPARAM>(child));
        }
        expect(started == Error::None && core.Alive() && core.Ready(), "real core fixture exposes its own ready window");
        expect(core.Stop(false) == Error::CoreStop, "real core fixture exceeds the first normal-shutdown deadline");
        expect(!core.Ready() && core.Start() == Error::CoreStop,
               "a core already shutting down cannot acknowledge a new enable request");
        expect(WaitForSingleObject(reinterpret_cast<HANDLE>(core.ExitSignal()), 3000) == WAIT_OBJECT_0,
               "the original delayed core really exits after the first deadline");
        expect(core.Stop(false) == Error::None && launches == 1 && !core.HasProcess(),
               "completed delayed normal shutdown is acknowledged without launching another core");
    }
    return passed;
}
}
int main(int count, char** args) {
    if (count == 2 && !std::strcmp(args[1], "--taskbar-kernel-fixture")) { Sleep(15000); return 0; }
    if (count == 3 && !std::strcmp(args[1], "--taskbar-atomic-owner-crash"))
        return CrashImmediatelyAfterChildCreation(args[2]);
    if (count == 2 && !std::strcmp(args[1], "--atomic-process-only")) {
        const bool passed = AtomicProcessRegression();
        if (passed) std::cout << "taskbar atomic non-graphical process regressions passed\n";
        return passed ? 0 : 1;
    }
    if (count == 3 && !std::strcmp(args[1], "--taskbar-core-fixture"))
        return RunCoreFixture(!std::strcmp(args[2], "never-ready"));
    Check(AtomicProcessRegression(), "atomic non-graphical subprocess regressions");
    Check(ProcessLifecycleRegression(), "real subprocess lifecycle regressions");
    Files files; Shared shared; std::atomic_int notifications{0};
    TaskbarPaths paths{"taskbar.json", {}, {}};
    {
        TaskbarRuntime guarded(paths);
        Preferences enabled; enabled.enabled = true;
        Check(!guarded.Restore() && !guarded.Apply(enabled) && !guarded.Snapshot().worker &&
              guarded.Snapshot().state.error == Error::SystemEffectsDisabled,
               "verification without an explicit factory or production opt-in cannot create a system port");
    }
    {
        // Fail only this process's private listener window creation. No Explorer
        // window, registered system setting, or real taskbar port is involved.
        WNDCLASSEXW rejected{}; rejected.cbSize = sizeof(rejected);
        rejected.hInstance = GetModuleHandleW(nullptr);
        rejected.lpszClassName = L"LumeTaskbarWatch"; rejected.lpfnWndProc = RejectWatchWindow;
        Check(RegisterClassExW(&rejected) != 0, "install isolated watcher failure fixture");
        Files recoveryFiles; Shared recoveryState; recoveryState.autoHide = true;
        Record recovery; recovery.ownsAutoHide = true;
        RecordStore recoveryStore(recoveryFiles);
        Check(recoveryStore.Save("taskbar.json", recovery), "save pending recovery for failed watcher");
        TaskbarRuntime recoveryRuntime(paths, {}, &recoveryFiles,
            [&](const auto&, const auto& cancel) { return std::make_unique<Port>(recoveryState, cancel); });
        Check(recoveryRuntime.Restore() && Wait([&] { return !recoveryRuntime.Snapshot().worker; }),
              "failed watcher worker settles");
        Check(recoveryRuntime.Shutdown() && !recoveryState.autoHide &&
              !recoveryRuntime.Snapshot().state.pendingRecovery,
              "watcher startup failure cannot skip pending recovery while shutdown reports success");
        Check(recoveryStore.Save("taskbar.json", recovery), "save a second independent recovery failure");
        recoveryState.autoHide = true; recoveryState.failAutoHide = true;
        TaskbarRuntime failedRecovery(paths, {}, &recoveryFiles,
            [&](const auto&, const auto& cancel) { return std::make_unique<Port>(recoveryState, cancel); });
        Check(failedRecovery.Restore() && Wait([&] {
            const auto snapshot = failedRecovery.Snapshot();
            return snapshot.state.pendingRecovery && snapshot.state.error == Error::ApplyAutoHide && !snapshot.applying;
        }), "watch failure keeps failed recovery on its owning worker");
        Check(!failedRecovery.Shutdown() && recoveryState.autoHide &&
              failedRecovery.Snapshot().state.pendingRecovery,
              "failed final recovery returns false and preserves its durable journal");
        Check(recoveryStore.Load("taskbar.json").record.ownsAutoHide,
              "shutdown never clears an unrestored item from durable recovery");
        Check(UnregisterClassW(rejected.lpszClassName, rejected.hInstance) != 0,
              "remove isolated watcher failure fixture");
    }
    TaskbarRuntime runtime(paths, [&] { ++notifications; }, &files,
        [&](const auto&, const auto& cancel) { return std::make_unique<Port>(shared, cancel); });
    Check(runtime.Restore() && !runtime.Snapshot().worker && shared.acquired == 0, "disabled module allocates no worker or system lease");
    Preferences on; on.enabled = true;
    Resources baseline;
    for (int cycle = 0; cycle < 16; ++cycle) {
        Check(runtime.Apply(on), "request enable");
        Check(Wait([&] { return runtime.Snapshot().state.running; }), "enable resolves asynchronously");
        Preferences off = on; off.enabled = false;
        Check(runtime.Apply(off), "request disable");
        Check(Wait([&] { const auto s = runtime.Snapshot(); return !s.worker && !s.applying; }), "disable releases worker");
        Check(runtime.Restore(), "join the completed disabled worker before sampling");
        Check(!shared.autoHide && !runtime.Snapshot().state.pendingRecovery, "original state restored each cycle");
        // Start() joins the previous completed worker before starting the next.
        if (cycle == 3) baseline = Sample();
    }
    const auto final = Sample();
    std::cout << "taskbar isolated resources warm=" << baseline.handles << ',' << baseline.gdi << ','
              << baseline.user << ',' << baseline.threads << ',' << baseline.commit
              << " final=" << final.handles << ',' << final.gdi << ',' << final.user << ','
              << final.threads << ',' << final.commit << '\n';
    Check(final.handles <= baseline.handles + 1 && final.gdi <= baseline.gdi &&
          final.user <= baseline.user && final.threads <= baseline.threads &&
          final.commit <= baseline.commit + 1024 * 1024, "repeated enable/disable releases handles, GUI resources, threads and bounded memory");
    shared.blockStart = true;
    const auto begin = GetTickCount64();
    Check(runtime.Apply(on) && GetTickCount64() - begin < 100, "slow core startup never blocks requester");
    Check(Wait([&] { return shared.active == 1; }), "slow startup entered");
    Preferences off = on; off.enabled = false;
    Check(runtime.Apply(off) && Wait([&] { return !runtime.Snapshot().worker; }),
          "disable cancels unfinished startup and restores journal");
    shared.blockStart = false;
    const auto startsBeforeDemand = shared.starts.load();
    Check(runtime.RequireAutoHide(true) && Wait([&] { return shared.autoHide.load() && runtime.Snapshot().worker; }),
          "auto-hide demand hides the taskbar on a worker while the module is disabled");
    Check(shared.starts == startsBeforeDemand && !runtime.Snapshot().state.preferences.enabled,
          "demand never starts transparency or changes saved preferences");
    Check(runtime.RequireAutoHide(false) &&
          Wait([&] { const auto s = runtime.Snapshot(); return !shared.autoHide && !s.worker && !s.applying; }),
          "withdrawn demand restores the taskbar and releases the worker");
    Check(runtime.Apply(on) && Wait([&] { return runtime.Snapshot().state.running; }), "re-enable after cancelled start");
    Check(runtime.Shutdown() && !shared.autoHide && shared.active == 0, "shutdown restores and joins owned worker");
    Check(notifications > 0 && !runtime.Apply(on), "completion is reported and shutdown is terminal");
    {
        Files queuedFiles; Shared queuedState; queuedState.blockStart = true;
        TaskbarRuntime queued(paths, {}, &queuedFiles,
            [&](const auto&, const auto& cancel) { return std::make_unique<Port>(queuedState, cancel); });
        Check(queued.Apply(on) && Wait([&] { return queuedState.active == 1; }),
              "shutdown persistence fixture starts an in-flight apply");
        auto visible = on; visible.enabled = false; visible.transparentTaskbar = false;
        visible.autoHideTaskbar = false; visible.hideBehavior = HideBehavior::FullyHidden;
        Check(queued.Apply(visible) && queued.Shutdown(), "shutdown finishes the latest accepted preferences");
        RecordStore queuedStore(queuedFiles); const auto saved = queuedStore.Load("taskbar.json");
        Check(saved.state == LoadState::Loaded && saved.record.preferences == visible &&
              !HasRecovery(saved.record) && !queuedState.autoHide && queuedState.active == 0,
              "cancelled startup retains final unchecked settings and restores system ownership");
        TaskbarRuntime restarted(paths, {}, &queuedFiles,
            [&](const auto&, const auto& cancel) { return std::make_unique<Port>(queuedState, cancel); });
        Check(restarted.Snapshot().state.preferences == visible && restarted.Restore() &&
              !restarted.Snapshot().worker && restarted.Shutdown(), "queued false choices remain disabled next launch");
    }
    {
        Files classicFiles; std::atomic_int writes{0};
        TaskbarRuntime classic(paths, {}, &classicFiles,
            [&](const auto&, const auto&) { return std::make_unique<ClassicPort>(writes); });
        Preferences transparent; transparent.enabled = true; transparent.transparentTaskbar = true;
        transparent.autoHideTaskbar = false;
        Check(classic.Apply(transparent) && Wait([&] { return classic.Snapshot().state.running; }),
              "classic transparency starts");
        Check(Wait([&] { return writes >= 8; }, 2000),
              "classic accent is maintained without shell events");
        Check(classic.Shutdown(), "classic shutdown restores accent");
        const int stopped = writes;
        Sleep(250);
        Check(writes == stopped, "classic maintenance stops after shutdown");
    }
    std::cout << "taskbar runtime passed\n";
}
