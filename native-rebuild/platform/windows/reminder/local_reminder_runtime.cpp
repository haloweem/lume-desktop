#include "platform/windows/reminder/local_reminder_runtime.h"
#include "core/todo/todo_model.h"
#include "platform/windows/reminder/reminder_attention.h"
#include <unordered_map>
#include <algorithm>
#include <cwchar>
#include <limits>
#include <tuple>
#include <utility>

namespace lume::platform::windows {
namespace {
constexpr wchar_t kClass[] = L"LumeLocalReminderRuntime";
constexpr UINT kRefresh = WM_APP + 0x392;
constexpr UINT_PTR kTimer = 1;
constexpr UINT_PTR kAttentionTimer = 2;
constexpr UINT kAttention = WM_APP + 0x393;
// OUTOFCONTEXT callbacks are dispatched on the installing UI thread.
thread_local std::unordered_map<HWINEVENTHOOK, HWND> attentionTargets;
void CALLBACK AttentionChanged(HWINEVENTHOOK hook, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    const auto found = attentionTargets.find(hook);
    if (found != attentionTargets.end()) PostMessageW(found->second, kAttention, 0, 0);
}
constexpr UINT kRetryMilliseconds = 30000;
std::wstring Wide(const std::string& text) {
    if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return {};
    const int length = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (!count) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, result.data(), count);
    return result;
}
std::wstring LocalText(const todo::ILocalTimePort& time, todo::Millis at) {
    const auto local = time.ToLocal(at);
    if (!local) return L"时间不可用";
    wchar_t value[48]{};
    std::swprintf(value, sizeof(value) / sizeof(value[0]), L"%04d-%02d-%02d %02d:%02d",
                  local->date.year, local->date.month, local->date.day, local->hour, local->minute);
    return value;
}
ReminderNotice Notice(const todo::TaskView& task, const todo::ILocalTimePort& time, todo::Millis now) {
    ReminderNotice result{task.id, *task.reminderAt, Wide(task.title), {}, {}};
    if (task.schedule) {
        result.time = LocalText(time, task.schedule->startAt) + L" — " + LocalText(time, task.schedule->endAt);
        result.status = now < task.schedule->startAt ? L"即将开始" : now < task.schedule->endAt ? L"进行中" : L"已结束";
    } else {
        result.time = LocalText(time, *task.reminderAt);
        result.status = L"提醒时间已到";
    }
    return result;
}
bool Active(const todo::TaskView& task) {
    return !task.id.empty() && !task.completed && !task.deletedAt && task.reminderAt.has_value();
}
} // namespace

struct LocalReminderRuntime::Impl : std::enable_shared_from_this<Impl> {
    HINSTANCE instance;
    reminder::ReminderLedger ledger;
    LocalReminderPorts ports;
    ReminderAppearance appearance;
    std::unique_ptr<ReminderPresenter> presenter;
    HWND window = nullptr;
    HWINEVENTHOOK attentionHook = nullptr;
    bool attentionTimerArmed = false;
    bool started = false, stopped = false, refreshing = false, posted = false;
    bool refreshClock = false, timerArmed = false, retry = false;
    std::size_t pending = 0;
    std::optional<todo::Millis> nextAt;
    std::vector<reminder::Key> quietKeys;
    LocalReminderError error = LocalReminderError::None;
    using State = std::tuple<bool, std::size_t, std::size_t, LocalReminderError, reminder::ErrorCode>;
    std::optional<State> lastState;

