#pragma once
#include "core/clock/clock_types.h"
#include <windows.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace lume::platform::windows {
struct ClockBitmap {
    unsigned width = 0;
    unsigned height = 0;
    int originX = 0;
    int originY = 0;
    std::vector<std::uint32_t> pixels;
};
// Owns a DirectWrite private font collection. Missing original assets fail explicitly.
// Pixels are top-down premultiplied BGRA, suitable for UpdateLayeredWindow.
class ClockRenderer final {
public:
    ClockRenderer();
    ~ClockRenderer();
    bool Initialize();
    bool Render(clock::Style style, const clock::Appearance& appearance,
                const SYSTEMTIME& time, unsigned dpi, ClockBitmap& bitmap);
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace lume::platform::windows
