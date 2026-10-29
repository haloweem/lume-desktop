#pragma once
#include "core/organizer/organizer_store.h"
#include "core/organizer/desktop_organization.h"
#include "core/ui/page_bridge.h"
#include <windows.h>
#include <memory>
namespace lume::shell {
class OrganizerSession final {
public:
    using Notify=std::function<void(const std::wstring&,bool)>;
    using OpenTarget=std::function<bool(HWND,const std::wstring&)>;
    // Drags files out to other programs; true when they were released over the
    // Windows desktop, which takes the icons back out of the organizer.
    using DragTargets=std::function<bool(HWND,const std::vector<std::wstring>&)>;
    OrganizerSession(HINSTANCE,std::string,Notify,std::function<void()> settings,
        settings::IFileStore* files=nullptr,OpenTarget open={},DragTargets drag={},bool automaticDesktop=false,
        std::vector<std::wstring> desktopRoots={});
    ~OrganizerSession();
    bool Restore();
    std::optional<std::wstring> HandlePage(ui::PageCommand);
    std::wstring State()const;
    void Shutdown();
    bool Running()const;
    HWND Handle()const;
    HWND Handle(std::string_view collectionId)const;
    bool AddPaths(const std::vector<std::string>&);
    bool AddPaths(std::string_view collectionId,const std::vector<std::string>&);
    const organizer::State& Snapshot()const;
    // UI-thread snapshot port, also used by deterministic desktop fixtures.
    void SynchronizeDesktop(organizer::DesktopSnapshot);
    // Verification: runs the end-of-move-loop merge for collectionId's window
    // at the current cursor position, as releasing a dragged header does.
    bool MergeAtCursorForTest(std::string_view collectionId);
    // Verification: decides whether a screen point counts as the Windows desktop.
    void SetDesktopProbeForTest(std::function<bool(POINT)>);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
