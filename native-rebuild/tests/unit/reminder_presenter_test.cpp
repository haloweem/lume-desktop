#include "platform/windows/reminder/reminder_presenter.h"
#include <windowsx.h>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace lume::platform::windows;
namespace {
void Check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
LRESULT CALLBACK DenyLayeredSurface(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        const auto* message = reinterpret_cast<CWPRETSTRUCT*>(lp);
        wchar_t name[128]{};
        if (message->message == WM_CREATE) {
            GetClassNameW(message->hwnd, name, 128);
            // Mixing LWA with ULW is rejected by Windows until the layered style
            // is reset. This produces a deterministic failure without allocations.
            if (wcscmp(name, L"LumeNativeReminder") == 0)
                SetLayeredWindowAttributes(message->hwnd, 0, 255, LWA_ALPHA);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
ReminderNotice Notice(const char* id) { return {id, 100, L"Local reminder", L"09:00", L"Due"}; }
void View(HWND window) {
    RECT bounds{}; GetClientRect(window, &bounds);
    const UINT dpi = GetDpiForWindow(window);
    const int x = bounds.right - MulDiv(55, static_cast<int>(dpi), 96);
    const int y = bounds.bottom - MulDiv(31, static_cast<int>(dpi), 96);
    SendMessageW(window, WM_LBUTTONDOWN, 0, MAKELPARAM(x,y));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x,y));
    Pump();
}
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto instance = GetModuleHandleW(nullptr);
    ReminderAppearance appearance;
    ReminderPresenter presenter(instance, {}, {});
    bool committed = false;
    const auto hook = SetWindowsHookExW(WH_CALLWNDPROCRET, DenyLayeredSurface, nullptr, GetCurrentThreadId());
    Check(hook != nullptr, "install failed surface probe");
    const bool failedSurface = presenter.Show(Notice("unpaintable"), appearance, [&] { committed = true; return true; });
    UnhookWindowsHookEx(hook);
    Check(!failedSurface && !committed && presenter.Count() == 0,
          "unpaintable hidden window never commits delivery");
    Check(!presenter.Show(Notice("rejected"), appearance, [&] {
        Check(presenter.Count() == 1 && !presenter.HasVisible(), "pending delivery is created hidden");
        return false;
    }) && presenter.Count() == 0, "commit failure destroys hidden window");
    const HWND foreground = GetForegroundWindow();
    Check(presenter.Show(Notice("first"), appearance, [&] {
        Check(!presenter.HasVisible(), "successful commit still precedes visibility");
        return true;
    }), "show committed reminder");
    Pump();
    const auto window = presenter.Handle(0);
    const auto styles = GetWindowLongPtrW(window, GWL_EXSTYLE);
    Check(presenter.HasVisible() && (styles & WS_EX_TOOLWINDOW) && (styles & WS_EX_NOACTIVATE) &&
          !(styles & WS_EX_TOPMOST) && !(styles & WS_EX_APPWINDOW), "nonactivating tool window never enters topmost band");
    Check(GetForegroundWindow() == foreground, "show preserves foreground window");
    Check(!presenter.Show(Notice("first"), appearance, [] { return true; }), "duplicate visible key refused");
    for (int i = 0; i < 5 && presenter.CanShow(); ++i)
        Check(presenter.Show(Notice(std::to_string(i).c_str()), appearance, [] { return true; }), "fill available capacity");
    Check(presenter.Count() == presenter.Capacity() && presenter.Capacity() <= 3 && !presenter.CanShow(),
          "capacity follows work area and never exceeds three");
    committed = false;
    Check(!presenter.Show(Notice("queued"), appearance, [&] { committed = true; return true; }) && !committed,
          "overflow stays uncommitted for host queue");
    presenter.Keep({});
    Check(presenter.Count() == 0 && !IsWindow(window), "removed tasks destroy their notices");

    int openAttempts = 0;
    ReminderPresenter retryOpen(instance, [&](const std::string& id) {
        Check(id == "retry-open", "retry preserves task identity");
        return ++openAttempts > 1;
    }, {});
    Check(retryOpen.Show(Notice("retry-open"), appearance, [] { return true; }), "create view retry fixture");
    const auto retryWindow = retryOpen.Handle(0);
    View(retryWindow);
    Check(openAttempts == 1 && retryOpen.Count() == 1 && retryOpen.HasVisible(), "failed view keeps reminder visible");
    View(retryWindow);
    Check(openAttempts == 2 && retryOpen.Count() == 0 && !IsWindow(retryWindow), "successful view retry removes reminder");

    std::unique_ptr<ReminderPresenter> dying;
    dying = std::make_unique<ReminderPresenter>(instance, [&](const std::string& id) {
        Check(id == "open", "view passes task identity"); dying.reset(); return true;
    }, [] {});
    Check(dying->Show(Notice("open"), appearance, [] { return true; }), "create view callback fixture");
    Pump(); const auto doomed = dying->Handle(0); View(doomed);
    Check(!dying && !IsWindow(doomed), "view callback may synchronously destroy presenter");

    dying = std::make_unique<ReminderPresenter>(instance, ReminderPresenter::Open{}, [] {});
    const bool stopped = dying->Show(Notice("stop"), appearance, [&] { dying.reset(); return true; });
    Check(!stopped && !dying, "commit callback may synchronously destroy presenter");

    ReminderPresenter reentrant(instance, {}, {});
    Check(!reentrant.Show(Notice("removed"), appearance, [&] { reentrant.Keep({}); return true; }) &&
          reentrant.Count() == 0, "commit callback can synchronously invalidate task");
    bool allowed = true;
    ReminderPresenter guarded(instance, {}, {}, [&] { return allowed; });
    Check(guarded.Show(Notice("attention-race"), appearance, [&] { allowed = false; return true; }) &&
          !guarded.HasVisible() && guarded.Count() == 0, "attention change during commit consumes silently without showing");
    allowed = true;
    Check(guarded.Show(Notice("attention-visible"), appearance, [] { return true; }), "safe attention can show");
    allowed = false;
    guarded.UpdateAppearance(appearance);
    Check(!guarded.HasVisible(), "layout never reveals or promotes notices while attention is blocked");
    guarded.Keep({});
    allowed = true;
    guarded.UpdateAppearance(appearance);
    Check(!guarded.Count(), "dismissed notice stays dismissed when attention returns");
    std::cout << "reminder_presenter_test passed\n";
}
