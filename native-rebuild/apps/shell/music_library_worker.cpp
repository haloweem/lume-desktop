#include "apps/shell/music_library_worker.h"
#include "platform/windows/text_util.h"

#include "core/music/music_model.h"
#include "platform/windows/music_metadata.h"
#include "platform/windows/wallpaper/media_server.h"
#include "vendor/nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <windows.h>

namespace lume::shell {
namespace {
using Clock = std::chrono::steady_clock;
using Action = MusicLibraryPlaybackAction;
using Json = nlohmann::json;
constexpr std::size_t kMaxRequests = 32;
constexpr std::size_t kMaxPageArtwork = 100;
constexpr std::size_t kMaxArtworkBytes = 8 * 1024 * 1024;

bool Id(std::string_view id) {
    return id.size() > 6 && id.size() <= 96 && id.substr(0, 6) == "local-" &&
        id.find_first_not_of("0123456789", 6) == std::string_view::npos;
}

bool Replacement(Action action) {
    return action == Action::Play || action == Action::Restore;
}

bool SameQueue(const music::MusicSnapshot& a, const music::MusicSnapshot& b) {
    if (a.queue.size() != b.queue.size() || a.currentIndex != b.currentIndex ||
        a.repeat != b.repeat || a.shuffle != b.shuffle || a.volume != b.volume ||
        a.muted != b.muted) return false;
    for (std::size_t i = 0; i < a.queue.size(); ++i)
        if (a.queue[i].id != b.queue[i].id) return false;
    return true;
}

bool SameSnapshot(const music::MusicSnapshot& a, const music::MusicSnapshot& b) {
    return SameQueue(a, b) && a.position == b.position;
}

bool Compact(music::MusicSnapshot& snapshot) {
    if (snapshot.queue.size() > music::kMaxMusicTracks || !std::isfinite(snapshot.position) ||
        snapshot.position < 0 || snapshot.position > 1e9 || !std::isfinite(snapshot.volume) ||
        snapshot.volume < 0 || snapshot.volume > 1 ||
        (snapshot.repeat != music::RepeatMode::None && snapshot.repeat != music::RepeatMode::One &&
            snapshot.repeat != music::RepeatMode::All) ||
        (!snapshot.queue.empty() && snapshot.currentIndex >= snapshot.queue.size())) return false;
    std::set<std::string> ids;
    for (auto& track : snapshot.queue) {
        if (!Id(track.id) || !ids.insert(track.id).second) return false;
        auto id = std::move(track.id);
        track = {};
        track.id = std::move(id);
    }
    const auto current = snapshot.queue.empty() ? std::string{} : snapshot.queue[snapshot.currentIndex].id;
    if (snapshot.current && snapshot.current->id != current) return false;
    snapshot.current.reset();
    if (snapshot.queue.empty()) { snapshot.currentIndex = 0; snapshot.position = 0; }
    snapshot.playing = false;
    snapshot.status = music::PlaybackStatus::Empty;
    snapshot.session = snapshot.revision = 0;
    snapshot.analysis = {};
    return true;
}

using lume::text::Utf8;

using lume::text::Wide;

std::wstring StateError(std::wstring_view state, std::string_view error, bool clearArtwork,
    const std::set<std::string>* allowed = nullptr) {
    auto json = state.empty() ? Json{{"version", 1}, {"scope", "musicLibrary"}, {"type", "state"},
        {"loaded", false}, {"error", ""}, {"total", 0}, {"offset", 0}, {"limit", 100},
        {"tracks", Json::array()}} : Json::parse(Utf8(state));
    if (!error.empty()) json["error"] = error;
    for (auto& track : json["tracks"]) {
        if (clearArtwork || (allowed && !allowed->count(track.value("artwork", std::string{}))))
            track["artwork"] = "";
    }
    return Wide(json.dump());
}
}

struct MusicLibraryWorker::Impl {
    enum class Kind { Restore, Import, Remove, Page, Play, Artwork };
    struct Request {
        Kind kind;
        std::vector<std::wstring> paths;
        std::string id;
        std::size_t offset = 0, limit = 100;
        Action action = Action::None;
        std::uint64_t generation = 0, artworkEpoch = 0;
        explicit Request(Kind input) : kind(input) {}
    };
    struct Result {
        MusicLibraryWorkerResult value;
        std::uint64_t artworkEpoch = 0;
        bool artworkState = false;
    };
    struct ArtworkLease {
        std::shared_ptr<const std::string> bytes;
        std::string url;
    };

