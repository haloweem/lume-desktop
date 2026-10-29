#pragma once
#include "core/account/ports.h"
#include "core/ui/account_page_protocol.h"
#include "core/ui/wallpaper_page_protocol.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace lume::shell {
struct AccountSessionOptions {
    std::string storageDirectory;
    bool verification = false;
    // Shared ports outlive cancelled work; verification never creates production ports.
    std::shared_ptr<account::ITransport> transport;
    std::shared_ptr<account::ICredentials> credentials;
    account::Clock clock;
    std::function<bool(std::wstring_view)> openBrowser;
    std::function<std::optional<std::wstring>(HWND)> chooseAvatarFile;
    std::function<bool()> ensureLoginProtocol;
};
class AccountSession final {
public:
    using Notify = std::function<void(const std::wstring&)>;
    AccountSession(HINSTANCE, AccountSessionOptions, Notify);
    ~AccountSession();
    AccountSession(const AccountSession&) = delete;
    AccountSession& operator=(const AccountSession&) = delete;
    // All public calls and destruction belong to the creating UI thread.
    bool Restore();
    std::wstring HandlePage(const ui::AccountPageCommand&, HWND owner = nullptr);
    bool HandleCallback(std::wstring_view);
    std::wstring State() const;
    bool SignedIn() const;
    void HandleWallpaper(const ui::WallpaperPageCommand&);
    void RecordWallpaperDownload(std::string id, std::string sourceUrl);
    void AuthorizeWallpaperDownload(std::string id, std::string sourceUrl, std::function<void(std::string)> completed);
    std::wstring WallpaperState() const;
    void Shutdown();
    bool HasWork() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
