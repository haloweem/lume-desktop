#pragma once
#include "platform/windows/widget_ui/widget_canvas.h"
#include <algorithm>
#include <string>

namespace lume::platform::windows {

// Shortens text with a trailing ellipsis until it fits in width.
inline std::wstring FitText(WidgetCanvas& canvas, const std::wstring& text, float width, float size) {
    if (canvas.MeasureText(text, size) <= width) return text;
    // The old one-character-at-a-time loop created one DirectWrite layout per
    // removed character. Expansion redraws this path every display frame, so a
    // long title could turn one frame into dozens of synchronous COM calls.
    // Prefix widths are monotonic; binary search keeps the rendered text the
    // same while reducing the measurement count to O(log n).
    std::size_t low = 0, high = text.size();
    while (low < high) {
        const std::size_t mid = low + (high - low + 1) / 2;
        std::size_t end = mid;
        if (end < text.size() && text[end - 1] >= 0xd800 && text[end - 1] <= 0xdbff) ++end;
        const auto candidate = text.substr(0, std::min(end, text.size())) + L"…";
        if (canvas.MeasureText(candidate, size) <= width) low = std::min(end, text.size());
        else high = mid - 1;
    }
    if (low && low < text.size() && text[low - 1] >= 0xd800 && text[low - 1] <= 0xdbff) --low;
    return text.substr(0, low) + L"…";
}

// The same with a font weight. Never splits a surrogate pair, and an empty or
// negative width gives no text at all.
inline std::wstring FitWeightedText(WidgetCanvas& canvas, std::wstring text, float width, float size, WidgetTextWeight weight) {
    if (width <= 0) return {};
    if (canvas.MeasureText(text, size, weight) <= width) return text;
    std::size_t low = 0, high = text.size();
    while (low < high) {
        const std::size_t mid = low + (high - low + 1) / 2;
        std::size_t end = mid;
        if (end < text.size() && text[end - 1] >= 0xd800 && text[end - 1] <= 0xdbff) ++end;
        const auto candidate = text.substr(0, std::min(end, text.size())) + L"…";
        if (canvas.MeasureText(candidate, size, weight) <= width) low = std::min(end, text.size());
        else high = mid - 1;
    }
    if (low && low < text.size() && text[low - 1] >= 0xd800 && text[low - 1] <= 0xdbff) --low;
    return text.substr(0, low) + L"…";
}

}  // namespace lume::platform::windows