    Impl(HINSTANCE i, settings::IFileStore& files, std::string path, LocalReminderPorts p, ReminderAppearance a)
        : instance(i), ledger(files, std::move(path)), ports(std::move(p)), appearance(a) {}
    ~Impl() { Stop(); }
    bool Allowed() const {
        return ports.canPresent ? ports.canPresent() : ReminderMayPresent(QueryReminderAttention());
    }
    void WatchAttention() {
        const bool needed = presenter && presenter->Count() > 0;
        if (needed && !attentionTimerArmed)
            attentionTimerArmed = SetTimer(window, kAttentionTimer, 250, nullptr) != 0;
        else if (!needed && attentionTimerArmed) {
            KillTimer(window, kAttentionTimer); attentionTimerArmed = false;
        }
        // If monitoring cannot be armed, prefer dismissing a notice to leaving
        // an unmonitored window over a future game or work session.
        if (needed && !attentionTimerArmed) presenter->Keep({});
    }
    void CheckAttention() {
        if (stopped || !presenter || !presenter->Count()) return;
        if (!Allowed()) { presenter->Keep({}); Queue(); }
        WatchAttention();
    }
    bool Work() const {
        return started && !stopped && ((presenter && presenter->Count()) || pending || nextAt || retry);
    }
    void Changed() {
        const State state{Work(), presenter ? presenter->Count() : 0, pending, error, ledger.Error()};
        if (lastState && *lastState == state) return;
        lastState = state;
        const auto callback = ports.changed;
        if (callback) callback();
    }
    void Queue(bool clock = false) {
        if (!started || stopped || !window) return;
        refreshClock = refreshClock || clock;
        if (!posted) posted = PostMessageW(window, kRefresh, 0, 0) != FALSE;
    }
    void CancelTimer() {
        if (window && timerArmed) KillTimer(window, kTimer);
        timerArmed = false;
    }
    void Arm(todo::Millis now) {
        CancelTimer();
        if (!window || stopped || (!nextAt && !retry)) return;
        std::uint64_t delay = retry ? kRetryMilliseconds : USER_TIMER_MAXIMUM;
        if (nextAt) {
            const auto difference = *nextAt > now ? static_cast<std::uint64_t>(*nextAt) - static_cast<std::uint64_t>(now) : 1ULL;
            delay = (std::min)(delay, difference);
        }
        delay = (std::clamp)(delay, static_cast<std::uint64_t>(USER_TIMER_MINIMUM), static_cast<std::uint64_t>(USER_TIMER_MAXIMUM));
        timerArmed = SetTimer(window, kTimer, static_cast<UINT>(delay), nullptr) != 0;
        if (!timerArmed) { error = LocalReminderError::TimerFailed; retry = true; }
    }
    void Refresh() {
        if (!started || stopped) return;
        if (refreshing) { Queue(); return; }
        refreshing = true;
        CancelTimer();
        if (std::exchange(refreshClock, false) && ports.refreshTime) ports.refreshTime();
        if (stopped) { refreshing = false; return; }
        auto snapshot = ports.read();
        const auto now = ports.now();
        if (stopped) { refreshing = false; return; }
        // Each occurrence of a repeating schedule is its own (task, time) reminder.
        todo::AppendReminderOccurrences(snapshot.tasks, now, *ports.time);
        error = LocalReminderError::None;
        retry = false;
        pending = 0;
        nextAt.reset();
        if (!snapshot.writable) {
            // An unavailable repository is not an authoritative empty list.
            // Preserve visible notices and retry the injected repository/time
            // refresh before evaluating delivery again.
            error = LocalReminderError::RepositoryUnavailable;
            retry = true;
            pending = static_cast<std::size_t>(std::count_if(snapshot.tasks.begin(), snapshot.tasks.end(), Active));
        } else if (!ledger.Ready() && !ledger.Load()) {
            error = LocalReminderError::LedgerLoadFailed;
            pending = static_cast<std::size_t>(std::count_if(snapshot.tasks.begin(), snapshot.tasks.end(), Active));
            retry = pending > 0;
        } else {
            // A failed disk write must not turn a suppressed reminder into a
            // popup when attention becomes available again in this session.
            if (!quietKeys.empty()) {
                if (ledger.MarkHandled(quietKeys)) quietKeys.clear();
                else { error = LocalReminderError::LedgerWriteFailed; retry = true; }
            }
            const auto plan = ledger.Evaluate(snapshot, now);
            nextAt = plan.nextAt;
            // Evaluate excludes handled keys; already displayed windows must be
            // retained from the current repository, including handled keys.
            std::vector<ReminderNotice> active;
            for (const auto& task : snapshot.tasks)
                if (Active(task) && *task.reminderAt <= now) active.push_back(Notice(task, *ports.time, now));
            presenter->Keep(active);
            pending = plan.due.size();
            if (!plan.expired.empty() && !ledger.MarkHandled(plan.expired)) {
                error = LocalReminderError::LedgerWriteFailed;
                retry = true;
            }
            // The dynamic island decides for itself (it steps aside for fullscreen
            // apps and games), so it gets due reminders before the popup policy.
            auto due = plan.due;
            if (!retry && ports.deliver) {
                std::vector<todo::TaskView> left;
                for (const auto& task : due) {
                    if (!retry && ports.deliver(Notice(task, *ports.time, now))) {
                        if (!ledger.MarkHandled({{task.id, *task.reminderAt}})) { error = LocalReminderError::LedgerWriteFailed; retry = true; }
                        else --pending;
                        continue;
                    }
                    left.push_back(task);
                }
                due = std::move(left);
            }
            if (!retry && !Allowed()) {
                // Consume only reminder delivery keys; never complete/delete tasks.
                // No backlog is displayed when the user returns from a game.
                presenter->Keep({});
                for (const auto& task : due) quietKeys.push_back({task.id, *task.reminderAt});
                if (!quietKeys.empty() && !ledger.MarkHandled(quietKeys)) {
                    error = LocalReminderError::LedgerWriteFailed; retry = true;
                } else { quietKeys.clear(); pending = 0; }
            } else if (!retry) {
                for (const auto& task : due) {
                    if (!presenter->CanShow()) {
                        if (presenter->Count() == 0 && presenter->Capacity() > 0) {
                            error = LocalReminderError::PresenterFailed; retry = true;
                        }
                        break;
                    }
                    bool committed = false;
                    bool attempted = false;
                    const bool shown = presenter->Show(Notice(task, *ports.time, now), appearance, [&] {
                        attempted = true;
                        if (!Allowed()) quietKeys.push_back({task.id, *task.reminderAt});
                        committed = ledger.MarkHandled({{task.id, *task.reminderAt}});
                        return committed;
                    });
                    if (!shown) {
                        error = !attempted || committed
                            ? LocalReminderError::PresenterFailed : LocalReminderError::LedgerWriteFailed;
                        retry = true;
                        break;
                    }
                    --pending;
                }
            }
        }
        if (!stopped) { WatchAttention(); Arm(now); }
        refreshing = false;
        if (!stopped) Changed();
    }
    bool Start() {
        if (stopped) return false;
        if (started) return true;
        if (!ports.read || !ports.time || !ports.now) { error = LocalReminderError::InvalidPorts; return false; }
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc); wc.hInstance = instance; wc.lpfnWndProc = Proc; wc.lpszClassName = kClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            error = LocalReminderError::WindowCreationFailed; return false;
        }
        // Broadcasts do not reach message-only windows. This top-level window
        // is never shown and has no taskbar button or input surface.
        if (!CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP,
                             0, 0, 0, 0, nullptr, nullptr, instance, this)) {
            error = LocalReminderError::WindowCreationFailed; return false;
        }
        const std::weak_ptr<Impl> weak = shared_from_this();
        presenter = std::make_unique<ReminderPresenter>(instance,
            [weak](const std::string& id) {
                const auto self = weak.lock();
                if (!self || self->stopped) return false;
                const auto callback = self->ports.openTask;
                return callback && callback(id);
            }, [weak] { if (const auto self = weak.lock()) self->Queue(); },
            [weak] { const auto self = weak.lock(); return self && !self->stopped && self->Allowed(); });
        attentionHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
            AttentionChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
        if (attentionHook) attentionTargets.emplace(attentionHook, window);
        presenter->UpdateAppearance(appearance);
        started = true;
        Refresh();
        return !stopped;
    }
    void Stop() {
        if (stopped) return;
        stopped = true;
        if (attentionHook) {
            attentionTargets.erase(attentionHook); UnhookWinEvent(attentionHook); attentionHook = nullptr;
        }
        if (attentionTimerArmed && window) KillTimer(window, kAttentionTimer);
        attentionTimerArmed = false;
        CancelTimer();
        if (presenter) presenter->Shutdown();
        if (window) DestroyWindow(window);
        started = false; posted = false; pending = 0; nextAt.reset(); retry = false;
        ports.openTask = {}; ports.changed = {};
    }
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) {
            const auto self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        const auto raw = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!raw) return DefWindowProcW(hwnd, message, wp, lp);
        const auto self = raw->shared_from_this();
        switch (message) {
        case WM_NCDESTROY:
            self->window = nullptr; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); break;
        case kRefresh: self->posted = false; self->Refresh(); return 0;
        case kAttention: self->CheckAttention(); return 0;
        case WM_TIMER:
            if (wp == kAttentionTimer) { self->CheckAttention(); return 0; }
            if (wp == kTimer) { self->CancelTimer(); self->Queue(self->retry); return 0; }
            break;
        case WM_TIMECHANGE: self->Queue(true); return 0;
        case WM_SETTINGCHANGE:
            // Includes TimeZoneInformation and work-area changes. Refreshing
            // the injected clock is inexpensive and avoids OS-specific names.
            if (self->presenter) self->presenter->UpdateAppearance(self->appearance);
            self->Queue(true); return 0;
        case WM_DISPLAYCHANGE:
            if (self->presenter) self->presenter->UpdateAppearance(self->appearance);
            self->Queue(); return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND || wp == PBT_APMRESUMECRITICAL) self->Queue(true);
            return TRUE;
        case WM_CLOSE: return 0;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
};

