#include "platform/windows/win32_window_backend.h"

#include <algorithm>

namespace lume::window {
namespace {

constexpr wchar_t kWindowClassName[] = L"LumeNativeRebuildWindow";

}  // namespace

Win32WindowBackend::~Win32WindowBackend() {
    Stop();
}

bool Win32WindowBackend::Start() {
    if (windowClass_ != 0) {
        return true;
    }
    instance_ = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance_;
    windowClass.lpfnWndProc = &Win32WindowBackend::WindowProc;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass_ = RegisterClassExW(&windowClass);
    if (windowClass_ == 0 && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        windowClass_ = 1;
    }
    return windowClass_ != 0;
}

void Win32WindowBackend::Stop() {
    while (!windows_.empty()) {
        Destroy(windows_.begin()->first);
    }
    clickThrough_.clear();
    if (windowClass_ != 0 && windowClass_ != 1) {
        UnregisterClassW(kWindowClassName, instance_);
    }
    windowClass_ = 0;
    instance_ = nullptr;
}

bool Win32WindowBackend::Create(WindowId id, WindowRole, const PhysicalRect& bounds, bool clickThrough) {
    if (windowClass_ == 0 || windows_.find(id) != windows_.end()) {
        return false;
    }
    const DWORD extendedStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    HWND window = CreateWindowExW(
        extendedStyle,
        kWindowClassName,
        L"Lume",
        WS_POPUP,
        bounds.x,
        bounds.y,
        std::max(1, bounds.width),
        std::max(1, bounds.height),
        nullptr,
        nullptr,
        instance_,
        this);
    if (!window) {
        return false;
    }
    windows_.emplace(id, window);
    clickThrough_.emplace(window, clickThrough);
    return true;
}

void Win32WindowBackend::Show(WindowId id) {
    if (HWND window = Find(id)) {
        ShowWindow(window, SW_SHOWNOACTIVATE);
    }
}

void Win32WindowBackend::Hide(WindowId id) {
    if (HWND window = Find(id)) {
        ShowWindow(window, SW_HIDE);
    }
}

void Win32WindowBackend::Move(WindowId id, const PhysicalRect& bounds) {
    if (HWND window = Find(id)) {
        SetWindowPos(window, HWND_TOP, bounds.x, bounds.y, std::max(1, bounds.width), std::max(1, bounds.height), SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

void Win32WindowBackend::SetClickThrough(WindowId id, bool clickThrough) {
    if (HWND window = Find(id)) {
        clickThrough_[window] = clickThrough;
    }
}

void Win32WindowBackend::Destroy(WindowId id) {
    const auto found = windows_.find(id);
    if (found == windows_.end()) {
        return;
    }
    const HWND window = found->second;
    clickThrough_.erase(window);
    windows_.erase(found);
    DestroyWindow(window);
}

HWND Win32WindowBackend::Find(WindowId id) const noexcept {
    const auto found = windows_.find(id);
    return found == windows_.end() ? nullptr : found->second;
}

bool Win32WindowBackend::IsClickThrough(HWND window) const noexcept {
    const auto found = clickThrough_.find(window);
    return found != clickThrough_.end() && found->second;
}

LRESULT CALLBACK Win32WindowBackend::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* backend = reinterpret_cast<Win32WindowBackend*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        backend = static_cast<Win32WindowBackend*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(backend));
    }
    if (message == WM_NCHITTEST && backend && backend->IsClickThrough(window)) {
        return HTTRANSPARENT;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace lume::window
