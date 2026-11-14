#include "apps/shell/taskbar_session.h"
#include "platform/windows/taskbar/taskbar_desktop_recovery.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "vendor/nlohmann/json.hpp"
#include <atomic>

namespace lume::shell {
namespace {
constexpr UINT Changed = WM_APP + 141;
constexpr UINT RecoveryCheck = WM_APP + 142;
constexpr UINT_PTR RecoveryTimer = 1;
}
struct TaskbarSession::Impl {
    Impl(HINSTANCE instance, platform::windows::TaskbarPaths paths, Notify changed,
         settings::IFileStore* files, platform::windows::TaskbarRuntime::PortFactory factory)
        : notify(std::move(changed)), systemRecovery(paths.allowSystemEffects && !factory), runtime(std::move(paths), [this] {
            if (window && !posted.exchange(true) && !PostMessageW(window, Changed, 0, 0)) posted = false;
        }, files, std::move(factory)) {
        requested = runtime.Snapshot().state.preferences;
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = instance;
        wc.lpfnWndProc = WindowProc; wc.lpszClassName = L"LumeTaskbarSession";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        window = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, instance, this);
        if (window && systemRecovery && !recoveryOwner) {
            recoveryOwner = this;
            foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                RecoveryEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
            if (!foregroundHook) StopRecovery();
            else ArmRecovery();
        }
    }
    ~Impl() { notify = {}; Shutdown(); }
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == Changed) {
            self->posted = false;
            if (!self->stopped) {
                const auto status = self->runtime.Snapshot();
                if (!status.applying) self->requested = status.state.preferences;
                if (self->foregroundHook) self->ArmRecovery();
                // Worker status arrives in the background (Explorer restarts, recovery):
                // the page shows the error, but the manager is never brought forward.
                if (self->notify) self->notify(self->State(), false);
            }
            return 0;
        }
        if (self && message == WM_TIMER && w == RecoveryTimer) {
            self->RecoveryTick(true); return 0;
        }
        if (self && message == RecoveryCheck) {
            self->recoveryPosted = false;
            self->RecoveryTick(false); return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
    std::wstring State() const {
        const auto status = runtime.Snapshot();
        const auto& p = requested;
        const auto text = nlohmann::json{
            {"version", 1}, {"type", "taskbar-state"}, {"enabled", p.enabled},
            {"transparentTaskbar", p.transparentTaskbar}, {"autoHideTaskbar", p.autoHideTaskbar},
            {"hideBehavior", p.hideBehavior == taskbar::HideBehavior::FullyHidden ? "fully-hidden" : "auto-hide"},
            {"running", status.state.running}, {"applying", status.applying},
            {"pendingRecovery", status.state.pendingRecovery}, {"taskbars", status.state.taskbars},
            {"hideApplied", status.state.hideApplied}, {"effectiveAutoHide", status.state.effectiveAutoHide},
            {"fullyHidden", status.state.fullyHidden}, {"autoHideDemand", status.state.autoHideDemand},
            {"effectStatus", status.transparency.effectStatus}, {"stage", status.transparency.stage},
            {"taskbarType", status.transparency.taskbarType},
            {"hresult", status.transparency.hresult},
            {"registeredTaskbars", status.transparency.registeredTaskbars},
            {"backgrounds", status.transparency.backgrounds},
            {"appliedTaskbars", status.transparency.appliedTaskbars},
            {"mode", taskbar::ModeName(status.state.mode)},
            {"error", window ? taskbar::ErrorName(status.state.error) : "runtime"},
            {"loadStatus", status.loaded == taskbar::LoadState::Loaded ? "loaded" :
                           status.loaded == taskbar::LoadState::Failed ? "failed" : "fresh"},
            {"revision", status.revision}
        }.dump();
        return std::wstring(text.begin(), text.end());
    }
    bool Apply(taskbar::Preferences value) {
        if (stopped || !window || !runtime.Apply(value)) return false;
        requested = value; return true;
    }
    bool Shutdown() {
        stopped = true;
        StopRecovery();
        const bool ok = runtime.Shutdown();
        if (window) { DestroyWindow(window); window = nullptr; }
        return ok;
    }
    bool SuppressRecovery() const {
        const auto status = runtime.Snapshot();
        const auto& state = status.state;
        // Respect both committed runtime state and a newly accepted hide edit,
        // including an independent Dock demand before the worker applies it.
        return stopped || status.applying || state.fullyHidden || state.effectiveAutoHide || state.autoHideDemand ||
            (requested.enabled && requested.autoHideTaskbar);
    }
    void StopRecovery() {
        if (foregroundHook) { UnhookWinEvent(foregroundHook); foregroundHook = nullptr; }
        if (trayHook) { UnhookWinEvent(trayHook); trayHook = nullptr; }
        if (window) KillTimer(window, RecoveryTimer);
        recoveryArmed = false; settledDesktop = nullptr;
        recoveryPosted = false;
        desktopRecovery.Reset();
        trayProcess = 0;
        if (recoveryOwner == this) recoveryOwner = nullptr;
    }
    void ArmRecovery(DWORD eventTime = 0, DWORD eventKind = 0) {
        namespace recovery = platform::windows::taskbar_desktop_recovery;
        DWORD shell = 0;
        GetWindowThreadProcessId(GetShellWindow(), &shell);
        if (shell != trayProcess) {
            if (trayHook) { UnhookWinEvent(trayHook); trayHook = nullptr; }
            trayProcess = shell;
            if (shell) trayHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
                RecoveryEvent, shell, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        }
        const HWND desktop = SuppressRecovery() ? nullptr : recovery::CurrentDesktop();
        if (!desktop) {
            if (window) KillTimer(window, RecoveryTimer);
            desktopRecovery.Reset();
            recoveryArmed = false; recoveryPosted = false; settledDesktop = nullptr; return;
        }
        if (desktop != settledDesktop) {
            desktopRecovery.Reset();
            settledDesktop = desktop;
            KillTimer(window, RecoveryTimer); recoveryArmed = false;
        }
        // Coalesce foreground/tray events; Explorer's animation can emit many.
        if (!recoveryArmed) {
            recoveryChecks = 0;
            recoveryArmed = true;
        }
        // There is no intentional first-frame delay. A native event can arrive
        // after Explorer already obscured the tray; immediately re-read every
        // guard in the callback (or coalesce a non-event request). Our events do
        // not reset the bounded follow-up cadence.
        if (!recoveryPosted) {
            recoveryEventTime = eventTime; recoveryEventKind = eventKind;
            if (eventKind) {
                // WinEvent is already marshalled to this UI thread. Sampling
                // here avoids an extra queue turn; the Explorer write itself
                // remains asynchronous and only targets an observed occlusion.
                RecoveryTick(false);
            } else recoveryPosted = PostMessageW(window, RecoveryCheck, 0, 0) != FALSE;
            if (!recoveryPosted && !recoveryChecks) recoveryArmed = false;
        }
    }
    void RecoveryTick(bool timed) {
        namespace recovery = platform::windows::taskbar_desktop_recovery;
        if (recoveryChecking) return;
        if (!recoveryArmed || !settledDesktop || SuppressRecovery() || recovery::CurrentDesktop() != settledDesktop) {
            desktopRecovery.Reset();
            KillTimer(window, RecoveryTimer); recoveryArmed = false; settledDesktop = nullptr; return;
        }
        recoveryChecking = true;
        struct Finish { bool& active; ~Finish() { active = false; } } finish{recoveryChecking};
        const auto sampledAt = GetTickCount64();
        desktopRecovery.Tick(settledDesktop, SuppressRecovery());
        if (!recoveryChecks) platform::windows::diag::Log("taskbar-desktop",
            "first-check event=" + std::to_string(recoveryEventKind) +
            " eventTick=" + std::to_string(recoveryEventTime) +
            " sampledTick=" + std::to_string(sampledAt) +
            " finishedTick=" + std::to_string(GetTickCount64()));
        // A short bounded follow-up covers Explorer's delayed Win+D reorder.
        // No recurring timer remains when an application is in front or after
        // the transition; a later tray/foreground event arms a new check.
        if (!timed && recoveryChecks) return;
        if (++recoveryChecks >= 5) {
            KillTimer(window, RecoveryTimer); recoveryArmed = false;
        } else SetTimer(window, RecoveryTimer, recoveryChecks == 1 ? 300 : 500, nullptr);
    }
    static void CALLBACK RecoveryEvent(HWINEVENTHOOK, DWORD event, HWND target, LONG object, LONG child, DWORD, DWORD eventTime) {
        if (!recoveryOwner || recoveryOwner->stopped) return;
        if (event == EVENT_SYSTEM_FOREGROUND) {
            // Even the same Explorer HWND must settle again after an app exits.
            recoveryOwner->settledDesktop = nullptr;
            recoveryOwner->ArmRecovery(eventTime, event); return;
        }
        if (object != OBJID_WINDOW || child != CHILDID_SELF || !target) return;
        wchar_t name[64]{};
        DWORD targetProcess = 0; GetWindowThreadProcessId(target, &targetProcess);
        if (GetClassNameW(target, name, 64) &&
            (!wcscmp(name, L"Shell_TrayWnd") || !wcscmp(name, L"Shell_SecondaryTrayWnd")))
            recoveryOwner->ArmRecovery(eventTime, event);
        else if (targetProcess == recoveryOwner->trayProcess &&
            (!wcscmp(name, L"Progman") || !wcscmp(name, L"WorkerW")))
            recoveryOwner->ArmRecovery(eventTime, event);
    }
    Notify notify;
    HWND window = nullptr;
    std::atomic_bool posted{false};
    bool systemRecovery = false;
    platform::windows::TaskbarRuntime runtime;
    taskbar::Preferences requested;
    bool stopped = false;
    HWINEVENTHOOK foregroundHook = nullptr, trayHook = nullptr;
    DWORD trayProcess = 0;
    HWND settledDesktop = nullptr;
    unsigned recoveryChecks = 0;
    bool recoveryArmed = false, recoveryPosted = false, recoveryChecking = false;
    DWORD recoveryEventTime = 0, recoveryEventKind = 0;
    platform::windows::taskbar_desktop_recovery::Recovery desktopRecovery;
    static inline thread_local Impl* recoveryOwner = nullptr;
};
TaskbarSession::TaskbarSession(HINSTANCE i, platform::windows::TaskbarPaths p, Notify n,
                               settings::IFileStore* f, platform::windows::TaskbarRuntime::PortFactory factory)
    : impl_(std::make_unique<Impl>(i, std::move(p), std::move(n), f, std::move(factory))) {}
TaskbarSession::~TaskbarSession() = default;
bool TaskbarSession::Restore() { return impl_->window && !impl_->stopped && impl_->runtime.Restore(); }
bool TaskbarSession::Apply(taskbar::Preferences p) { return impl_->Apply(p); }
taskbar::Preferences TaskbarSession::Preferences() const { return impl_->requested; }
std::wstring TaskbarSession::State() const { return impl_->State(); }
bool TaskbarSession::Running() const { return impl_->runtime.Snapshot().state.running; }
bool TaskbarSession::HasWork() const {
    const auto snapshot = impl_->runtime.Snapshot();
    return snapshot.applying || snapshot.worker || snapshot.state.pendingRecovery || snapshot.state.running;
}
bool TaskbarSession::HasRecovery() const { return impl_->runtime.Snapshot().state.pendingRecovery; }
bool TaskbarSession::Shutdown() { return impl_->Shutdown(); }
}  // namespace lume::shell
