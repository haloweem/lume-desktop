#include "platform/windows/clock_runtime.h"
#include <utility>

namespace lume::platform::windows {
NativeClockRuntime::NativeClockRuntime(HINSTANCE instance, std::function<void(clock::Action)> handler) {
    registry_.Register("time", [this,instance,handler=std::move(handler)]() -> std::unique_ptr<widget::IWidget> {
        auto window = std::make_unique<NativeClockWindow>(instance, handler);
        if (!window->Configure(style_, appearance_)) return {};
        surface_ = window.get();
        return window;
    });
}
bool NativeClockRuntime::Apply(bool enabled, clock::Style style, const clock::Appearance& appearance) {
    style_ = style;
    appearance_ = appearance;
    if (!enabled && surface_) widgets_.Hide("time");
    if (surface_ && !surface_->Configure(style, appearance)) return false;
    if (enabled) {
        widgets_.Start();
        const bool hadSurface = surface_ != nullptr;
        if (!widgets_.Show("time")) {
            if (!hadSurface) surface_ = nullptr;
            return false;
        }
    }
    return true;
}
bool NativeClockRuntime::IsRunning() const { return surface_ && surface_->IsRunning(); }
void NativeClockRuntime::Center() { if (surface_) surface_->Center(); }
void NativeClockRuntime::Shutdown() { widgets_.Dispose(); surface_ = nullptr; }
}
