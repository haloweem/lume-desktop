#pragma once

#include "core/clock/clock_types.h"
#include "core/widget/widget_registry.h"
#include "platform/windows/widget_ui/widget_snap_controller.h"
#include <windows.h>
#include <functional>
#include <memory>

namespace lume::platform::windows {
class ClockRenderer;
class NativeClockWindow final : public widget::IWidget {
public:
    NativeClockWindow(HINSTANCE instance, std::function<void(clock::Action)> handler);
    ~NativeClockWindow() override;
    NativeClockWindow(const NativeClockWindow&) = delete;
    NativeClockWindow& operator=(const NativeClockWindow&) = delete;
    bool Configure(clock::Style style, const clock::Appearance& appearance);
    bool Start() override;
    void Stop() override;
    void Suspend() override;
    void Resume() override;
    void Dispose() override;
    HWND Handle() const { return window_; }
    bool IsRunning() const { return window_ != nullptr && !suspended_; }
    void Center();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    bool Paint();
    void ArmMinuteTimer();
    void ShowMenu(POINT position);
    void Dispatch(clock::Action action);
    void ClearSnapGuides();
    POINT Constrain(POINT position, SIZE size) const;
    HINSTANCE instance_;
    std::function<void(clock::Action)> handler_;
    HWND window_ = nullptr;
    clock::Style style_ = clock::Style::Welfare;
    clock::Appearance appearance_;
    std::unique_ptr<ClockRenderer> renderer_;
    bool suspended_ = false;
    bool disposed_ = false;
    bool dragging_ = false;
    bool menuOpen_ = false;
    POINT dragCursor_{};
    POINT dragOrigin_{};
    UINT dpi_ = 96;
    SIZE size_{};
    POINT contentOffset_{};
    unsigned snapEdges_=0;
    HWND snapSource_=nullptr;
    WidgetSnapController dragSnap_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};
} // namespace lume::platform::windows
