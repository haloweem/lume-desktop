#include "platform/windows/dynamic_island/notification_receiver.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include <windows.h>
#include <appmodel.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace lume::platform::windows {
namespace {
using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::UI::Notifications::Management;
using Access = UserNotificationListenerAccessStatus;
constexpr std::size_t MaxPending = 16, MaxBaseline = 1024;
constexpr UINT WakeMessage = WM_APP + 0x361;
bool Same(const NotificationReceiverOptions& a, const NotificationReceiverOptions& b) {
    return a.enabled == b.enabled && a.qq == b.qq && a.wechat == b.wechat && a.preview == b.preview;
}
bool PackageIdentity() {
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
}
std::wstring Lower(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}
std::wstring Executable(HWND window) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (!processId || processId == GetCurrentProcessId()) return {};
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return {};
    wchar_t path[32768]; DWORD length = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    CloseHandle(process);
    return ok ? std::wstring(path, length) : std::wstring{};
}
bool ApplicationWindow(HWND window, std::string_view source) {
    if (!IsWindow(window) || GetAncestor(window, GA_ROOT) != window) return false;
    const auto path = Executable(window);
    if (source == "wechat") return IsWeChatExecutablePath(path);
    return source == "qq" && IsQQExecutablePath(path);
}
std::string AccessName(Access access) {
    return access == Access::Allowed ? "connected" : access == Access::Denied ? "denied" : "access-required";
}
std::wstring BoundedText(const winrt::hstring& value, std::size_t limit) {
    std::wstring result(value.c_str(), std::min<std::size_t>(value.size(), limit));
    for (auto& c : result) if (c < 0x20 || c == 0x7f) c = L' ';
    return result;
}
}

bool IsQQNotificationSource(std::wstring_view identity) {
    // QQ's desktop shortcut registers this exact AppUserModelID. Other QQ
    // products, arbitrary Tencent identities and display names are excluded.
    return identity == L"QQ";
}
bool IsQQExecutablePath(std::wstring_view path) {
    if (path.empty() || path.size() > 32767) return false;
    const auto slash = path.find_last_of(L"\\/");
    return Lower(path.substr(slash == std::wstring_view::npos ? 0 : slash + 1)) == L"qq.exe";
}
bool IsWeChatExecutablePath(std::wstring_view path) {
    if (path.empty() || path.size() > 32767) return false;
    const auto slash = path.find_last_of(L"\\/");
    const auto name = Lower(path.substr(slash == std::wstring_view::npos ? 0 : slash + 1));
    return name == L"wechat.exe" || name == L"weixin.exe";
}
QQNotificationChannel SelectQQNotificationChannel(bool enabled, bool /*packaged*/,
                                                  QQNotificationAccess access, bool systemUsable) {
    if (!enabled) return QQNotificationChannel::Disabled;
    if (access == QQNotificationAccess::Allowed && systemUsable) return QQNotificationChannel::System;
    // Shell attention reports no notification text. It remains usable without
    // requesting or overriding Windows notification-listener permission.
    return QQNotificationChannel::Attention;
}
void QQNotificationDeduplicator::Reset(std::int64_t listeningSince) {
    since_ = listeningSince; discardedThrough_ = 0; order_.clear(); seen_.clear();
}
bool QQNotificationDeduplicator::CanAccept(std::uint32_t id, std::int64_t creationTime) const {
    if (creationTime < since_ || creationTime <= discardedThrough_) return false;
    const auto key = std::to_string(id) + ":" + std::to_string(creationTime);
    if (seen_.count(key)) return false;
    return order_.size() < Capacity || creationTime > std::max(discardedThrough_, order_.front().second);
}
bool QQNotificationDeduplicator::Accept(std::uint32_t id, std::int64_t creationTime) {
    if (!CanAccept(id, creationTime)) return false;
    const auto key = std::to_string(id) + ":" + std::to_string(creationTime);
    if (order_.size() == Capacity) {
        discardedThrough_ = std::max(discardedThrough_, order_.front().second);
        seen_.erase(order_.front().first); order_.pop_front();
        if (creationTime <= discardedThrough_) return false;
    }
    seen_.insert(key); order_.emplace_back(key, creationTime); return true;
}

