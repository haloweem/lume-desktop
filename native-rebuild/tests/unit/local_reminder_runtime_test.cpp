#include "platform/windows/reminder/local_reminder_runtime.h"
#include "todo_test_support.h"
#include "platform/windows/reminder/reminder_attention.h"
#include <windowsx.h>
#include <algorithm>
#include <memory>
#include <utility>

using namespace lume::platform::windows;
using todo_test::Expect;
namespace {
void Pump() {
    MSG message{};
    unsigned count = 0;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        Expect(++count < 1000, "refresh messages converge without busy polling");
    }
}
lume::todo::TaskView Task(const std::string& id, lume::todo::Millis at) {
    lume::todo::TaskView task;
    task.id = id;
    task.title = "本地提醒 · " + id;
    task.reminderAt = at;
    task.schedule = lume::todo::Schedule{at + 60000, at + 3600000};
    return task;
}
struct Fixture {
    todo_test::Files files;
    todo_test::Utc time;
    lume::todo::RepositorySnapshot snapshot;
    lume::todo::Millis now = todo_test::At(2026, 9, 26, 12);
    int reads = 0, clockRefreshes = 0, changes = 0;
    std::string opened;
    bool allowed = true;
    Fixture() { snapshot.writable = true; }
    LocalReminderPorts Ports() {
        return {[this] { ++reads; return snapshot; }, &time, [this] { return now; },
                [this] { ++clockRefreshes; }, [this](const std::string& id) { opened = id; return true; },
                [this] { ++changes; }, [this] { return allowed; }};
    }
};
}

