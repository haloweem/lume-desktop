#include "core/wallpaper/wallpaper_service.h"

#include <cstdlib>
#include <iostream>
#include <map>

namespace {
class FakeMpv final : public lume::wallpaper::IMpvController {
public:
    bool Start(std::uint64_t displayId, const std::string& mediaPath) override {
        ++starts; paths[displayId] = mediaPath; return failDisplay != displayId;
    }
    bool Command(std::uint64_t displayId, lume::wallpaper::MpvCommand command) override {
        ++commands; lastCommand[displayId] = command; return true;
    }
    void Stop(std::uint64_t displayId) override { ++stops; paths.erase(displayId); }
    bool Recover(std::uint64_t) override { ++recovers; return true; }
    std::uint64_t failDisplay = 0; int starts = 0, commands = 0, stops = 0, recovers = 0;
    std::map<std::uint64_t, std::string> paths; std::map<std::uint64_t, lume::wallpaper::MpvCommand> lastCommand;
};
void Expect(bool value, const char* message) { if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }

void TestScreensAreIndependent() {
    auto controller = std::make_unique<FakeMpv>(); auto* raw = controller.get();
    lume::wallpaper::WallpaperService service(std::move(controller));
    Expect(service.Start(), "wallpaper start");
    Expect(service.Set(1, "one.mp4"), "screen one");
    Expect(service.Set(2, "two.mp4"), "screen two");
    Expect(service.Pause(1), "pause one"); Expect(service.Resume(1), "resume one");
    Expect(service.Set(1, "changed.mp4"), "switch one");
    Expect(service.state(2).has_value() && service.state(2)->mediaPath == "two.mp4", "screen two preserved");
    Expect(raw->stops == 1 && raw->starts == 3, "switch only owns one player");
    service.Stop(); Expect(raw->stops == 3, "stop releases each player once");
    service.Dispose(); // The borrowed controller is destroyed here; never inspect it afterwards.
    Expect(!service.Start()&&!service.Set(1,"after-dispose.mp4"),"disposed service cannot restart");
}

void TestFailureAndRecovery() {
    auto controller = std::make_unique<FakeMpv>(); auto* raw = controller.get(); raw->failDisplay = 3;
    lume::wallpaper::WallpaperService service(std::move(controller));
    Expect(service.Start(), "start with optional controller");
    Expect(!service.Set(3, "broken.mp4"), "failed display isolated");
    Expect(service.Recover(3), "recovery request"); Expect(raw->recovers == 1, "recovery delegated");
}

void TestAutomaticResumePreservesManualPause() {
    auto controller = std::make_unique<FakeMpv>();
    lume::wallpaper::WallpaperService service(std::move(controller));
    Expect(service.Start(), "start for pause policy");
    Expect(service.Set(1, "manual.mp4") && service.Set(2, "automatic.mp4"), "two playing displays");
    Expect(service.Pause(1), "manual pause before session lock");
    service.Suspend();
    service.Resume();
    Expect(service.state(1)->state == lume::wallpaper::WallpaperState::Paused,
        "automatic resume must preserve the user's manual pause");
    Expect(service.state(2)->state == lume::wallpaper::WallpaperState::Playing,
        "automatic resume restores only automatically paused playback");
}
}
int main() { TestScreensAreIndependent(); TestFailureAndRecovery(); TestAutomaticResumePreservesManualPause(); return 0; }
