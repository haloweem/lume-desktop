#pragma once
#include "core/reminder/reminder_ledger.h"
#include "platform/windows/reminder/reminder_presenter.h"
#include <optional>

namespace lume::platform::windows {
struct LocalReminderPorts {
    std::function<todo::RepositorySnapshot()> read;
    const todo::ILocalTimePort* time = nullptr;
    std::function<todo::Millis()> now;
    std::function<void()> refreshTime;
    std::function<bool(const std::string&)> openTask;
    std::function<void()> changed;
    // Optional deterministic policy port; empty uses the Windows attention policy.
    std::function<bool()> canPresent;
    // Optional alternative surface (the dynamic island). Returning true means it
    // took the notice; the classic popup is then not shown for that reminder.
    std::function<bool(const ReminderNotice&)> deliver;
};
enum class LocalReminderError {
    None, InvalidPorts, WindowCreationFailed, LedgerLoadFailed,
    LedgerWriteFailed, PresenterFailed, TimerFailed, RepositoryUnavailable
};

// UI-thread owned, event-driven local delivery. The caller creates the ledger's
// parent directory. No calendar window, shell, account or network dependency.
class LocalReminderRuntime {
public:
    LocalReminderRuntime(HINSTANCE, settings::IFileStore&, std::string ledgerPath,
                         LocalReminderPorts, ReminderAppearance = {});
    ~LocalReminderRuntime();
    LocalReminderRuntime(const LocalReminderRuntime&) = delete;
    LocalReminderRuntime& operator=(const LocalReminderRuntime&) = delete;
    // Success means the event receiver started; inspect Error/LedgerError for
    // recoverable delivery failures. Shutdown is terminal for this instance.
    bool Start();
    void Refresh();
    void UpdateAppearance(const ReminderAppearance&);
    void Shutdown();
    bool HasWork() const;
    HWND Handle() const;
    std::size_t PresenterCount() const;
    HWND PresenterHandle(std::size_t index) const;
    std::size_t PendingCount() const;
    bool TimerArmed() const;
    std::optional<todo::Millis> NextAt() const;
    LocalReminderError Error() const;
    reminder::ErrorCode LedgerError() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace lume::platform::windows
