#include "platform/windows/taskbar/taskbar_watch.h"
#include <cwchar>

namespace lume::platform::windows {
thread_local TaskbarWatch* TaskbarWatch::current_ = nullptr;
TaskbarWatch::~TaskbarWatch() { Stop(); }
bool TaskbarWatch::Start() {
    if (window_) return true;
    if (current_) return false;
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc; wc.lpszClassName = L"LumeTaskbarWatch";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (!taskbarCreated_) return false;
    // HWND_MESSAGE would not receive Explorer's broadcast messages.
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
                              WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!window_) return false;
    current_ = this;
    foreground_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, EventProc,
                                  0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    objects_ = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, EventProc,
                               0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!foreground_ || !objects_) { Stop(); return false; }
    return true;
}
void TaskbarWatch::Stop() {
    if (foreground_) { UnhookWinEvent(foreground_); foreground_ = nullptr; }
    if (objects_) { UnhookWinEvent(objects_); objects_ = nullptr; }
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
    if (current_ == this) current_ = nullptr;
    dirty_ = false;
}
bool TaskbarWatch::Consume() { const bool dirty = dirty_; dirty_ = false; return dirty; }
LRESULT CALLBACK TaskbarWatch::WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<TaskbarWatch*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<TaskbarWatch*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && (message == self->taskbarCreated_ || message == WM_DISPLAYCHANGE ||
                 message == WM_THEMECHANGED || message == WM_SETTINGCHANGE || message == WM_DWMCOMPOSITIONCHANGED))
        self->dirty_ = true;
    return DefWindowProcW(window, message, w, l);
}
void CALLBACK TaskbarWatch::EventProc(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG, DWORD, DWORD) {
    if (!current_) return;
    if (event == EVENT_SYSTEM_FOREGROUND) { current_->dirty_ = true; return; }
    if (object != OBJID_WINDOW || !window) return;
    const auto root = GetAncestor(window, GA_ROOT);
    if ((event == EVENT_OBJECT_LOCATIONCHANGE || event == EVENT_OBJECT_STATECHANGE) &&
        (root ? root : window) == GetForegroundWindow()) {
        current_->dirty_ = true; return;
    }
    wchar_t name[64]{};
    if (GetClassNameW(root ? root : window, name, 64) &&
        (!wcscmp(name, L"Shell_TrayWnd") || !wcscmp(name, L"Shell_SecondaryTrayWnd")))
        current_->dirty_ = true;
}
}  // namespace lume::platform::windows
