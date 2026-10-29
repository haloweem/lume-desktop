#pragma once
#include "core/organizer/desktop_organization.h"
#include <windows.h>
#include <functional>
#include <memory>
namespace lume::platform::windows {
// UI-thread owner, background filesystem/shortcut reads, message-only receiver.
// Watches user/public desktop roots only; never recursively scans or moves files.
class DesktopOrganizerSource final {
public:
    using Receive=std::function<void(organizer::DesktopSnapshot)>;
    DesktopOrganizerSource(HINSTANCE,Receive,std::vector<std::wstring> roots={});
    ~DesktopOrganizerSource();
    bool Start();
    void Refresh();
    void Stop();
    bool Running()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
