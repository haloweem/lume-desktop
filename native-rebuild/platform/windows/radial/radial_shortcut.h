#pragma once
#include <windows.h>
#include <array>
#include <memory>

namespace lume::platform::windows {
struct RadialKeyDecision { bool suppress=false,invoke=false,dismiss=false; };
class RadialKeyGate final {
public:
    RadialKeyDecision Key(unsigned key,unsigned modifiers,bool down,bool injected,
                          bool desktop,bool enabled,unsigned shortcut);
    bool NeedsRelease()const;
private:
    std::array<bool,256> held_{},consumed_{};
};
bool IsRadialDesktopWindow(HWND);
bool IsRadialDesktopPoint(POINT);
class RadialShortcut final {
public:
    static constexpr UINT InvokeMessage=WM_APP+0x6d1,DismissMessage=WM_APP+0x6d2,
                          ReleaseMessage=WM_APP+0x6d4;
    RadialShortcut();
    ~RadialShortcut();
    bool Start(HWND target,unsigned shortcut,bool paused=false);
    void Configure(unsigned shortcut);
    void Visible(HWND popup);
    // Disable invocation immediately, retire hooks after consumed physical keys release.
    void Disable();
    void Pause(bool);
    void Stop();
    bool Running() const;
    // Consumed trigger state is reliable even when async key state is not.
    bool NeedsRelease() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
