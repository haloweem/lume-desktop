#pragma once

#include "apps/shell/music_provider_account_session.h"
#include "core/music/music_types.h"
#include "core/music/music_lyrics.h"
#include <memory>

namespace lume::shell {

struct MusicOnlineCommand {
    std::string action;
    music::auth::Provider provider = music::auth::Provider::Netease;
    std::uint64_t request = 0;
    music::auth::Json arguments = music::auth::Json::object();
};
std::optional<MusicOnlineCommand> ParseMusicOnlineCommand(std::wstring_view) noexcept;

struct MusicOnlineOptions {
    using Lease = MusicProviderCredentialLease;
    std::function<std::optional<Lease>(music::auth::Provider)> acquire;
    std::function<bool(const Lease&)> current;
    std::function<music::auth::Json(music::auth::Provider, std::string_view,
        const music::auth::Json&, const music::auth::Json&, const music::auth::Cancelled&)> execute;
    std::function<void(std::vector<music::Track>, std::string, std::string)> play;
    std::function<void()> wake;
    bool verification = false;
};

class MusicOnlineSession final {
public:
    explicit MusicOnlineSession(MusicOnlineOptions);
    ~MusicOnlineSession();
    MusicOnlineSession(const MusicOnlineSession&) = delete;
    MusicOnlineSession& operator=(const MusicOnlineSession&) = delete;
    std::wstring HandlePage(const MusicOnlineCommand&);
    std::optional<std::wstring> DrainNotification();
    std::string ResolveSource(const music::Track&, const music::auth::Cancelled&);
    music::ResolvedAudioSource ResolveAudioSource(const music::Track&, std::string_view quality,
        const music::auth::Cancelled&);
    music::LocalLyricsResult LoadLyrics(const music::Track&, const music::auth::Cancelled&);
    void AccountsChanged();
    void Cancel();
    void Shutdown();
    bool HasWork() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace lume::shell
