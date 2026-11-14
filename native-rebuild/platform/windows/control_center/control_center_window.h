#pragma once
#include <windows.h>
#include <memory>

namespace lume::platform::windows {
namespace control_center { class PanelWorker; }
// Lume's own control center, opened from the Dock's right end so Wi-Fi,
// Bluetooth, hotspot, airplane mode, volume, output, brightness and the
// notification icons stay reachable while the taskbar is hidden. Native
// Direct2D/DirectWrite UI; system calls run on a worker thread.
class ControlCenterWindow final {
  public:
    // The panel follows the Dock's material: Lume liquid or frosted glass,
    // or an opaque dark panel for "none".
    enum class Material { Liquid, Frosted, None };
    ControlCenterWindow();
    explicit ControlCenterWindow(std::shared_ptr<control_center::PanelWorker> worker);
    ~ControlCenterWindow();
    ControlCenterWindow(const ControlCenterWindow &) = delete;
    ControlCenterWindow &operator=(const ControlCenterWindow &) = delete;
    // anchor: the control center icon on screen. Opens above it, or closes
    // when already open. A click that just closed the panel does not reopen it.
    bool Toggle(HINSTANCE instance, HWND owner, RECT anchor, Material material = Material::Liquid);
    // Read one system snapshot asynchronously into a hidden lightweight HWND.
    // No browser/canvas/material is created and no hidden polling is retained.
    bool Prewarm(HINSTANCE instance, HWND owner, Material material = Material::Liquid);
    void Close();
    bool IsOpen() const noexcept;
    HWND Handle() const noexcept;
    static void SetEnglish(bool english);
  private:
    bool Create(HINSTANCE instance, HWND owner, RECT anchor, Material material, bool show);
    struct Impl;
    std::shared_ptr<Impl> impl_;
    std::shared_ptr<control_center::PanelWorker> worker_;
};
}  // namespace lume::platform::windows
