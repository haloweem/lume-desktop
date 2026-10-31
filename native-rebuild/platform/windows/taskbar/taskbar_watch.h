#pragma once
#include <windows.h>

namespace lume::platform::windows {
// Worker-owned event listener. No timers or polling; Stop releases both hooks.
class TaskbarWatch final {
public:
    enum class Change { None, Appearance, Environment };
    ~TaskbarWatch();
    bool Start();
    void Stop();
    Change Consume();
private:
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK EventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    HWND window_ = nullptr;
    HWINEVENTHOOK foreground_ = nullptr, objects_ = nullptr;
    UINT taskbarCreated_ = 0;
    Change change_ = Change::None;
    static thread_local TaskbarWatch* current_;
};
}  // namespace lume::platform::windows
