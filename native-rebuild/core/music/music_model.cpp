#include "core/music/music_model.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace lume::music {
namespace {

bool ValidTrack(const Track& track) {
    return !track.id.empty() && track.id.size() <= 128 &&
        track.id.find('\0') == std::string::npos && track.source.size() <= 4096 &&
        track.source.find('\0') == std::string::npos && track.title.size() <= 512 &&
        track.artist.size() <= 512 && track.album.size() <= 512 && track.artwork.size() <= 2048 &&
        std::isfinite(track.duration) && track.duration >= 0.0 && track.duration <= 1e9;
}

std::vector<Track> BoundedTracks(std::vector<Track> tracks) {
    std::vector<Track> result;
    std::unordered_set<std::string> ids;
    for (auto& track : tracks) {
        if (ValidTrack(track) && ids.insert(track.id).second) result.push_back(std::move(track));
        if (result.size() == kMaxMusicTracks) break;
    }
    return result;
}

}  // namespace

void NativeTrackOwner::Replace(std::vector<Track> tracks, bool imported) {
    tracks_ = BoundedTracks(std::move(tracks));
    importedIds_.clear();
    if (imported) for (const auto& track : tracks_) importedIds_.insert(track.id);
}

const Track* NativeTrackOwner::Resolve(std::string_view id) const noexcept {
    const auto found = std::find_if(tracks_.begin(), tracks_.end(),
        [id](const auto& track) { return track.id == id; });
    return found == tracks_.end() ? nullptr : &*found;
}

bool NativeTrackOwner::IsImported(const Track& track) const noexcept {
    const auto* authorized = Resolve(track.id);
    return importedIds_.count(track.id) != 0 && authorized && authorized->source == track.source;
}

const Track* NativeTrackOwner::Authorize(const Track& track, bool imported) {
    if (!ValidTrack(track)) return nullptr;
    if (const auto* existing = Resolve(track.id)) {
        if (existing->source != track.source) return nullptr;
        if (imported) importedIds_.insert(track.id);
        return existing;
    }
    if (tracks_.size() >= kMaxMusicTracks) return nullptr;
    tracks_.push_back(track);
    if (imported) importedIds_.insert(track.id);
    return &tracks_.back();
}

void MusicModel::Touch() {
    ++snapshot_.revision;
}

void MusicModel::RefreshCurrent() {
    if (snapshot_.queue.empty() || snapshot_.currentIndex >= snapshot_.queue.size()) {
        snapshot_.current.reset();
        snapshot_.status = snapshot_.queue.empty() ? PlaybackStatus::Empty : PlaybackStatus::Ready;
        return;
    }
    snapshot_.current = snapshot_.queue[snapshot_.currentIndex];
    if (snapshot_.status == PlaybackStatus::Empty) snapshot_.status = PlaybackStatus::Ready;
}

void MusicModel::SetCurrent(std::size_t index, bool playing) {
    if (index >= snapshot_.queue.size()) return;
    snapshot_.currentIndex = index;
    snapshot_.position = 0.0;
    snapshot_.analysis = {};
    snapshot_.current = snapshot_.queue[index];
    snapshot_.playing = playing;
    snapshot_.status = playing ? PlaybackStatus::Playing : PlaybackStatus::Ready;
    Touch();
}

void MusicModel::ReplaceQueue(std::vector<Track> queue) {
    snapshot_.analysis = {};
    snapshot_.queue = BoundedTracks(std::move(queue));
    snapshot_.currentIndex = 0;
    snapshot_.position = 0.0;
    snapshot_.playing = false;
    snapshot_.status = snapshot_.queue.empty() ? PlaybackStatus::Empty : PlaybackStatus::Ready;
    ++snapshot_.session;
    RefreshCurrent();
    ResetShuffleOrder();
    Touch();
}

void MusicModel::ResetShuffleOrder() {
    shuffleOrder_.clear();
    if (snapshot_.current) shuffleOrder_.push_back(snapshot_.current->id);
    for (const auto& track : snapshot_.queue) {
        if (!snapshot_.current || track.id != snapshot_.current->id) shuffleOrder_.push_back(track.id);
    }
    std::shuffle(shuffleOrder_.begin() + (snapshot_.current ? 1 : 0), shuffleOrder_.end(), random_);
}

bool MusicModel::Enqueue(const Track& track) {
    if (!ValidTrack(track)) return false;
    auto& queue = snapshot_.queue;
    const auto existing = std::find_if(queue.begin(), queue.end(),
        [&](const auto& item) { return item.id == track.id; });
    if (existing != queue.end()) {
        if ((HasCurrent() && snapshot_.current->id == track.id) || existing + 1 == queue.end()) return false;
        auto moved = *existing;
        const auto index = static_cast<std::size_t>(existing - queue.begin());
        queue.erase(existing);
        queue.push_back(std::move(moved));
        if (HasCurrent() && index < snapshot_.currentIndex) --snapshot_.currentIndex;
    } else {
        if (queue.size() >= kMaxMusicTracks) return false;
        const bool empty = queue.empty();
        queue.push_back(track);
        if (empty) SetCurrent(0, false);
        auto start = shuffleOrder_.begin();
        if (HasCurrent()) {
            const auto current = std::find(start, shuffleOrder_.end(), snapshot_.current->id);
            if (current != shuffleOrder_.end()) start = current + 1;
        }
        const auto remaining = static_cast<std::size_t>(shuffleOrder_.end() - start);
        shuffleOrder_.insert(start + std::uniform_int_distribution<std::size_t>(0, remaining)(random_), track.id);
    }
    Touch();
    return true;
}