    explicit Impl(MusicLibraryWorkerOptions input) : options(std::move(input)) {}

    bool Start() {
        if (thread.joinable()) return true;
        try { thread = std::thread([this] { Run(); }); return true; }
        catch (...) { return false; }
    }

    bool Submit(Request request) {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || (request.kind == Kind::Artwork && !visible)) return false;
        const bool replacePage = request.kind == Kind::Page && !requests.empty() &&
            requests.back().kind == Kind::Page;
        if (!replacePage && requests.size() + results.size() + (active ? 1 : 0) >= kMaxRequests)
            return false;
        if (!Start()) return false;
        if (request.action != Action::None) {
            request.generation = ++generation;
            if (Replacement(request.action)) replacement = generation;
        }
        request.artworkEpoch = stateEpoch;
        if (replacePage) requests.back() = std::move(request);
        else requests.push_back(std::move(request));
        condition.notify_one();
        return true;
    }

    bool Save(music::MusicSnapshot snapshot, bool retry) {
        if (!Compact(snapshot)) return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || !Start()) return false;
        if (queueCorrupt && !retry) return false;
        const auto now = Clock::now();
        if (!retry && failed && SameSnapshot(*failed, snapshot)) {
            pending.reset();
            condition.notify_one();
            return true;
        }
        // One pending snapshot, even when the UI supplies progress every 250 ms.
        if (!retry && saved && SameSnapshot(*saved, snapshot) && !saving) {
            pending.reset();
            condition.notify_one();
            return true;
        }
        const auto& previous = failed ? failed : saved;
        const bool structural = !previous || !SameQueue(*previous, snapshot);
        const auto due = structural || retry ? now + std::chrono::milliseconds(750) :
            std::max(now, lastAttempt + std::chrono::seconds(30));
        if (!pending) saveDue = due;
        else saveDue = std::min(saveDue, due);
        pending = std::move(snapshot);
        pendingRetry = retry;
        condition.notify_one();
        return true;
    }

    void Wake() {
        try { if (options.wake) options.wake(); } catch (...) {}
    }

    void Cache(MusicLibrarySession& session) {
        cacheReady = session.Restore();
        auto tracks = cacheReady ? session.Tracks() : std::vector<music::Track>{};
        if (tracks.size() > music::kMaxLibraryTracks) tracks.resize(music::kMaxLibraryTracks);
        std::lock_guard<std::mutex> lock(mutex);
        cache = std::move(tracks);
    }

    void ReleaseArtworkExcept(const std::set<std::string>& keep,
        std::unique_ptr<platform::windows::WallpaperMediaServer>& media) {
        for (auto it = artwork.begin(); it != artwork.end();) {
            if (keep.count(it->first)) {
                ++it;
                continue;
            }
            if (media) media->Remove(it->first);
            {
                std::lock_guard<std::mutex> lock(mutex);
                liveArtwork.erase(it->second.url);
            }
            artworkBytes -= it->second.bytes->size();
            it = artwork.erase(it);
        }
    }

    void ReleaseArtwork(std::unique_ptr<platform::windows::WallpaperMediaServer>& media) {
        if (media) media->Stop();
        artwork.clear();
        artworkBytes = 0;
        std::lock_guard<std::mutex> lock(mutex);
        liveArtwork.clear();
    }

    std::string EnsureArtwork(MusicLibrarySession& session, std::string_view id,
        std::unique_ptr<platform::windows::WallpaperMediaServer>& media) {
        if (!visible || !Id(id)) return {};
        const auto found = artwork.find(std::string(id));
        if (found != artwork.end()) return found->second.url;
        if (artwork.size() >= kMaxPageArtwork || artworkBytes >= kMaxArtworkBytes) return {};
        const auto epochAtRead = artworkEpoch->load();
        const auto png = session.Artwork(id);
        if (!png || png->empty() || png->size() > music::kMaxMusicArtworkBytes ||
            artworkBytes + png->size() > kMaxArtworkBytes ||
            !visible || epochAtRead != artworkEpoch->load()) {
            return {};
        }
        if (!media) media = std::make_unique<platform::windows::WallpaperMediaServer>(
            "https://lume.local");
        const auto bytes = std::make_shared<const std::string>(*png);
        const auto epoch = artworkEpoch;
        platform::windows::WallpaperMediaSource source;
        source.bytes = bytes->size();
        source.contentType = "image/png";
        source.extension = "png";
        source.read = [bytes, epoch, expected = epochAtRead]
            (std::uint64_t offset, char* buffer, std::size_t count, std::size_t& actual) {
            actual = 0;
            if (epoch->load() != expected || offset > bytes->size()) return false;
            actual = std::min({count, bytes->size() - static_cast<std::size_t>(offset),
                std::size_t{65536}});
            if (actual) std::memcpy(buffer, bytes->data() + offset, actual);
            return true;
        };
        const auto url = media->Register(std::string(id), std::move(source));
        if (url.empty()) return {};
        artworkBytes += bytes->size();
        artwork.emplace(std::string(id), ArtworkLease{bytes, url});
        {
            std::lock_guard<std::mutex> lock(mutex);
            liveArtwork.insert(url);
        }
        return url;
    }

    std::wstring BuildState(MusicLibrarySession& session, std::string_view error,
        std::unique_ptr<platform::windows::WallpaperMediaServer>& media,
        bool& hasArtwork) {
        hasArtwork = false;
        Json json = Json::parse(Utf8(session.State(pageOffset, pageLimit)));
        if (!error.empty()) json["error"] = error;
        std::set<std::string> keep;
        if (visible) {
            for (const auto& item : json["tracks"]) {
                if (item.value("available", false) &&
                    item.value("artwork", std::string{}).rfind("music-artwork:", 0) == 0)
                    keep.insert(item.at("id").get<std::string>());
            }
            if (!explicitArtwork.empty()) keep.insert(explicitArtwork);
        }
        ReleaseArtworkExcept(keep, media);
        if (visible && json.contains("tracks") && json["tracks"].is_array()) {
            for (auto& item : json["tracks"]) {
                const auto id = item.value("id", std::string{});
                const auto token = item.value("artwork", std::string{});
                if (item.value("available", false) && token.rfind("music-artwork:", 0) == 0) {
                    const auto url = EnsureArtwork(session, id, media);
                    item["artwork"] = url;
                    hasArtwork = hasArtwork || !url.empty();
                } else {
                    item["artwork"] = "";
                }
            }
        } else if (json.contains("tracks") && json["tracks"].is_array()) {
            for (auto& item : json["tracks"]) item["artwork"] = "";
        }
        return Wide(json.dump());
    }

    std::optional<music::Track> Validate(MusicLibrarySession& session, std::string_view id) {
        auto track = session.Resolve(id);
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = std::find_if(cache.begin(), cache.end(),
            [&](const auto& item) { return item.id == id; });
        if (found != cache.end()) {
            if (track) *found = *track;
            else cache.erase(found);
        } else if (track && cache.size() < music::kMaxLibraryTracks) cache.push_back(*track);
        return track;
    }

    Result Execute(MusicLibrarySession& session, const Request& request,
        std::unique_ptr<platform::windows::WallpaperMediaServer>& media) {
        Result result;
        auto& out = result.value;
        out.action = request.action;
        out.generation = request.generation;
        {
            std::lock_guard<std::mutex> lock(mutex);
            result.artworkEpoch = stateEpoch;
        }
        switch (request.kind) {
        case Kind::Restore:
            out.restored = true;
            if (session.Restore()) out.restoredQueue = session.RestoredQueue();
            out.error = session.LastError();
            {
                std::lock_guard<std::mutex> lock(mutex);
                queueCorrupt = out.error == "queue-unreadable";
                queueChecked = out.error.empty() || queueCorrupt;
            }
            if (out.restoredQueue) {
                out.restoredQueue->playing = false;
                out.restoredQueue->status = out.restoredQueue->queue.empty() ?
                    music::PlaybackStatus::Empty : music::PlaybackStatus::Paused;
                auto compact = *out.restoredQueue;
                Compact(compact);
                std::lock_guard<std::mutex> lock(mutex);
                saved = std::move(compact);
                lastAttempt = Clock::now();
                queueCorrupt = false;
            }
            Cache(session);
            break;
        case Kind::Import:
            ReleaseArtwork(media);
            out.imported = session.Import(request.paths);
            out.error = out.imported->error;
            Cache(session);
            break;
        case Kind::Remove:
            if (!session.Remove(request.id)) out.error = session.LastError();
            Cache(session);
            // An imported file may be removed while its artwork is visible.
            ReleaseArtwork(media);
            break;
        case Kind::Page:
            if (!session.Restore()) out.error = session.LastError();
            if (!cacheReady) Cache(session);
            pageOffset = request.offset;
            pageLimit = request.limit;
            break;
        case Kind::Play: {
            if (!cacheReady) Cache(session);
            const auto selected = Validate(session, request.id);
            if (!selected) { out.error = "track-unavailable"; break; }
            if (request.action == Action::Play) {
                std::vector<music::Track> candidates;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    const auto found = std::find_if(cache.begin(), cache.end(),
                        [&](const auto& track) { return track.id == request.id; });
                    if (found == cache.end()) { out.error = "track-unavailable"; break; }
                    const auto index = static_cast<std::size_t>(found - cache.begin());
                    // ponytail: only validate the bounded playback window, not the entire library.
                    const auto begin = index < music::kMaxMusicTracks ? 0 : index - music::kMaxMusicTracks + 1;
                    const auto end = std::min(cache.size(), begin + music::kMaxMusicTracks);
                    candidates.assign(cache.begin() + begin, cache.begin() + end);
                }
                for (const auto& candidate : candidates) {
                    const auto track = candidate.id == request.id ? selected : Validate(session, candidate.id);
                    if (track) out.playTracks.push_back(*track);
                }
            } else out.playTracks.push_back(*selected);
            out.playId = request.id;
            break;
        }
        case Kind::Artwork: {
            out.artworkId = request.id;
            result.artworkEpoch = request.artworkEpoch;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!visible || request.artworkEpoch != stateEpoch) break;
            }
            explicitArtwork = request.id;
            // Remove a previous explicit off-page lease before applying the shared budget.
            bool hasArtwork = false;
            BuildState(session, {}, media, hasArtwork);
            out.artworkUrl = EnsureArtwork(session, request.id, media);
            if (out.artworkUrl.empty()) out.error = "artwork-unavailable";
            break;
        }
        }
        {
            bool hasArtwork = false;
            out.state = BuildState(session, out.error, media, hasArtwork);
            result.artworkState = hasArtwork;
        }
        return result;
    }

    void Run() {
        MusicLibrarySession session({options.root, music::kMaxLibraryTracks,
            [this] { return stopping.load(); }});
        std::unique_ptr<platform::windows::WallpaperMediaServer> media;
        for (;;) {
            std::optional<Request> request;
            std::optional<music::MusicSnapshot> snapshot;
            bool revoke = false, retry = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                for (;;) {
                    if (revokeArtwork) { revoke = true; revokeArtwork = false; break; }
                    if (stopping) {
                        requests.clear();
                        if (pending) {
                            snapshot = std::move(pending); pending.reset(); saving = true; retry = pendingRetry;
                        }
                        break;
                    }
                    if (pending && Clock::now() >= saveDue) {
                        snapshot = std::move(pending); pending.reset(); saving = true; retry = pendingRetry; break;
                    }
                    if (!requests.empty()) {
                        request = std::move(requests.front()); requests.pop_front(); break;
                    }
                    if (pending) condition.wait_until(lock, saveDue);
                    else condition.wait(lock);
                }
                if (!revoke && !request && !snapshot) break;
                active = true;
            }
            if (revoke) {
                ReleaseArtwork(media);
                explicitArtwork.clear();
                Result result;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    result.artworkEpoch = visibilityEpoch;
                }
                try { result.value.state = BuildState(session, {}, media, result.artworkState); }
                catch (...) {
                    result.value.error = "artwork-unavailable";
                    result.value.state = StateError({}, result.value.error, true);
                }
                std::lock_guard<std::mutex> lock(mutex);
                visibilityResult = std::move(result);
            } else if (snapshot) {
                if (!queueChecked) {
                    session.RestoredQueue();
                    std::lock_guard<std::mutex> lock(mutex);
                    queueCorrupt = session.LastError() == "queue-unreadable";
                    queueChecked = session.LastError().empty() || queueCorrupt;
                }
                bool blocked = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    blocked = queueCorrupt && !retry;
                }
                const bool ok = !blocked && session.SaveQueue(*snapshot);
                const auto error = blocked ? std::string("queue-unreadable") : session.LastError();
                std::wstring failureState;
                if (!ok) {
                    failureState = StateError(session.State(pageOffset, pageLimit), error, true);
                }
                std::lock_guard<std::mutex> lock(mutex);
                saving = false;
                lastAttempt = Clock::now();
                if (ok) {
                    saved = std::move(snapshot); failed.reset(); saveError.reset();
                    queueCorrupt = false;
                    if (pending) {
                        if (SameQueue(*saved, *pending)) {
                            if (saved->position == pending->position) pending.reset();
                            else saveDue = lastAttempt + std::chrono::seconds(30);
                        }
                    }
                }
                else {
                    failed = std::move(snapshot);
                    saveError = error;
                    saveErrorState = std::move(failureState);
                    if (pending && SameQueue(*failed, *pending)) {
                        if (SameSnapshot(*failed, *pending)) pending.reset();
                        else saveDue = lastAttempt + std::chrono::seconds(30);
                    }
                }
            } else {
                Result result;
                try { result = Execute(session, *request, media); }
                catch (...) {
                    result.value.error = "library-operation-failed";
                    result.value.action = request->action;
                    result.value.generation = request->generation;
                    result.value.restored = request->kind == Kind::Restore;
                    result.value.state = StateError(session.State(pageOffset, pageLimit),
                        result.value.error, true);
                }
                std::lock_guard<std::mutex> lock(mutex);
                if (!stopping) results.push_back(std::move(result));
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                active = false;
            }
            Wake();
        }
        ReleaseArtwork(media);
    }

    MusicLibraryWorkerOptions options;
    mutable std::mutex mutex;
    std::mutex shutdownMutex;
    std::condition_variable condition;
    std::thread thread;
    std::atomic<bool> stopping{false};
    bool active = false, saving = false, revokeArtwork = false, pendingRetry = false;
    std::atomic<bool> visible{false};
    bool cacheReady = false; // Worker thread only.
    bool queueCorrupt = false, queueChecked = false;
    std::deque<Request> requests;
    std::vector<Result> results;
    std::vector<music::Track> cache;
    std::optional<music::MusicSnapshot> pending, saved, failed;
    std::optional<std::string> saveError;
    std::optional<Result> visibilityResult;
    std::wstring saveErrorState;
    std::map<std::string, ArtworkLease> artwork;
    std::set<std::string> liveArtwork; // Mutex-protected projection of worker-owned leases.
    std::string explicitArtwork;
    std::size_t artworkBytes = 0;
    Clock::time_point saveDue{}, lastAttempt{};
    std::uint64_t generation = 0, replacement = 0, stateEpoch = 0, visibilityEpoch = 0;
    std::shared_ptr<std::atomic<std::uint64_t>> artworkEpoch =
        std::make_shared<std::atomic<std::uint64_t>>(0);
    std::size_t pageOffset = 0, pageLimit = 100;
};

