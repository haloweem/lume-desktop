#pragma once
#include "core/settings/file_store.h"
#include "core/spectrum/spectrum.h"
#include "core/ui/page_bridge.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
namespace lume::shell {
class SpectrumSession final {
public:
    using Notify=std::function<void(const std::wstring&,bool)>;
    SpectrumSession(HINSTANCE,std::string path,Notify,std::function<void()> settings,
                    settings::IFileStore* files=nullptr);
    ~SpectrumSession();
    bool Restore();
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::wstring State()const;
    void Shutdown();
    bool Running()const;
    HWND Handle()const;
    const spectrum::Preferences& Snapshot()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