bool MusicModel::RemoveFromQueue(std::string_view id) {
    auto& queue = snapshot_.queue;
    const auto found = std::find_if(queue.begin(), queue.end(),
        [id](const auto& track) { return track.id == id; });
    if (found == queue.end()) return false;
    // Own the key before erasing: callers may pass an id from Snapshot().
    const std::string key(id);
    const auto index = static_cast<std::size_t>(found - queue.begin());
    const bool current = HasCurrent() && snapshot_.current->id == key;
    const auto status = snapshot_.status;
    shuffleOrder_.erase(std::remove(shuffleOrder_.begin(), shuffleOrder_.end(), key), shuffleOrder_.end());
    queue.erase(found);
    if (queue.empty()) StopAtEnd();
    else if (current) {
        SetCurrent(std::min(index, queue.size() - 1), snapshot_.playing);
        if (status == PlaybackStatus::Paused) snapshot_.status = PlaybackStatus::Paused;
        if (snapshot_.shuffle) ResetShuffleOrder();
    } else if (HasCurrent() && index < snapshot_.currentIndex) --snapshot_.currentIndex;
    Touch();
    return true;
}

bool MusicModel::InsertNext(const Track& track) {
    if (!ValidTrack(track) || (HasCurrent() && snapshot_.current->id == track.id)) return false;
    // Own the value: callers can pass a track from Snapshot().queue.
    auto inserted = track;
    auto& queue = snapshot_.queue;
    const auto found = std::find_if(queue.begin(), queue.end(),
        [&](const auto& item) { return item.id == inserted.id; });
    if (found == queue.end() && queue.size() >= kMaxMusicTracks) return false;
    if (found != queue.end()) {
        inserted = *found;
        if (HasCurrent() && static_cast<std::size_t>(found - queue.begin()) < snapshot_.currentIndex)
            --snapshot_.currentIndex;
        queue.erase(found);
    }
    const auto index = HasCurrent() ? snapshot_.currentIndex + 1 : 0;
    queue.insert(queue.begin() + index, inserted);
    if (!HasCurrent()) SetCurrent(0, false);
    shuffleOrder_.erase(std::remove(shuffleOrder_.begin(), shuffleOrder_.end(), inserted.id), shuffleOrder_.end());
    const auto current = std::find(shuffleOrder_.begin(), shuffleOrder_.end(), snapshot_.current->id);
    shuffleOrder_.insert(current == shuffleOrder_.end() ? shuffleOrder_.begin() : current + 1, inserted.id);
    Touch();
    return true;
}

void MusicModel::ClearQueue() { ReplaceQueue({}); }

bool MusicModel::Play() {
    if (!HasCurrent()) {
        if (snapshot_.queue.empty()) return false;
        SetCurrent(0, true);
        ResetShuffleOrder();
    }
    snapshot_.playing = true;
    snapshot_.status = PlaybackStatus::Playing;
    Touch();
    return true;
}

bool MusicModel::Pause() {
    if (!HasCurrent()) return false;
    snapshot_.playing = false;
    snapshot_.analysis = {};
    snapshot_.status = PlaybackStatus::Paused;
    Touch();
    return true;
}

bool MusicModel::Toggle() {
    return snapshot_.playing ? Pause() : Play();
}

bool MusicModel::PlayNow(std::size_t index) {
    if (index >= snapshot_.queue.size()) return false;
    SetCurrent(index, true);
    ResetShuffleOrder();
    return true;
}

bool MusicModel::Advance(bool ended) {
    if (snapshot_.queue.empty() || (ended && (!HasCurrent() || !snapshot_.playing))) return false;
    if (!HasCurrent()) return PlayNow(0);
    const bool playing = snapshot_.playing;
    const bool paused = snapshot_.status == PlaybackStatus::Paused;
    if (ended && snapshot_.repeat == RepeatMode::One) {
        SetCurrent(snapshot_.currentIndex, true);
        return true;
    }
    auto next = snapshot_.currentIndex + 1;
    if (snapshot_.shuffle) {
        const auto current = std::find(shuffleOrder_.begin(), shuffleOrder_.end(), snapshot_.current->id);
        if (current != shuffleOrder_.end() && current + 1 != shuffleOrder_.end()) {
            const auto track = std::find_if(snapshot_.queue.begin(), snapshot_.queue.end(),
                [&](const auto& item) { return item.id == *(current + 1); });
            next = static_cast<std::size_t>(track - snapshot_.queue.begin());
        } else if (snapshot_.repeat == RepeatMode::All) {
            ResetShuffleOrder();
            if (shuffleOrder_.size() > 1) std::rotate(shuffleOrder_.begin(), shuffleOrder_.begin() + 1, shuffleOrder_.end());
            const auto track = std::find_if(snapshot_.queue.begin(), snapshot_.queue.end(),
                [&](const auto& item) { return item.id == shuffleOrder_.front(); });
            next = static_cast<std::size_t>(track - snapshot_.queue.begin());
        } else next = snapshot_.queue.size();
    }
    if (next >= snapshot_.queue.size() && snapshot_.repeat == RepeatMode::All) next = 0;
    if (next >= snapshot_.queue.size()) StopAtEnd();
    else {
        SetCurrent(next, playing);
        if (paused) snapshot_.status = PlaybackStatus::Paused;
    }
    return true;
}

