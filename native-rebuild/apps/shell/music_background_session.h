#pragma once

#include "core/ui/music_background_protocol.h"
#include "core/settings/file_store.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lume::shell {

struct MusicBackgroundSessionOptions {
    // The media directory is always rooted below this isolated directory.
    std::string root;
    // Explicit for verification; production points this into root.
    std::string statePath;
    bool verification = false;
    // Tests may provide a deterministic picker. Production uses IFileOpenDialog.
    std::function<std::wstring(HWND)> chooseMedia;
    // Read-only catalog root. Never a WallpaperSession or playback dependency.
    std::string wallpaperLibraryRoot;
    // Worker notification only; post a main-thread wake, do not reenter here.
    std::function<void()> wake;
    // Production can restore metadata before music opens without starting runtime resources.
    bool initiallyVisible = true;
};

class MusicBackgroundSession final {
public:
    MusicBackgroundSession(MusicBackgroundSessionOptions, settings::IFileStore&);
    ~MusicBackgroundSession();

    MusicBackgroundSession(const MusicBackgroundSession&) = delete;
    MusicBackgroundSession& operator=(const MusicBackgroundSession&) = delete;

    bool Restore();
    std::wstring HandlePage(const ui::MusicBackgroundCommand&, HWND owner = nullptr);
    std::wstring State() const;
    std::wstring MediaDirectory() const;
    bool HasWork() const noexcept;
    // All public methods belong to the main thread; workers only call wake.
    std::vector<std::wstring> Drain();
    std::optional<std::wstring> DrainNotification();
    void SetVisible(bool);
    void CancelActive();
    void Shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lume::shell
