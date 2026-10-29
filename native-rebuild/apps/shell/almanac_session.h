#pragma once
#include "core/settings/file_store.h"
#include "core/ui/page_bridge.h"
#include "core/widget/widget_preferences.h"
#include "core/calendar/calendar_date.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
namespace lume::shell {
class AlmanacSession final {
public:
    using Notify=std::function<void(const std::wstring&,bool)>;
    using Now=std::function<SYSTEMTIME()>;
    AlmanacSession(HINSTANCE,std::string path,Notify,std::function<void()> settings,
                   settings::IFileStore* files=nullptr,Now now={});
    ~AlmanacSession();
    bool Restore();
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::wstring State()const;
    void Shutdown();
    bool Running()const;
    HWND Handle()const;
    const widget::Preferences& Snapshot()const;
    calendar::Date DisplayedDate()const;
    unsigned DataRevision()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
