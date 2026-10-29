#include "apps/shell/music_session.h"

#include "platform/windows/music_host_client.h"
#include "platform/windows/music_local_files.h"
#include "platform/windows/music_local_lyrics.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>

#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace lume::shell {
namespace {

bool OnlineTrack(const music::Track& track) {
    return track.source.empty() || track.source == "online:" + track.id;
}

std::string_view TrackProvider(const music::Track& track) {
    const std::string_view id(track.id);
    const auto separator = id.find(':');
    return separator == id.npos ? std::string_view{} : id.substr(0, separator);
}

bool SourceCurrent(const std::function<bool()>& current) {
    try { return !current || current(); }
    catch (...) { return false; }
}

class MusicHostClientAdapter final : public IMusicPlaybackHost {
public:
    explicit MusicHostClientAdapter(std::wstring executable)
        : client_({std::move(executable), 1000}) {}

    bool Start(EventHandler handler) override { return client_.Start(std::move(handler)); }
    bool Send(std::string frame) override { return client_.Send(std::move(frame)); }
    bool Send(std::string frame, const std::function<bool()>& cancelled) override {
        return client_.Send(std::move(frame), cancelled);
    }
    void Stop() override { client_.Stop(); }
    bool Running() const noexcept override { return client_.Running(); }

private:
    music::MusicHostClient client_;
};

}  // namespace

struct MusicSession::Impl {
    explicit Impl(MusicSessionOptions options) : options(std::move(options)) {}

    ~Impl() { Shutdown(); }

    std::optional<std::wstring> HandlePage(const ui::MusicPageCommand& command) {
        bool cancelContent = false;
        std::function<void()> cancel;
        std::function<void(std::uint64_t)> loadHome;
        std::function<void(std::uint64_t, std::string)> loadLyrics;
        std::uint64_t request = 0;
        std::wstring state;

        {
            std::lock_guard lock(mutex);
            if (closed) return {};
            // Cleanup revokes page access even when a queue replacement raced
            // with its message. It cannot authorize playback or a new read.
            const bool cleanup = command.action == ui::MusicAction::Close ||
                (command.action == ui::MusicAction::Visibility && !command.flag);
            if (!cleanup && command.session != 0 && command.session != model.Snapshot().session) return StateLocked();
            switch (command.action) {
            case ui::MusicAction::Snapshot:
                break;
            case ui::MusicAction::Open:
                pageOpen = true;
                pageVisible = true;
                pageDismissed = false;
                error.clear();
                break;
            case ui::MusicAction::Close:
                cancelContent = pageRequestsActive;
                cancel = options.content.cancel;
                pageRequestsActive = false;
                pageOpen = false;
                pageVisible = false;
                pageDismissed = true;
                model.SetAnalysis({});
                CancelLyricsLocked();
                // Leaving the page abandons any melody analysis in flight.
                ++scoreGeneration;
                pendingScoreRequest.reset();
                pendingScore.reset();
                scoreRequestedId.clear();
                scoreRunningKey.clear();
                if (!model.Snapshot().playing && hostActive) QueuePlaybackLocked();
                break;
            case ui::MusicAction::Visibility:
                pageVisible = pageOpen && command.flag;
                if (!command.flag) {
                    model.SetAnalysis({});
                    CancelLyricsLocked();
                    cancelContent = pageRequestsActive;
                    cancel = options.content.cancel;
                    pageRequestsActive = false;
                }
                break;
            case ui::MusicAction::LoadHome:
                pageRequestsActive = true;
                request = ++requestGeneration;
                loadHome = options.content.loadHome;
                break;
            case ui::MusicAction::LoadLyrics:
                if (command.session == 0 || command.session > 9007199254740991ull ||
                    !pageOpen || !pageVisible || !model.Snapshot().current ||
                    command.id != model.Snapshot().current->id) break;
                if (lyricsRequested) break;
                lyricsRequested = true;
                lyricsSession = model.Snapshot().session;
                lyricsTrackId = model.Snapshot().current->id;
                if (tracks.IsImported(*model.Snapshot().current)) {
                    QueueLyricsLocked({lyricsGeneration.load(), lyricsSession, *model.Snapshot().current});
                    break;
                }
                if (OnlineTrack(*model.Snapshot().current) && options.readOnlineLyrics) {
                    QueueLyricsLocked({lyricsGeneration.load(), lyricsSession, *model.Snapshot().current, true});
                    break;
                }
                PublishEmptyLyricsLocked();
                pageRequestsActive = true;
                request = ++requestGeneration;
                loadLyrics = options.content.loadLyrics;
                break;
            case ui::MusicAction::LoadScore:
                if (command.session == 0 || command.session > 9007199254740991ull || !options.analyzeScore ||
                    !pageOpen || !pageVisible || !model.Snapshot().current ||
                    command.id != model.Snapshot().current->id) break;
                scoreRequestedId = command.id;
                StartScoreLocked();
                break;
            case ui::MusicAction::Login:
            case ui::MusicAction::Logout:
                break;
            case ui::MusicAction::Play:
                if (!model.Play()) return StateLocked();
                QueuePlaybackLocked();
                break;
            case ui::MusicAction::Pause:
                if (!model.Pause()) return StateLocked();
                QueuePlaybackLocked();
                break;
            case ui::MusicAction::Toggle:
                if (!model.Toggle()) return StateLocked();
                QueuePlaybackLocked();
                break;
            case ui::MusicAction::Previous:
                if (!model.Previous()) return StateLocked();
                QueuePlaybackLocked(true);
                break;
            case ui::MusicAction::Next:
                if (!model.Next()) return StateLocked();
                QueuePlaybackLocked(true);
                break;
            case ui::MusicAction::Seek:
                if (!model.Seek(command.position)) return StateLocked();
                model.SetAnalysis({});
                ++seekGeneration;
                QueuePlaybackLocked();
                break;
            case ui::MusicAction::PlayNow:
                if (command.id.empty() || command.id.size() > 128 || command.id.find('\0') != std::string::npos ||
                    command.index >= model.Snapshot().queue.size() ||
                    model.Snapshot().queue[command.index].id != command.id) return StateLocked();
                if (!model.PlayNow(command.index)) return StateLocked();
                QueuePlaybackLocked(true);
                break;
            case ui::MusicAction::ClearQueue:
                model.ClearQueue();
                error.clear();
                QueuePlaybackLocked(true);
                break;
            case ui::MusicAction::SetRepeat:
                model.SetRepeat(command.value == "one" ? music::RepeatMode::One :
                    command.value == "all" ? music::RepeatMode::All : music::RepeatMode::None);
                break;
            case ui::MusicAction::SetShuffle:
                model.SetShuffle(command.flag);
                break;
            case ui::MusicAction::SetVolume:
                if (!model.SetVolume(command.volume)) return StateLocked();
                QueuePlaybackLocked();
                break;
            case ui::MusicAction::SetMuted:
                model.SetMuted(command.flag);
                QueuePlaybackLocked();
                break;
            case ui::MusicAction::SetQuality: {
                if (!music::ValidAudioQuality(command.value) ||
                    (model.Snapshot().current && OnlineTrack(*model.Snapshot().current) &&
                        !music::ValidAudioQuality(command.value, TrackProvider(*model.Snapshot().current)))) return StateLocked();
                audioRequested = true;
                if (command.value == requestedQuality && (audio->switching || audio->quality.empty() ||
                    command.value == audio->quality)) return StateLocked();
                const bool retry = command.value == requestedQuality;
                requestedQuality = command.value;
                audio->requestedQuality = requestedQuality;
                error.clear();
                if (model.Snapshot().current && OnlineTrack(*model.Snapshot().current))
                    QueuePlaybackLocked(false, retry);
                break;
            }
            case ui::MusicAction::InsertNext:
                if (command.session == 0) return StateLocked();
                if (const auto* track = tracks.Resolve(command.id)) model.InsertNext(*track);
                break;
            case ui::MusicAction::Enqueue:
                if (command.session == 0) return StateLocked();
                if (const auto* track = tracks.Resolve(command.id)) model.Enqueue(*track);
                break;
            case ui::MusicAction::RemoveFromQueue:
                if (command.session == 0) return StateLocked();
                {
                    const auto current = model.Snapshot().current;
                    if (!model.RemoveFromQueue(command.id)) return StateLocked();
                    if (current && current->id == command.id) QueuePlaybackLocked(true);
                    if (model.Snapshot().queue.empty()) error.clear();
                }
                break;
            }
            InvalidateChangedLyricsLocked();
            state = StateLocked();
        }
        if (cancelContent && cancel) cancel();
        if (loadHome) loadHome(request);
        if (loadLyrics) loadLyrics(request, command.id);
        return state;
    }

