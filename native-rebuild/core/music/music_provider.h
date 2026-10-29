#pragma once

#include "core/music/music_types.h"

#include <string>
#include <string_view>
#include <vector>

namespace lume::music {

enum class ProviderError { None, SignedOut, Empty, NotFound, Unsupported };

struct ProviderResult {
    std::vector<Track> tracks;
    ProviderError error = ProviderError::Empty;
    bool cached = false;
};

struct LyricsResult {
    std::vector<std::string> lines;
    ProviderError error = ProviderError::Empty;
    bool cached = false;
};

class MusicProviderGateway {
public:
    virtual ~MusicProviderGateway() = default;
    virtual ProviderResult LoadHome(bool signedIn) = 0;
    virtual ProviderResult Search(std::string_view query, bool signedIn) = 0;
    virtual LyricsResult LoadLyrics(std::string_view trackId, bool signedIn) = 0;
    virtual void Cancel() = 0;
};

// The cache gateway is deliberately provider-neutral. Network adapters can be
// added behind this interface without coupling the page or playback host to a
// service SDK.
class CachedMusicProvider final : public MusicProviderGateway {
public:
    explicit CachedMusicProvider(std::vector<Track> home = {});

    void ReplaceHome(std::vector<Track> home);
    ProviderResult LoadHome(bool signedIn) override;
    ProviderResult Search(std::string_view query, bool signedIn) override;
    LyricsResult LoadLyrics(std::string_view trackId, bool signedIn) override;
    void Cancel() override { cancelled_ = true; }
    bool Cancelled() const noexcept { return cancelled_; }

private:
    std::vector<Track> home_;
    bool cancelled_ = false;
};

}  // namespace lume::music
