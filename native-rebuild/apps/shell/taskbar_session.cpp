#include "apps/shell/taskbar_session.h"
#include "vendor/nlohmann/json.hpp"
#include <atomic>

namespace lume::shell {
namespace { constexpr UINT Changed = WM_APP + 141; }
struct TaskbarSession::Impl {
    Impl(HINSTANCE instance, platform::windows::TaskbarPaths paths, Notify changed,
         settings::IFileStore* files, platform::windows::TaskbarRuntime::PortFactory factory)
        : notify(std::move(changed)), runtime(std::move(paths), [this] {
            if (window && !posted.exchange(true) && !PostMessageW(window, Changed, 0, 0)) posted = false;
        }, files, std::move(factory)) {
        requested = runtime.Snapshot().state.preferences;
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = instance;
        wc.lpfnWndProc = WindowProc; wc.lpszClassName = L"LumeTaskbarSession";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        window = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, instance, this);
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
            if (!self->stopped && self->notify) {
                const auto status = self->runtime.Snapshot();
                if (!status.applying) self->requested = status.state.preferences;
                // Worker status arrives in the background (Explorer restarts, recovery):
                // the page shows the error, but the manager is never brought forward.
                self->notify(self->State(), false);
            }
            return 0;
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
        const bool ok = runtime.Shutdown();
        if (window) { DestroyWindow(window); window = nullptr; }
        return ok;
    }
    Notify notify;
    HWND window = nullptr;
    std::atomic_bool posted{false};
    platform::windows::TaskbarRuntime runtime;
    taskbar::Preferences requested;
    bool stopped = false;
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