    void SetQueue(std::vector<music::Track> queue) {
        std::lock_guard lock(mutex);
        if (closed) return;
        tracks.Replace(std::move(queue));
        model.ReplaceQueue(tracks.Tracks());
        QueuePlaybackLocked(true);
        CancelLyricsLocked();
        PublishEmptyLyricsLocked();
        error.clear();
        MarkPendingLocked();
    }

    void RestoreQueue(const music::MusicSnapshot& saved, bool imported) {
        std::lock_guard lock(mutex);
        if (closed) return;
        tracks.Replace(saved.queue, imported);
        model.ReplaceQueue(tracks.Tracks());
        if (saved.current) {
            const auto& queue = model.Snapshot().queue;
            const auto selected = std::find_if(queue.begin(), queue.end(),
                [&](const auto& track) { return track.id == saved.current->id; });
            if (selected != queue.end()) {
                model.PlayNow(static_cast<std::size_t>(selected - queue.begin()));
                model.Pause();
                model.Seek(saved.position);
            }
        }
        model.SetRepeat(saved.repeat);
        model.SetShuffle(saved.shuffle);
        model.SetVolume(saved.volume);
        model.SetMuted(saved.muted);
        QueuePlaybackLocked(true);
        CancelLyricsLocked();
        PublishEmptyLyricsLocked();
        error.clear();
        MarkPendingLocked();
    }

    bool PlayTracks(std::vector<music::Track> queue, const std::string& id, bool imported) {
        music::NativeTrackOwner replacement;
        replacement.Replace(std::move(queue), imported);
        const auto* selected = replacement.Resolve(id);
        if (!selected) return false;
        const auto index = static_cast<std::size_t>(selected - replacement.Tracks().data());
        std::lock_guard lock(mutex);
        if (closed) return false;
        tracks = std::move(replacement);
        model.ReplaceQueue(tracks.Tracks());
        model.PlayNow(index);
        QueuePlaybackLocked(true);
        CancelLyricsLocked();
        PublishEmptyLyricsLocked();
        error.clear();
        MarkPendingLocked();
        return true;
    }

    bool EnqueueTracks(std::vector<music::Track> additions, bool insertNext, bool imported) {
        std::lock_guard lock(mutex);
        if (closed) return false;
        if (additions.size() > music::kMaxMusicTracks) additions.resize(music::kMaxMusicTracks);
        bool changed = false;
        if (insertNext && !model.Snapshot().current) {
            while (!additions.empty() && !model.Snapshot().current) {
                if (const auto* track = tracks.Authorize(additions.front(), imported)) changed = model.InsertNext(*track) || changed;
                additions.erase(additions.begin());
            }
        }
        if (insertNext) std::reverse(additions.begin(), additions.end());
        for (const auto& candidate : additions) {
            const auto* track = tracks.Authorize(candidate, imported);
            if (track) changed = (insertNext ? model.InsertNext(*track) : model.Enqueue(*track)) || changed;
        }
        if (changed) {
            InvalidateChangedLyricsLocked();
            MarkPendingLocked();
        }
        return changed;
    }

    music::MusicSnapshot Snapshot() const {
        std::lock_guard lock(mutex);
        return model.Snapshot();
    }

    void ImportLocalFiles(std::vector<std::wstring> paths) {
        const auto result = music::ImportLocalMusicFiles(paths);
        std::lock_guard lock(mutex);
        if (closed) return;
        if (!result.error.empty()) {
            error = "local-import-" + result.error;
            MarkPendingLocked();
            return;
        }
        tracks.Replace(result.tracks, true);
        model.ReplaceQueue(tracks.Tracks());
        QueuePlaybackLocked(true);
        CancelLyricsLocked();
        PublishEmptyLyricsLocked();
        error.clear();
        MarkPendingLocked();
    }

    std::wstring State() const {
        std::lock_guard lock(mutex);
        return StateLocked();
    }

    std::wstring WidgetState() const {
        std::lock_guard lock(mutex);
        return ui::EncodeMusicWidgetState(model.Snapshot());
    }

    std::optional<std::wstring> DrainNotification() {
        std::lock_guard lock(mutex);
        if (closed) return {};
        // A finished melody analysis goes out first: it is a one-off message,
        // while playback ticks re-arm the state notification several times a
        // second and would otherwise starve it.
        if (pendingScore && pageOpen && pageVisible) {
            const auto& snapshot = model.Snapshot();
            auto score = std::move(*pendingScore);
            pendingScore.reset();
            if (pendingNotification && options.wake) options.wake();
            if (snapshot.current && snapshot.current->id == score.trackId) {
                score.session = snapshot.session;
                return ui::EncodeMusicScore(score);
            }
        }
        if (pendingNotification && !(pendingLyrics && lyricsNext)) {
            pendingNotification = false;
            lyricsNext = pendingLyrics.has_value();
            if (pendingLyrics && options.wake) options.wake();
            return StateLocked();
        }
        if (!pendingLyrics || !pageOpen || !pageVisible) return {};
        auto message = ui::EncodeMusicLyrics(*pendingLyrics);
        pendingLyrics.reset();
        lyricsNext = false;
        if (pendingNotification && options.wake) options.wake();
        return message;
    }

