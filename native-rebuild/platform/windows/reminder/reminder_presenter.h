#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lume::platform::windows {
struct ReminderNotice {
    std::string taskId;
    std::int64_t reminderAt = 0;
    std::wstring title, time, status;
};
struct ReminderAppearance {
    COLORREF foreground = RGB(255,255,255), accent = RGB(10,132,255);
    RECT anchor{};
};

// Own and call on the UI thread. Count/Handle include temporarily hidden overflow;
// HasVisible reports actual visibility. The host owns scheduling and persistence.
class ReminderPresenter {
public:
    using Open = std::function<bool(const std::string&)>;
    using Changed = std::function<void()>;
    ReminderPresenter(HINSTANCE instance, Open open, Changed changed, std::function<bool()> allowed = {});
    ~ReminderPresenter();
    ReminderPresenter(const ReminderPresenter&) = delete;
    ReminderPresenter& operator=(const ReminderPresenter&) = delete;
    // Work-area capacity, including zero when a complete notice cannot fit.
    std::size_t Capacity() const;
    bool CanShow() const;
    bool Show(const ReminderNotice&, const ReminderAppearance&, std::function<bool()> beforeShow);
    void UpdateAppearance(const ReminderAppearance&);
    void Keep(const std::vector<ReminderNotice>& active);
    bool HasVisible() const;
    std::size_t Count() const;
    HWND Handle(std::size_t index) const;
    void Shutdown();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
