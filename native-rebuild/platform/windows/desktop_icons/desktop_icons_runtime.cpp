#include "platform/windows/desktop_icons/desktop_icons_runtime.h"
#include "core/desktop_icons/desktop_icons_input.h"
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <future>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>

namespace lume::platform::windows {
namespace {
using desktop_icons::Change;
using desktop_icons::Preferences;
constexpr ULONGLONG Never = std::numeric_limits<ULONGLONG>::max();
std::atomic_uint activeWorkers{0};
bool Same(const Preferences& a, const Preferences& b) {
    return a.enabled == b.enabled && a.enableAutoHide == b.enableAutoHide &&
        a.autoHideDelay == b.autoHideDelay && a.showIconsOnAnyInput == b.showIconsOnAnyInput &&
        a.manuallyHidden == b.manuallyHidden;
}
bool Same(const DesktopIconsSnapshot& a, const DesktopIconsSnapshot& b) {
    return Same(a.preferences, b.preferences) && a.running == b.running &&
        a.desktops == b.desktops && a.autoHidden == b.autoHidden &&
        a.inputDevices == b.inputDevices && a.eventHooks == b.eventHooks &&
        a.pendingRestores == b.pendingRestores &&
        a.scheduled == b.scheduled && a.error == b.error;
}
std::optional<std::vector<RAWINPUTDEVICE>> RawDevices() {
    UINT count = 0;
    if (GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT(-1) ||
        count > 128) return {};
    std::vector<RAWINPUTDEVICE> devices(count);
    if (count) {
        const UINT result = GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE));
        if (result == UINT(-1)) return {};
        devices.resize(result);
    }
    return devices;
}
bool PointerOrKeyboard(const RAWINPUTDEVICE& device) {
    return device.usUsagePage == 1 && (device.usUsage == 2 || device.usUsage == 6);
}
}