    // ---- lyrics for the island ----
    std::vector<music::LyricLine> CurrentLyrics() {
        std::lock_guard lock(mutex);
        const auto& snapshot = model.Snapshot();
        if (closed || !snapshot.current) return {};
        const auto& track = *snapshot.current;
        const auto key = std::to_string(snapshot.session) + "\n" + track.id;
        if (islandLyricsKey == key) return islandLyrics;
        if (islandWantedKey == key) return {};
        const bool imported = tracks.IsImported(track);
        const bool online = !imported && OnlineTrack(track) && options.readOnlineLyrics;
        islandWantedKey = key;
        if (!imported && !online) { islandLyricsKey = key; islandLyrics.clear(); return {}; }
        islandRequest = LyricsRequest{++islandGeneration, snapshot.session, track, online};
        if (islandThread.joinable() && islandWorkerFinished) islandThread.join();
        if (!islandThread.joinable()) {
            islandWorkerFinished = false;
            islandThread = std::thread([this] { IslandLyricsLoop(); });
        }
        return {};
    }
    void IslandLyricsLoop() {
        for (;;) {
            LyricsRequest request;
            {
                std::lock_guard lock(mutex);
                if (closed || !islandRequest) { islandWorkerFinished = true; return; }
                request = std::move(*islandRequest);
                islandRequest.reset();
            }
            const auto cancelled = [&] { return islandGeneration.load() != request.generation; };
            music::LocalLyricsResult result;
            try {
                if (request.online) result = options.readOnlineLyrics(request.track, cancelled);
                else result = options.readLocalLyrics ? options.readLocalLyrics(request.track, cancelled) :
                    music::LoadLocalLyrics(request.track, cancelled);
            } catch (...) { result = {{}, "read"}; }
            if (!result.error.empty() || !music::ValidateLyrics(result.lines)) result.lines.clear();
            std::lock_guard lock(mutex);
            if (closed || cancelled()) continue;
            islandLyricsKey = std::to_string(request.session) + "\n" + request.track.id;
            islandLyrics = std::move(result.lines);
            if (options.wake) options.wake();
        }
    }

    void Shutdown() {
        std::unique_lock lock(mutex);
        if (closed) return;
        closed = true;
        ++islandGeneration;
        islandRequest.reset();
        auto islandWorker = std::move(islandThread);
        model.Pause();
        CancelLyricsLocked();
        tracks.Replace({});
        stopWorker = true;
        operations.clear();
        condition.notify_all();
        auto worker = std::move(workerThread);
        auto lyricsWorker = std::move(lyricsThread);
        ++scoreGeneration;
        pendingScoreRequest.reset();
        auto scoreWorker = std::move(scoreThread);
        lock.unlock();
        if (worker.joinable()) worker.join();
        if (lyricsWorker.joinable()) lyricsWorker.join();
        if (scoreWorker.joinable()) scoreWorker.join();
        if (islandWorker.joinable()) islandWorker.join();
    }

    bool HasWork() const noexcept {
        std::lock_guard lock(mutex);
        return !closed && (hostActive || !operations.empty() || pendingNotification || pendingLyrics || pendingScore ||
            (workerThread.joinable() && !workerFinished) ||
            (lyricsThread.joinable() && !lyricsWorkerFinished) ||
            (scoreThread.joinable() && !scoreWorkerFinished));
    }

    // ---- 音符律动 melody analysis ----
    // The page asks once per track while the style is shown; the analysis waits
    // for playback to have loaded that track's source, runs once per source on
    // its own worker, and keeps the last few results for replays.
    struct ScoreRequest {
        std::uint64_t generation = 0;
        std::string trackId;
        std::string source;
    };
    struct ScoreEntry {
        std::string trackId;
        std::string source;
        ui::MusicScoreMessage message;
    };
    void StartScoreLocked() {
        const auto& snapshot = model.Snapshot();
        if (closed || scoreRequestedId.empty() || !snapshot.current || snapshot.current->id != scoreRequestedId ||
            scoreSourceId != scoreRequestedId || scoreSource.empty()) return;
        for (const auto& entry : scoreCache) if (entry.trackId == scoreRequestedId && entry.source == scoreSource) {
            pendingScore = entry.message;
            scoreRequestedId.clear();
            if (options.wake) options.wake();
            return;
        }
        const auto key = scoreRequestedId + '\n' + scoreSource;
        if (key == scoreRunningKey) return;
        scoreRunningKey = key;
        pendingScoreRequest = ScoreRequest{++scoreGeneration, scoreRequestedId, scoreSource};
        if (scoreThread.joinable() && scoreWorkerFinished) scoreThread.join();
        if (!scoreThread.joinable()) {
            scoreWorkerFinished = false;
            scoreThread = std::thread([this] { ScoreWorkerLoop(); });
        }
    }
    void PublishScoreSourceLocked(const std::string& trackId, const std::string& source) {
        scoreSourceId = trackId;
        scoreSource = source;
        StartScoreLocked();
    }
    void ScoreWorkerLoop() {
        for (;;) {
            ScoreRequest request;
            {
                std::lock_guard lock(mutex);
                if (closed || !pendingScoreRequest) { scoreWorkerFinished = true; return; }
                request = std::move(*pendingScoreRequest);
                pendingScoreRequest.reset();
            }
            const auto cancelled = [&] { return scoreGeneration.load() != request.generation; };
            music::MelodyAnalysis analysis;
            try { analysis = options.analyzeScore(request.source, cancelled); } catch (...) {}
            std::lock_guard lock(mutex);
            if (closed || cancelled()) continue;
            scoreRunningKey.clear();
            ui::MusicScoreMessage message{0, request.trackId, analysis.hop, std::move(analysis.pitch),
                analysis.tempo, std::move(analysis.beats), analysis.start, analysis.end};
            scoreCache.push_back({request.trackId, request.source, message});
            if (scoreCache.size() > 4) scoreCache.pop_front();
            const auto& snapshot = model.Snapshot();
            if (scoreRequestedId == request.trackId && snapshot.current && snapshot.current->id == request.trackId) {
                scoreRequestedId.clear();
                pendingScore = std::move(message);
                if (options.wake) options.wake();
            }
        }
    }

    bool HostRunning() const noexcept {
        std::lock_guard lock(mutex);
        return hostActive;
    }

private:
    struct PlaybackTarget {
        std::uint64_t generation = 0;
        std::uint64_t selection = 0;
        std::uint64_t seek = 0;
        std::uint64_t session = 0;
        std::optional<music::Track> current;
        double position = 0.0;
        bool playing = false;
        bool paused = false;
        double volume = 1.0;
        bool muted = false;
        std::string quality;
        bool retryQuality = false;
    };
    struct LyricsRequest {
        std::uint64_t generation;
        std::uint64_t session;
        music::Track track;
        bool online = false;
    };
    struct Operation {
        bool poll = false;
        PlaybackTarget target{};
    };
    struct HostEventResponse {
        music::HostEvent event;
        std::string action;
        bool publish = false;
    };

    PlaybackTarget TargetLocked() const {
        const auto& state = model.Snapshot();
        return {playbackGeneration, selectionGeneration, seekGeneration, state.session,
            state.current, state.position, state.playing, state.status == music::PlaybackStatus::Paused,
            state.volume, state.muted, requestedQuality};
    }

