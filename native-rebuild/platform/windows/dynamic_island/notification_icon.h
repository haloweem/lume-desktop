#pragma once
#include "platform/windows/widget_ui/widget_canvas.h"
#include <array>
#include <memory>
#include <string_view>

namespace lume::platform::windows {
// UI-thread, per-window cache. Keys select bundled assets, never file paths.
// Missing or invalid files are remembered until Clear, so notification
// rendering and repeated arrivals never retry disk I/O for the same key.
class NotificationIconCache final {
public:
    static constexpr std::size_t Capacity = 11;
    std::shared_ptr<const WidgetBitmap> Load(std::string_view key);
    void Clear();
private:
    std::array<std::shared_ptr<const WidgetBitmap>, Capacity> bitmaps_{};
    std::array<bool, Capacity> attempted_{};
};
} // namespace lume::platform::windows
