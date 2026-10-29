#pragma once
#include <windows.h>
#include <array>

namespace lume::platform::windows {
// Dock-only policy: no application window may be below the Dock. Other desktop
// widgets retain their existing direct-selection behavior.
class DockDesktopLayer final {
public:
    static constexpr UINT Message = WM_APP + 0x7c2;
    static constexpr UINT_PTR FallbackTimer = 0x7c2;
    DockDesktopLayer() = default;
    DockDesktopLayer(const DockDesktopLayer&) = delete;
    DockDesktopLayer& operator=(const DockDesktopLayer&) = delete;
    ~DockDesktopLayer() { Stop(); }
    void Start(HWND icons, HWND bar);
    void Stop();
    void Place();
    void Consumed() { pending_ = false; }
    // True while a maximized application window fills the Dock's monitor; the
    // Dock is then hidden instead of waiting below it.
    bool Covered() const { return covered_; }
private:
    void Request();
    static void CALLBACK Event(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    HWND icons_ = nullptr, bar_ = nullptr;
    std::array<HWINEVENTHOOK, 4> hooks_{};
    bool pending_ = false, covered_ = false, fallback_ = false;
};
}  // namespace lume::platform::windows