MusicLibraryWorker::MusicLibraryWorker(MusicLibraryWorkerOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
MusicLibraryWorker::~MusicLibraryWorker() { Shutdown(); }
bool MusicLibraryWorker::Restore() {
    Impl::Request request(Impl::Kind::Restore);
    request.action = Action::Restore;
    return impl_->Submit(std::move(request));
}
bool MusicLibraryWorker::Import(std::vector<std::wstring> paths) {
    if (paths.empty() || paths.size() > 256 ||
        std::any_of(paths.begin(), paths.end(), [](const auto& path) {
            return path.empty() || path.size() > 32767 || path.find(L'\0') != std::wstring::npos;
        })) return false;
    Impl::Request request(Impl::Kind::Import);
    request.paths = std::move(paths);
    return impl_->Submit(std::move(request));
}
bool MusicLibraryWorker::Remove(std::string id) {
    if (!Id(id)) return false;
    Impl::Request request(Impl::Kind::Remove);
    request.id = std::move(id);
    return impl_->Submit(std::move(request));
}
bool MusicLibraryWorker::Page(std::size_t offset, std::size_t limit) {
    if (!limit) return false;
    Impl::Request request(Impl::Kind::Page);
    request.offset = offset; request.limit = std::min<std::size_t>(limit, 256);
    return impl_->Submit(std::move(request));
}
bool MusicLibraryWorker::Play(std::string id, Action action) {
    if (!Id(id) || (action != Action::Play && action != Action::Enqueue && action != Action::InsertNext))
        return false;
    Impl::Request request(Impl::Kind::Play);
    request.id = std::move(id); request.action = action;
    return impl_->Submit(std::move(request));
}
bool MusicLibraryWorker::SaveQueue(music::MusicSnapshot snapshot, bool retry) {
    return impl_->Save(std::move(snapshot), retry);
}
bool MusicLibraryWorker::Artwork(std::string id) {
    if (!Id(id)) return false;
    Impl::Request request(Impl::Kind::Artwork);
    request.id = std::move(id);
    return impl_->Submit(std::move(request));
}
void MusicLibraryWorker::SetVisible(bool visible) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping || impl_->visible == visible) return;
    impl_->visible = visible;
    impl_->visibilityEpoch = ++impl_->stateEpoch;
    if (!visible) {
        ++*impl_->artworkEpoch;
    }
    if (impl_->thread.joinable()) {
        impl_->revokeArtwork = true;
        impl_->condition.notify_one();
    }
}
std::vector<MusicLibraryWorkerResult> MusicLibraryWorker::Drain() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<MusicLibraryWorkerResult> out;
    if (impl_->visibilityResult) {
        impl_->results.insert(impl_->results.begin(), std::move(*impl_->visibilityResult));
        impl_->visibilityResult.reset();
    }
    for (auto& result : impl_->results) {
        auto& value = result.value;
        if (result.artworkEpoch != impl_->stateEpoch) {
            value.artworkUrl.clear();
            if (value.error.empty()) value.state.clear();
            else value.state = StateError(value.state, value.error, true);
        } else if (!impl_->visible) {
            value.artworkUrl.clear();
            if (!value.state.empty()) value.state = StateError(value.state, {}, true);
        } else {
            if (!impl_->liveArtwork.count(value.artworkUrl)) value.artworkUrl.clear();
            if (!value.state.empty())
                value.state = StateError(value.state, {}, false, &impl_->liveArtwork);
        }
        if (Replacement(value.action) && value.generation != impl_->replacement) {
            value.action = Action::None;
            value.playTracks.clear(); value.playId.clear();
            value.restoredQueue.reset();
        }
        out.push_back(std::move(value));
    }
    impl_->results.clear();
    if (impl_->saveError) {
        MusicLibraryWorkerResult failure;
        failure.error = std::move(*impl_->saveError);
        failure.state = std::move(impl_->saveErrorState);
        impl_->saveError.reset();
        out.push_back(std::move(failure));
    }
    return out;
}
std::optional<music::Track> MusicLibraryWorker::Resolve(std::string_view id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping || !Id(id)) return {};
    const auto found = std::find_if(impl_->cache.begin(), impl_->cache.end(),
        [&](const auto& track) { return track.id == id; });
    return found == impl_->cache.end() ? std::nullopt : std::optional<music::Track>(*found);
}
bool MusicLibraryWorker::HasWork() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->active || !impl_->requests.empty() || !impl_->results.empty() ||
        impl_->pending.has_value() || impl_->saveError.has_value() || impl_->revokeArtwork ||
        impl_->visibilityResult.has_value();
}
void MusicLibraryWorker::Shutdown() {
    std::lock_guard<std::mutex> shutdown(impl_->shutdownMutex);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stopping = true;
        impl_->visible = false;
        ++*impl_->artworkEpoch;
        impl_->condition.notify_one();
    }
    if (impl_->thread.joinable()) impl_->thread.join();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->requests.clear();
    impl_->results.clear();
    impl_->visibilityResult.reset();
    impl_->cache.clear();
    impl_->liveArtwork.clear();
    impl_->active = impl_->revokeArtwork = false;
}
}  // namespace lume::shell
