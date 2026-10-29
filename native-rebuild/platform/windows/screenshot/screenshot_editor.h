#pragma once
#include "platform/windows/screenshot/screenshot_image.h"
#include <functional>
#include <memory>

namespace lume::platform::windows::capture {
struct EditorPorts {
    std::function<bool(Image&,POINT&)> capture;
    std::function<bool(HWND,const Image&)> copy;
    std::function<bool(HWND,const Image&)> save;
};
class ScreenshotEditor final {
public:
    // WM_COMMAND IDs follow capture::Command order for native automation/accessibility.
    static constexpr UINT CommandFirst=4200;
    explicit ScreenshotEditor(HINSTANCE,EditorPorts ports={});
    ~ScreenshotEditor();
    ScreenshotEditor(const ScreenshotEditor&)=delete;
    ScreenshotEditor& operator=(const ScreenshotEditor&)=delete;
    bool Start();
    void Stop();
    bool Active()const;
    HWND Handle()const;
    Image Result()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
