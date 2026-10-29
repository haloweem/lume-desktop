#pragma once

#include "core/music/music_types.h"

#include <cstddef>
#include <random>
#include <string_view>
#include <vector>
#include <unordered_set>

namespace lume::music {

inline constexpr std::size_t kMaxMusicTracks = 512;

// A bounded native authorization scope. No filesystem or provider work lives here.
class NativeTrackOwner {
public:
    void Replace(std::vector<Track> tracks, bool imported = false);
    const std::vector<Track>& Tracks() const noexcept { return tracks_; }
    const Track* Resolve(std::string_view id) const noexcept;
    bool IsImported(const Track& track) const noexcept;
    const Track* Authorize(const Track& track, bool imported = false);

private:
    std::vector<Track> tracks_;
    std::unordered_set<std::string> importedIds_;
};

class MusicModel {
public:
    const MusicSnapshot& Snapshot() const noexcept { return snapshot_; }

    void ReplaceQueue(std::vector<Track> queue);
    bool Enqueue(const Track& track);
    bool InsertNext(const Track& track);
    bool RemoveFromQueue(std::string_view id);
    void ClearQueue();
    bool Play();
    bool Pause();
    bool Toggle();
    bool PlayNow(std::size_t index);
    bool Next();
    bool EndOfTrack();
    bool Previous();
    bool Seek(double position);
    void SetDuration(double duration);
    void SetRepeat(RepeatMode mode);
    void SetShuffle(bool enabled);
    bool SetVolume(double volume);
    void SetMuted(bool muted);
    void SetAnalysis(const AudioAnalysis& analysis);
    void FailPlayback();

private:
    bool HasCurrent() const noexcept { return snapshot_.current.has_value(); }
    void SetCurrent(std::size_t index, bool playing);
    void Touch();
    void RefreshCurrent();
    void ResetShuffleOrder();
    bool Advance(bool ended);
    void StopAtEnd();

    MusicSnapshot snapshot_;
    std::vector<std::string> shuffleOrder_;
    std::mt19937 random_{std::random_device{}()};
};

}  // namespace lume::music