struct IslandNotificationReceiver::Impl : std::enable_shared_from_this<Impl> {
    mutable std::mutex mutex;
    std::condition_variable wake;
    NotificationReceiverOptions options;
    NotificationReceiverState state;
    std::deque<ReceivedIslandNotification> pending;
    struct NotificationEvent { std::uint32_t id; std::uint64_t generation; ULONGLONG arrived; bool removed; };
    std::deque<NotificationEvent> notificationEvents;
    std::unordered_set<std::uint32_t> removedNotificationIds;
    std::deque<std::uint32_t> removedNotificationOrder;
    std::int64_t listeningSince = 0;
    HWND wakeTarget = nullptr;
    UINT wakeTargetMessage = 0;
    WPARAM wakeTargetCookie = 0;
    bool wakePosted = false;
    std::atomic<bool> stopped{false}, dirty{false};
    std::atomic<DWORD> threadId{0};
    std::uint64_t generation = 0;
    const bool packaged = PackageIdentity();
    bool done = false;
    bool qqAttention = false, qqDenied = false, qqAccessAllowed = false;
    UINT shellMessage = 0;
    ULONGLONG lastWechat = 0, lastQQ = 0;
    std::deque<std::string> failures;

    void Failure(const char* stage, HRESULT code) {
        char value[16]{};
        std::snprintf(value, sizeof(value), "0x%08lX", static_cast<unsigned long>(code));
        const auto detail = std::string("stage=") + stage + " hr=" + value;
        if (std::find(failures.begin(), failures.end(), detail) != failures.end()) return;
        if (failures.size() == 16) failures.pop_front();
        failures.push_back(detail);
        // Do not record notification contents, titles, identities or exception text.
        diag::Log("island-receiver", detail + " packaged=" + (packaged ? "true" : "false"));
    }

