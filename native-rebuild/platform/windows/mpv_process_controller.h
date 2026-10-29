#pragma once

#include "core/wallpaper/mpv_controller.h"
#include "platform/windows/wallpaper/static_image.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <memory>
#include <optional>
#include <atomic>
#include <string>
#include <vector>

namespace lume::platform::windows {

struct MpvDisplay {
    std::uint64_t id = 0;
    std::wstring name;
    RECT bounds{};
    bool primary = false;
};

std::vector<MpvDisplay> EnumerateMpvDisplays();

// Lifecycle and playback methods belong to one actor thread. Its message loop
// owns the native containers; destruction must also happen on that thread.
class MpvProcessController final : public lume::wallpaper::IMpvController {
public:
    explicit MpvProcessController(std::wstring executable, HWND verificationParent = nullptr);
    ~MpvProcessController() override;
    MpvProcessController(const MpvProcessController&) = delete;
    MpvProcessController& operator=(const MpvProcessController&) = delete;

    bool Start(std::uint64_t displayId, const std::string& mediaPath) override;
    bool Command(std::uint64_t displayId, lume::wallpaper::MpvCommand command) override;
    void Stop(std::uint64_t displayId) override;
    bool Recover(std::uint64_t displayId) override;

    bool IsRunning(std::uint64_t displayId) const;
    // Private (commit) bytes of the display's mpv process; 0 when none runs.
    std::uint64_t PrivateBytes(std::uint64_t displayId) const;
    // Exit code of the display's mpv process once it has exited.
    std::optional<unsigned long> ExitCode(std::uint64_t displayId) const;
    // Writes what the display shows now as a JPEG: the fitted static image, or
    // mpv's current video frame at source size. Actor-only.
    bool SaveFrame(std::uint64_t displayId, const std::wstring& path);
    std::string LastError() const;
    bool SetVolume(std::uint64_t displayId, int volume);
    bool SetFit(std::uint64_t displayId, bool cover);
    // Actor-only, transactional across active displays; later starts/recovery
    // inherit the last successful value. Each IPC phase has a shared deadline.
    bool SetBalancedMode(bool enabled);
    // Only the in-process protected-media server may register this capability.
    // The prefix must be an exact numeric loopback origin and opaque path.
    bool AllowMediaUrlPrefix(const std::string& trustedPrefix);
    // Also removes every static image reader registered below this prefix.
    void RevokeMediaUrlPrefix(const std::string& trustedPrefix);
    // Registers the in-process reader for one exact, already-trusted loopback
    // image URL. Single-frame images it can decode are shown by a native GDI
    // window instead of mpv; local image files need no registration.
    bool AllowStaticImageSource(const std::string& url, wallpaper::StaticImageSource source);
    std::vector<std::uint64_t> ActiveDisplays() const;
    bool EnsureAttached(std::uint64_t displayId);
    // The atomic must outlive this controller; only its value may change across
    // threads. Set this pointer on the controller's actor before starting work.
    void SetCancellation(const std::atomic<bool>* cancellation);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lume::platform::windows