int main() {
    const auto instance = GetModuleHandleW(nullptr);
    Expect(ReminderMayPresent({true, true, true, false}), "desktop or ordinary Lume foreground allows reminders");
    Expect(!ReminderMayPresent({true, true, false, false}), "windowed game, browser and office foreground stay quiet");
    Expect(!ReminderMayPresent({true, true, true, true}), "fullscreen Lume also stays quiet");
    Expect(!ReminderMayPresent({false, true, true, false}), "shell busy, presentation, locked and quiet states stay quiet");
    Expect(!ReminderMayPresent({true, false, true, false}), "unknown foreground fails closed");
    Fixture quiet;
    quiet.allowed = false;
    quiet.snapshot.tasks = {Task("game", quiet.now), Task("future-game", quiet.now + 10000)};
    LocalReminderRuntime quietRuntime(instance, quiet.files, "quiet.json", quiet.Ports());
    Expect(quietRuntime.Start() && quietRuntime.PresenterCount() == 0 && quietRuntime.PendingCount() == 0,
           "due reminder while gaming creates no popup or delayed queue");
    lume::reminder::ReminderLedger quietLedger(quiet.files, "quiet.json");
    Expect(quietLedger.Load() && quietLedger.Handled({"game", quiet.now}) &&
           !quietLedger.Handled({"future-game", quiet.now + 10000}), "suppression consumes due keys only");
    Expect(!quiet.snapshot.tasks[0].completed && !quiet.snapshot.tasks[0].deletedAt,
           "suppressed task stays incomplete and readable in calendar");
    quiet.allowed = true;
    quietRuntime.Refresh();
    Expect(quietRuntime.PresenterCount() == 0, "returning to desktop never replays suppressed reminder");
    quiet.now += 10000;
    quietRuntime.Refresh();
    Expect(quietRuntime.PresenterCount() == 1, "later reminder can display normally on desktop");
    const HWND quietWindow = quietRuntime.PresenterHandle(0);
    const auto stableReads = quiet.reads;
    SendMessageW(quietRuntime.Handle(), WM_TIMER, 2, 0);
    Expect(quiet.reads == stableReads, "attention watchdog does not rescan repository while safe");
    quiet.allowed = false;
    NotifyWinEvent(EVENT_SYSTEM_FOREGROUND, quietRuntime.Handle(), OBJID_WINDOW, CHILDID_SELF);
    for (int i = 0; i < 100 && quietRuntime.PresenterCount(); ++i) { Pump(); Sleep(10); }
    Expect(!quietRuntime.PresenterCount() && !IsWindow(quietWindow),
           "foreground WinEvent dismisses already visible reminder without activating another window");
    quiet.allowed = true;
    quietRuntime.Refresh();
    Expect(!quietRuntime.PresenterCount(), "dismissed reminder is never restored on return");
    quietRuntime.Shutdown();
    LocalReminderRuntime quietRestart(instance, quiet.files, "quiet.json", quiet.Ports());
    Expect(quietRestart.Start() && !quietRestart.PresenterCount(), "restart does not replay quiet delivery keys");
    quietRestart.Shutdown();

    Fixture quietFailure;
    quietFailure.allowed = false;
    quietFailure.snapshot.tasks = {Task("quiet-write", quietFailure.now)};
    quietFailure.files.fail = "write:quiet-failure.json.tmp";
    LocalReminderRuntime quietWrite(instance, quietFailure.files, "quiet-failure.json", quietFailure.Ports());
    Expect(quietWrite.Start() && quietWrite.Error() == LocalReminderError::LedgerWriteFailed &&
           quietWrite.TimerArmed() && !quietWrite.PresenterCount(), "quiet ledger failure retries without displaying");
    quietFailure.allowed = true;
    quietWrite.Refresh();
    Expect(!quietWrite.PresenterCount() && quietWrite.Error() == LocalReminderError::LedgerWriteFailed,
           "return from game while quiet ledger write fails still does not replay");
    quietFailure.files.fail.clear();
    quietWrite.Refresh();
    Expect(!quietWrite.PresenterCount() && quietWrite.Error() == LocalReminderError::None,
           "quiet ledger recovery consumes key silently");
    quietWrite.Shutdown();

    Fixture watchdog;
    watchdog.snapshot.tasks = {Task("watchdog", watchdog.now)};
    LocalReminderRuntime watched(instance, watchdog.files, "watchdog.json", watchdog.Ports());
    Expect(watched.Start() && watched.PresenterCount() == 1, "watchdog fixture displays notice");
    watchdog.allowed = false;
    SendMessageW(watched.Handle(), WM_TIMER, 2, 0);
    Pump();
    Expect(!watched.PresenterCount(), "watchdog covers fullscreen or shell state changes without foreground event");
    watched.Shutdown();
    Fixture f;
    LocalReminderRuntime runtime(instance, f.files, "runtime.json", f.Ports());
    Expect(runtime.Start() && runtime.Handle(), "runtime starts without calendar or shell");
    Expect(!IsWindowVisible(runtime.Handle()) && GetParent(runtime.Handle()) == nullptr,
           "event receiver is hidden top-level window for broadcasts");
    Expect(!runtime.HasWork() && !runtime.TimerArmed(), "empty runtime has no scan timer or keepalive");
    const auto reads = f.reads;
    Pump();
    Expect(f.reads == reads, "idle runtime does not scan repository");

    f.snapshot.tasks = {Task("future", f.now + 10000)};
    runtime.Refresh();
    Expect(runtime.HasWork() && runtime.TimerArmed() && runtime.NextAt() == f.now + 10000 && !runtime.PresenterCount(),
           "nearest future reminder arms timer and keeps host alive without calendar");
    f.now += 10000;
    SendMessageW(runtime.Handle(), WM_TIMER, 1, 0);
    Expect(runtime.PresenterCount() == 0, "timer dispatch defers delivery to avoid reentrancy");
    Pump();
    Expect(runtime.PresenterCount() == 1 && runtime.HasWork() && !runtime.TimerArmed(), "due timer displays reminder");
    Expect(f.clockRefreshes == 0, "ordinary due timer does not reload the repository or time port");
    const auto first = runtime.PresenterHandle(0);
    runtime.Refresh();
    Expect(runtime.PresenterCount() == 1 && runtime.PresenterHandle(0) == first,
           "handled displayed reminder is retained and not duplicated on refresh");
    f.snapshot.tasks[0].completed = true;
    runtime.Refresh();
    Pump();
    Expect(runtime.PresenterCount() == 0 && !runtime.HasWork(), "completion removes obsolete reminder and keepalive");

    f.snapshot.tasks = {Task("a", f.now), Task("b", f.now), Task("c", f.now), Task("d", f.now)};
    runtime.Refresh();
    const auto capacity = runtime.PresenterCount();
    Expect(capacity >= 1 && capacity <= 3 && runtime.PendingCount() == 4 - capacity,
           "at most three windows display and remaining reminders queue in memory");
    lume::reminder::ReminderLedger check(f.files, "runtime.json");
    Expect(check.Load() && !check.Handled({"d", f.now}), "queued reminder not prematurely persisted as handled");
    SendMessageW(runtime.PresenterHandle(0), WM_CLOSE, 0, 0);
    Pump();
    Expect(runtime.PresenterCount() == capacity && runtime.PendingCount() == 3 - capacity,
           "closing reminder asynchronously fills available slot from queue");
    // Discard remaining queue through a real snapshot edit, then test the
    // painted view button. No calendar dependency is needed for task routing.
    f.snapshot.tasks.clear();
    runtime.Refresh();
    Pump();
    f.snapshot.tasks = {Task("view", f.now)};
    runtime.Refresh();
    const auto view = runtime.PresenterHandle(0);
    Expect(view != nullptr, "view reminder displayed");
    UpdateWindow(view);
    RECT rect{};
    GetClientRect(view, &rect);
    const auto point = MAKELPARAM(rect.right * 85 / 100, rect.bottom * 85 / 100);
    SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(view, WM_LBUTTONUP, 0, point);
    Pump();
    Expect(f.opened == "view" && runtime.PresenterCount() == 0 && !runtime.HasWork(),
           "view action invokes narrow task callback and closes notice");

    const auto beforeBroadcast = f.clockRefreshes;
    SendMessageW(runtime.Handle(), WM_TIMECHANGE, 0, 0);
    SendMessageW(runtime.Handle(), WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"TimeZoneInformation"));
    SendMessageW(runtime.Handle(), WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
    SendMessageW(runtime.Handle(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
    Expect(f.clockRefreshes == beforeBroadcast, "system notifications defer clock refresh");
    Pump();
    Expect(f.clockRefreshes == beforeBroadcast + 1, "system notification burst coalesces into one clock refresh");

    const auto receiver = runtime.Handle();
    runtime.Shutdown();
    Expect(!IsWindow(receiver) && !runtime.Handle() && !runtime.HasWork() && !runtime.TimerArmed(),
           "shutdown releases receiver timers and host lifetime");
    LocalReminderRuntime reopened(instance, f.files, "runtime.json", f.Ports());
    Expect(reopened.Start() && reopened.PresenterCount() == 0 && !reopened.HasWork(), "restart deduplicates displayed key");
    reopened.Shutdown();

    Fixture failures;
    failures.snapshot.tasks = {Task("retry", failures.now)};
    failures.files.fail = "write:failed.json.tmp";
    LocalReminderRuntime failed(instance, failures.files, "failed.json", failures.Ports());
    Expect(failed.Start() && failed.PresenterCount() == 0 && failed.PendingCount() == 1 && failed.HasWork() &&
           failed.TimerArmed() && failed.Error() == LocalReminderError::LedgerWriteFailed,
           "storage failure does not show or consume reminder and arms bounded retry");
    const auto calls = failures.files.calls.size();
    Pump();
    Expect(failures.files.calls.size() == calls, "failed delivery does not retry in a tight message loop");
    failures.files.fail.clear();
    SendMessageW(failed.Handle(), WM_TIMER, 1, 0);
    Pump();
    Expect(failed.PresenterCount() == 1 && failed.PendingCount() == 0 && failed.Error() == LocalReminderError::None,
           "successful retry persists and displays previously failed delivery");
    Expect(failures.clockRefreshes == 1, "retry timer refreshes the recovery port before delivery");
    const auto failedWindow = failed.PresenterHandle(0);
    failed.Shutdown();
    Expect(!IsWindow(failedWindow) && !failed.PresenterCount(), "shutdown releases displayed reminder windows");

    Fixture corrupt;
    corrupt.snapshot.tasks = {Task("recover", corrupt.now)};
    corrupt.files.data["broken.json"] = "broken";
    LocalReminderRuntime broken(instance, corrupt.files, "broken.json", corrupt.Ports());
    Expect(broken.Start() && broken.Error() == LocalReminderError::LedgerLoadFailed && broken.HasWork() && broken.TimerArmed(),
           "invalid ledger stays explicit and keeps recoverable work alive");
    corrupt.files.data.erase("broken.json");
    SendMessageW(broken.Handle(), WM_TIMER, 1, 0);
    Pump();
    Expect(broken.PresenterCount() == 1 && broken.Error() == LocalReminderError::None, "ledger reload recovers after repair");
    broken.Shutdown();

    Fixture emptyCorrupt;
    emptyCorrupt.files.data["empty-broken.json"] = "broken";
    LocalReminderRuntime emptyBroken(instance, emptyCorrupt.files, "empty-broken.json", emptyCorrupt.Ports());
    Expect(emptyBroken.Start() && emptyBroken.Error() == LocalReminderError::LedgerLoadFailed &&
           emptyBroken.PendingCount() == 0 && !emptyBroken.HasWork() && !emptyBroken.TimerArmed(),
           "bad ledger with no active reminders retains error without timer or host keepalive");
    const auto emptyReads = emptyCorrupt.reads;
    Pump();
    Expect(emptyCorrupt.reads == emptyReads, "bad ledger with no work stays idle");
    emptyBroken.Shutdown();

    Fixture unavailable;
    unavailable.snapshot.writable = false;
    auto unavailablePorts = unavailable.Ports();
    unavailablePorts.refreshTime = [&] {
        ++unavailable.clockRefreshes;
        unavailable.snapshot.writable = true;
        unavailable.snapshot.tasks = {Task("reloaded", unavailable.now)};
    };
    LocalReminderRuntime repositoryRetry(instance, unavailable.files, "unavailable.json", std::move(unavailablePorts));
    Expect(repositoryRetry.Start() && repositoryRetry.Error() == LocalReminderError::RepositoryUnavailable &&
           repositoryRetry.PendingCount() == 0 && repositoryRetry.HasWork() && repositoryRetry.TimerArmed() &&
           repositoryRetry.PresenterCount() == 0 && unavailable.files.calls.empty(),
           "unavailable empty repository reports error and waits for recovery without consuming reminders");
    SendMessageW(repositoryRetry.Handle(), WM_TIMER, 1, 0);
    Expect(unavailable.clockRefreshes == 0, "repository recovery remains asynchronous");
    Pump();
    Expect(unavailable.clockRefreshes == 1 && repositoryRetry.Error() == LocalReminderError::None &&
           repositoryRetry.PresenterCount() == 1 && !repositoryRetry.TimerArmed(),
           "repository retry refreshes port and delivers newly recovered reminder");
    unavailable.snapshot.writable = false;
    unavailable.snapshot.tasks.clear();
    repositoryRetry.Refresh();
    Expect(repositoryRetry.Error() == LocalReminderError::RepositoryUnavailable && repositoryRetry.PresenterCount() == 1,
           "temporary unavailable snapshot does not dismiss already displayed reminder");
    repositoryRetry.Shutdown();

    Fixture queued;
    queued.snapshot.tasks = {Task("q1", queued.now), Task("q2", queued.now), Task("q3", queued.now), Task("q4", queued.now)};
    LocalReminderRuntime queueFirst(instance, queued.files, "queue.json", queued.Ports());
    Expect(queueFirst.Start(), "first queue runtime starts");
    const auto queueCapacity = queueFirst.PresenterCount();
    const auto deferred = queueFirst.PendingCount();
    queueFirst.Shutdown();
    LocalReminderRuntime queueNext(instance, queued.files, "queue.json", queued.Ports());
    Expect(queueNext.Start() && queueNext.PresenterCount() == (std::min)(deferred, queueCapacity),
           "restart reconstructs unhandled queue and suppresses previously displayed keys");
    queueNext.Shutdown();
    Pump();
    std::cout << "local_reminder_runtime_test passed\n";
}
