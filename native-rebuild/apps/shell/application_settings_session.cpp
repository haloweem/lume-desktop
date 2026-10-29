#include "apps/shell/application_settings_session.h"
#include "platform/windows/application_settings/application_settings_ports.h"
#include <condition_variable>
#include <algorithm>
#include <mutex>
#include <shobjidl.h>
#include <thread>
#include <utility>
#include <filesystem>

namespace lume::shell {
namespace {
constexpr UINT CompletedMessage = WM_APP + 0x351;
constexpr UINT_PTR CompletionTimer = 1;
constexpr wchar_t WindowClass[] = L"Lume.ApplicationSettings.Queue.v1";
using application_settings::Snapshot;
using ui::ApplicationSettingsAction;
struct Job {
    bool restore = false;
    ui::ApplicationSettingsPageCommand command;
    bool manual = true;
};
bool SavesPreferences(const Job& job) {
    return !job.restore && (job.command.action == ApplicationSettingsAction::Language ||
        job.command.action == ApplicationSettingsAction::BalancedMode ||
        job.command.action == ApplicationSettingsAction::Autostart ||
        job.command.action == ApplicationSettingsAction::SetWallpaperDirectory);
}
struct UpdateCompletion {
    bool manual = true;
    application_settings::ManifestResult result;
};
// The settings page only reads these; the picker runs on the UI thread and the
// chosen directory is applied by the worker like the other preference writes.
std::string ChooseFolder(HWND owner, const wchar_t* title, const std::string& initial) {
    std::string chosen;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)))
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(title);
        if (!initial.empty()) {
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(std::filesystem::u8path(initial).c_str(),
                                                      nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        if (SUCCEEDED(dialog->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    const int bytes = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                    if (bytes > 0) {
                        chosen.resize(static_cast<std::size_t>(bytes - 1));
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, chosen.data(), bytes, nullptr, nullptr);
                    }
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return chosen;
}
struct Completion {
    Snapshot snapshot;
    std::optional<UpdateCompletion> update;
};
struct UpdateCallbacks {
    bool active = true;
    ApplicationSettingsSession::UpdateObserver observer;
};
void SetSessionError(Snapshot& snapshot, const char* error) {
    if (snapshot.error == "balanced-rollback" || snapshot.error == "balanced-runtime") return;
    snapshot.error = error;
}
}
struct ApplicationSettingsSession::Impl {
    HINSTANCE instance;
    const DWORD owner = GetCurrentThreadId();
    Notify notify;
    std::shared_ptr<application_settings::IPreferencesFile> file;
    std::shared_ptr<application_settings::IStartupRegistration> startup;
    std::shared_ptr<application_settings::IUpdateTransport> updates;
    std::shared_ptr<application_settings::IUpdateInstaller> installer;
    std::wstring installDirectory;
    std::optional<application_settings::ManifestResult> available;
    application_settings::PreparedUpdate prepared;
    application_settings::Cancellation cancellation;
    application_settings::Cancellation preferenceCancellation;
    std::unique_ptr<application_settings::Service> service;
    mutable std::mutex mutex;
    std::condition_variable condition;
    Snapshot snapshot;
    std::optional<Job> pending;
    std::optional<Completion> completed;
    std::optional<Snapshot> progressSnapshot;
    std::shared_ptr<UpdateCallbacks> updateCallbacks = std::make_shared<UpdateCallbacks>();
    std::thread worker;
    HWND window = nullptr;
    bool workerRunning = false, active = false, activePreference = false, restoreRequested = false;
    // Resolved once per preference change; the page reads them from every state.
    std::string dataRoot, wallpaperRoot;
    std::function<std::string(const std::string&)> wallpaperResolver;
    std::function<bool(const std::string&)> setDataDirectory;
    void RefreshPaths() {
        wallpaperRoot = wallpaperResolver ? wallpaperResolver(snapshot.preferences.wallpaperDirectory)
                                          : std::string{};
    }
    Snapshot WithPaths(Snapshot value) const {
        value.dataDirectory = dataRoot;
        value.wallpaperDirectory = wallpaperRoot;
        return value;
    }

    Impl(HINSTANCE module, ApplicationSettingsOptions options, Notify callback, std::function<bool(bool)> apply)
        : instance(module ? module : GetModuleHandleW(nullptr)), notify(std::move(callback)) {
        namespace windows = platform::windows;
        dataRoot = std::move(options.dataDirectory);
        wallpaperResolver = std::move(options.wallpaperDirectory);
        setDataDirectory = std::move(options.setDataDirectory);
        file = options.testPorts.preferencesFile ? std::move(options.testPorts.preferencesFile)
            : windows::CreateApplicationSettingsFile(std::move(options.preferencesPath));
        startup = options.testPorts.startup ? std::move(options.testPorts.startup)
            : windows::CreateApplicationStartupRegistration(options.verification, windows::SystemStartupTaskHooks());
        updates = options.testPorts.updates ? std::move(options.testPorts.updates)
            : windows::CreateApplicationUpdateTransport(options.verification);
        installer = options.testPorts.installer ? std::move(options.testPorts.installer)
            : windows::CreateApplicationUpdateInstaller(options.verification);
        auto executable = options.testPorts.executablePath.empty() ? windows::ApplicationSettingsExecutablePath()
            : std::move(options.testPorts.executablePath);
        installDirectory = std::filesystem::path(executable).parent_path().wstring();
        service = std::make_unique<application_settings::Service>(*file, *startup, std::move(executable),
                                                                  std::move(apply), &preferenceCancellation, !options.verification);
        service->LoadPreferences();
        snapshot = service->State();
        RefreshPaths();
    }
    ~Impl() { JoinShutdown(); }

    static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if ((message == CompletedMessage || (message == WM_TIMER && wparam == CompletionTimer)) && self) {
            try { self->Drain(); } catch (...) {}
            return 0;
        }
        if (message == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    bool EnsureWorker() {
        if (worker.joinable()) return true;
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.hInstance = instance;
        cls.lpfnWndProc = WindowProcedure;
        cls.lpszClassName = WindowClass;
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        HWND hwnd = CreateWindowExW(0, WindowClass, L"", 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, nullptr, instance, this);
        if (!hwnd) return false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            window = hwnd;
            workerRunning = true;
        }
        try { worker = std::thread([this] { Run(); }); }
        catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                window = nullptr;
                workerRunning = false;
            }
            DestroyWindow(hwnd);
            return false;
        }
        return true;
    }
    void Drain() {
        Notify callback;
        std::wstring json;
        std::optional<UpdateCompletion> update;
        const auto callbacks = updateCallbacks;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancellation.cancelled || (!completed && !progressSnapshot)) return;
            const auto revision = snapshot.revision + 1;
            if (completed) {
                snapshot = std::move(completed->snapshot);
                update = std::move(completed->update);
                completed.reset();
                KillTimer(window, CompletionTimer);
                snapshot.busy = false;
            } else snapshot = std::move(*progressSnapshot);
            progressSnapshot.reset();
            snapshot.revision = revision;
            RefreshPaths();
            json = application_settings::SerializeSnapshot(WithPaths(snapshot));
            callback = notify;
        }
        // Only the shared callback gate may be touched after a callback destroys us.
        if (callback) {
            try { callback(json); } catch (...) {}
        }
        if (callbacks->active && update && callbacks->observer) {
            const auto observer = callbacks->observer;
            observer(update->manual, update->result);
        }
    }
    bool Enqueue(Job job) {
        if (GetCurrentThreadId() != owner || cancellation.cancelled) return false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (job.restore && restoreRequested) return true;
            if (active || pending || completed || snapshot.update.status == "installing") {
                if (job.restore) return false;
                if (job.command.action != ApplicationSettingsAction::CheckUpdate && snapshot.error.empty()) {
                    snapshot.error = "busy";
                    ++snapshot.revision;
                }
                return job.command.action != ApplicationSettingsAction::CheckUpdate;
            }
        }
        if (!EnsureWorker()) {
            std::lock_guard<std::mutex> lock(mutex);
            SetSessionError(snapshot, "worker-start");
            ++snapshot.revision;
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancellation.cancelled) return false;
            if (!SetTimer(window, CompletionTimer, 250, nullptr)) {
                SetSessionError(snapshot, "notification-failed");
                ++snapshot.revision;
                return false;
            }
            if (job.restore) restoreRequested = true;
            snapshot.busy = true;
            if (!job.restore && job.command.action == ApplicationSettingsAction::CheckUpdate)
                snapshot.update = {"checking", {}, 0};
            if (!job.restore && job.command.action == ApplicationSettingsAction::DownloadUpdate) {
                snapshot.update.status = "downloading"; snapshot.update.progress = 0;
            }
            if (!job.restore && job.command.action == ApplicationSettingsAction::InstallUpdate)
                snapshot.update.status = "installing";
            ++snapshot.revision;
            pending = std::move(job);
        }
        condition.notify_one();
        return true;
    }
    Completion Execute(const Job& job) {
        Completion completedJob;
        if (job.restore) service->Restore();
        else switch (job.command.action) {
        case ApplicationSettingsAction::Language: service->SetLanguage(job.command.language); break;
        case ApplicationSettingsAction::BalancedMode: service->SetBalancedMode(job.command.flag); break;
        case ApplicationSettingsAction::Autostart: service->SetAutostart(job.command.flag); break;
        case ApplicationSettingsAction::SetWallpaperDirectory:
            service->SetWallpaperDirectory(job.command.directory); break;
        case ApplicationSettingsAction::CheckUpdate: {
            if (!service->BeginCheck()) break;
            const auto response = updates->Fetch(cancellation);
            if (cancellation.cancelled) service->CancelCheck();
            else {
                auto result = !response.error.empty()
                    ? application_settings::ManifestResult{{"error", {}, 0}, response.error}
                    : application_settings::EvaluateManifest(response.body);
                service->FinishCheck(result);
                available = result.update.status == "available" && result.error.empty()
                    ? std::optional<application_settings::ManifestResult>(result) : std::nullopt;
                prepared = {};
                completedJob.update = UpdateCompletion{job.manual, std::move(result)};
            }
            break;
        }
        case ApplicationSettingsAction::DownloadUpdate:
        case ApplicationSettingsAction::InstallUpdate: {
            if (!available) { service->UnsupportedUpdate(); break; }
            const auto release = *available;
            if (job.command.action == ApplicationSettingsAction::DownloadUpdate) {
                service->UpdateProgress({"downloading", release.update.version, 0});
                prepared = installer->Download(release, cancellation, [&](int percent) {
                    if (cancellation.cancelled) return;
                    service->UpdateProgress({"downloading", release.update.version, std::clamp(percent, 0, 100)});
                    std::lock_guard<std::mutex> lock(mutex);
                    progressSnapshot = service->State();
                    if (window) PostMessageW(window, CompletedMessage, 0, 0);
                });
            }
            if (cancellation.cancelled) { service->UpdateProgress({"error", release.update.version, 0}, "update-cancelled"); break; }
            if (!prepared.error.empty() || prepared.path.empty()) {
                service->UpdateProgress({"error", release.update.version, 0}, prepared.error.empty() ? "update-cache" : prepared.error); break;
            }
            service->UpdateProgress({"installing", release.update.version, 100});
            const auto error = installer->Launch(prepared, release, installDirectory);
            service->UpdateProgress({error.empty() ? "installing" : "ready", release.update.version, 100}, error);
            break;
        }
        case ApplicationSettingsAction::Snapshot: break;
        }
        completedJob.snapshot = service->State();
        return completedJob;
    }
    void Run() noexcept {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [&] { return cancellation.cancelled || pending.has_value(); });
                if (cancellation.cancelled && (!pending || !SavesPreferences(*pending))) break;
                job = std::move(*pending);
                pending.reset();
                active = true;
                activePreference = SavesPreferences(job);
            }
            Completion result;
            try { result = Execute(job); }
            catch (...) {
                if (!job.restore && (job.command.action == ApplicationSettingsAction::DownloadUpdate ||
                    job.command.action == ApplicationSettingsAction::InstallUpdate))
                    service->UpdateProgress({"error", service->State().update.version, 0}, "update-installer");
                else service->CancelCheck();
                result.snapshot = service->State();
                result.snapshot.busy = false;
                SetSessionError(result.snapshot, "settings-internal");
                if (!job.restore && job.command.action == ApplicationSettingsAction::CheckUpdate)
                    result.update = UpdateCompletion{job.manual, {{"error", {}, 0}, "settings-internal"}};
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                active = false;
                activePreference = false;
                if (cancellation.cancelled) break;
                completed = std::move(result);
                // An active-job timer drains this same completion if posting fails.
                if (window) PostMessageW(window, CompletedMessage, 0, 0);
            }
        }
        std::lock_guard<std::mutex> lock(mutex);
        pending.reset();
        active = false;
        const auto revision = snapshot.revision + 1;
        snapshot = service->State();
        snapshot.revision = revision;
        snapshot.busy = false;
        workerRunning = false;
    }
    void Shutdown() {
        HWND hwnd = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cancellation.cancelled = true;
            // Finish the accepted local settings transaction (including its
            // rollback), but cancel startup restoration and network/update work.
            // Shutdown stays nonblocking; JoinShutdown owns the final join.
            preferenceCancellation.cancelled = !(activePreference || (pending && SavesPreferences(*pending)));
            notify = {};
            updateCallbacks->active = false;
            updateCallbacks->observer = {};
            if (pending && !SavesPreferences(*pending)) pending.reset();
            completed.reset();
            progressSnapshot.reset();
            snapshot.busy = workerRunning;
            if (GetCurrentThreadId() == owner) {
                hwnd = window;
                window = nullptr;
            }
        }
        condition.notify_all();
        if (hwnd) {
            KillTimer(hwnd, CompletionTimer);
            // No completion is posted after clearing window under the same lock.
            MSG message{};
            while (PeekMessageW(&message, hwnd, CompletedMessage, CompletedMessage, PM_REMOVE)) {}
            DestroyWindow(hwnd);
        }
    }
    void FlushPreferences() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!cancellation.cancelled || preferenceCancellation.cancelled) return;
        }
        if (!worker.joinable()) return;
        const HANDLE finished = worker.native_handle();
        // The wallpaper actor may synchronously notify an owned UI window.
        // Service sent messages while waiting, without reentering page commands.
        while (MsgWaitForMultipleObjectsEx(1, &finished, INFINITE, QS_SENDMESSAGE,
                   MWMO_INPUTAVAILABLE) != WAIT_OBJECT_0) {
            MSG sent{}; PeekMessageW(&sent, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
        }
        worker.join();
    }
    void JoinShutdown() {
        Shutdown();
        if (worker.joinable()) worker.join();
        std::lock_guard<std::mutex> lock(mutex);
        snapshot.busy = false;
    }
};
ApplicationSettingsSession::ApplicationSettingsSession(HINSTANCE instance, ApplicationSettingsOptions options,
    Notify notify, std::function<bool(bool)> apply)
    : impl_(std::make_unique<Impl>(instance, std::move(options), std::move(notify), std::move(apply))) {}
