#pragma once
#include <cstdint>
#include <cstdlib>

namespace lume::desktop_icons {
// Input coordinates are physical pixels, including negative monitor coordinates.
// The transport supplies Windows' double-click time and half-rectangle tolerances.
class DoubleClick final {
public:
    bool Press(std::uintptr_t target, bool blank, int x, int y, std::uint32_t now,
               std::uint32_t interval, int halfWidth, int halfHeight) noexcept {
        if (!blank || !target) { Reset(); return false; }
        const bool matched = armed_ && target_ == target &&
            std::uint32_t(now - time_) <= interval &&
            std::llabs(static_cast<long long>(x) - x_) <= halfWidth &&
            std::llabs(static_cast<long long>(y) - y_) <= halfHeight;
        if (matched) { Reset(); return true; }
        armed_ = true; target_ = target; x_ = x; y_ = y; time_ = now;
        return false;
    }
    void Reset() noexcept { armed_ = false; }
private:
    bool armed_ = false;
    std::uintptr_t target_ = 0;
    int x_ = 0, y_ = 0;
    std::uint32_t time_ = 0;
};
}
