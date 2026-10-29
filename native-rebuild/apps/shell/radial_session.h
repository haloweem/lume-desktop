#pragma once
#include "core/radial/radial_menu.h"
#include "core/ui/page_bridge.h"
#include "core/settings/file_store.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
namespace lume::shell {
struct RadialSessionOptions {
    std::string preferencesPath;
    bool verification=false;
    // Test-only ports are ignored in production; tests must never inject global input.
    std::function<bool(POINT)> desktop;
    std::function<bool()> screenshot;
};
class RadialSession final {
public:
    using Notify=std::function<void(const std::wstring&,bool)>;
    RadialSession(HINSTANCE,RadialSessionOptions,Notify,std::function<void(bool)> settings,
                  settings::IFileStore* files=nullptr);
    ~RadialSession();
    bool Restore();
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::wstring State()const;
    bool Running()const;
    HWND Handle()const;
    const radial::Preferences& Snapshot()const;
    void CancelCapture();
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