struct DesktopIconsRuntime::Impl {
    struct Desktop {
        DesktopIconsTarget target;
        desktop_icons::Policy policy;
        bool touched = false;
    };
    struct Input {
        bool keyboard = false, leftDown = false, action = false;
        POINT point{};
        std::uint32_t time = 0;
    };
    struct Command {
        Preferences preferences;
        std::promise<bool> completed;
    };
    explicit Impl(Discover discover) : discover(std::move(discover)) {}
    ~Impl() { Stop(); }
    DesktopIconsSnapshot Snapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        return snapshot;
    }
    bool Cancelled() const { return cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0; }
    void Publish() {
        DesktopIconsSnapshot next;
        next.preferences = preferences; next.running = running;
        next.desktops = static_cast<unsigned>(desktops.size());
        next.inputDevices = inputDevices; next.eventHooks = static_cast<unsigned>(hooks.size());
        next.pendingRestores = static_cast<unsigned>(pendingRestores.size());
        next.scheduled = due != Never || rescanDue != Never; next.error = error;
        for (const auto& desktop : desktops) if (desktop.policy.AutoHidden()) ++next.autoHidden;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            changed = !Same(snapshot, next);
            snapshot = next;
        }
        if (changed && callbacks.changed) {
            try { callbacks.changed(); } catch (...) { /* Never unwind across a window callback. */ }
        }
    }
    bool Save(const Preferences& next) {
        try { return callbacks.save && callbacks.save(next); } catch (...) { return false; }
    }
    bool AcquireInput() {
        const auto existing = RawDevices();
        if (!existing) { error = DesktopIconsError::Startup; return false; }
        for (const auto& device : *existing) {
            if (PointerOrKeyboard(device)) { error = DesktopIconsError::InputBusy; return false; }
        }
        const RAWINPUTDEVICE requested[]{{1, 2, RIDEV_INPUTSINK, window}, {1, 6, RIDEV_INPUTSINK, window}};
        if (!RegisterRawInputDevices(requested, 2, sizeof(RAWINPUTDEVICE))) {
            error = DesktopIconsError::Startup; return false;
        }
        inputDevices = 2;
        return true;
    }
    bool ReleaseInput() {
        const auto devices = RawDevices();
        bool ok = devices.has_value();
        if (devices) for (auto device : *devices) {
            // Another module may have replaced registration after startup.
            // Remove only registrations still owned by this worker window.
            if (PointerOrKeyboard(device) && device.hwndTarget == window) {
                device.dwFlags = RIDEV_REMOVE; device.hwndTarget = nullptr;
                if (!RegisterRawInputDevices(&device, 1, sizeof(device))) ok = false;
            }
        }
        inputDevices = 0;
        return ok;
    }
    void ReleaseHooks() {
        for (auto hook : hooks) UnhookWinEvent(hook);
        hooks.clear();
    }
    bool WatchTargets() {
        ReleaseHooks();
        std::vector<std::pair<DWORD, DWORD>> owners;
        for (const auto& desktop : desktops) {
            const auto owner = std::make_pair(desktop.target.process, desktop.target.thread);
            if (std::find(owners.begin(), owners.end(), owner) != owners.end()) continue;
            const auto hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE, nullptr,
                ObjectEvent, owner.first, owner.second, WINEVENT_OUTOFCONTEXT);
            if (!hook) { ReleaseHooks(); error = DesktopIconsError::Startup; return false; }
            hooks.push_back(hook); owners.push_back(owner);
        }
        return true;
    }
    bool Apply(Desktop& desktop, Change change, bool saveManual = false) {
        if (change == Change::None) return true;
        const auto before = ObserveDesktopIcons(desktop.target);
        const auto beforePolicy = desktop.policy;
        desktop.touched = true; // Also undo a queued request if acknowledgement fails.
        const auto result = SetDesktopIconsVisible(desktop.target, desktop_icons::MakesVisible(change), cancel);
        if (result != DesktopVisibilityResult::Applied) {
            if (!Cancelled()) error = DesktopIconsError::Visibility;
            retryAfter = GetTickCount64() + 1000;
            return false;
        }
        desktop.policy.Confirm(change, ObserveDesktopIcons(desktop.target));
        if (saveManual) {
            auto next = preferences;
            next.manuallyHidden = desktop.policy.Settings().manuallyHidden;
            if (!Save(next)) {
                const bool restored = before.available &&
                    SetDesktopIconsVisible(desktop.target, before.visible, cancel) == DesktopVisibilityResult::Applied;
                if (restored) desktop.policy = beforePolicy;
                error = restored ? DesktopIconsError::Save : DesktopIconsError::Rollback;
                return false;
            }
            preferences = next;
            for (auto& other : desktops) other.policy.Configure(preferences);
            error = DesktopIconsError::None;
        } else if (error == DesktopIconsError::Visibility) error = DesktopIconsError::None;
        return true;
    }
    bool Restore(Desktop& desktop) {
        if (!desktop.touched || !ValidDesktopIconsTarget(desktop.target)) return true;
        return SetDesktopIconsVisible(desktop.target, true) == DesktopVisibilityResult::Applied;
    }
    bool Rescan() {
        rescan = false;
        auto found = discover();
        if (found.size() > 8) { error = DesktopIconsError::Startup; return false; }
        std::vector<Desktop> next;
        bool different = false;
        for (const auto& target : found) {
            if (!ValidDesktopIconsTarget(target)) continue;
            if (std::any_of(next.begin(), next.end(), [&](const auto& item) { return item.target.list == target.list; })) continue;
            auto previous = std::find_if(desktops.begin(), desktops.end(), [&](const auto& item) {
                return item.target.shell == target.shell && item.target.list == target.list &&
                       item.target.process == target.process && item.target.thread == target.thread;
            });
            if (previous != desktops.end()) next.push_back(*previous);
            else {
                different = true;
                next.push_back({target, desktop_icons::Policy(preferences), false});
            }
        }
        for (auto& prior : desktops) {
            const bool remains = std::any_of(next.begin(), next.end(), [&](const auto& item) {
                return item.target.list == prior.target.list && item.target.process == prior.target.process &&
                       item.target.thread == prior.target.thread && item.target.shell == prior.target.shell;
            });
            if (!remains) {
                different = true;
                if (!Restore(prior)) { error = DesktopIconsError::Rollback; return false; }
            }
        }
        desktops = std::move(next);
        if ((different || (hooks.empty() && !desktops.empty())) && !WatchTargets()) return false;
        for (auto& desktop : desktops) {
            // Only apply a persisted manual state when the target is first attached.
            if (!desktop.touched && !Apply(desktop, desktop.policy.Attached(ObserveDesktopIcons(desktop.target))))
                return false;
        }
        Refresh();
        return true;
    }
    void Refresh() {
        const auto now = GetTickCount64();
        due = desktops.empty() ? now + 1000 : Never;
        for (auto& desktop : desktops) {
            auto observation = ObserveDesktopIcons(desktop.target);
            if (!observation.available) { rescan = true; due = std::min(due, now + 1000); continue; }
            if (now >= retryAfter) {
                const auto change = desktop.policy.Tick(observation);
                if (change != Change::None) {
                    Apply(desktop, change);
                    observation = ObserveDesktopIcons(desktop.target);
                }
            }
            if (const auto delay = desktop.policy.NextWake(observation))
                due = std::min(due, std::max(now + *delay, retryAfter));
        }
        Publish();
    }
    void RawInput(HRAWINPUT handle) {
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(handle, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
        const bool anyAutoHidden = std::any_of(desktops.begin(), desktops.end(),
            [](const auto& desktop) { return desktop.policy.AutoHidden(); });
        Input input;
        if (raw.header.dwType == RIM_TYPEKEYBOARD) {
            if (!anyAutoHidden || !preferences.showIconsOnAnyInput) return;
            input.keyboard = true;
        } else if (raw.header.dwType == RIM_TYPEMOUSE) {
            input.leftDown = (raw.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0;
            input.action = raw.data.mouse.usButtonFlags != 0;
            if (!input.leftDown && (!anyAutoHidden || !preferences.showIconsOnAnyInput)) return;
            if (!GetCursorPos(&input.point)) return;
        } else return;
        input.time = static_cast<std::uint32_t>(GetMessageTime());
        if (inputCount == inputs.size()) { inputCount = 0; clicks.Reset(); return; }
        inputs[inputCount++] = input;
    }
    void ProcessInputs() {
        const auto pending = inputs; const auto count = inputCount; inputCount = 0;
        bool changed = false;
        for (std::size_t i = 0; i < count && !Cancelled(); ++i) {
            const auto& input = pending[i];
            for (auto& desktop : desktops) {
                if (!desktop.policy.AutoHidden()) continue;
                auto observation = ObserveDesktopIcons(desktop.target);
                if (!input.keyboard) {
                    observation.mouseX = input.point.x; observation.mouseY = input.point.y;
                }
                const auto change = input.keyboard ? desktop.policy.Tick(observation) :
                    desktop.policy.DesktopInput(observation, input.action);
                if (change != Change::None) { Apply(desktop, change); changed = true; }
            }
            if (input.leftDown) {
                Desktop* clicked = nullptr;
                for (auto& desktop : desktops) {
                    if (HitDesktopIcons(desktop.target, input.point) == DesktopHit::Blank) {
                        clicked = &desktop; break;
                    }
                }
                const auto identity = clicked ? reinterpret_cast<std::uintptr_t>(clicked->target.shell) : 0;
                if (clicks.Press(identity, clicked != nullptr, input.point.x, input.point.y, input.time,
                                 GetDoubleClickTime(), GetSystemMetrics(SM_CXDOUBLECLK) / 2,
                                 GetSystemMetrics(SM_CYDOUBLECLK) / 2)) {
                    Apply(*clicked, clicked->policy.ManualToggle(ObserveDesktopIcons(clicked->target)), true);
                    changed = true;
                }
            }
        }
        if (changed) Refresh();
    }
    void ProcessCommand() {
        std::shared_ptr<Command> pending;
        { std::lock_guard<std::mutex> lock(mutex); pending = std::move(command); }
        if (!pending) return;
        auto next = pending->preferences;
        // A page setting change cannot overwrite a newer manual click saved by the worker.
        next.manuallyHidden = preferences.manuallyHidden;
        bool saved = false;
        if (!Cancelled() && next.enabled && desktop_icons::Valid(next)) {
            if (Save(next)) {
                preferences = next;
                for (auto& desktop : desktops) desktop.policy.Configure(next);
                error = DesktopIconsError::None; saved = true;
                // Report persistence independently of the visibility operation.
                // Refresh may publish a visibility error with the new preferences.
                Refresh();
            } else error = DesktopIconsError::Save;
        }
        Publish();
        pending->completed.set_value(saved);
    }
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            if (message == WM_INPUT) self->RawInput(reinterpret_cast<HRAWINPUT>(l));
            else if (message == self->taskbarCreated || message == WM_DISPLAYCHANGE ||
                     message == WM_SETTINGCHANGE) self->rescan = true;
        }
        // DefWindowProc performs the required raw-input cleanup for foreground input.
        return DefWindowProcW(hwnd, message, w, l);
    }
    static void CALLBACK ObjectEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG object, LONG child, DWORD, DWORD) {
        if (!threadState || object != OBJID_WINDOW || child != CHILDID_SELF || !hwnd) return;
        for (const auto& desktop : threadState->desktops) {
            if (hwnd == desktop.target.list || hwnd == desktop.target.shell ||
                GetParent(hwnd) == desktop.target.shell) {
                threadState->rescan = true; break;
            }
        }
    }
    void Run(std::promise<bool> ready, bool persistEnable) {
        ++activeWorkers;
        threadId = GetCurrentThreadId();
        threadState = this;
        bool initialized = false, cancellationEnabled = false, signalled = false;
        try {
            initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
            cancellationEnabled = initialized && SUCCEEDED(CoEnableCallCancellation(nullptr));
            if (!initialized || !cancellationEnabled) throw DesktopIconsError::Startup;
            WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
            wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = WindowProc;
            wc.lpszClassName = L"LumeDesktopIconsObserver";
            if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                throw DesktopIconsError::Startup;
            taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
            if (!taskbarCreated) throw DesktopIconsError::Startup;
            // Hidden top-level window receives Explorer's restart broadcast.
            window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP,
                                      0, 0, 0, 0, nullptr, nullptr, wc.hInstance, this);
            if (!window) throw DesktopIconsError::Startup;
            if (!AcquireInput() || !Rescan()) throw error;
            if (persistEnable && !Save(preferences)) throw DesktopIconsError::Save;
            running = true; Refresh();
            ready.set_value(true); signalled = true;
            const HANDLE waits[]{cancel, wake};
            while (!Cancelled()) {
                ProcessCommand();
                if (rescan || GetTickCount64() >= rescanDue) {
                    if (!Rescan()) {
                        // Discovery/restoration failure must retry even when a
                        // still-valid, manually hidden target has no policy timer.
                        rescanError = error;
                        rescanDue = GetTickCount64() + 1000;
                    } else {
                        rescanDue = Never;
                        if (rescanError != DesktopIconsError::None && error == rescanError)
                            error = DesktopIconsError::None;
                        rescanError = DesktopIconsError::None;
                    }
                    Publish();
                }
                if (GetTickCount64() >= due) {
                    if (desktops.empty()) rescan = true;
                    else Refresh();
                }
                MSG message{};
                unsigned processed = 0;
                while (processed++ < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message == WM_QUIT) { SetEvent(cancel); break; }
                    TranslateMessage(&message); DispatchMessageW(&message);
                }
                ProcessInputs();
                if (rescan || inputCount) continue;
                const auto now = GetTickCount64();
                const auto nextDue = std::min(due, rescanDue);
                const DWORD delay = nextDue == Never ? INFINITE :
                    static_cast<DWORD>(std::min<ULONGLONG>(nextDue > now ? nextDue - now : 0, 60000));
                if (MsgWaitForMultipleObjectsEx(2, waits, delay, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                    throw DesktopIconsError::Startup;
            }
        } catch (DesktopIconsError failure) { error = failure; }
        catch (...) { error = DesktopIconsError::Startup; }
        ReleaseHooks();
        bool restored = true;
        for (auto& desktop : desktops) if (!Restore(desktop)) {
            restored = false;
            pendingRestores.push_back(desktop.target);
        }
        desktops.clear();
        if (window) {
            if (!ReleaseInput()) restored = false;
            DestroyWindow(window); window = nullptr;
        }
        if (!restored) error = DesktopIconsError::Rollback;
        restoreOk = restored;
        due = rescanDue = Never; running = false; Publish();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (command) { command->completed.set_value(false); command.reset(); }
        }
        if (cancellationEnabled) CoDisableCallCancellation(nullptr);
        if (initialized) CoUninitialize();
        threadState = nullptr; threadId = 0; --activeWorkers;
        if (!signalled) ready.set_value(false);
    }
    bool Start(const Preferences& initial, DesktopIconsCallbacks nextCallbacks, bool persistEnable) {
        if (!Stop()) return false;
        if (!initial.enabled || !desktop_icons::Valid(initial) || !nextCallbacks.save || !discover) return false;
        preferences = initial; callbacks = std::move(nextCallbacks);
        error = rescanError = DesktopIconsError::None;
        retryAfter = 0; due = rescanDue = Never; rescan = false;
        inputCount = 0; clicks.Reset(); restoreOk = true;
        cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!cancel || !wake) {
            error = DesktopIconsError::Startup; Publish(); Stop(); return false;
        }
        std::promise<bool> ready; auto result = ready.get_future();
        try { worker = std::thread([this, ready = std::move(ready), persistEnable]() mutable { Run(std::move(ready), persistEnable); }); }
        catch (...) { error = DesktopIconsError::Startup; Publish(); Stop(); return false; }
        const bool started = result.get();
        if (!started) Stop();
        return started;
    }
    bool Configure(const Preferences& next) {
        if (!next.enabled || !desktop_icons::Valid(next) || !worker.joinable()) return false;
        auto request = std::make_shared<Command>();
        request->preferences = next;
        auto result = request->completed.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!snapshot.running || command || Cancelled()) return false;
            command = std::move(request);
        }
        SetEvent(wake);
        return result.get();
    }
    bool Stop() {
        const bool joining = worker.joinable();
        if (cancel) SetEvent(cancel);
        const DWORD id = threadId.load();
        if (id) CoCancelCall(id, 0);
        if (worker.joinable()) worker.join();
        if (cancel) { CloseHandle(cancel); cancel = nullptr; }
        if (wake) { CloseHandle(wake); wake = nullptr; }
        // The first Stop reports its actual failure. A later call rechecks the
        // same target identities, rather than forgetting recovery after join.
        if (!joining && !pendingRestores.empty()) {
            pendingRestores.erase(std::remove_if(pendingRestores.begin(), pendingRestores.end(),
                [](const auto& target) {
                    return !ValidDesktopIconsTarget(target) ||
                        SetDesktopIconsVisible(target, true) == DesktopVisibilityResult::Applied;
                }), pendingRestores.end());
            restoreOk = pendingRestores.empty();
            if (restoreOk && error == DesktopIconsError::Rollback) error = DesktopIconsError::None;
            Publish();
        }
        return restoreOk;
    }
    Discover discover;
    mutable std::mutex mutex;
    DesktopIconsSnapshot snapshot;
    std::shared_ptr<Command> command;
    std::thread worker;
    std::atomic<DWORD> threadId{0};
    HANDLE cancel = nullptr, wake = nullptr;
    Preferences preferences;
    DesktopIconsCallbacks callbacks;
    std::vector<Desktop> desktops;
    std::vector<DesktopIconsTarget> pendingRestores;
    std::vector<HWINEVENTHOOK> hooks;
    HWND window = nullptr;
    UINT taskbarCreated = 0;
    unsigned inputDevices = 0;
    bool running = false, rescan = false, restoreOk = true;
    DesktopIconsError error = DesktopIconsError::None;
    DesktopIconsError rescanError = DesktopIconsError::None;
    ULONGLONG due = Never, rescanDue = Never, retryAfter = 0;
    desktop_icons::DoubleClick clicks;
    std::array<Input, 16> inputs{};
    std::size_t inputCount = 0;
    static thread_local Impl* threadState;
};
thread_local DesktopIconsRuntime::Impl* DesktopIconsRuntime::Impl::threadState = nullptr;
DesktopIconsRuntime::DesktopIconsRuntime(Discover discover) : impl_(std::make_unique<Impl>(std::move(discover))) {}
DesktopIconsRuntime::~DesktopIconsRuntime() = default;
bool DesktopIconsRuntime::Start(const Preferences& p, DesktopIconsCallbacks c, bool persist) { return impl_->Start(p, std::move(c), persist); }
bool DesktopIconsRuntime::Configure(const Preferences& p) { return impl_->Configure(p); }
bool DesktopIconsRuntime::Stop() { return impl_->Stop(); }
DesktopIconsSnapshot DesktopIconsRuntime::Snapshot() const { return impl_->Snapshot(); }
unsigned DesktopIconsRuntime::ActiveWorkers() { return activeWorkers.load(); }
}
