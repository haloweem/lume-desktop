#pragma once

#include "core/window/window_backend.h"

#include <windows.h>

#include <unordered_map>

namespace lume::window {

class Win32WindowBackend final : public IWindowBackend {
public:
    Win32WindowBackend() = default;
    ~Win32WindowBackend() override;

    bool Start() override;
    void Stop() override;
    bool Create(WindowId id, WindowRole role, const PhysicalRect& bounds, bool clickThrough) override;
    void Show(WindowId id) override;
    void Hide(WindowId id) override;
    void Move(WindowId id, const PhysicalRect& bounds) override;
    void SetClickThrough(WindowId id, bool clickThrough) override;
    void Destroy(WindowId id) override;

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    HWND Find(WindowId id) const noexcept;
    bool IsClickThrough(HWND window) const noexcept;

    HINSTANCE instance_ = nullptr;
    ATOM windowClass_ = 0;
    std::unordered_map<WindowId, HWND> windows_;
    std::unordered_map<HWND, bool> clickThrough_;
};

}  // namespace lume::window
