#pragma once

#include <windows.h>
#include "../lume_window_ids.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lume_glass {

class WakeSignal {
  HANDLE event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
public:
  WakeSignal() { if (!event_) throw std::runtime_error("Cannot create glass wake event"); }
  ~WakeSignal() { CloseHandle(event_); }
  WakeSignal(const WakeSignal&) = delete;
  WakeSignal& operator=(const WakeSignal&) = delete;
  HANDLE Handle() const { return event_; }
  void Notify() const { SetEvent(event_); }
};

// A layer owns its subscription. A capture callback owns only this fanout, so
// callbacks cannot retain a scene or touch a destroyed capture/window object.
class FrameNotifications {
public:
  struct Subscription { std::shared_ptr<WakeSignal> signal; };
private:
  std::mutex mutex_;
  std::vector<std::weak_ptr<Subscription>> subscribers_;
public:
  std::shared_ptr<Subscription> Subscribe(const std::shared_ptr<WakeSignal>& signal) {
    auto subscription = std::make_shared<Subscription>();
    subscription->signal = signal;
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(),
        [](const auto& subscriber) { return subscriber.expired(); }), subscribers_.end());
    subscribers_.push_back(subscription);
    return subscription;
  }
  void Notify() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = subscribers_.begin(); it != subscribers_.end();) {
      if (auto subscription = it->lock()) { subscription->signal->Notify(); ++it; }
      else it = subscribers_.erase(it);
    }
  }
};

class WindowEvents {
  inline static thread_local WindowEvents* current_ = nullptr;
  HWINEVENTHOOK hook_ = nullptr;
  static bool IsDesktopIconWindow(HWND window) {
    for (HWND current = window; current; current = GetParent(current)) {
      wchar_t name[64]{};
      GetClassNameW(current, name, ARRAYSIZE(name));
      if (wcscmp(name, L"SHELLDLL_DefView") == 0 ||
          wcscmp(name, L"SysListView32") == 0 ||
          wcscmp(name, L"Progman") == 0 || wcscmp(name, L"WorkerW") == 0)
        return true;
    }
    return false;
  }
  std::shared_ptr<WakeSignal> signal_;
  static void CALLBACK OnEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object,
      LONG child, DWORD, DWORD) {
    if (!current_) return;
    if (event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_DESTROY &&
        event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_HIDE &&
        event != EVENT_OBJECT_REORDER && event != EVENT_OBJECT_LOCATIONCHANGE) return;
    const bool wholeWindowEvent = object == OBJID_WINDOW && child == CHILDID_SELF;
    const bool desktopIconEvent = object == OBJID_CLIENT && IsDesktopIconWindow(window);
    if (!wholeWindowEvent && !desktopIconEvent) return;
    if (window && GetPropW(window, lume::window_ids::GlassSurfaceOwnerProp)) return;
    current_->signal_->Notify();
  }
public:
  explicit WindowEvents(std::shared_ptr<WakeSignal> signal) : signal_(std::move(signal)) {
    current_ = this;
    hook_ = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
        OnEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
  }
  ~WindowEvents() { if (hook_) UnhookWinEvent(hook_); current_ = nullptr; }
  WindowEvents(const WindowEvents&) = delete;
  WindowEvents& operator=(const WindowEvents&) = delete;
  bool Available() const { return hook_ != nullptr; }
};

}  // namespace lume_glass
