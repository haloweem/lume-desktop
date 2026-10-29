#pragma once
#include "platform/windows/widget_ui/widget_canvas.h"
#include <string>

namespace lume::platform::windows {

// Shortens text with a trailing ellipsis until it fits in width.
inline std::wstring FitText(WidgetCanvas& canvas, const std::wstring& text, float width, float size) {
    if (canvas.MeasureText(text, size) <= width) return text;
    std::wstring result = text;
    while (!result.empty() && canvas.MeasureText(result + L"…", size) > width) result.pop_back();
    return result + L"…";
}

// The same with a font weight. Never splits a surrogate pair, and an empty or
// negative width gives no text at all.
inline std::wstring FitWeightedText(WidgetCanvas& canvas, std::wstring text, float width, float size, WidgetTextWeight weight) {
    if (width <= 0) return {};
    if (canvas.MeasureText(text, size, weight) <= width) return text;
    while (!text.empty() && canvas.MeasureText(text + L"…", size, weight) > width) {
        text.pop_back();
        if (!text.empty() && text.back() >= 0xd800 && text.back() <= 0xdbff) text.pop_back();
    }
    return text + L"…";
}

}  // namespace lume::platform::windows