ApplicationSettingsSession::~ApplicationSettingsSession() = default;
bool ApplicationSettingsSession::Restore() { return impl_->Enqueue({true, {}}); }
bool ApplicationSettingsSession::RequestUpdateCheck(bool manual) {
    return impl_->Enqueue({false, {ApplicationSettingsAction::CheckUpdate}, manual});
}
void ApplicationSettingsSession::SetUpdateObserver(UpdateObserver observer) {
    if (GetCurrentThreadId() != impl_->owner || impl_->cancellation.cancelled) return;
    impl_->updateCallbacks->observer = std::move(observer);
}
std::optional<std::wstring> ApplicationSettingsSession::HandlePage(const ui::ApplicationSettingsPageCommand& command, HWND owner) {
    if (GetCurrentThreadId() != impl_->owner || impl_->cancellation.cancelled) return {};
    switch (command.action) {
    case ApplicationSettingsAction::Snapshot: return State();
    case ApplicationSettingsAction::ChooseWallpaperDirectory: {
        const auto chosen = ChooseFolder(owner ? owner : impl_->window, L"选择壁纸下载目录", impl_->wallpaperRoot);
        if (chosen.empty()) return State();
        ui::ApplicationSettingsPageCommand write;
        write.action = ApplicationSettingsAction::SetWallpaperDirectory;
        write.directory = chosen;
        impl_->Enqueue({false, write});
        return State();
    }
    case ApplicationSettingsAction::ChooseDataDirectory: {
        const auto chosen = ChooseFolder(owner ? owner : impl_->window, L"选择用户数据目录", impl_->dataRoot);
        if (chosen.empty() || !impl_->setDataDirectory) return State();
        // Persisted now, moved at the next launch; the current folder stays in use.
        impl_->setDataDirectory(chosen);
        return State();
    }
    case ApplicationSettingsAction::Language:
    case ApplicationSettingsAction::BalancedMode:
    case ApplicationSettingsAction::Autostart:
    case ApplicationSettingsAction::CheckUpdate:
    case ApplicationSettingsAction::DownloadUpdate:
    case ApplicationSettingsAction::InstallUpdate:
        impl_->Enqueue({false, command});
        return State();
    }
    return {};
}
std::wstring ApplicationSettingsSession::State() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return application_settings::SerializeSnapshot(impl_->WithPaths(impl_->snapshot));
}
application_settings::Preferences ApplicationSettingsSession::Preferences() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot.preferences;
}
bool ApplicationSettingsSession::BalancedMode() const { return Preferences().balancedMode; }
void ApplicationSettingsSession::Shutdown() { impl_->Shutdown(); }
void ApplicationSettingsSession::FlushPreferences() { impl_->FlushPreferences(); }
void ApplicationSettingsSession::JoinShutdown() { impl_->JoinShutdown(); }
bool ApplicationSettingsSession::HasWork() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->cancellation.cancelled ? impl_->workerRunning
        : impl_->active || impl_->pending.has_value() || impl_->completed.has_value();
}
}