    void QueuePlaybackLocked(bool select = false, bool retryQuality = false) {
        ++playbackGeneration;
        if (select) {
            ++selectionGeneration;
            const auto& current = model.Snapshot().current;
            if (current && OnlineTrack(*current) && !music::ValidAudioQuality(requestedQuality, TrackProvider(*current)))
                requestedQuality = "standard";
            if (audio) { audio->requestedQuality = requestedQuality; audio->quality.clear(); audio->bitrate = 0; }
        }
        if (audio) audio->switching = !select && !audio->quality.empty() &&
            (requestedQuality != resolvedRequestedQuality || retryQuality);
        operations.clear();
        // A native queue replacement alone must keep the playback host lazy.
        if (!hostActive && !playbackInFlight && !model.Snapshot().playing) return;
        Operation operation;
        operation.target = TargetLocked();
        operation.target.retryQuality = retryQuality;
        QueueLocked(std::move(operation));
    }

    void CancelLyricsLocked() {
        ++lyricsGeneration;
        pendingLyricsRequest.reset();
        pendingLyrics.reset();
        lyricsNext = false;
        lyricsRequested = false;
        lyricsTrackId.clear();
        lyricsSession = 0;
    }

    void QueueLyricsLocked(LyricsRequest request) {
        pendingLyricsRequest = std::move(request);
        // Finished is published after the last read, with no further I/O or
        // mutex acquisition on that thread. Only a retired worker is joined here.
        if (lyricsThread.joinable() && lyricsWorkerFinished) lyricsThread.join();
        if (!lyricsThread.joinable()) {
            lyricsWorkerFinished = false;
            lyricsThread = std::thread([this] { LyricsWorkerLoop(); });
        }
    }

    void LyricsWorkerLoop() {
        for (;;) {
            LyricsRequest request;
            {
                std::lock_guard lock(mutex);
                if (closed || !pendingLyricsRequest) {
                    lyricsWorkerFinished = true;
                    return;
                }
                request = std::move(*pendingLyricsRequest);
                pendingLyricsRequest.reset();
            }
            ReadLyrics(request);
        }
    }

    void PublishEmptyLyricsLocked() {
        if (!pageOpen || !pageVisible) return;
        const auto& snapshot = model.Snapshot();
        pendingLyrics = ui::MusicLyricsMessage{snapshot.session, snapshot.current ? snapshot.current->id : "", {}};
        if (options.wake) options.wake();
    }

    void InvalidateChangedLyricsLocked() {
        const auto& snapshot = model.Snapshot();
        if ((lyricsRequested && (lyricsSession != snapshot.session || !snapshot.current || lyricsTrackId != snapshot.current->id)) ||
            (pendingLyrics && (pendingLyrics->session != snapshot.session ||
                pendingLyrics->trackId != (snapshot.current ? snapshot.current->id : "")))) {
            CancelLyricsLocked();
            PublishEmptyLyricsLocked();
        }
    }

    void ReadLyrics(const LyricsRequest& request) {
        const auto cancelled = [&] { return lyricsGeneration.load() != request.generation; };
        if (cancelled()) return;
        music::LocalLyricsResult result;
        try {
            if (request.online) result = options.readOnlineLyrics(request.track, cancelled);
            else result = options.readLocalLyrics ? options.readLocalLyrics(request.track, cancelled) :
                    music::LoadLocalLyrics(request.track, cancelled);
        } catch (...) {
            result = {{}, "read"};
        }
        if (!result.error.empty() || !music::ValidateLyrics(result.lines)) result.lines.clear();
        std::lock_guard lock(mutex);
        const auto& snapshot = model.Snapshot();
        if (closed || cancelled() || !pageOpen || !pageVisible || snapshot.session != request.session ||
            !snapshot.current || snapshot.current->id != request.track.id || snapshot.current->source != request.track.source) return;
        pendingLyrics = ui::MusicLyricsMessage{request.session, request.track.id, std::move(result.lines)};
        if (options.wake) options.wake();
    }

    std::wstring StateLocked() const {
        ui::MusicPageState state;
        state.snapshot = model.Snapshot();
        state.pageOpen = pageOpen;
        state.pageVisible = pageVisible;
        state.accountSignedIn = options.accountSignedIn && options.accountSignedIn();
        state.error = error;
        if (audioRequested) state.audio = audio;
        return ui::EncodeMusicPageState(state);
    }

    void QueueLocked(Operation operation) {
        if (workerThread.joinable() && workerFinished) workerThread.join();
        if (!workerThread.joinable()) {
            stopWorker = false;
            workerFinished = false;
            workerThread = std::thread([this] { WorkerLoop(); });
        }
        operations.push_back(std::move(operation));
        condition.notify_one();
    }

    void MarkPendingLocked() {
        pendingNotification = true;
        if (options.wake) options.wake();
    }

    std::string SessionToken() const {
        return std::to_string(GetCurrentProcessId()) + "." + std::to_string(sessionGeneration);
    }

    std::string Frame(std::string action, bool hasValue = false, double value = 0.0,
                      std::string source = {}) {
        music::HostCommand command;
        command.session = hostSession;
        command.sequence = ++sequence;
        command.action = std::move(action);
        command.hasValue = hasValue;
        command.value = value;
        command.flag = value != 0.0;
        command.source = std::move(source);
        command.analysis = command.action == "hello" && (pageOpen && pageVisible ||
            (options.analysisActive && options.analysisActive()));
        return music::EncodeHostCommand(command);
    }

    bool EnsureHost(const PlaybackTarget& target, const std::function<bool()>& current = {}) {
        if (host && host->Running()) return true;
        if (host) DropHost();
        std::unique_ptr<IMusicPlaybackHost> created;
        if (options.createHost) created = options.createHost();
        else if (!options.executable.empty()) created = std::make_unique<MusicHostClientAdapter>(options.executable);
        if (!created) {
            SetError("music-host-unavailable", target.generation);
            return false;
        }
        {
            std::lock_guard lock(mutex);
            ++sessionGeneration;
            hostSession = SessionToken();
        }
        sequence = 0;
        closeSent = false;
        const bool started = created->Start([this](std::string frame) { OnHostFrame(std::move(frame)); });
        if (!started) {
            created->Stop();
            SetError("music-host-unavailable", target.generation);
            return false;
        }
        host = std::move(created);
        {
            std::lock_guard lock(mutex);
            hostActive = true;
        }
        return SendTarget(target, "hello", false, 0.0, current);
    }

    void DropHost() {
        if (host) host->Stop();
        host.reset();
        std::lock_guard lock(mutex);
        hostActive = false;
        hostSession.clear();
        loadedSelection = 0;
        loadedSession = 0;
        loadedId.clear();
        loadedSource.clear();
        resolvedSource.clear();
        resolvedId.clear();
        resolvedSession = 0;
        resolvedRequestedQuality.clear();
        resolvedQuality.clear();
        resolvedBitrate = 0;
        resolvedCurrent = {};
        sourcePending = false;
        if (audio) { audio->quality.clear(); audio->bitrate = 0; audio->switching = false; }
        appliedVolume = 1.0;
        appliedMuted = false;
        expectedSequence = 0;
        response.reset();
    }

