#pragma once
#include "platform/windows/widget_ui/widget_canvas.h"
#include <atomic>
#include <memory>
#include <string>

namespace lume::platform::windows {
// Opens Shell targets outside LumeShell so broken third-party shell
// extensions cannot be loaded into the main process.
bool OpenOrganizerTarget(const std::wstring&);

// A finite extraction batch owns one helper. A blocked third-party Shell
// extension can only block that disposable process, never the application.
class OrganizerIconExtractor final {
public:
    explicit OrganizerIconExtractor(std::wstring executable={},unsigned timeoutMs=2500);
    ~OrganizerIconExtractor();
    OrganizerIconExtractor(const OrganizerIconExtractor&)=delete;
    OrganizerIconExtractor& operator=(const OrganizerIconExtractor&)=delete;
    std::shared_ptr<const WidgetBitmap> Read(const std::wstring&,const std::atomic_bool&,unsigned pixelSize=64);
    // False retains observable cleanup ownership; it never means "released".
    bool Stop();
    unsigned ProcessId()const;
    static unsigned ActiveHosts();
    static unsigned PendingCleanup();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