    void Wake() {
        wake.notify_all();
        if (const auto id = threadId.load()) PostThreadMessageW(id, WakeMessage, 0, 0);
    }
    void PostPendingWake() {
        // The caller holds mutex. PostMessage is asynchronous: shutdown can
        // clear this target under the same lock without ever waiting on the UI.
        if (!stopped && !pending.empty() && wakeTarget && wakeTargetMessage && !wakePosted)
            wakePosted = PostMessageW(wakeTarget, wakeTargetMessage, wakeTargetCookie, 0) != FALSE;
    }
    void Queue(ReceivedIslandNotification notice) {
        const bool empty = pending.empty();
        if (pending.size() == MaxPending) pending.pop_front();
        pending.push_back(std::move(notice));
        if (empty) PostPendingWake();
    }
    void ClearSource(std::string_view source) {
        pending.erase(std::remove_if(pending.begin(), pending.end(), [source](const auto& notice) {
            return notice.source == source;
        }), pending.end());
        if (pending.empty()) wakePosted = false;
    }
    void Attention(HWND window) {
        NotificationReceiverOptions current;
        bool allowQQ;
        { std::lock_guard lock(mutex); current = options; allowQQ = qqAttention; }
        if (!current.enabled) return;
        const bool qq = current.qq && allowQQ && ApplicationWindow(window, "qq");
        if (!qq && (!current.wechat || !ApplicationWindow(window, "wechat"))) return;
        // An attention event means only that the app requested attention. It
        // is not a message count and provides no access to chat text. QQ uses
        // this only when the system channel is unavailable, never after denial.
        const auto now = GetTickCount64();
        std::lock_guard lock(mutex);
        if (stopped || !options.enabled || (qq ? !options.qq || !qqAttention : !options.wechat)) return;
        auto& last = qq ? lastQQ : lastWechat;
        if (last && now - last < 30000) return;
        last = now;
        const auto source = qq ? "qq" : "wechat";
        Queue({source, std::string(source) + ":attention:" + std::to_string(now), {}, {}});
    }
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == self->shellMessage && wparam == HSHELL_FLASH) self->Attention(reinterpret_cast<HWND>(lparam));
        return DefWindowProcW(window, message, wparam, lparam);
    }
    HWND CreateAttentionWindow() {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW type{}; type.lpfnWndProc = WindowProc; type.hInstance = instance;
        type.lpszClassName = L"LumeIslandNotificationReceiver";
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
        shellMessage = RegisterWindowMessageW(L"SHELLHOOK");
        const auto window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName, L"", WS_POPUP,
                                            0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (window && shellMessage && RegisterShellHookWindow(window)) return window;
        if (window) DestroyWindow(window);
        return nullptr;
    }
    template<class Operation> bool Complete(Operation const& operation, std::uint64_t expected,
                                            const std::function<void()>& processEvents) {
        // No unbounded WinRT .get(): a wedged notification service must not
        // hold shutdown or keep an old subscription alive forever.
        const auto deadline = GetTickCount64() + 10000;
        while (operation.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
            if (stopped || GetTickCount64() >= deadline) { operation.Cancel(); return false; }
            {
                std::unique_lock lock(mutex);
                if (generation != expected) { lock.unlock(); operation.Cancel(); return false; }
                wake.wait_for(lock, std::chrono::milliseconds(50));
            }
            // A slow Windows notification query must not hold WeChat shell
            // attention events behind it for the duration of the query.
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message != WakeMessage) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            if (processEvents) processEvents();
        }
        return operation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed;
    }
    static ReceivedIslandNotification Notice(UserNotification const& notification, std::uint32_t id, std::int64_t created, bool preview) {
        ReceivedIslandNotification notice{"qq", "qq:" + std::to_string(id) + ":" + std::to_string(created), {}, {}};
        if (preview) {
          try {
            const auto binding = notification.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric());
            if (binding) {
                const auto elements = binding.GetTextElements();
                if (elements.Size() > 0) notice.title = BoundedText(elements.GetAt(0).Text(), 120);
                if (elements.Size() > 1) notice.body = BoundedText(elements.GetAt(1).Text(), 240);
            }
          } catch (...) { notice.title.clear(); notice.body.clear(); }
        }
        return notice;
    }
    void InvalidateQQEvents(std::uint64_t expected, bool denied) {
        std::lock_guard lock(mutex);
        if (generation != expected) return;
        notificationEvents.clear(); removedNotificationIds.clear(); removedNotificationOrder.clear(); ClearSource("qq"); qqAttention = false; qqAccessAllowed = false;
        state.qqRealtime = false;
        if (denied) { qqDenied = true; state.qq = "denied"; }
        else if (state.qq != "requesting") state.qq = "access-required";
    }
    void ProcessEvents(UserNotificationListener const& listener, const NotificationReceiverOptions current,
                       std::uint64_t expected, QQNotificationDeduplicator& deduplicator) {
        std::deque<NotificationEvent> events;
        {
            std::lock_guard lock(mutex);
            if (stopped || expected != generation || !options.enabled || !options.qq) return;
            events.swap(notificationEvents);
        }
        if (events.empty()) return;
        try {
            const auto access = listener.GetAccessStatus();
            if (access != Access::Allowed) { InvalidateQQEvents(expected, access == Access::Denied); dirty = true; return; }
        } catch (...) { dirty = true; return; }
        for (const auto& event : events) {
            if (event.generation != expected) continue;
            const auto prefix = "qq:" + std::to_string(event.id) + ":";
            if (event.removed) {
                std::lock_guard lock(mutex);
                if (generation == expected) {
                    pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const auto& notice) {
                        return notice.id.compare(0, prefix.size(), prefix) == 0;
                    }), pending.end());
                    if (pending.empty()) wakePosted = false;
                }
                continue;
            }
            try {
                const auto notification = listener.GetNotification(event.id);
                if (!notification) continue;
                const auto info = notification.AppInfo();
                if (!info || !IsQQNotificationSource(info.AppUserModelId().c_str())) continue;
                const auto created = notification.CreationTime().time_since_epoch().count();
                {
                    std::lock_guard lock(mutex);
                    if (stopped || expected != generation || !options.enabled || !options.qq || !qqAccessAllowed || qqDenied || removedNotificationIds.count(event.id)) continue;
                    if (!deduplicator.CanAccept(event.id, created)) continue;
                }
                auto notice = Notice(notification, event.id, created, current.preview);
                std::lock_guard lock(mutex);
                if (!stopped && expected == generation && options.enabled && options.qq && qqAccessAllowed && !qqDenied && !removedNotificationIds.count(event.id) &&
                    deduplicator.Accept(event.id, created))
                    Queue(std::move(notice));
            } catch (const winrt::hresult_error& error) {
                // A dismissed toast can disappear before GetNotification. It
                // is not an error that warrants replaying a full snapshot.
                if (error.code() == E_ACCESSDENIED) { InvalidateQQEvents(expected, true); dirty = true; return; }
            } catch (...) { dirty = true; }
        }
    }
    bool ReadQQ(UserNotificationListener const& listener, const NotificationReceiverOptions current,
                std::uint64_t expected, QQNotificationDeduplicator& deduplicator, bool eventsAvailable, const char*& stage) {
        stage = "snapshot-request";
        const auto operation = listener.GetNotificationsAsync(NotificationKinds::Toast);
        const std::function<void()> fastEvents = eventsAvailable ? std::function<void()>([&] {
            ProcessEvents(listener, current, expected, deduplicator);
        }) : std::function<void()>{};
        stage = "snapshot-wait";
        if (!Complete(operation, expected, fastEvents)) {
            const auto code = operation.Status() == winrt::Windows::Foundation::AsyncStatus::Error
                ? operation.ErrorCode() : winrt::hresult(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            throw winrt::hresult_error(code);
        }
        stage = "snapshot-results";
        const auto notifications = operation.GetResults();
        std::size_t examined = 0, qqExamined = 0;
        bool unreadableSource = false;
        stage = "snapshot-enumerate";
        for (const auto& notification : notifications) {
            if (++examined > 4096) break;
            if (eventsAvailable) ProcessEvents(listener, current, expected, deduplicator);
            try {
            stage = "snapshot-app-info";
            const auto info = notification.AppInfo();
            stage = "snapshot-app-id";
            if (!info) { unreadableSource = true; continue; }
            if (!IsQQNotificationSource(info.AppUserModelId().c_str())) continue;
            if (++qqExamined > MaxBaseline) break;
            stage = "snapshot-metadata";
            const auto id = notification.Id();
            const auto created = notification.CreationTime().time_since_epoch().count();
            {
                std::lock_guard lock(mutex);
                if (stopped || expected != generation || !options.enabled || !options.qq || !qqAccessAllowed || qqDenied) return false;
                if (removedNotificationIds.count(id) || !deduplicator.CanAccept(id, created)) continue;
            }
            auto notice = Notice(notification, id, created, current.preview);
            std::lock_guard lock(mutex);
            if (stopped || expected != generation || !options.enabled || !options.qq || !qqAccessAllowed || qqDenied) return false;
            if (!removedNotificationIds.count(id) && deduplicator.Accept(id, created)) Queue(std::move(notice));
            } catch (const winrt::hresult_error& error) {
                if (error.code() == E_ACCESSDENIED) throw;
                // One desktop toast without AppInfo must not disconnect the
                // subscription or hide subsequent, identifiable QQ toasts.
                // Never read text from a notification whose source is unknown.
                unreadableSource = true;
                Failure(stage, error.code());
            }
        }
        std::lock_guard lock(mutex);
        if (stopped || expected != generation || !options.enabled || !options.qq || !qqAccessAllowed || qqDenied) return false;
        return !unreadableSource || qqExamined > 0;
    }
    void Run() {
        threadId = GetCurrentThreadId();
        MSG message{}; PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        bool apartment = false;
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment = true; } catch (...) {}
        UserNotificationListener listener{nullptr};
        winrt::event_token subscription{}; bool subscribed = false, eventsUnavailable = false;
        QQNotificationDeduplicator deduplicator;
        HWND attentionWindow = nullptr;
        std::uint64_t observed = ~std::uint64_t{0};
        ULONGLONG nextRead = 0, nextAttentionAttempt = 0;
        auto channel = QQNotificationChannel::Disabled;
        for (;;) {
            NotificationReceiverOptions current; std::uint64_t expected; std::int64_t listenSince;
            {
                std::unique_lock lock(mutex);
                if (!options.enabled || (!options.qq && !options.wechat)) {
                    // Tear down before sleeping as well as after a pump. A
                    // disable can arrive between the last pump and this lock.
                    lock.unlock();
                    if (attentionWindow) { DeregisterShellHookWindow(attentionWindow); DestroyWindow(attentionWindow); attentionWindow = nullptr; }
                    if (subscribed) { try { listener.NotificationChanged(subscription); } catch (...) {} subscribed = false; }
                    listener = nullptr; deduplicator.Reset(winrt::clock::now().time_since_epoch().count());
                    lock.lock();
                    wake.wait(lock, [this] { return stopped || (options.enabled && (options.qq || options.wechat)); });
                }
                if (stopped) break;
                current = options; expected = generation; listenSince = listeningSince;
            }
            if (observed != expected) {
                if (subscribed) { try { listener.NotificationChanged(subscription); } catch (...) {} subscribed = false; }
                deduplicator.Reset(listenSince);
                nextRead = 0; nextAttentionAttempt = 0; observed = expected; lastWechat = lastQQ = 0;
                eventsUnavailable = false;
                failures.clear();
            }
            if (!current.qq) {
                if (subscribed) { try { listener.NotificationChanged(subscription); } catch (...) {} subscribed = false; }
                listener = nullptr; deduplicator.Reset(winrt::clock::now().time_since_epoch().count());
                std::lock_guard lock(mutex);
                channel = QQNotificationChannel::Disabled;
                if (expected == generation) { state.qq = "disabled"; state.qqRealtime = false; qqAttention = false; }
            } else if (!apartment) {
                channel = SelectQQNotificationChannel(true, packaged, QQNotificationAccess::Unavailable, false);
            } else {
                if (subscribed && listener) ProcessEvents(listener, current, expected, deduplicator);
                if (dirty.exchange(false) || GetTickCount64() >= nextRead) {
                const auto readStarted = GetTickCount64();
                nextRead = readStarted + 5000;
                auto observedAccess = QQNotificationAccess::Unavailable;
                const char* stage = "listener-current";
                try {
                    if (!listener) listener = UserNotificationListener::Current();
                    stage = "access-status";
                    const auto access = listener.GetAccessStatus();
                    observedAccess = access == Access::Allowed ? QQNotificationAccess::Allowed :
                        access == Access::Denied ? QQNotificationAccess::Denied : QQNotificationAccess::Unspecified;
                    {
                        std::lock_guard lock(mutex);
                        if (expected == generation) {
                            qqDenied = access == Access::Denied;
                            qqAccessAllowed = access == Access::Allowed;
                            if (state.qq != "requesting" && access != Access::Allowed) state.qq = AccessName(access);
                        }
                        if (access != Access::Allowed) { ClearSource("qq"); notificationEvents.clear(); state.qqRealtime = false; }
                    }
                    if (access == Access::Allowed) {
                        if (!subscribed && !eventsUnavailable) {
                            const std::weak_ptr<Impl> weak = shared_from_this();
                            try {
                                subscription = listener.NotificationChanged([weak, expected](auto&&, UserNotificationChangedEventArgs const& args) noexcept {
                                    try {
                                        const auto kind = args.ChangeKind();
                                        if (kind != UserNotificationChangedKind::Added && kind != UserNotificationChangedKind::Removed) return;
                                        if (const auto self = weak.lock()) {
                                            const auto id = args.UserNotificationId();
                                            {
                                                std::lock_guard lock(self->mutex);
                                                if (self->stopped || self->generation != expected || !self->options.enabled || !self->options.qq || self->qqDenied) return;
                                                const bool removed = kind == UserNotificationChangedKind::Removed;
                                                if (removed) {
                                                    if (!self->removedNotificationIds.count(id)) {
                                                        if (self->removedNotificationOrder.size() == 64) {
                                                            self->removedNotificationIds.erase(self->removedNotificationOrder.front());
                                                            self->removedNotificationOrder.pop_front();
                                                        }
                                                        self->removedNotificationIds.insert(id); self->removedNotificationOrder.push_back(id);
                                                    }
                                                    const auto prefix = "qq:" + std::to_string(id) + ":";
                                                    self->pending.erase(std::remove_if(self->pending.begin(), self->pending.end(), [&](const auto& notice) {
                                                        return notice.id.compare(0, prefix.size(), prefix) == 0;
                                                    }), self->pending.end());
                                                    if (self->pending.empty()) self->wakePosted = false;
                                                } else {
                                                    self->removedNotificationIds.erase(id);
                                                    self->removedNotificationOrder.erase(std::remove(self->removedNotificationOrder.begin(), self->removedNotificationOrder.end(), id), self->removedNotificationOrder.end());
                                                }
                                                // Supersede duplicate additions; a removal also cancels
                                                // additions not yet passed to GetNotification.
                                                self->notificationEvents.erase(std::remove_if(self->notificationEvents.begin(), self->notificationEvents.end(),
                                                    [id](const auto& event) { return event.id == id; }), self->notificationEvents.end());
                                                if (self->notificationEvents.size() == 64) { self->notificationEvents.pop_front(); self->dirty = true; }
                                                self->notificationEvents.push_back({id, expected, GetTickCount64(), removed});
                                            }
                                            self->Wake();
                                        }
                                    } catch (...) {}
                                });
                                subscribed = true;
                            } catch (...) { eventsUnavailable = true; Failure("subscribe", winrt::to_hresult()); }
                        }
                        stage = "snapshot";
                        const bool usable = ReadQQ(listener, current, expected, deduplicator, subscribed, stage);
                        channel = SelectQQNotificationChannel(true, packaged, observedAccess, usable);
                        // Some desktop environments allow the public snapshot
                        // API but reject the change event. Keep that working
                        // channel with bounded, content-filtered synchronization.
                        const auto finished = GetTickCount64();
                        nextRead = eventsUnavailable ? std::max(readStarted + 1000, finished + 100) : finished + 5000;
                    } else {
                        if (subscribed) { listener.NotificationChanged(subscription); subscribed = false; }
                        deduplicator.Reset(winrt::clock::now().time_since_epoch().count());
                        channel = SelectQQNotificationChannel(true, packaged, observedAccess, false);
                    }
                } catch (...) {
                    const auto code = winrt::to_hresult();
                    Failure(stage, code);
                    if (code == E_ACCESSDENIED) {
                        observedAccess = QQNotificationAccess::Denied;
                        InvalidateQQEvents(expected, true);
                    }
                    if (subscribed) { try { listener.NotificationChanged(subscription); } catch (...) {} subscribed = false; }
                    listener = nullptr; deduplicator.Reset(winrt::clock::now().time_since_epoch().count());
                    { std::lock_guard lock(mutex); if (expected == generation) { notificationEvents.clear(); state.qqRealtime = false; } }
                    channel = SelectQQNotificationChannel(true, packaged, observedAccess, false);
                    nextRead = GetTickCount64() + 5000;
                }
                }
            }
            const bool attentionWanted = current.wechat || channel == QQNotificationChannel::Attention;
            if (attentionWanted) {
                if (!attentionWindow && GetTickCount64() >= nextAttentionAttempt) {
                    nextAttentionAttempt = GetTickCount64() + 30000;
                    attentionWindow = CreateAttentionWindow();
                }
            } else if (attentionWindow) {
                DeregisterShellHookWindow(attentionWindow); DestroyWindow(attentionWindow); attentionWindow = nullptr;
            }
            {
                std::lock_guard lock(mutex);
                if (expected == generation) {
                    if (current.qq && qqDenied) channel = QQNotificationChannel::Attention;
                    state.qqRealtime = channel == QQNotificationChannel::System && subscribed && qqAccessAllowed;
                    qqAttention = channel == QQNotificationChannel::Attention;
                    if (current.wechat) state.wechat = attentionWindow ? "listening" : "error";
                    if (state.qq != "requesting") {
                        switch (channel) {
                        case QQNotificationChannel::Disabled: state.qq = "disabled"; break;
                        case QQNotificationChannel::System: state.qq = "connected"; break;
                        case QQNotificationChannel::Denied: state.qq = "denied"; ClearSource("qq"); break;
                        case QQNotificationChannel::AccessRequired: state.qq = "access-required"; break;
                        case QQNotificationChannel::Attention: state.qq = attentionWindow ? "attention" : "package-required"; break;
                        default: state.qq = "error"; ClearSource("qq"); break;
                        }
                    }
                }
            }
            // Shell notifications are event driven. The five-second QQ sync
            // repairs a missed change event and rechecks permission revocation.
            DWORD waitMilliseconds = 500;
            if (current.qq && apartment) {
                const auto now = GetTickCount64();
                waitMilliseconds = nextRead > now ? static_cast<DWORD>(std::min<ULONGLONG>(500, nextRead - now)) : 0;
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, waitMilliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message != WakeMessage) { TranslateMessage(&message); DispatchMessageW(&message); }
            }
            // Disabling must destroy listeners before going back to idle.
            bool inactive;
            { std::lock_guard lock(mutex); inactive = !options.enabled || (!options.qq && !options.wechat); }
            if (inactive) {
                if (attentionWindow) { DeregisterShellHookWindow(attentionWindow); DestroyWindow(attentionWindow); attentionWindow = nullptr; }
                if (subscribed) { try { listener.NotificationChanged(subscription); } catch (...) {} subscribed = false; }
                listener = nullptr; deduplicator.Reset(winrt::clock::now().time_since_epoch().count());
            }
        }
        if (attentionWindow) { DeregisterShellHookWindow(attentionWindow); DestroyWindow(attentionWindow); }
        if (subscribed) { try { listener.NotificationChanged(subscription); } catch (...) {} }
        listener = nullptr;
        if (apartment) winrt::uninit_apartment();
        { std::lock_guard lock(mutex); pending.clear(); notificationEvents.clear(); state.qqRealtime = false; wakePosted = false; done = true; }
        wake.notify_all(); threadId = 0;
    }
};