    void SetError(std::string value, std::uint64_t generation) {
        std::lock_guard lock(mutex);
        if (closed || generation != playbackGeneration) return;
        error = std::move(value);
        if (audio) { audio->quality.clear(); audio->bitrate = 0; audio->switching = false; }
        model.FailPlayback();
        MarkPendingLocked();
    }

    void SetQualityError(std::string value, std::uint64_t generation) {
        std::lock_guard lock(mutex);
        if (closed || generation != playbackGeneration) return;
        requestedQuality = resolvedRequestedQuality.empty() ? "standard" : resolvedRequestedQuality;
        audio->requestedQuality = requestedQuality;
        audio->switching = false;
        error = std::move(value);
        MarkPendingLocked();
    }

    // A level the member cannot actually stream (empty URL, undecodable or
    // refused) falls back one step instead of failing this and every later
    // track. Surround/master levels fold to lossless or Hi-Res first.
    static std::string LowerQuality(std::string_view quality) {
        if (quality == "jymaster") return "hires";
        if (quality == "sky" || quality == "jyeffect" || quality == "hires") return "lossless";
        if (quality == "lossless") return "high";
        if (quality == "high") return "standard";
        return {};
    }

    bool DowngradeQuality(const PlaybackTarget& target) {
        std::lock_guard lock(mutex);
        if (closed || target.generation != playbackGeneration || !target.current || !OnlineTrack(*target.current))
            return false;
        // Re-login is the only remedy for these; a lower level would fail too.
        if (resolveFailure == "expired" || resolveFailure == "signed-out") return false;
        const auto next = LowerQuality(target.quality);
        if (next.empty()) return false;
        requestedQuality = next;
        resolvedRequestedQuality.clear();
        if (audio) { audio->requestedQuality = requestedQuality; audio->quality.clear(); audio->bitrate = 0; audio->switching = false; }
        ++playbackGeneration;
        operations.clear();
        Operation operation;
        operation.target = TargetLocked();
        QueueLocked(std::move(operation));
        MarkPendingLocked();
        return true;
    }

    std::string ResolveError() const {
        std::lock_guard lock(mutex);
        if (resolveFailure == "expired" || resolveFailure == "signed-out") return "music-source-expired";
        if (resolveFailure == "network") return "music-source-network";
        return "music-source-unavailable";
    }

    bool IsCurrent(const PlaybackTarget& target) const {
        std::lock_guard lock(mutex);
        return !closed && target.generation == playbackGeneration;
    }

    bool SendTarget(const PlaybackTarget& target, const char* action, bool publish = true,
                    double position = 0.0, const std::function<bool()>& sourceCurrent = {}) {
        if (!SourceCurrent(sourceCurrent)) {
            SetError("music-source-unavailable", target.generation);
            DropHost();
            return false;
        }
        std::string frame;
        {
            std::lock_guard lock(mutex);
            if (closed || target.generation != playbackGeneration) return false;
            frame = Frame(action, std::string_view(action) == "seek" || std::string_view(action) == "setVolume", position,
                std::string_view(action) == "load" ? resolvedSource : "");
            expectedSequence = sequence;
            expectedGeneration = target.generation;
            response.reset();
        }
        const bool sent = host->Send(std::move(frame), [this, target, sourceCurrent] {
            return !IsCurrent(target) || !SourceCurrent(sourceCurrent);
        });
        std::optional<music::HostEvent> reply;
        bool current;
        {
            std::lock_guard lock(mutex);
            expectedSequence = 0;
            reply = std::move(response);
            response.reset();
            current = !closed && target.generation == playbackGeneration;
        }
        if (!SourceCurrent(sourceCurrent)) {
            SetError("music-source-unavailable", target.generation);
            DropHost();
            return false;
        }
        if (!current) {
            // Only an obsolete load changes the loaded source; controls keep live audio.
            if (std::string_view(action) == "load") {
                loadedSelection = 0;
                loadedSession = 0;
            }
            appliedVolume = -1;
            appliedMuted.reset();
            return false;
        }
        if (!sent || !reply) {
            SetError(sent ? "music-host-unconfirmed" : "music-host-unavailable", target.generation);
            DropHost();
            return false;
        }
        if (!reply->error.empty()) {
            if (std::string_view(action) == "load" && reply->error == "load-failed" && DowngradeQuality(target)) {
                DropHost();
                return false;
            }
            SetError(reply->error, target.generation);
            DropHost();
            return false;
        }
        const bool applied = ApplyHostResponse(target, {*reply, action, publish}, sourceCurrent);
        if (!applied) {
            if (!SourceCurrent(sourceCurrent)) {
                SetError("music-source-unavailable", target.generation);
                DropHost();
            } else if (IsCurrent(target)) DropHost();
        }
        return applied;
    }

    bool ApplyHostResponse(const PlaybackTarget& target, const HostEventResponse& acknowledgement,
                           const std::function<bool()>& sourceCurrent = {}) {
        const auto& event = acknowledgement.event;
        std::lock_guard lock(mutex);
        if (closed || target.generation != playbackGeneration || !SourceCurrent(sourceCurrent)) return false;
        if (acknowledgement.action == "hello" && !acknowledgement.publish) return true;
        if (target.current && !event.trackId.empty() && event.trackId != resolvedSource) {
            model.FailPlayback();
            error = "music-host-source-mismatch";
            MarkPendingLocked();
            return false;
        }
        // Some HTTP/ADTS streams report no duration; keep the provider value.
        if(event.duration>0)model.SetDuration(event.duration);
        if ((acknowledgement.action == "setVolume" && event.volume != target.volume) ||
            (acknowledgement.action == "setMuted" && event.muted != target.muted)) {
            model.FailPlayback();
            error = "music-control-unconfirmed";
            MarkPendingLocked();
            return false;
        }
        if (!acknowledgement.publish) {
            if (acknowledgement.action == "load") {
                error.clear();
                if (target.current && OnlineTrack(*target.current)) audioRequested = true;
                if (audio) { audio->quality = resolvedQuality; audio->bitrate = resolvedBitrate; }
            }
            MarkPendingLocked();
            return true;
        }
        const bool wasPlaying = model.Snapshot().playing;
        if (model.Snapshot().current) model.Seek(event.position);
        const bool eof = (acknowledgement.action == "hello" || acknowledgement.action == "play") &&
            wasPlaying && !event.playing && event.duration > 0.0 &&
            event.position >= event.duration - 0.01;
        if (eof && model.EndOfTrack()) {
            error.clear();
            QueuePlaybackLocked(true);
            InvalidateChangedLyricsLocked();
        } else if (event.playing != target.playing) {
            model.FailPlayback();
            error = "music-playback-stopped";
            MarkPendingLocked();
            return false;
        } else {
            if (event.playing) model.Play();
            else if (target.paused) model.Pause();
            model.SetAnalysis((pageOpen && pageVisible) || (options.analysisActive && options.analysisActive())
                ? event.analysis : music::AudioAnalysis{});
            if (acknowledgement.action != "hello") error.clear();
        }
        MarkPendingLocked();
        return true;
    }