LocalReminderRuntime::LocalReminderRuntime(HINSTANCE i, settings::IFileStore& files, std::string path,
                                         LocalReminderPorts ports, ReminderAppearance appearance)
    : impl_(std::make_shared<Impl>(i, files, std::move(path), std::move(ports), appearance)) {}
LocalReminderRuntime::~LocalReminderRuntime() { Shutdown(); }
bool LocalReminderRuntime::Start() { const auto self = impl_; return self->Start(); }
void LocalReminderRuntime::Refresh() { const auto self = impl_; self->Refresh(); }
void LocalReminderRuntime::UpdateAppearance(const ReminderAppearance& appearance) {
    const auto self = impl_;
    if (self->stopped) return;
    self->appearance = appearance;
    if (self->presenter) self->presenter->UpdateAppearance(appearance);
    self->Queue();
}
void LocalReminderRuntime::Shutdown() { const auto self = impl_; self->Stop(); }
bool LocalReminderRuntime::HasWork() const { return impl_->Work(); }
HWND LocalReminderRuntime::Handle() const { return impl_->window; }
std::size_t LocalReminderRuntime::PresenterCount() const { return impl_->presenter ? impl_->presenter->Count() : 0; }
HWND LocalReminderRuntime::PresenterHandle(std::size_t index) const { return impl_->presenter ? impl_->presenter->Handle(index) : nullptr; }
std::size_t LocalReminderRuntime::PendingCount() const { return impl_->pending; }
bool LocalReminderRuntime::TimerArmed() const { return impl_->timerArmed; }
std::optional<todo::Millis> LocalReminderRuntime::NextAt() const { return impl_->nextAt; }
LocalReminderError LocalReminderRuntime::Error() const { return impl_->error; }
reminder::ErrorCode LocalReminderRuntime::LedgerError() const { return impl_->ledger.Error(); }
} // namespace lume::platform::windows
