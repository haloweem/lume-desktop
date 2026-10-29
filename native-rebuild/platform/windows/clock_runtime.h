#pragma once
#include "core/clock/clock_controller.h"
#include "core/widget/widget_service.h"
#include "platform/windows/clock/native_clock_window.h"

namespace lume::platform::windows {
class NativeClockRuntime final : public clock::IClockRuntime {
public:
    NativeClockRuntime(HINSTANCE instance, std::function<void(clock::Action)> handler);
    ~NativeClockRuntime() override { Shutdown(); }
    bool Apply(bool enabled, clock::Style style, const clock::Appearance& appearance) override;
    bool IsRunning() const override;
    void Center() override;
    void Shutdown() override;
private:
    widget::WidgetRegistry registry_;
    widget::WidgetService widgets_{registry_};
    NativeClockWindow* surface_ = nullptr;
    clock::Style style_ = clock::Style::Welfare;
    clock::Appearance appearance_;
};
}
