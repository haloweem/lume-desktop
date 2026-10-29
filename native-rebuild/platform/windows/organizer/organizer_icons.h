#pragma once
#include "platform/windows/widget_ui/widget_canvas.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace lume::platform::windows {
// The cache is shared by the Dock and the launchpad. It must be larger than
// organizer::MaxItems: the launchpad can contribute MaxItems paths and the
// Dock adds its own pinned/running entries on top.
inline constexpr std::size_t MaxIconPaths = 256;
unsigned OrganizerIconPixelsForDpi(unsigned dpi);
std::shared_ptr<const WidgetBitmap> ReadOrganizerIcon(const std::wstring&,const std::atomic_bool& cancelled,unsigned pixelSize=64);
// Independent feature resource. Paint only reads immutable pixels; Shell
// extraction runs serially in a disposable process, never the material layer.
// The worker exits after each finite batch; cached pixels need no idle thread.
class OrganizerIconCache final {
public:
    using Loader=std::function<std::shared_ptr<const WidgetBitmap>(const std::wstring&,const std::atomic_bool&,unsigned pixelSize)>;
    using Notify=std::function<void()>;
    // Recent mode retains other tabs' completed icons in a bounded LRU cache.
    // Default mode keeps only wanted paths, as used by the Dock.
    explicit OrganizerIconCache(Loader={},bool keepRecent=false);
    ~OrganizerIconCache();
    OrganizerIconCache(const OrganizerIconCache&)=delete;
    OrganizerIconCache& operator=(const OrganizerIconCache&)=delete;
    // UI-owner calls are serialized. Notify runs on the worker and may only
    // post a notification; it must not call SetPaths/Stop.
    // A size change refreshes each path once while retaining its old visible
    // bitmap. Completed paths keep only one bitmap, not one per historical DPI.
    // Input order is load priority. Recent mode retains at most MaxIconPaths
    // completed paths; current paths are protected from eviction.
    bool SetPaths(const std::vector<std::wstring>&,Notify,unsigned pixelSize=64);
    // Explicit owner polling: two delayed recovery batches, then stop until
    // the wanted paths/DPI change. Successful icons are never re-extracted.
    bool RetryMissing();
    std::shared_ptr<const WidgetBitmap> Find(const std::wstring&)const;
    std::size_t CachedPaths()const;
    void Stop();
    static unsigned ActiveWorkers();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
