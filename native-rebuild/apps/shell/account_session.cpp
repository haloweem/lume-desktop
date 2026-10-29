#include "apps/shell/account_session.h"
#include "core/account/backend.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include "platform/windows/account/avatar.h"
#include "platform/windows/account/credentials.h"
#include "platform/windows/account/http.h"
#include "platform/windows/account/protocol_registration.h"
#include <shellapi.h>
#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace lume::shell {
using namespace account;
namespace native = platform::windows::account;
namespace {
constexpr UINT ResultMessage = WM_APP + 0x3a1;
constexpr UINT_PTR TickTimer = 1;
constexpr wchar_t WindowClass[] = L"Lume.Native.AccountSession.v1";
enum class Operation { Restore, Login, Refresh, Name, Avatar, Device, Logout, WallpaperStats, WallpaperLike, WallpaperDownload, WallpaperAuthorize };
bool SelectedOperation(Operation operation) {
    return operation == Operation::WallpaperStats || operation == Operation::WallpaperLike;
}
bool WallpaperOperation(Operation operation) {
    return SelectedOperation(operation) || operation == Operation::WallpaperDownload || operation == Operation::WallpaperAuthorize;
}
struct Task {
    Operation operation = Operation::Restore;
    std::uint64_t generation = 0;
    std::shared_ptr<Cancellation> cancel;
    std::shared_ptr<Cancellation> selectionCancel;
    Grant grant;
    std::string value;
    std::wstring path;
    Error completionError = Error::LocalLogout;
    std::uint64_t selection = 0;
    std::string source;
    bool liked = false;
    std::uint64_t request = 0;
};
struct Result {
    Operation operation = Operation::Restore;
    std::uint64_t generation = 0;
    Error error = Error::None;
    std::optional<Profile> profile;
    std::uint64_t selection = 0;
    std::string id;
    std::optional<WallpaperEngagement> engagement;
    std::uint64_t request = 0;
};
bool SameProfile(const Profile& left, const Profile& right) {
    return left.id == right.id && left.displayName == right.displayName && left.avatarUrl == right.avatarUrl &&
        left.vip == right.vip && left.vipExpiresAt == right.vipExpiresAt;
}
bool Retryable(Error error) {
    return error == Error::Network || error == Error::Service || error == Error::InvalidResponse;
}
struct Worker {
    std::mutex mutex;
    std::condition_variable cv, finished;
    std::deque<Task> tasks;
    std::deque<Result> results;
    bool stop = false, done = false;
    HWND window = nullptr;
    std::shared_ptr<Cancellation> running;
    std::shared_ptr<ITransport> transport;
    std::shared_ptr<ICredentials> credentials;
    Clock clock;
};
void Run(const std::shared_ptr<Worker>& shared) {
    Backend backend(shared->transport, shared->credentials, shared->clock);
    for (;;) {
        Task task;
        {
            std::unique_lock lock(shared->mutex);
            shared->cv.wait(lock, [&] { return shared->stop || !shared->tasks.empty(); });
            if (shared->stop) break;
            task = std::move(shared->tasks.front()); shared->tasks.pop_front();
            shared->running = task.cancel;
        }
        Result result; result.operation = task.operation; result.generation = task.generation;
        result.selection = task.selection; result.id = task.value;
        result.request = task.request;
        try {
            task.cancel->Check();
            switch (task.operation) {
            case Operation::Restore: backend.Restore(*task.cancel); break;
            case Operation::Login: backend.Login(task.grant, *task.cancel); break;
            case Operation::Refresh: backend.Refresh(*task.cancel); break;
            case Operation::Name: backend.UpdateName(task.value, *task.cancel); break;
            case Operation::Avatar: backend.UploadAvatar(native::ReadAvatarFile(task.path, *task.cancel), *task.cancel); break;
            case Operation::Device: backend.CheckDevice(*task.cancel); break;
            case Operation::Logout: backend.Logout(); result.error = task.completionError; break;
            case Operation::WallpaperStats:
                result.engagement = backend.WallpaperStats(task.value, *task.selectionCancel, task.cancel.get()); break;
            case Operation::WallpaperLike:
                result.engagement = backend.SetWallpaperLike(task.value, task.liked, *task.selectionCancel, task.cancel.get()); break;
            case Operation::WallpaperDownload: backend.RecordWallpaperDownload(task.value, task.source, *task.cancel); break;
            case Operation::WallpaperAuthorize: backend.AuthorizeWallpaperDownload(task.value, task.source, *task.cancel); break;
            }
            task.cancel->Check();
        } catch (const Failure& failure) { result.error = failure.code; }
        catch (...) { result.error = Error::InvalidResponse; }
        result.profile = backend.Current();
        {
            std::lock_guard lock(shared->mutex);
            shared->running.reset();
            // Never post to a destroyed/reused HWND. Shutdown disconnects under this lock.
            if (!shared->stop && !task.cancel->Cancelled() && shared->window) {
                shared->results.push_back(std::move(result));
                PostMessageW(shared->window, ResultMessage, 0, 0);
            }
        }
    }
    {
        std::lock_guard lock(shared->mutex);
        shared->done = true;
    }
    shared->finished.notify_all();
}
bool OpenBrowser(std::wstring_view url) {
    if (!ValidAuthorizationUrl(Utf8(url))) return false;
    const std::wstring copy(url);
    SHELLEXECUTEINFOW execute{}; execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    execute.lpVerb = L"open"; execute.lpFile = copy.c_str(); execute.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&execute) != FALSE;
}
}
struct AccountSession::Impl {
    explicit Impl(HINSTANCE instance, AccountSessionOptions value, Notify callback)
        : options(std::move(value)), notify(std::move(callback)), uiThread(GetCurrentThreadId()) {
        if (!options.clock) options.clock = UnixNow;
        disabled = options.verification && (!options.transport || !options.credentials);
        if (!options.verification) {
            if (!options.transport) options.transport = std::make_shared<native::WinHttpTransport>();
            if (!options.credentials) options.credentials = std::make_shared<native::DpapiCredentials>(options.storageDirectory);
            if (!options.openBrowser) options.openBrowser = OpenBrowser;
            if (!options.chooseAvatarFile) options.chooseAvatarFile = native::ChooseAvatarFile;
            if (!options.ensureLoginProtocol) options.ensureLoginProtocol = [] { return native::EnsureLoginProtocol(); };
        }
        instance = instance ? instance : GetModuleHandleW(nullptr);
        WNDCLASSW wc{}; wc.lpfnWndProc = Procedure; wc.hInstance = instance; wc.lpszClassName = WindowClass;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw Failure(Error::Service);
        window = CreateWindowExW(0, WindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
        if (!window) throw Failure(Error::Service);
    }
    ~Impl() { Shutdown(); }
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            try {
                if (message == ResultMessage) { self->Drain(); return 0; }
                if (message == WM_TIMER && wparam == TickTimer) { self->Tick(); return 0; }
            } catch (...) {
                if (!self->closed) { self->error = Error::Service; self->Publish(); }
            }
        }
        if (message == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    bool OnUiThread() const { return GetCurrentThreadId() == uiThread; }
    std::wstring State() const {
        Json publicProfile = nullptr;
        if (status == "signed-in" && profile) publicProfile = {
            {"id", profile->id}, {"displayName", profile->displayName}, {"avatarUrl", profile->avatarUrl},
            {"vip", profile->vip}, {"vipExpiresAt", profile->vipExpiresAt}};
        return Wide(Json{{"version", 1}, {"type", "account-state"}, {"revision", revision},
            {"status", status}, {"busy", busy}, {"error", ErrorCode(error)}, {"profile", publicProfile}}.dump());
    }

    bool SignedIn() const { return status == "signed-in" && profile.has_value(); }
    std::wstring WallpaperState() const {
        return Wide(Json{{"version", 1}, {"type", "wallpaper-engagement-state"}, {"revision", engagementRevision},
            {"id", engagementId}, {"status", engagementStatus},
            {"likes", engagement ? Json(engagement->likes) : Json(nullptr)},
            {"downloads", engagement ? Json(engagement->downloads) : Json(nullptr)},
            {"liked", SignedIn() && engagement && engagement->liked},
            {"canLike", SignedIn() && engagementStatus == "ready"},
            {"busy", engagementBusy}, {"error", engagementError}}.dump());
    }
    void PublishEngagement() {
        if (closed) return;
        ++engagementRevision;
        if (notify) { try { notify(WallpaperState()); } catch (...) {} }
    }
    void PublishRecordingNotice(const std::string& id, const std::string& code) {
        if (closed || !notify) return;
        try { notify(Wide(Json{{"version", 1}, {"type", "wallpaper-engagement-notice"},
            {"id", id}, {"error", code}}.dump())); } catch (...) {}
    }
    void CancelSelected() {
        ++selectionGeneration;
        if (selectedCancellation) selectedCancellation->Cancel();
        selectedCancellation.reset();
        if (worker) {
            std::lock_guard lock(worker->mutex);
            for (auto& task : worker->tasks) if (SelectedOperation(task.operation)) task.cancel->Cancel();
            worker->tasks.erase(std::remove_if(worker->tasks.begin(), worker->tasks.end(),
                [](const Task& task) { return SelectedOperation(task.operation); }), worker->tasks.end());
        }
        engagement.reset(); engagementBusy = false; engagementError.clear();
        engagementStatus = engagementId.empty() ? "idle" : "loading";
    }
    void StartStats() {
        CancelSelected();
        if (engagementId.empty()) { PublishEngagement(); return; }
        if (disabled) {
            engagementStatus = "failed"; engagementError = "WALLPAPER_STATS_UNAVAILABLE"; PublishEngagement(); return;
        }
        engagementBusy = true;
        Task task; task.operation = Operation::WallpaperStats; task.value = engagementId;
        task.selection = selectionGeneration;
        selectedCancellation = task.selectionCancel = std::make_shared<Cancellation>();
        Enqueue(std::move(task)); PublishEngagement();
    }
    void SyncEngagementAccount() {
        const auto identity = SignedIn() ? profile->id : std::string{};
        if (identity == engagementAccount && engagementAccountGeneration == generation) return;
        engagementAccount = identity; engagementAccountGeneration = generation;
        CancelSelected();
        if (!engagementId.empty()) StartStats();
    }
    void HandleWallpaper(const ui::WallpaperPageCommand& command) {
        if (closed || !OnUiThread()) return;
        if (command.action == ui::WallpaperAction::EndStats) {
            engagementId.clear(); CancelSelected(); PublishEngagement(); return;
        }
        if (!wallpaper::IsSafeWallpaperId(command.id)) return;
        if (command.action == ui::WallpaperAction::Stats) {
            engagementId = command.id; StartStats(); return;
        }
        if (command.action != ui::WallpaperAction::Like || command.id != engagementId || engagementBusy) return;
        if (!SignedIn()) {
            engagementError = "WALLPAPER_AUTH_REQUIRED"; PublishEngagement(); return;
        }
        if (engagementStatus != "ready" || !engagement || disabled) return;
        engagementBusy = true; engagementError.clear();
        Task task; task.operation = Operation::WallpaperLike; task.value = command.id; task.liked = command.flag;
        task.selection = selectionGeneration;
        selectedCancellation = task.selectionCancel = std::make_shared<Cancellation>();
        Enqueue(std::move(task)); PublishEngagement();
    }
    void RecordWallpaperDownload(std::string id, std::string source) {
        if (closed || !OnUiThread() || disabled || !wallpaper::IsSafeWallpaperId(id)) return;
        if (recordsPending >= 16) { PublishRecordingNotice(id, "WALLPAPER_DOWNLOAD_STATS_FAILED"); return; }
        ++recordsPending;
        Task task; task.operation = Operation::WallpaperDownload; task.value = std::move(id); task.source = std::move(source);
        Enqueue(std::move(task));
    }
    void Publish() {
        if (closed) return;
        ++revision;
        if (window) {
            if (busy || transaction.active || profile || restoreRetryAt) SetTimer(window, TickTimer, 1000, nullptr);
            else KillTimer(window, TickTimer);
        }
        if (notify) {
            auto state = State();
            try { notify(state); } catch (...) {}
        }
        SyncEngagementAccount();
    }
    void CancelWork(bool forgetCredentials = true) {
        ++generation;
        restoreRetryAt=0;restoreFailures=0;
        auto cancelled = std::move(downloadAuthorizations); downloadAuthorizations.clear();
        for (auto& [id, callback] : cancelled) { (void)id; if (callback) callback("WALLPAPER_CANCELLED"); }
        CancelSelected(); recordsPending = 0;
        transaction.Cancel();
        devicePending = false;
        if (!worker) return;
        std::lock_guard lock(worker->mutex);
        if (worker->running) worker->running->Cancel(forgetCredentials);
        for (auto& task : worker->tasks) task.cancel->Cancel(forgetCredentials);
        worker->tasks.clear(); worker->results.clear();
    }
    void Enqueue(Task task) {
        if (closed || disabled) return;
        if (!worker) {
            worker = std::make_shared<Worker>();
            worker->transport = options.transport; worker->credentials = options.credentials;
            worker->clock = options.clock; worker->window = window;
            thread = std::thread(Run, worker);
        }
        // Generation identifies an account lifetime, not an individual FIFO job.
        const bool device = task.operation == Operation::Device;
        const bool background = device || WallpaperOperation(task.operation);
        task.generation = generation;
        if (!task.cancel) task.cancel = std::make_shared<Cancellation>();
        {
            std::lock_guard lock(worker->mutex);
            worker->tasks.push_back(std::move(task));
        }
        if (device) devicePending = true;
        else if (!background) busy = true;
        worker->cv.notify_one();
        if (!background) Publish();
    }
    void Drain() {
        if (closed || !worker) return;
        std::deque<Result> results;
        {
            std::lock_guard lock(worker->mutex); results.swap(worker->results);
        }
        for (auto& result : results) {
            if (closed || result.generation != generation) continue;
            if (result.operation == Operation::WallpaperAuthorize) {
                const auto found = downloadAuthorizations.find(result.request);
                auto callback = found == downloadAuthorizations.end() ? std::function<void(std::string)>{} : std::move(found->second);
                if (found != downloadAuthorizations.end()) downloadAuthorizations.erase(found);
                const auto failure = result.error == Error::None ? std::string{} :
                    result.error == Error::WallpaperQuota ? "WALLPAPER_DOWNLOAD_QUOTA" :
                    result.error == Error::Cancelled ? "WALLPAPER_CANCELLED" : "WALLPAPER_DOWNLOAD_AUTHORIZATION";
                if (profile && !result.profile) {
                    CancelWork(); busy = false; profile.reset(); status = "signed-out"; error = result.error; Publish();
                }
                if (callback) callback(failure);
                continue;
            }
            if (WallpaperOperation(result.operation)) {
                if (result.operation == Operation::WallpaperDownload && recordsPending) --recordsPending;
                const auto recordingError = result.error == Error::WallpaperQuota ?
                    "WALLPAPER_DOWNLOAD_STATS_QUOTA" : "WALLPAPER_DOWNLOAD_STATS_FAILED";
                if (result.operation == Operation::WallpaperDownload && result.error != Error::None)
                    PublishRecordingNotice(result.id, recordingError);
                if (profile && !result.profile) {
                    CancelWork(); busy = false; profile.reset(); status = "signed-out";
                    error = result.error; Publish(); continue;
                }
                if (result.operation == Operation::WallpaperDownload) {
                    if (result.id == engagementId && !engagementBusy) {
                        if (result.error == Error::None) StartStats();
                        else {
                            engagement.reset(); engagementStatus = "failed";
                            engagementError = recordingError; PublishEngagement();
                        }
                    }
                    continue;
                }
                if (result.selection != selectionGeneration || result.id != engagementId) continue;
                engagementBusy = false;
                engagement = std::move(result.engagement);
                engagementStatus = result.error == Error::None && engagement ? "ready" : "failed";
                engagementError = result.error == Error::None ? "" : result.error == Error::AuthRequired ?
                    "WALLPAPER_AUTH_REQUIRED" : result.operation == Operation::WallpaperLike ?
                    "WALLPAPER_LIKE_FAILED" : "WALLPAPER_STATS_UNAVAILABLE";
                PublishEngagement(); continue;
            }
            if (result.operation == Operation::Device) {
                devicePending = false;
                lastDeviceCheck = options.clock();
                if (!result.profile) {
                    // Device invalidation also invalidates any queued user operation.
                    CancelWork(); busy = false; profile.reset(); status = "signed-out";
                    error = result.error; Publish();
                } else if (!busy) {
                    const bool changed = error != result.error || !profile || !SameProfile(*profile, *result.profile);
                    error = result.error; profile = std::move(result.profile); status = "signed-in";
                    if (changed) Publish();
                }
                // A queued user job owns busy and the next public completion.
                continue;
            }
            if (result.operation == Operation::Restore && !result.profile && transaction.active) {
                // A background restore retry finished after the user started signing
                // in: the browser sign-in is still the one that counts. Reporting
                // signed-out here would invite a second login that voids the first.
                restoreRetryAt=0;restoreFailures=0;
                continue;
            }
            busy = false; error = result.error; profile = std::move(result.profile);
            if (result.operation == Operation::Restore && !profile && Retryable(error)) {
                // A temporary startup outage must not turn retained credentials
                // into a permanent guest session until the next process restart.
                ++restoreFailures;
                restoreRetryAt=options.clock()+(std::min)(300LL,5LL << (std::min)(restoreFailures-1,6u));
            } else { restoreRetryAt=0;restoreFailures=0; }
            if (result.operation == Operation::Logout) profile.reset();
            status = profile ? "signed-in" : "signed-out";
            lastDeviceCheck = options.clock();
            Publish();
        }
    }
    void Tick() {
        Drain();
        if (closed) return;
        if (transaction.active && options.clock() >= transaction.expires) {
            CancelWork(); busy = false; status = "signed-out"; profile.reset(); error = Error::CallbackExpired; Publish();
        } else if (restoreRetryAt && !busy && !profile && !transaction.active && options.clock() >= restoreRetryAt) {
            restoreRetryAt=0;status="restoring";Task task;task.operation=Operation::Restore;Enqueue(std::move(task));
        } else if (profile && !busy && !devicePending && options.clock() - lastDeviceCheck >= 60) {
            lastDeviceCheck = options.clock();
            Task task; task.operation = Operation::Device; Enqueue(std::move(task));
        }
    }
    bool Restore() {
        if (closed || !OnUiThread()) return false;
        if (restored) return true;
        restored = true;
        if (disabled) return true;
        if (busy || transaction.active || profile) return true;
        status = "restoring"; error = Error::None;
        Task task; task.operation = Operation::Restore; Enqueue(std::move(task)); return true;
    }
    void Logout(Error completion = Error::LocalLogout) {
        CancelWork(); profile.reset(); status = "signed-out"; busy = false;
        error = completion;
        if (!disabled) {
            // Local erase is immediate. Queued erase also follows any racing cancelled save.
            try { options.credentials->Clear(); } catch (...) { error = Error::Storage; }
            Task task; task.operation = Operation::Logout; task.completionError = completion; Enqueue(std::move(task));
        } else Publish();
    }
    std::wstring HandlePage(const ui::AccountPageCommand& command, HWND owner) {
        if (closed || !OnUiThread()) return State();
        if (command.action != ui::AccountAction::UpdateName && !command.value.empty()) {
            error = Error::InvalidCommand; Publish(); return State();
        }
        try {
            switch (command.action) {
            case ui::AccountAction::Snapshot: Publish(); break;
            case ui::AccountAction::Logout: Logout(); break;
            case ui::AccountAction::CancelLogin:
                if (status == "authorizing") Logout(Error::Cancelled);
                break;
            case ui::AccountAction::Login:
                if (busy || profile) break;
                restoreRetryAt=0;restoreFailures=0;
                if (disabled || !options.openBrowser) throw Failure(Error::Browser);
                {
                    bool registered = options.verification;
                    try { if (options.ensureLoginProtocol) registered = options.ensureLoginProtocol(); }
                    catch (...) { registered = false; }
                    if (!registered) throw Failure(Error::ProtocolRegistration);
                    auto url = transaction.Begin(options.clock());
                    status = "authorizing"; busy = true; error = Error::None; profile.reset();
                    if (!ValidAuthorizationUrl(url) || !options.openBrowser(Wide(url))) {
                        transaction.Cancel(); status = "signed-out"; busy = false; throw Failure(Error::Browser);
                    }
                    Publish();
                }
                break;
            case ui::AccountAction::Refresh:
            case ui::AccountAction::UpdateName:
                if (busy) break;
                if (!profile) throw Failure(Error::AuthRequired);
                {
                    Task task; task.operation = command.action == ui::AccountAction::Refresh ? Operation::Refresh : Operation::Name;
                    if (task.operation == Operation::Name) task.value = NormalizeName(command.value);
                    error = Error::None; Enqueue(std::move(task));
                }
                break;
            case ui::AccountAction::ChooseAvatar:
                if (busy) break;
                if (!profile) throw Failure(Error::AuthRequired);
                if (!options.chooseAvatarFile) throw Failure(Error::InvalidAvatar);
                {
                    const auto current = generation;
                    busy = true; error = Error::None; Publish();
                    std::optional<std::wstring> path;
                    try { path = options.chooseAvatarFile(owner); }
                    catch (...) { if (!closed && current == generation) busy = false; throw; }
                    if (closed || current != generation) break;
                    busy = false;
                    if (path) { Task task; task.operation = Operation::Avatar; task.path = *path; Enqueue(std::move(task)); }
                    else Publish();
                }
                break;
            default: throw Failure(Error::InvalidCommand);
            }
        } catch (const Failure& failure) { error = failure.code; if (!worker && !transaction.active) busy = false; Publish(); }
        catch (...) { error = Error::Service; Publish(); }
        return State();
    }
    bool HandleCallback(std::wstring_view uri) {
        if (closed || !OnUiThread() || disabled) return false;
        try {
            Task task; task.operation = Operation::Login;
            if (uri.size() > 8192) throw Failure(Error::CallbackInvalid);
            task.grant = transaction.Complete(Utf8(uri), options.clock());
            error = Error::None; Enqueue(std::move(task)); return true;
        } catch (const Failure& failure) {
            error = failure.code;
            if (status == "authorizing" && !transaction.active && !busy) status = "signed-out";
            if (failure.code == Error::CallbackExpired || failure.code == Error::AuthorizationDenied) {
                status = "signed-out"; busy = false;
            }
            Publish(); return false;
        } catch (...) { error = Error::CallbackInvalid; Publish(); return false; }
    }
    void Shutdown() {
        if (closed) return;
        closed = true; CancelWork(false); notify = {};
        if (worker) {
            {
                std::lock_guard lock(worker->mutex); worker->stop = true; worker->window = nullptr; worker->results.clear();
            }
            worker->cv.notify_all();
        }
        if (window) { KillTimer(window, TickTimer); DestroyWindow(window); window = nullptr; }
        if (thread.joinable()) {
            std::unique_lock lock(worker->mutex);
            // The native HTTP port has a 15s deadline. Allow an already-consumed
            // refresh response to commit before the production process exits.
            // Injected verification ports keep their original bounded shutdown.
            const auto done = worker->finished.wait_for(lock, std::chrono::milliseconds(options.verification ? 200 : 16000),
                [&] { return worker->done; });
            lock.unlock();
            if (done) thread.join();
            else thread.detach(); // Only shared ports/state remain, never an Impl or UI pointer.
        }
        profile.reset(); busy = false; status = "signed-out"; ++revision;
    }
    AccountSessionOptions options;
    Notify notify;
    DWORD uiThread = 0;
    HWND window = nullptr;
    bool closed = false, disabled = false, restored = false, busy = false, devicePending = false;
    std::uint64_t generation = 0, revision = 0;
    std::uint64_t selectionGeneration = 0, engagementRevision = 0;
    std::uint64_t engagementAccountGeneration = 0;
    unsigned recordsPending = 0;
    bool engagementBusy = false;
    std::string engagementId, engagementAccount, engagementStatus = "idle", engagementError;
    std::optional<WallpaperEngagement> engagement;
    std::shared_ptr<Cancellation> selectedCancellation;
    std::int64_t lastDeviceCheck = 0;
    std::int64_t restoreRetryAt = 0;
    unsigned restoreFailures = 0;
    std::string status = "signed-out";
    Error error = Error::None;
    std::optional<Profile> profile;
    Transaction transaction;
    std::shared_ptr<Worker> worker;
    std::thread thread;
    std::uint64_t nextDownloadAuthorization = 0;
    std::map<std::uint64_t, std::function<void(std::string)>> downloadAuthorizations;
};
AccountSession::AccountSession(HINSTANCE instance, AccountSessionOptions options, Notify notify)
    : impl_(std::make_unique<Impl>(instance, std::move(options), std::move(notify))) {}
AccountSession::~AccountSession() = default;
bool AccountSession::Restore() { return impl_->Restore(); }
std::wstring AccountSession::HandlePage(const ui::AccountPageCommand& command, HWND owner) { return impl_->HandlePage(command, owner); }
bool AccountSession::HandleCallback(std::wstring_view uri) { return impl_->HandleCallback(uri); }
std::wstring AccountSession::State() const { return impl_->State(); }
bool AccountSession::SignedIn() const { return impl_->SignedIn(); }
void AccountSession::HandleWallpaper(const ui::WallpaperPageCommand& command) { impl_->HandleWallpaper(command); }
void AccountSession::RecordWallpaperDownload(std::string id, std::string source) {
    impl_->RecordWallpaperDownload(std::move(id), std::move(source));
}
void AccountSession::AuthorizeWallpaperDownload(std::string id, std::string source, std::function<void(std::string)> completed) {
    if (!completed) return;
    if (impl_->closed || !impl_->OnUiThread() || impl_->disabled || !wallpaper::IsSafeWallpaperId(id)) {
        completed("WALLPAPER_DOWNLOAD_AUTHORIZATION"); return;
    }
    if (impl_->downloadAuthorizations.size() >= 16) { completed("WALLPAPER_BUSY"); return; }
    Task task; task.operation = Operation::WallpaperAuthorize; task.value = std::move(id); task.source = std::move(source);
    task.request = ++impl_->nextDownloadAuthorization;
    impl_->downloadAuthorizations.emplace(task.request, std::move(completed));
    impl_->Enqueue(std::move(task));
}
std::wstring AccountSession::WallpaperState() const { return impl_->WallpaperState(); }
void AccountSession::Shutdown() { impl_->Shutdown(); }
bool AccountSession::HasWork() const {
    return !impl_->closed && (impl_->busy || impl_->devicePending || impl_->engagementBusy || impl_->recordsPending || !impl_->downloadAuthorizations.empty() ||
        impl_->transaction.active || impl_->profile.has_value() ||
        impl_->restoreRetryAt || impl_->status == "restoring" || impl_->status == "authorizing");
}
}
