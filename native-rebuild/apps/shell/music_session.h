#pragma once

#include "core/music/melody_analysis.h"
#include "core/music/music_host_protocol.h"
#include "core/music/music_model.h"
#include "core/ui/music_page_protocol.h"

#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lume::shell {

class IMusicPlaybackHost {
public:
    using EventHandler = std::function<void(std::string)>;
    virtual ~IMusicPlaybackHost() = default;
    virtual bool Start(EventHandler handler) = 0;
    // Synchronous request/response: success must deliver the matching event
    // to the handler before returning. Session also validates that ACK.
    virtual bool Send(std::string frame) = 0;
    // Blocking transports override this to interrupt an obsolete request.
    virtual bool Send(std::string frame, const std::function<bool()>& cancelled) {
        if (cancelled && cancelled()) return false;
        return Send(std::move(frame));
    }
    // Stop must release playback and finish callbacks before returning.
    virtual void Stop() = 0;
    virtual bool Running() const noexcept = 0;
};

struct MusicContentPorts {
    // These callbacks only schedule work in the provider layer. They must not
    // retain the session or call back synchronously into the page.
    std::function<void(std::uint64_t)> loadHome;
    std::function<void(std::uint64_t, std::string)> loadLyrics;
    std::function<void()> cancel;
};

struct MusicSessionOptions {
    std::wstring executable;
    std::function<std::unique_ptr<IMusicPlaybackHost>()> createHost;
    std::function<bool()> accountSignedIn;
    MusicContentPorts content;
    std::function<void()> wake;
    std::function<bool()> analysisActive;
    // Native adapter only: the session authorizes the current imported track
    // before scheduling on a separate, short-lived worker with one pending
    // request. Readers must honor cancellation; shutdown joins outside the lock.
    std::function<music::LocalLyricsResult(const music::Track&,
        const std::function<bool()>&)> readLocalLyrics;
    // "online:" + id (or legacy empty-source) tracks are opaque identities. Resolve only on the
    // playback worker; enforce entitlement in the adapter, honor cancellation,
    // and return an HTTPS source or empty on denial. Never retain cancelled.
    std::function<std::string(const music::Track&,
        const std::function<bool()>&)> resolveSource;
    std::function<music::LocalLyricsResult(const music::Track&,
        const std::function<bool()>&)> readOnlineLyrics;
    // 音符律动: decode and analyze the source playback just loaded (path or
    // signed URL). Runs on its own worker and must honor cancellation.
    std::function<music::MelodyAnalysis(const std::string& source,
        const std::function<bool()>&)> analyzeScore;
    std::function<music::ResolvedAudioSource(const music::Track&, std::string_view quality,
        const std::function<bool()>&)> resolveAudioSource;
};

class MusicSession final {
public:
    explicit MusicSession(MusicSessionOptions options);
    ~MusicSession();
    MusicSession(const MusicSession&) = delete;
    MusicSession& operator=(const MusicSession&) = delete;

    std::optional<std::wstring> HandlePage(const ui::MusicPageCommand& command);
    void SetQueue(std::vector<music::Track> tracks);
    // Native persisted queues restore selection/settings without starting playback.
    void RestoreQueue(const music::MusicSnapshot&, bool imported = false);
    // Native-only atomic replacement/start. An unknown id leaves the queue unchanged.
    bool PlayTracks(std::vector<music::Track> tracks, std::string id, bool imported = false);
    bool EnqueueTracks(std::vector<music::Track> tracks, bool insertNext = false, bool imported = false);
    // Native consumers only; unlike page/widget projection this includes local sources.
    music::MusicSnapshot Snapshot() const;
    // Lyrics of the current track for native consumers (the dynamic island),
    // independent of the music page. Empty lines while loading or when the
    // track has none; the first call for a track starts a background read.
    std::vector<music::LyricLine> CurrentLyrics();
    void ImportLocalFiles(std::vector<std::wstring> paths);
    std::wstring State() const;
    std::wstring WidgetState() const;
    std::optional<std::wstring> DrainNotification();
    void Shutdown();
    bool HasWork() const noexcept;
    bool HostRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lume::shell
