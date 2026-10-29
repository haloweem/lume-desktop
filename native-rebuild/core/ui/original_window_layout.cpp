#include "core/ui/original_window_layout.h"

#include <algorithm>
#include <cmath>

namespace lume::ui {
namespace {
double JsRound(double value) noexcept {
    // JavaScript Math.round chooses the integer toward +infinity at ties.
    return std::floor(value + 0.5);
}
}  // namespace

// Port of calculateMainWindowBounds and calculateLumeUiZoomFactor from
// history/lume1.0.1/src/lume-electron-shell/host/windowState.js.
// Keep the original rounding, clamp order, and 24-DIP vertical title-bar inset.
PageBounds CalculateOriginalWindowBounds(PageBounds workArea) noexcept {
    constexpr double aspectRatio = 16.0 / 9.0;
    const double maxWidth = std::max(640.0, workArea.width - 32.0);
    const double maxHeight = std::max(360.0, workArea.height - 32.0);
    double finalWidth = JsRound(workArea.width * 0.75);
    double finalHeight = JsRound(finalWidth / aspectRatio);
    const double scaledHeight = JsRound(workArea.height * 0.75);
    if (finalHeight > scaledHeight) {
        finalHeight = scaledHeight;
        finalWidth = JsRound(finalHeight * aspectRatio);
    }
    if (finalWidth < 960 && maxWidth >= 960 && maxHeight >= 540) {
        finalWidth = 960;
        finalHeight = 540;
    }
    if (finalWidth > maxWidth) {
        finalWidth = maxWidth;
        finalHeight = JsRound(finalWidth / aspectRatio);
    }
    if (finalHeight > maxHeight) {
        finalHeight = maxHeight;
        finalWidth = JsRound(finalHeight * aspectRatio);
    }

    const double centerY = workArea.y + std::floor((workArea.height - finalHeight) / 2.0);
    const double minY = workArea.y + 24.0;
    const double maxY = workArea.y + std::max(24.0, workArea.height - finalHeight - 24.0);
    return {
        static_cast<int>(workArea.x + std::floor((workArea.width - finalWidth) / 2.0)),
        static_cast<int>(std::min(maxY, std::max(minY, centerY))),
        static_cast<int>(finalWidth),
        static_cast<int>(finalHeight),
    };
}

PageBounds CalculateManagerWindowBounds(PageBounds workArea) noexcept {
    constexpr double comfortableWidth = 1152, comfortableHeight = 648;
    const auto original = CalculateOriginalWindowBounds(workArea);
    if (original.width >= comfortableWidth) return original;
    double width = comfortableWidth, height = comfortableHeight;
    const double maxWidth = workArea.width - 32.0, maxHeight = workArea.height - 32.0;
    if (width > maxWidth) {
        width = maxWidth;
        height = JsRound(width * 9.0 / 16.0);
    }
    if (height > maxHeight) {
        height = maxHeight;
        width = JsRound(height * 16.0 / 9.0);
    }
    if (width <= original.width) return original;
    // Same centring and 24-DIP title-bar inset as the original layout.
    const double centerY = workArea.y + std::floor((workArea.height - height) / 2.0);
    const double minY = workArea.y + 24.0;
    const double maxY = workArea.y + std::max(24.0, workArea.height - height - 24.0);
    return {
        static_cast<int>(workArea.x + std::floor((workArea.width - width) / 2.0)),
        static_cast<int>(std::min(maxY, std::max(minY, centerY))),
        static_cast<int>(width),
        static_cast<int>(height),
    };
}

double CalculateOriginalZoom(int widthDip, int heightDip) noexcept {
    if (widthDip <= 0 || heightDip <= 0) return 1.0;
    return std::clamp(std::min(widthDip / 1536.0, heightDip / 864.0), 0.5, 2.0);
}

PageBounds RestoreOriginalWindowBounds(PageBounds savedPixels, PageBounds savedWorkAreaPixels,
                                      PageBounds currentWorkAreaPixels, int savedDpi,
                                      int currentDpi) noexcept {
    if (currentWorkAreaPixels.width <= 0 || currentWorkAreaPixels.height <= 0) {
        return {currentWorkAreaPixels.x, currentWorkAreaPixels.y, 0, 0};
    }
    const double scale = static_cast<double>(currentDpi > 0 ? currentDpi : 96) /
                         (savedDpi > 0 ? savedDpi : 96);
    double width = std::max(1.0, JsRound(savedPixels.width * scale));
    double height = std::max(1.0, JsRound(savedPixels.height * scale));
    if (width > currentWorkAreaPixels.width || height > currentWorkAreaPixels.height) {
        constexpr double aspectRatio = 16.0 / 9.0;
        width = std::min(static_cast<double>(currentWorkAreaPixels.width),
                         JsRound(currentWorkAreaPixels.height * aspectRatio));
        height = std::max(1.0, std::min(static_cast<double>(currentWorkAreaPixels.height),
                                     JsRound(width / aspectRatio)));
    }
    const double x = currentWorkAreaPixels.x +
                     JsRound((static_cast<double>(savedPixels.x) - savedWorkAreaPixels.x) * scale);
    const double y = currentWorkAreaPixels.y +
                     JsRound((static_cast<double>(savedPixels.y) - savedWorkAreaPixels.y) * scale);
    return {
        static_cast<int>(std::clamp(x, static_cast<double>(currentWorkAreaPixels.x),
                                   currentWorkAreaPixels.x + currentWorkAreaPixels.width - width)),
        static_cast<int>(std::clamp(y, static_cast<double>(currentWorkAreaPixels.y),
                                   currentWorkAreaPixels.y + currentWorkAreaPixels.height - height)),
        static_cast<int>(width),
        static_cast<int>(height),
    };
}

}  // namespace lume::ui