bool MusicModel::Next() { return Advance(false); }
bool MusicModel::EndOfTrack() { return Advance(true); }

void MusicModel::StopAtEnd() {
    snapshot_.analysis = {};
    snapshot_.current.reset();
    snapshot_.currentIndex = 0;
    snapshot_.position = 0.0;
    snapshot_.playing = false;
    snapshot_.status = snapshot_.queue.empty() ? PlaybackStatus::Empty : PlaybackStatus::Ready;
    Touch();
}

bool MusicModel::Previous() {
    if (snapshot_.queue.empty()) return false;
    if (!HasCurrent()) return PlayNow(0);
    if (snapshot_.position > 3.0) {
        snapshot_.position = 0.0;
        Touch();
        return true;
    }
    auto previous = snapshot_.currentIndex;
    if (snapshot_.shuffle) {
        const auto current = std::find(shuffleOrder_.begin(), shuffleOrder_.end(), snapshot_.current->id);
        if (current != shuffleOrder_.begin() && current != shuffleOrder_.end()) {
            const auto track = std::find_if(snapshot_.queue.begin(), snapshot_.queue.end(),
                [&](const auto& item) { return item.id == *(current - 1); });
            previous = static_cast<std::size_t>(track - snapshot_.queue.begin());
        }
    } else if (previous > 0) --previous;
    else if (snapshot_.repeat == RepeatMode::All) previous = snapshot_.queue.size() - 1;
    const bool paused = snapshot_.status == PlaybackStatus::Paused;
    SetCurrent(previous, snapshot_.playing);
    if (paused) snapshot_.status = PlaybackStatus::Paused;
    return true;
}

bool MusicModel::Seek(double position) {
    if (!HasCurrent() || !std::isfinite(position) || position < 0.0) {
        return false;
    }
    snapshot_.position = std::clamp(position, 0.0, snapshot_.current->duration);
    Touch();
    return true;
}

bool MusicModel::SetVolume(double volume) {
    if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0) return false;
    if (snapshot_.volume != volume) {
        snapshot_.volume = volume;
        snapshot_.analysis = {};
        Touch();
    }
    return true;
}

void MusicModel::SetMuted(bool muted) {
    if (snapshot_.muted == muted) return;
    snapshot_.muted = muted;
    snapshot_.analysis = {};
    Touch();
}

void MusicModel::SetAnalysis(const AudioAnalysis& analysis) {
    AudioAnalysis bounded;
    const auto unit = [](double value) { return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0; };
    if (snapshot_.playing && !snapshot_.muted && snapshot_.volume > 0.0) {
        bounded.energy = unit(analysis.energy);
        std::transform(analysis.bins.begin(), analysis.bins.end(), bounded.bins.begin(), unit);
    }
    if (bounded.energy == snapshot_.analysis.energy && bounded.bins == snapshot_.analysis.bins) return;
    snapshot_.analysis = bounded;
    Touch();
}

void MusicModel::SetDuration(double duration) {
    if (!HasCurrent() || !std::isfinite(duration) || duration < 0.0 || duration > 1e9) return;
    if (snapshot_.currentIndex >= snapshot_.queue.size() ||
        snapshot_.current->duration == duration) return;
    snapshot_.queue[snapshot_.currentIndex].duration = duration;
    snapshot_.current->duration = duration;
    snapshot_.position = std::clamp(snapshot_.position, 0.0, duration);
    Touch();
}

void MusicModel::SetRepeat(RepeatMode mode) {
    snapshot_.repeat = mode;
    Touch();
}

void MusicModel::SetShuffle(bool enabled) {
    if (snapshot_.shuffle == enabled) return;
    snapshot_.shuffle = enabled;
    ResetShuffleOrder();
    Touch();
}

void MusicModel::FailPlayback() {
    snapshot_.analysis = {};
    snapshot_.playing = false;
    snapshot_.status = HasCurrent() ? PlaybackStatus::Error :
        snapshot_.queue.empty() ? PlaybackStatus::Empty : PlaybackStatus::Ready;
    Touch();
}

}  // namespace lume::music
