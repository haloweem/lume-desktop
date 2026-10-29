#pragma once
#include "core/ui/wallpaper_page_protocol.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lume::shell {
struct WallpaperSessionOptions {
    // Required library root; verification callers must supply an isolated fixture root.
    std::string root;
    std::wstring executable;
    bool verification=false;
    HWND verificationParent=nullptr;
    // Supply the restored AppSettings value before Restore starts any player.
    bool balancedMode=false;
    // UI-thread notification after an official download is successfully registered.
    std::function<void(std::string id,std::string sourceUrl)> downloadCompleted;
    // UI-thread admission; completion is delivered on that same UI thread.
    std::function<void(std::string id,std::string sourceUrl,std::function<void(std::string)> completed)> authorizeDownload;
};
class WallpaperSession final {
public:
    using Notify=std::function<void(const std::wstring&)>;
    WallpaperSession(HINSTANCE,WallpaperSessionOptions,Notify);
    ~WallpaperSession();
    WallpaperSession(const WallpaperSession&)=delete;
    WallpaperSession& operator=(const WallpaperSession&)=delete;
    bool Restore();
    // Call from a background settings worker, not the UI/actor thread.
    // An actor-confirmed unchanged policy need not wait for restored players.
    // Actual changes stay ordered: queued work expires after 2s, started work
    // waits for bounded IPC/rollback.
    // False guarantees that this request cannot change the mode later.
    bool SetBalancedMode(bool enabled);
    void HandlePage(const ui::WallpaperPageCommand&,HWND owner=nullptr);
    // Batch-local path deduplication, at most 32 successful imports, first error retained.
    // Verification permits explicit file imports, but does not enable system playback.
    void HandleDroppedFiles(std::vector<std::wstring> paths);
    std::wstring State()const;
    bool IsOfficialId(std::string_view id)const;
    std::wstring MediaDirectory()const;
    bool HasWork()const;
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
