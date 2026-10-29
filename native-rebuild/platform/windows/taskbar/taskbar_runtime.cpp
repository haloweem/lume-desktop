#include "platform/windows/taskbar/taskbar_runtime.h"
#include "platform/windows/taskbar/taskbar_port.h"
#include "platform/windows/taskbar/taskbar_watch.h"
#include "platform/windows/local_settings_file_store.h"
#include <algorithm>
#include <mutex>
#include <optional>
#include <thread>

namespace lume::platform::windows {
namespace {
constexpr ULONGLONG ClassicMaintainMs = 100;
bool Equal(const taskbar::Status& a, const taskbar::Status& b) {
    return a.preferences == b.preferences && a.mode == b.mode && a.error == b.error &&
        a.running == b.running && a.pendingRecovery == b.pendingRecovery && a.taskbars == b.taskbars &&
        a.active == b.active;
}
}
struct TaskbarRuntime::Impl {
    Impl(TaskbarPaths paths, Notify changed, settings::IFileStore* files, PortFactory factory)
        : notify(std::move(changed)), store(files ? *files : localFiles),
          port(factory ? factory(paths, cancel) : paths.allowSystemEffects ? std::make_unique<WindowsTaskbarPort>(paths, cancel) : nullptr),
          controller(port ? std::make_unique<taskbar::Controller>(store, paths.stateFile, *port) : nullptr) {
        if (controller) { status.state = controller->Snapshot(); status.loaded = controller->Loaded(); }
        else { status.state.error = paths.allowSystemEffects ? taskbar::Error::Unsupported : taskbar::Error::SystemEffectsDisabled; status.loaded = taskbar::LoadState::Failed; }
    }
    ~Impl() { Shutdown(); }
    void Publish(bool exiting = false, std::optional<taskbar::Error> error = {}) {
        Notify callback;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto next = controller->Snapshot(); if (error) next.error = *error;
            const bool applying = pending.has_value();
            if (!Equal(status.state, next) || status.applying != applying || exiting) {
                status.state = next; status.loaded = controller->Loaded();
                status.applying = applying; ++status.revision; callback = notify;
            }
            if (exiting) status.worker = false;
        }
        if (callback) callback();
    }
    bool Start(std::optional<taskbar::Preferences> next, std::optional<bool> demand = {}) {
        std::lock_guard<std::mutex> operation(operationMutex);
        std::thread finished;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopped || !controller || status.loaded == taskbar::LoadState::Failed) return false;
            if (status.worker) {
                if (next) { pending = *next; accepted = *next; }
                if (demand) pendingDemand = *demand;
                cancel = true; status.applying = true;
                SetEvent(requestEvent); return true;
            }
            if (worker.joinable()) finished = std::move(worker);
        }
        if (finished.joinable()) finished.join();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (next) pending = *next;
            if (demand) demandValue = *demand; // No worker runs, so the mutex owns it.
            if (!next && !demandValue && !status.state.preferences.enabled && !status.state.pendingRecovery) return true;
            requestEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!requestEvent) { status.state.error = taskbar::Error::Watch; return false; }
            cancel = false; status.worker = true; status.applying = true;
            try { worker = std::thread([this] { Run(); }); }
            catch (...) {
                CloseHandle(requestEvent); requestEvent = nullptr;
                status.worker = false; status.applying = false; status.state.error = taskbar::Error::Watch;
                return false;
            }
            if (next) accepted = *next;
        }
        return true;
    }
    void CloseEvent() {
        std::lock_guard<std::mutex> lock(mutex);
        if (requestEvent) { CloseHandle(requestEvent); requestEvent = nullptr; }
    }
    void Run() {
        TaskbarWatch watch;
        bool watching = false;
        unsigned failures = 0;
        ULONGLONG due = 0;
        bool settle = false;
        for (;;) {
            std::optional<taskbar::Preferences> request;
            bool stop = false, demand = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                stop = stopped; request = pending; pending.reset();
                if (pendingDemand) { demandValue = *pendingDemand; pendingDemand.reset(); }
                demand = demandValue;
                ResetEvent(requestEvent);
                if (!stop) cancel = false;
            }
            if (stop) {
                const bool restored = controller->Shutdown(request);
                watch.Stop(); CloseEvent(); Publish(true);
                controller.reset(); port.reset();
                std::lock_guard<std::mutex> lock(mutex);
                shutdownOk = restored; status.applying = false;
                return;
            }
            if (controller->AutoHideDemand() != demand) {
                controller->SetAutoHideDemand(demand);
                failures = 0; settle = false;
            }
            const auto before = controller->Snapshot();
            if (request) { failures = 0; settle = false; }
            const bool wantsActive = before.active || (request && request->enabled);
            bool watchFailed = false;
            if (!watching && (before.pendingRecovery || wantsActive)) {
                watching = watch.Start();
                watchFailed = !watching;
            }
            // Restoration must work even when hooks/window creation fail. Keep
            // this worker alive while recovery is pending so its mutex ownership
            // never moves to a different thread. Enabling requires a listener.
            const bool ok = watchFailed && wantsActive
                ? controller->Shutdown() : request ? controller->Apply(*request) : controller->Tick();
            if (!ok && controller->Snapshot().error == taskbar::Error::Cancelled) {
                // Superseded by a newer request: run it instead of reporting.
                std::lock_guard<std::mutex> lock(mutex);
                if (stopped || pending || pendingDemand) continue;
            }
            const std::optional<taskbar::Error> watchError = watchFailed && ok && wantsActive
                ? std::optional<taskbar::Error>{taskbar::Error::Watch} : std::nullopt;
            Publish(false, watchError);
            const auto state = controller->Snapshot();
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (stopped || pending || pendingDemand) continue;
                accepted.reset(); // Settled failures remain failures; exit only flushes unfinished requests.
                if ((!state.active || watchFailed) && !state.pendingRecovery) status.worker = false;
            }
            if ((!state.active || watchFailed) && !state.pendingRecovery) {
                watch.Stop(); CloseEvent(); Publish(true, watchError); return;
            }
            if (!ok) {
                // Six bounded retries, then only a new request or shell event can retry.
                ++failures;
                due = failures <= 6 ? GetTickCount64() + (std::min)(30000ULL, 1000ULL << (failures - 1)) : 0;
                settle = false;
            } else {
                failures = 0;
                due = settle ? 0 : GetTickCount64() + 120;
                settle = true; // One delayed reapply covers Explorer's own redraw.
                // Win10 Explorer rewrites the taskbar accent without a reliable
                // event (TranslucentTB blocks it with an Explorer hook). Keep the
                // classic accent maintained, as the 1.0.1 helper did.
                if (state.running && state.mode == taskbar::Mode::Classic && state.preferences.enabled &&
                    state.preferences.transparentTaskbar)
                    due = GetTickCount64() + ClassicMaintainMs;
                if (state.running && state.preferences.enabled && state.preferences.autoHideTaskbar &&
                    state.preferences.hideBehavior == taskbar::HideBehavior::FullyHidden)
                    due = GetTickCount64() + ClassicMaintainMs;
            }
            bool repeat = false;
            while (!repeat) {
                HANDLE handles[2]{requestEvent, nullptr}; DWORD count = 1;
                const auto signal = port->ExitSignal();
                if (state.running && signal) handles[count++] = reinterpret_cast<HANDLE>(signal);
                const auto now = GetTickCount64();
                const DWORD wait = due ? static_cast<DWORD>(due > now ? due - now : 0) : INFINITE;
                const auto result = MsgWaitForMultipleObjectsEx(count, handles, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (result == WAIT_OBJECT_0 || result == WAIT_TIMEOUT) { repeat = true; continue; }
                if (count == 2 && result == WAIT_OBJECT_0 + 1) { repeat = true; settle = false; continue; }
                if (result == WAIT_OBJECT_0 + count) {
                    MSG message{};
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&message); DispatchMessageW(&message);
                    }
                    if (watch.Consume()) {
                        failures = 0; settle = false;
                        const auto after = GetTickCount64();
                        if (!due || due > after + 50) due = after + 50;
                    }
                } else {
                    Publish(false, taskbar::Error::Watch);
                    WaitForSingleObject(requestEvent, INFINITE); repeat = true;
                }
            }
        }
    }
    bool Shutdown() {
        std::lock_guard<std::mutex> operation(operationMutex);
        std::thread finishing;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopped && !worker.joinable()) return shutdownOk;
            stopped = true; pending = accepted; cancel = true;
            if (requestEvent) SetEvent(requestEvent);
            if (worker.joinable()) finishing = std::move(worker);
        }
        if (finishing.joinable()) finishing.join();
        std::lock_guard<std::mutex> lock(mutex);
        status.worker = false; status.applying = false;
        return shutdownOk;
    }
    TaskbarRuntimeStatus Snapshot() const { std::lock_guard<std::mutex> lock(mutex); return status; }
    LocalSettingsFileStore localFiles;
    Notify notify;
    taskbar::RecordStore store;
    std::atomic_bool cancel{false};
    std::unique_ptr<taskbar::ITaskbarPort> port;
    std::unique_ptr<taskbar::Controller> controller;
    mutable std::mutex mutex;
    std::mutex operationMutex;
    std::optional<taskbar::Preferences> pending;
    std::optional<taskbar::Preferences> accepted;
    std::optional<bool> pendingDemand;
    bool demandValue = false;
    std::thread worker;
    HANDLE requestEvent = nullptr;
    TaskbarRuntimeStatus status;
    bool stopped = false, shutdownOk = true;
};
TaskbarRuntime::TaskbarRuntime(TaskbarPaths p, Notify n, settings::IFileStore* f, PortFactory factory)
    : impl_(std::make_unique<Impl>(std::move(p), std::move(n), f, std::move(factory))) {}
TaskbarRuntime::~TaskbarRuntime() = default;
bool TaskbarRuntime::Apply(taskbar::Preferences p) { return impl_->Start(p); }
bool TaskbarRuntime::Restore() {
    {
        // No worker runs before the first Start, so this thread may use the controller.
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->controller && !impl_->status.worker) impl_->controller->RepairOrphans();
    }
    return impl_->Start({});
}
bool TaskbarRuntime::RequireAutoHide(bool value) { return impl_->Start({}, value); }
bool TaskbarRuntime::Shutdown() { return impl_->Shutdown(); }
TaskbarRuntimeStatus TaskbarRuntime::Snapshot() const { return impl_->Snapshot(); }
}  // namespace lume::platform::windows
