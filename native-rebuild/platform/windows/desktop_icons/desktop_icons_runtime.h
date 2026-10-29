#pragma once
#include "platform/windows/desktop_icons/desktop_icons_port.h"
#include <functional>
#include <memory>

namespace lume::platform::windows {
enum class DesktopIconsError { None, Startup, InputBusy, Visibility, Save, Rollback };
struct DesktopIconsSnapshot {
    desktop_icons::Preferences preferences;
    bool running = false;
    unsigned desktops = 0;
    unsigned autoHidden = 0;
    unsigned inputDevices = 0;
    unsigned eventHooks = 0;
    unsigned pendingRestores = 0;
    bool scheduled = false;
    DesktopIconsError error = DesktopIconsError::None;
};
struct DesktopIconsCallbacks {
    // Invoked serially on the worker. Must not call Start/Configure/Stop.
    std::function<bool(const desktop_icons::Preferences&)> save;
    // Notification only: callers should post to their owning thread to consume Snapshot().
    std::function<void()> changed;
};
class DesktopIconsRuntime final {
public:
    using Discover = std::function<std::vector<DesktopIconsTarget>()>;
    explicit DesktopIconsRuntime(Discover discover = FindDesktopIconsTargets);
    ~DesktopIconsRuntime();
    DesktopIconsRuntime(const DesktopIconsRuntime&) = delete;
    DesktopIconsRuntime& operator=(const DesktopIconsRuntime&) = delete;
    // Start/Configure/Stop are serialized by the owning session. Snapshot is thread safe.
    // Restore does not write settings; a new user enable is persisted before readiness.
    bool Start(const desktop_icons::Preferences&, DesktopIconsCallbacks, bool persistEnable = false);
    // True means the preferences were saved and installed in the runtime policy.
    // A subsequent visibility acknowledgement can still fail; Snapshot().error
    // reports that failure without undoing the persisted preferences.
    bool Configure(const desktop_icons::Preferences&);
    bool Stop();
    DesktopIconsSnapshot Snapshot() const;
    static unsigned ActiveWorkers();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