IslandNotificationReceiver::IslandNotificationReceiver() : impl_(std::make_shared<Impl>()), worker_([self = impl_] { self->Run(); }) {}
IslandNotificationReceiver::~IslandNotificationReceiver() { Shutdown(); }
void IslandNotificationReceiver::Configure(NotificationReceiverOptions next) {
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopped || Same(impl_->options, next)) return;
        impl_->options = next; ++impl_->generation; impl_->pending.clear(); impl_->notificationEvents.clear(); impl_->wakePosted = false; impl_->qqAttention = false;
        impl_->removedNotificationIds.clear(); impl_->removedNotificationOrder.clear();
        impl_->listeningSince = winrt::clock::now().time_since_epoch().count();
        impl_->state.qqRealtime = false; impl_->qqAccessAllowed = false;
        impl_->state.qq = !next.enabled || !next.qq ? "disabled" : "access-required";
        impl_->state.wechat = !next.enabled || !next.wechat ? "disabled" : "starting";
    }
    impl_->Wake();
}
void IslandNotificationReceiver::SetWakeTarget(HWND target, UINT message, WPARAM cookie) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopped) return;
    if (impl_->wakeTarget != target || impl_->wakeTargetMessage != message || impl_->wakeTargetCookie != cookie)
        impl_->wakePosted = false;
    impl_->wakeTarget = target;
    impl_->wakeTargetMessage = target ? message : 0;
    impl_->wakeTargetCookie = target ? cookie : 0;
    impl_->PostPendingWake();
}
bool IslandNotificationReceiver::RequestAccess() {
    // Retained for older callers; production never launches consent UI.
    return false;
}
NotificationReceiverState IslandNotificationReceiver::Snapshot() const {
    std::lock_guard lock(impl_->mutex); return impl_->state;
}
std::vector<ReceivedIslandNotification> IslandNotificationReceiver::Drain() {
    std::lock_guard lock(impl_->mutex);
    std::vector<ReceivedIslandNotification> result;
    result.reserve(impl_->pending.size());
    while (!impl_->pending.empty()) { result.push_back(std::move(impl_->pending.front())); impl_->pending.pop_front(); }
    impl_->wakePosted = false;
    return result;
}
bool IslandNotificationReceiver::OpenApp(std::string_view source) {
    if (source != "qq" && source != "wechat") return false;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopped || !impl_->options.enabled ||
            (source == "qq" ? !impl_->options.qq : !impl_->options.wechat)) return false;
    }
    struct Search { std::string_view source; HWND window = nullptr; } search{source};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& context = *reinterpret_cast<Search*>(data);
        if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || !ApplicationWindow(window, context.source)) return TRUE;
        context.window = window; return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.window || !ApplicationWindow(search.window, source)) return false;
    ShowWindowAsync(search.window, SW_RESTORE);
    return SetForegroundWindow(search.window) != FALSE;
}
void IslandNotificationReceiver::Shutdown() {
    if (!impl_ || !worker_.joinable()) return;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopped = true; ++impl_->generation; impl_->pending.clear(); impl_->state = {}; impl_->qqAttention = false;
        impl_->notificationEvents.clear();
        impl_->wakeTarget = nullptr; impl_->wakeTargetMessage = 0; impl_->wakeTargetCookie = 0; impl_->wakePosted = false;
    }
    impl_->Wake();
    bool done;
    { std::unique_lock lock(impl_->mutex); done = impl_->wake.wait_for(lock, std::chrono::milliseconds(750), [this] { return impl_->done; }); }
    // The worker owns shared state and never calls the UI. A broken system
    // service therefore cannot block closing Lume or use a destroyed session.
    if (done) worker_.join(); else worker_.detach();
}
}
