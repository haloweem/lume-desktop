#pragma once
#include "platform/windows/dynamic_island/island_window.h"
#include <memory>
#include <string>

namespace lume::platform::windows {
// An on-demand offscreen frame. Uses the production material shader bytecode,
// and IslandWindow::Preview uses the production foreground drawing functions.
class IslandPreviewRenderer final {
public:
    IslandPreviewRenderer();
    ~IslandPreviewRenderer();
    IslandPreviewRenderer(const IslandPreviewRenderer&)=delete;
    IslandPreviewRenderer& operator=(const IslandPreviewRenderer&)=delete;
    WidgetBitmap Render(const IslandPreviewFrame&,const IslandDisplay&,const std::wstring& wallpaper);
    static std::string PngDataUrl(const WidgetBitmap&);
    static std::string JpegDataUrl(const WidgetBitmap&);
    static bool WritePng(const WidgetBitmap&,const std::wstring& path);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