    music::ResolvedAudioSource ResolveAudio(const PlaybackTarget& target,
                                          const std::function<bool()>& abort = {}) {
        music::ResolvedAudioSource source;
        source.url = target.current->source;
        if (OnlineTrack(*target.current)) {
            const auto cancelled = [this, target, abort] { return !IsCurrent(target) || (abort && abort()); };
            {
                std::lock_guard lock(mutex);
                resolveFailure.clear();
            }
            try {
                if (options.resolveAudioSource)
                    source = options.resolveAudioSource(*target.current, target.quality, cancelled);
                else if (target.quality == "standard" && options.resolveSource)
                    source.url = options.resolveSource(*target.current, cancelled);
            } catch (const std::exception& failure) {
                // Provider reasons are whitelisted words (expired, network...).
                std::lock_guard lock(mutex);
                resolveFailure = failure.what();
                if (resolveFailure.size() > 32) resolveFailure.clear();
                return {};
            } catch (...) { return {}; }
            if (cancelled() || (source.url.rfind("https://", 0) != 0 &&
                !(TrackProvider(*target.current) == "kugou" && music::IsKugouHttpAudioUrl(source.url)))) return {};
        }
        if (source.url.empty() || source.url.size() > 4096 || source.url.rfind("online:", 0) == 0 ||
            source.url.find_first_of("\r\n") != std::string::npos || source.url.find('\0') != std::string::npos ||
            (!source.quality.empty() && !music::ValidAudioQuality(source.quality, TrackProvider(*target.current))) ||
            source.bitrate > 10000000 || !SourceCurrent(source.current)) return {};
        return source;
    }

    void SwitchQuality(const PlaybackTarget& target) {
        // One budget covers resolution, process startup, load and preroll.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        std::atomic<bool> abort{false};
        const auto cancelled = [&] {
            return abort.load() || !IsCurrent(target) || std::chrono::steady_clock::now() >= deadline;
        };
        music::ResolvedAudioSource source;
        std::unique_ptr<IMusicPlaybackHost> candidate;
        // Polling may drop the live host, so do not read its mutable source
        // metadata from the preparation thread.
        const auto liveSource = resolvedSource;
        const auto liveQuality = resolvedQuality;
        const auto liveCurrent = resolvedCurrent;
        std::string token;
        {
            std::lock_guard lock(mutex);
            ++sessionGeneration;
            token = SessionToken();
        }
        struct CandidateResponse {
            std::mutex mutex;
            std::uint64_t expected = 0;
            std::optional<music::HostEvent> event;
        };
        auto replies = std::make_shared<CandidateResponse>();
        const auto onFrame = [this, replies, token](std::string frame) {
            OnHostFrame(frame);
            const auto event = music::DecodeHostEvent(frame);
            if (!event || event->session != token) return;
            std::lock_guard lock(replies->mutex);
            if (replies->expected && event->sequence == replies->expected && !replies->event) replies->event = *event;
        };
        std::uint64_t candidateSequence = 0;
        music::HostEvent last;
        std::string failure = "music-host-unavailable";
        const auto send = [&](std::string_view action, double value = 0.0) {
            if (cancelled() || !SourceCurrent(source.current)) return false;
            music::HostCommand command;
            command.session = token;
            command.sequence = ++candidateSequence;
            command.action = action;
            command.value = value;
            command.flag = value != 0;
            command.follow = action == "prepare" && target.playing;
            if (action == "load") command.source = source.url;
            {
                std::lock_guard lock(replies->mutex);
                replies->expected = command.sequence;
                replies->event.reset();
            }
            const bool sent = candidate->Send(music::EncodeHostCommand(command),
                [&] { return cancelled() || !SourceCurrent(source.current); });
            std::optional<music::HostEvent> reply;
            {
                std::lock_guard lock(replies->mutex);
                replies->expected = 0;
                reply = std::move(replies->event);
                replies->event.reset();
            }
            failure = sent ? "music-host-unconfirmed" : "music-host-unavailable";
            if (!sent || !reply || cancelled() || !SourceCurrent(source.current)) return false;
            if (!reply->error.empty()) {
                failure = action == "load" ? "load-failed" : action == "seek" || action == "prepare" ? "seek-failed" :
                    action == "setVolume" || action == "setMuted" ? "control-failed" : "playback-failed";
                return false;
            }
            if (action != "hello" && (!reply->canToggle ||
                (!reply->trackId.empty() && reply->trackId != source.url))) {
                failure = "music-host-source-mismatch"; return false;
            }
            if ((action == "setVolume" && reply->volume != value) ||
                (action == "setMuted" && reply->muted != (value != 0))) {
                failure = "music-control-unconfirmed"; return false;
            }
            if ((action == "play" && !reply->playing) || (action == "pause" && reply->playing)) {
                failure = "music-playback-stopped"; return false;
            }
            last = *reply;
            return true;
        };
        // Only the preparation thread touches the candidate. The session worker
        // keeps polling the live engine, including during provider/Load latency.
        // Always join before handing off or destroying the candidate.
        bool ready = false, unchanged = false;
        std::atomic<bool> finished{false};
        std::thread preparation;
        struct JoinPreparation {
            std::thread& thread;
            std::atomic<bool>& abort;
            ~JoinPreparation() {
                if (thread.joinable()) {
                    abort.store(true);
                    thread.join();
                }
            }
        } joinPreparation{preparation, abort};
        try {
            preparation = std::thread([&] {
                try {
                    source = ResolveAudio(target, cancelled);
                    if (source.url.empty()) failure = "music-source-unavailable";
                    else if (!cancelled()) {
                        unchanged = source.url == liveSource && source.quality == liveQuality &&
                            SourceCurrent(source.current) && SourceCurrent(liveCurrent);
                        if (unchanged) ready = true;
                        else {
                            candidate = options.createHost ? options.createHost() :
                                options.executable.empty() ? nullptr : std::make_unique<MusicHostClientAdapter>(options.executable);
                            ready = candidate && !cancelled() && candidate->Start(onFrame) &&
                                send("hello") && send("load") && send("setVolume", target.volume) && send("setMuted", 1.0);
                        }
                    }
                } catch (...) { ready = false; }
                finished.store(true);
                condition.notify_all();
            });
        } catch (...) {
            SetQualityError("music-host-unavailable", target.generation);
            return;
        }
        while (!finished.load()) {
            std::unique_lock lock(mutex);
            condition.wait_for(lock, std::chrono::milliseconds(50), [&] {
                return finished.load() || closed || playbackGeneration != target.generation;
            });
            lock.unlock();
            if (cancelled() || (!finished.load() && !SendTarget(target, "hello", true))) {
                abort.store(true);
                break;
            }
        }
        preparation.join();
        if (std::chrono::steady_clock::now() >= deadline) failure = "music-quality-buffering-timeout";
        ready = ready && !cancelled();
        if (ready && unchanged) {
            std::lock_guard lock(mutex);
            if (!closed && target.generation == playbackGeneration && SourceCurrent(source.current)) {
                resolvedRequestedQuality = target.quality;
                resolvedCurrent = source.current;
                resolvedBitrate = source.bitrate;
                audio = music::AudioQualityState{target.quality, source.quality, source.bitrate};
                error.clear();
                MarkPendingLocked();
            }
            return;
        }
        // Resolution/load may finish between live-host polls. Read its current
        // clock once more before choosing the candidate's prepare position.
        if (ready) {
            ready = SendTarget(target, "hello", true);
            double position = target.position;
            {
                std::lock_guard lock(mutex);
                position = model.Snapshot().position;
            }
            if (ready) ready = send("prepare", position);
        }
        // A seek ACK only accepts the target. Decode and buffer it before
        // replacing live audio; keep publishing the old host's clock for lyrics.
        while (ready && !last.ready) {
            std::unique_lock lock(mutex);
            condition.wait_for(lock, std::chrono::milliseconds(50),
                [this, &target] { return closed || playbackGeneration != target.generation; });
            lock.unlock();
            if (std::chrono::steady_clock::now() >= deadline) {
                ready = false;
                failure = "music-quality-buffering-timeout";
                break;
            }
            ready = IsCurrent(target) && SourceCurrent(source.current) &&
                SendTarget(target, "hello", true) && send("hello");
        }
        // A slow last packet must not rewind the song at handover.
        if (ready && target.playing) {
            ready = SendTarget(target, "hello", true);
            std::lock_guard lock(mutex);
            if (ready && std::abs(last.position - model.Snapshot().position) > 0.25) {
                ready = false;
                failure = "music-quality-buffering-timeout";
            }
        }
        if (ready) ready = send(target.playing ? "play" : "pause") && send("setMuted", target.muted ? 1.0 : 0.0);
        if (!ready || !IsCurrent(target) || !SourceCurrent(source.current)) {
            if (candidate) candidate->Stop();
            SetQualityError(SourceCurrent(source.current) ? failure : "music-source-unavailable", target.generation);
            return;
        }
        std::unique_ptr<IMusicPlaybackHost> previous;
        {
            std::lock_guard lock(mutex);
            if (!closed && target.generation == playbackGeneration && SourceCurrent(source.current)) {
                previous = std::move(host);
                host = std::move(candidate);
                hostSession = token;
                sequence = candidateSequence;
                closeSent = false;
                expectedSequence = 0;
                response.reset();
                loadedSession = target.session;
                loadedSelection = target.selection;
                loadedId = target.current->id;
                loadedSource = target.current->source;
                resolvedSession = target.session;
                resolvedId = target.current->id;
                resolvedRequestedQuality = target.quality;
                resolvedSource = source.url;
                resolvedQuality = source.quality;
                resolvedBitrate = source.bitrate;
                resolvedCurrent = source.current;
                sourcePending = false;
                PublishScoreSourceLocked(target.current->id, resolvedSource);
                appliedVolume = target.volume;
                appliedMuted = target.muted;
                appliedSeek = target.seek;
                audio = music::AudioQualityState{target.quality, source.quality, source.bitrate};
                if(last.duration>0)model.SetDuration(last.duration);
                model.Seek(last.position);
                if (target.playing) model.Play();
                else if (target.paused) model.Pause();
                model.SetAnalysis({});
                error.clear();
                MarkPendingLocked();
            }
        }
        if (candidate) {
            candidate->Stop();
            SetQualityError("music-source-unavailable", target.generation);
        }
        if (previous) previous->Stop();
    }

