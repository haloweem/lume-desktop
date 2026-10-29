#pragma once
#include "core/music/music_provider_account.h"
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

namespace lume::shell {
struct MusicProviderAccountCommand {
    std::string action;
    music::auth::Provider provider = music::auth::Provider::Netease;
    std::uint64_t request = 0;
};
std::optional<MusicProviderAccountCommand> ParseMusicProviderAccountCommand(std::wstring_view) noexcept;

struct MusicProviderAccountOptions {
    std::filesystem::path root;
    bool verification = false;
    music::auth::Execute execute;
    std::function<music::auth::Json(music::auth::Provider)> load;
    std::function<void(music::auth::Provider, const music::auth::Json&)> save;
    std::function<void(music::auth::Provider)> clear;
    // Login self-check; empty in tests so no real network probe runs there.
    std::function<std::string(const std::string&)> diagnoseHost;
    std::function<void()> wake;
    std::chrono::milliseconds pollInterval{2000};
    std::chrono::milliseconds qrLifetime{180000};
    // Folia: a scanned but unconfirmed code expires after a minute, and a QR
    // fetch that failed on the network is retried once after 300ms.
    std::chrono::milliseconds scannedLifetime{60000};
    std::chrono::milliseconds beginRetryDelay{300};
};

// Native-only, revocable identity snapshot. Never serialize into page messages.
struct MusicProviderCredentialLease {
    music::auth::Provider provider = music::auth::Provider::Netease;
    std::uint64_t generation = 0;
    std::string userId;
    music::auth::Json credentials;
};

class MusicProviderAccountSession final {
public:
    explicit MusicProviderAccountSession(MusicProviderAccountOptions);
    ~MusicProviderAccountSession();
    std::wstring HandlePage(const MusicProviderAccountCommand&);
    std::wstring State() const;
    std::optional<std::wstring> DrainNotification();
    void CancelActive();
    void Shutdown();
    bool HasWork() const;
    std::optional<MusicProviderCredentialLease> AcquireCredentials(music::auth::Provider) const;
    bool IsCurrent(const MusicProviderCredentialLease&) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lume::shell
