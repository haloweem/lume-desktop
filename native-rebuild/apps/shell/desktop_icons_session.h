#pragma once
#include "core/desktop_icons/desktop_icons_store.h"
#include "core/ui/page_bridge.h"
#include "platform/windows/desktop_icons/desktop_icons_runtime.h"
#include <functional>
#include <memory>
#include <optional>

namespace lume::shell {
class DesktopIconsSession final {
public:
    using Notify = std::function<void(const std::wstring&, bool)>;
    DesktopIconsSession(HINSTANCE, std::string path, Notify,
        settings::IFileStore* files = nullptr,
        platform::windows::DesktopIconsRuntime::Discover discover = platform::windows::FindDesktopIconsTargets);
    ~DesktopIconsSession();
    bool Restore();
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::wstring State() const;
    bool Running() const;
    bool HasRecovery() const;
    bool Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