    void ApplyPlayback(const PlaybackTarget& target) {
        if (!IsCurrent(target)) return;
        if (host && !host->Running()) DropHost();
        if (!target.current) {
            CloseHost(target.generation);
            return;
        }
        const bool sameSelection = loadedSession == target.session && loadedSelection == target.selection &&
            loadedId == target.current->id && loadedSource == target.current->source;
        const bool qualityChanged = OnlineTrack(*target.current) &&
            (resolvedRequestedQuality != target.quality || target.retryQuality);
        if (sameSelection && qualityChanged && host) { SwitchQuality(target); return; }
        const bool load = loadedSession != target.session || loadedSelection != target.selection || qualityChanged ||
            loadedId != target.current->id || loadedSource != target.current->source;
        if (load) {
            // Never mutate the model track with a resolver result. Only the worker
            // and native host see the signed URL.
            if (resolvedSession != target.session || resolvedId != target.current->id ||
                resolvedRequestedQuality != target.quality) {
                resolvedSource = target.current->source;
                resolvedQuality.clear();
                resolvedBitrate = 0;
                resolvedCurrent = {};
                if (OnlineTrack(*target.current)) {
                    CloseHost(target.generation);
                    auto source = ResolveAudio(target);
                    resolvedSource = std::move(source.url);
                    resolvedQuality = std::move(source.quality);
                    resolvedBitrate = source.bitrate;
                    resolvedCurrent = std::move(source.current);
                    if (!IsCurrent(target)) { resolvedSource.clear(); return; }
                }
                if (resolvedSource.empty() || resolvedSource.size() > 4096 ||
                    resolvedSource.rfind("online:", 0) == 0 ||
                    resolvedSource.find_first_of("\r\n") != std::string::npos ||
                    resolvedSource.find('\0') != std::string::npos) {
                    CloseHost(target.generation);
                    if (DowngradeQuality(target)) return;
                    SetError(ResolveError(), target.generation);
                    return;
                }
                resolvedSession = target.session;
                resolvedId = target.current->id;
                resolvedRequestedQuality = target.quality;
            }
        }
        // Keep the lease through superseded controls until playback is confirmed.
        if (load) sourcePending = true;
        const auto sourceCurrent = sourcePending ? resolvedCurrent : std::function<bool()>{};
        if (!EnsureHost(target, sourceCurrent) || !IsCurrent(target)) return;
        if (load) {
            if (!SendTarget(target, "load", false, 0.0, sourceCurrent)) return;
            loadedSession = target.session;
            loadedSelection = target.selection;
            loadedId = target.current->id;
            loadedSource = target.current->source;
            {
                std::lock_guard lock(mutex);
                PublishScoreSourceLocked(target.current->id, resolvedSource);
            }
        }
        if (appliedVolume != target.volume) {
            if (!SendTarget(target, "setVolume", false, target.volume, sourceCurrent)) return;
            appliedVolume = target.volume;
        }
        if (appliedMuted != target.muted) {
            if (!SendTarget(target, "setMuted", false, target.muted ? 1.0 : 0.0, sourceCurrent)) return;
            appliedMuted = target.muted;
        }
        if ((load && target.position > 0.0) || appliedSeek != target.seek) {
            if (!SendTarget(target, "seek", false, target.position, sourceCurrent)) return;
        }
        appliedSeek = target.seek;
        bool confirmed = true;
        if (target.playing) confirmed = SendTarget(target, "play", true, 0.0, sourceCurrent);
        else if (target.paused || !load) confirmed = SendTarget(target, "pause", true, 0.0, sourceCurrent);
        if (confirmed) sourcePending = false;
    }

    void OnHostFrame(std::string frame) {
        const auto event = music::DecodeHostEvent(frame);
        if (!event) return;
        std::lock_guard lock(mutex);
        if (closed || event->session != hostSession || expectedSequence == 0 ||
            event->sequence != expectedSequence || expectedGeneration != playbackGeneration || response) return;
        response = *event;
    }

    void WorkerLoop() {
        for (;;) {
            Operation operation{};
            {
                std::unique_lock lock(mutex);
                if (stopWorker) break;
                // A superseded target may leave no host or pending playback.
                if (!host && operations.empty()) {
                    workerFinished = true;
                    return;
                }
                if (host && model.Snapshot().playing) {
                    condition.wait_for(lock, std::chrono::milliseconds(250),
                        [this] { return stopWorker || !operations.empty(); });
                } else condition.wait(lock, [this] { return stopWorker || !operations.empty(); });
                if (stopWorker) break;
                if (operations.empty()) {
                    operation.poll = true;
                    operation.target = TargetLocked();
                } else {
                    operation = std::move(operations.front());
                    operations.pop_front();
                }
                playbackInFlight = true;
            }
            if (operation.poll) {
                if (host && IsCurrent(operation.target)) SendTarget(operation.target, "hello", true, 0.0,
                    sourcePending ? resolvedCurrent : std::function<bool()>{});
            } else {
                ApplyPlayback(operation.target);
                bool releasePausedHost = false;
                {
                    std::lock_guard lock(mutex);
                    releasePausedHost = !closed && operation.target.generation == playbackGeneration &&
                        pageDismissed && !pageOpen && !model.Snapshot().playing && hostActive;
                }
                if (releasePausedHost) CloseHost(operation.target.generation);
            }
            {
                std::lock_guard lock(mutex);
                playbackInFlight = false;
            }
        }
        CloseHost();
    }

    void CloseHost(std::optional<std::uint64_t> generation = {}) {
        if (!host) return;
        std::string closeFrame;
        {
            std::lock_guard lock(mutex);
            if (generation && *generation != playbackGeneration) return;
            expectedSequence = 0;
            response.reset();
            if (!closeSent) closeFrame = Frame("close");
        }
        // Only this worker owns the host. A newer target queued during Stop
        // runs after this host is released, with a fresh session token.
        if (!closeSent) {
            host->Send(std::move(closeFrame));
            closeSent = true;
        }
        DropHost();
    }

    MusicSessionOptions options;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::deque<Operation> operations;
    std::thread workerThread;
    std::unique_ptr<IMusicPlaybackHost> host;
    music::MusicModel model;
    music::NativeTrackOwner tracks;
    std::uint64_t playbackGeneration = 0;
    std::uint64_t selectionGeneration = 0;
    std::uint64_t seekGeneration = 0;
    std::uint64_t loadedSelection = 0;
    std::uint64_t loadedSession = 0;
    std::uint64_t appliedSeek = 0;
    std::string loadedId;
    std::string loadedSource;
    std::string resolvedSource;
    std::string resolvedId;
    std::uint64_t resolvedSession = 0;
    std::string resolvedRequestedQuality;
    std::string resolvedQuality;
    std::uint32_t resolvedBitrate = 0;
    std::function<bool()> resolvedCurrent;
    bool sourcePending = false;
    std::string requestedQuality = "standard";
    std::string resolveFailure;
    std::optional<music::AudioQualityState> audio = music::AudioQualityState{};
    bool audioRequested = false;
    double appliedVolume = 1.0;
    std::optional<bool> appliedMuted = false;
    std::uint64_t expectedSequence = 0;
    std::uint64_t expectedGeneration = 0;
    std::optional<music::HostEvent> response;
    bool playbackInFlight = false;
    std::thread lyricsThread;
    bool lyricsWorkerFinished = false;
    std::optional<LyricsRequest> pendingLyricsRequest;
    std::atomic<std::uint64_t> lyricsGeneration{0};
    std::uint64_t lyricsSession = 0;
    std::string lyricsTrackId;
    std::optional<ui::MusicLyricsMessage> pendingLyrics;
    bool lyricsNext = false;
    bool lyricsRequested = false;
    std::thread islandThread;
    bool islandWorkerFinished = false;
    std::atomic<std::uint64_t> islandGeneration{0};
    std::optional<LyricsRequest> islandRequest;
    std::string islandWantedKey, islandLyricsKey;
    std::vector<music::LyricLine> islandLyrics;
    std::thread scoreThread;
    bool scoreWorkerFinished = false;
    std::atomic<std::uint64_t> scoreGeneration{0};
    std::optional<ScoreRequest> pendingScoreRequest;
    std::optional<ui::MusicScoreMessage> pendingScore;
    std::deque<ScoreEntry> scoreCache;
    std::string scoreRequestedId, scoreSourceId, scoreSource, scoreRunningKey;
    std::string error;
    std::string hostSession;
    std::uint64_t sessionGeneration = 0;
    std::uint64_t sequence = 0;
    std::uint64_t requestGeneration = 0;
    bool pageOpen = false;
    bool pageVisible = false;
    bool pageDismissed = false;
    bool pageRequestsActive = false;
    bool pendingNotification = false;
    bool hostActive = false;
    bool closeSent = false;
    bool stopWorker = false;
    bool workerFinished = false;
    bool closed = false;
};

MusicSession::MusicSession(MusicSessionOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

MusicSession::~MusicSession() = default;

std::optional<std::wstring> MusicSession::HandlePage(const ui::MusicPageCommand& command) {
    return impl_->HandlePage(command);
}

void MusicSession::SetQueue(std::vector<music::Track> tracks) { impl_->SetQueue(std::move(tracks)); }
void MusicSession::RestoreQueue(const music::MusicSnapshot& snapshot, bool imported) {
    impl_->RestoreQueue(snapshot, imported);
}
bool MusicSession::PlayTracks(std::vector<music::Track> tracks, std::string id, bool imported) {
    return impl_->PlayTracks(std::move(tracks), id, imported);
}
bool MusicSession::EnqueueTracks(std::vector<music::Track> tracks, bool insertNext, bool imported) {
    return impl_->EnqueueTracks(std::move(tracks), insertNext, imported);
}
music::MusicSnapshot MusicSession::Snapshot() const { return impl_->Snapshot(); }
std::vector<music::LyricLine> MusicSession::CurrentLyrics() { return impl_->CurrentLyrics(); }
void MusicSession::ImportLocalFiles(std::vector<std::wstring> paths) { impl_->ImportLocalFiles(std::move(paths)); }
std::wstring MusicSession::State() const { return impl_->State(); }
std::wstring MusicSession::WidgetState() const { return impl_->WidgetState(); }
std::optional<std::wstring> MusicSession::DrainNotification() { return impl_->DrainNotification(); }
void MusicSession::Shutdown() { impl_->Shutdown(); }
bool MusicSession::HasWork() const noexcept { return impl_->HasWork(); }
bool MusicSession::HostRunning() const noexcept { return impl_->HostRunning(); }

}  // namespace lume::shell
