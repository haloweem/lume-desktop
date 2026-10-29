#include "apps/shell/music_session.h"
#include "core/music/music_host_protocol.h"
#include "core/ui/music_page_protocol.h"
#include "platform/windows/music_local_files.h"
#include "platform/windows/music_host_client.h"
#include "vendor/nlohmann/json.hpp"

#include <chrono>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace {
using namespace lume;
using namespace lume::shell;

bool passed = true;

void Check(bool value, const char* message) {
    if (!value) {
        passed = false;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct HostFixture {
    std::atomic<int> starts{0};
    std::atomic<int> sends{0};
    std::atomic<int> stops{0};
    std::atomic<int> wakes{0};
    bool failStart = false;
    std::atomic<bool> releaseStart{true};
    std::string failLoadSource;
    std::string failSendAction;
    bool failPlay = false;
    std::string heldAction;
    std::atomic<bool> replyHeld{false}, releaseReply{true};
    bool cancelHeldReply = false;
    int holdTimeoutMs = 2000;
    std::atomic<int> cancelledReplies{0};
    bool replyOnThread = false;
    bool injectStaleReplies = false;
    bool omitReply = false;
    std::atomic<bool> preparedReady{true};
    std::mutex mutex;
    std::vector<std::string> actions;
    std::vector<music::HostCommand> commands;
    std::vector<music::HostEvent> events;
    std::function<void(const music::HostCommand&)> onCommand;
    IMusicPlaybackHost::EventHandler handler;
    music::HostEvent audio;
    std::vector<std::string> Actions() {
        std::lock_guard lock(mutex);
        return actions;
    }
    music::HostEvent Audio() {
        std::lock_guard lock(mutex);
        return audio;
    }
    std::vector<music::HostCommand> Commands() {
        std::lock_guard lock(mutex);
        return commands;
    }
    void Emit(music::HostEvent event) {
        IMusicPlaybackHost::EventHandler callback;
        { std::lock_guard lock(mutex); callback = handler; }
        if (callback) callback(music::EncodeHostEvent(event));
    }
    void FinishTrack() {
        std::lock_guard lock(mutex);
        audio.position = audio.duration;
        audio.playing = false;
    }
};

class FakeHost final : public IMusicPlaybackHost {
public:
    explicit FakeHost(std::shared_ptr<HostFixture> fixture) : fixture_(std::move(fixture)) {}

    bool Start(EventHandler handler) override {
        ++fixture_->starts;
        while (!fixture_->releaseStart.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        handler_ = std::move(handler);
        { std::lock_guard lock(fixture_->mutex); fixture_->handler = handler_; }
        running_ = !fixture_->failStart;
        return running_;
    }

    bool Send(std::string frame) override {
        return Send(std::move(frame), {});
    }

    bool Send(std::string frame, const std::function<bool()>& cancelled) override {
        if (!running_) return false;
        const auto command = music::DecodeHostCommand(frame);
        if (!command) return false;
        if (fixture_->onCommand) fixture_->onCommand(*command);
        music::HostEvent event;
        bool holdReply = false;
        {
            std::lock_guard lock(fixture_->mutex);
            fixture_->actions.push_back(command->action);
            fixture_->commands.push_back(*command);
            if (command->action == fixture_->failSendAction) return false;
            auto& audio = fixture_->audio;
            audio.session = command->session;
            audio.sequence = command->sequence;
            audio.error.clear();
            if (command->action == "load") {
                audio.trackId = command->source;
                audio.position = 0;
                audio.duration = 30;
                audio.playing = false;
                if (command->source == fixture_->failLoadSource) audio.error = "load-failed";
            } else if (command->action == "play") {
                audio.playing = !fixture_->failPlay;
                if (fixture_->failPlay) audio.error = "playback-failed";
            } else if (command->action == "pause") audio.playing = false;
            else if (command->action == "seek" || command->action == "prepare") audio.position = command->value;
            else if (command->action == "setVolume") audio.volume = command->value;
            else if (command->action == "setMuted") audio.muted = command->flag;
            else if (command->action == "close") { audio.playing = false; audio.trackId.clear(); }
            audio.analysis = {};
            audio.ready = fixture_->preparedReady;
            if (command->analysis && audio.playing && !audio.muted) {
                audio.analysis.energy = audio.volume * 0.5;
                audio.analysis.bins.fill(audio.analysis.energy);
            }
            audio.status = audio.playing ? "playing" : command->action == "pause" ? "paused" : "ready";
            audio.canToggle = !audio.trackId.empty();
            audio.canSeek = audio.canToggle;
            event = audio;
            if (event.trackId.size() > 128) event.trackId.clear();
            fixture_->events.push_back(event);
            holdReply = command->action == fixture_->heldAction;
        }
        if (holdReply) {
            fixture_->replyHeld = true;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(fixture_->holdTimeoutMs);
            while (!fixture_->releaseReply.load() && std::chrono::steady_clock::now() < deadline) {
                if (fixture_->cancelHeldReply && cancelled && cancelled()) {
                    ++fixture_->cancelledReplies;
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        const auto deliver = [&] {
            if (!handler_ || fixture_->omitReply) return;
            if (fixture_->injectStaleReplies) {
                auto stale = event;
                --stale.sequence;
                stale.error = "obsolete-failure";
                handler_(music::EncodeHostEvent(stale));
                stale.sequence = event.sequence + 1;
                handler_(music::EncodeHostEvent(stale));
                stale = event;
                stale.session = "foreign-host";
                handler_(music::EncodeHostEvent(stale));
            }
            handler_(music::EncodeHostEvent(event));
        };
        if (fixture_->replyOnThread) {
            std::thread responseThread(deliver);
            responseThread.join();
        } else deliver();
        ++fixture_->sends;
        return true;
    }

    void Stop() override {
        if (running_) ++fixture_->stops;
        running_ = false;
        handler_ = {};
        std::lock_guard lock(fixture_->mutex);
        fixture_->handler = {};
        fixture_->audio.playing = false;
        fixture_->audio.trackId.clear();
    }

    bool Running() const noexcept override { return running_; }

private:
    std::shared_ptr<HostFixture> fixture_;
    EventHandler handler_;
    bool running_ = false;
};

ui::MusicPageCommand Command(std::string action) {
    std::wstring wide(action.begin(), action.end());
    const auto parsed = ui::ParseMusicPageJson(
        L"{\"version\":1,\"scope\":\"music\",\"action\":\"" + wide + L"\"}");
    return parsed.value_or(ui::MusicPageCommand{});
}

ui::MusicPageCommand CommandWithIndex(std::string action, std::size_t index, const std::string& id) {
    const auto wire = nlohmann::json{{"version", 1}, {"scope", "music"}, {"action", action},
        {"index", index}, {"id", id}}.dump(-1, ' ', true);
    const auto parsed = ui::ParseMusicPageJson(std::wstring(wire.begin(), wire.end()));
    Check(parsed.has_value(), "indexed fixture includes a valid stable track id");
    return parsed.value_or(ui::MusicPageCommand{});
}

ui::MusicPageCommand CommandWithId(std::string action, std::string id) {
    const std::wstring wideAction(action.begin(), action.end());
    const std::wstring wideId(id.begin(), id.end());
    const auto parsed = ui::ParseMusicPageJson(
        L"{\"version\":1,\"scope\":\"music\",\"action\":\"" + wideAction +
        L"\",\"session\":1,\"id\":\"" + wideId + L"\"}");
    return parsed.value_or(ui::MusicPageCommand{});
}

ui::MusicPageCommand CommandWithSession(std::string action, std::uint64_t session) {
    const std::wstring wide(action.begin(), action.end());
    const auto parsed = ui::ParseMusicPageJson(
        L"{\"version\":1,\"scope\":\"music\",\"action\":\"" + wide +
        L"\",\"session\":" + std::to_wstring(session) + L"}");
    return parsed.value_or(ui::MusicPageCommand{});
}

bool HasText(const std::wstring& value, std::wstring_view text) {
    return value.find(text) != std::wstring::npos;
}

bool Eventually(const std::function<bool()>& predicate, int timeoutMs = 1000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

// 音符律动: analysis starts only once playback has loaded the requested track's
// source, publishes one music-score message, and replays come from the cache.
void TestScoreAnalysisWaitsForLoadAndCaches() {
    auto fixture = std::make_shared<HostFixture>();
    std::atomic<int> analyses{0};
    std::string analyzed;
    std::mutex analyzedMutex;
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    options.analyzeScore = [&](const std::string& source, const std::function<bool()>&) {
        ++analyses;
        { std::lock_guard lock(analyzedMutex); analyzed = source; }
        music::MelodyAnalysis result;
        result.pitch = {0, 600, 605, 0};
        result.tempo = 120;
        result.beats = {0.5, 1.0};
        return result;
    };
    MusicSession session(std::move(options));
    session.HandlePage(Command("open"));
    music::Track track;
    track.id = "track-1";
    track.source = "fixture.wav";
    session.SetQueue({track});
    const auto request = CommandWithId("loadScore", "track-1");
    Check(request.action == ui::MusicAction::LoadScore, "loadScore parses like loadLyrics");
    session.HandlePage(request);
    Check(analyses == 0, "nothing is analyzed before playback loads the source");
    session.HandlePage(Command("play"));
    // Playback keeps re-arming state notifications; the finished analysis must
    // still be the next message out instead of waiting behind them.
    Check(Eventually([&] { return analyses == 1; }), "analysis runs once the source is loaded");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    session.HandlePage(Command("pause"));
    const auto first = session.DrainNotification();
    Check(first && HasText(*first, L"\"type\":\"music-score\""), "a pending score is not starved by playback state ticks");
    session.HandlePage(Command("play"));
    std::wstring score;
    const auto drainScore = [&] {
        for (int i = 0; i < 4; ++i) {
            const auto message = session.DrainNotification();
            if (message && HasText(*message, L"\"type\":\"music-score\"")) { score = *message; return true; }
        }
        return false;
    };
    score = first ? *first : std::wstring{};
    Check(HasText(score, L"\"pitch\":[0,600,605,0]") && HasText(score, L"\"trackId\":\"track-1\"") &&
        HasText(score, L"\"beats\":[0.5,1.0]"), "score message carries the pitch frames and beats");
    {
        std::lock_guard lock(analyzedMutex);
        Check(analyses == 1 && analyzed == "fixture.wav", "the playback source is analyzed once");
    }
    score.clear();
    session.HandlePage(request);
    Check(Eventually(drainScore) && analyses == 1, "asking again replays the cached analysis");
    session.Shutdown();
}

void TestPageLifecycleAndLazyHost() {
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    options.accountSignedIn = [] { return true; };
    MusicSession session(std::move(options));

    session.HandlePage(Command("open"));
    Check(fixture->starts == 0, "opening music page does not start playback host");
    Check(HasText(session.State(), L"\"type\":\"music-state\""), "open publishes music state");
    Check(HasText(session.State(), L"\"pageVisible\":true"), "open marks page visible");

    music::Track track;
    track.id = "track-1";
    track.title = "测试歌曲";
    track.artist = "测试歌手";
    track.source = "fixture.wav";
    track.duration = 120.0;
    session.SetQueue({track});
    session.HandlePage(CommandWithSession("play", 999));
    Check(fixture->starts == 0, "stale widget session cannot start a new host");
    session.HandlePage(CommandWithIndex("playNow", 0, track.id));

    Check(Eventually([&] { return fixture->starts == 1; }), "first playback starts host lazily");
    Check(Eventually([&] {
        const auto actions = fixture->Actions();
        return actions.size() >= 3 && actions[0] == "hello" &&
            actions[1] == "load" && actions[2] == "play";
    }), "first playback sends hello, load, and play in order");
    Check(HasText(session.State(), L"\"duration\":30.0"),
        "host-provided duration reaches the local playback timeline");

    session.HandlePage(Command("close"));
    Check(session.HostRunning(), "closing page keeps active background playback");
    session.HandlePage(Command("open"));
    Check(session.HostRunning() && fixture->starts == 1 && fixture->stops == 0,
        "returning to the music page reuses the active playback host");
    Check(HasText(session.State(), L"\"pageVisible\":true"),
        "reopening music restores page visibility without starting another host");
    session.HandlePage(Command("close"));
    Check(session.HostRunning() && fixture->starts == 1 && fixture->stops == 0,
        "leaving the page again keeps the same playback session alive");
    Check(HasText(session.WidgetState(), L"\"type\":\"music-widget-state\""),
        "background playback publishes widget projection");
    session.Shutdown();
    session.Shutdown();
    Check(fixture->stops == 1, "shutdown is idempotent and stops host once");
}

void TestCloseCancelsPageRequestsAndPreservesAccountProjection() {
    auto fixture = std::make_shared<HostFixture>();
    int homeRequests = 0;
    int lyricRequests = 0;
    int cancellations = 0;
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    options.accountSignedIn = [] { return true; };
    options.content.loadHome = [&](std::uint64_t) { ++homeRequests; };
    options.content.loadLyrics = [&](std::uint64_t, std::string) { ++lyricRequests; };
    options.content.cancel = [&] { ++cancellations; };
    MusicSession session(std::move(options));

    session.HandlePage(Command("open"));
    session.HandlePage(Command("loadHome"));
    music::Track track;
    track.id = "track-1";
    session.SetQueue({track});
    session.HandlePage(CommandWithId("loadLyrics", "track-1"));
    Check(homeRequests == 1 && lyricRequests == 1, "page requests use injected content boundary");

    session.HandlePage(Command("close"));
    Check(cancellations == 1, "closing page cancels page data requests");
    Check(HasText(session.State(), L"\"account\":{\"signedIn\":true}"),
        "host errors and page close do not clear account projection");
    session.Shutdown();
}

void TestHostFailureDoesNotLeakIntoOtherFeatures() {
    auto fixture = std::make_shared<HostFixture>();
    fixture->failStart = true;
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    options.accountSignedIn = [] { return true; };
    MusicSession session(std::move(options));
    music::Track track;
    track.id = "track-1";
    track.source = "fixture.wav";
    track.duration = 10.0;
    session.SetQueue({track});
    session.HandlePage(CommandWithIndex("playNow", 0, track.id));
    Check(Eventually([&] { return HasText(session.State(), L"music-host-unavailable"); }),
        "host startup failure is exposed as bounded music error");
    Check(HasText(session.State(), L"\"account\":{\"signedIn\":true}"),
        "host startup failure preserves account projection");
    session.Shutdown();
}

void TestLocalLyricsAreOnDemandAndScopedToImportedCurrentTrack() {
    const auto root = std::filesystem::temp_directory_path() / "lume-session-lyrics-test";
    std::filesystem::create_directories(root);
    const auto audio = root / "first.wav";
    const auto second = root / "second.wav";
    { std::ofstream file(audio, std::ios::binary); file << "audio fixture"; }
    { std::ofstream file(second, std::ios::binary); file << "audio fixture"; }
    { std::ofstream file(root / "first.lrc", std::ios::binary); file << "[00:01]first lyric\n"; }
    { std::ofstream file(root / "second.lrc", std::ios::binary); file << "[00:01]second lyric\n"; }
    const auto imported = music::ImportLocalMusicFiles({audio.wstring(), second.wstring()});
    Check(imported.tracks.size() == 2, "fixture imports local tracks");
    if (imported.tracks.size() != 2) return;
    MusicSession session({});
    session.ImportLocalFiles({audio.wstring(), second.wstring()});
    session.DrainNotification();
    auto request = CommandWithId("loadLyrics", imported.tracks[0].id);
    request.session = 1;
    session.HandlePage(request);
    Check(!session.HasWork(), "closed page never starts a lyrics worker");
    session.HandlePage(Command("open"));
    Check(!session.HasWork(), "open alone does not read local lyrics");
    auto wrongId = request;
    wrongId.id = imported.tracks[1].id;
    session.HandlePage(wrongId);
    Check(!session.HasWork(), "non-current imported track cannot request lyrics");
    auto stale = request;
    stale.session = 99;
    session.HandlePage(stale);
    Check(!session.HasWork(), "stale session cannot start a lyrics read");
    session.HandlePage(request);
    std::wstring lyricMessage;
    Check(Eventually([&] {
        const auto message = session.DrainNotification();
        if (message && HasText(*message, L"\"type\":\"music-lyrics\"") && HasText(*message, L"first lyric"))
            lyricMessage = *message;
        return !lyricMessage.empty();
    }), "current imported sidecar produces an independent lyrics notification");
    Check(!HasText(session.State(), L"fullText"), "state ticks never contain lyrics arrays");
    { std::ofstream file(root / "first.lrc", std::ios::binary); file << "[00:01]changed lyric\n"; }
    for (int tick = 0; tick < 20; ++tick) session.HandlePage(request);
    Check(!session.DrainNotification(), "same page and track requests do not reread or retransmit lyrics");
    Check(Eventually([&] { return !session.HasWork(); }), "lyrics-only work does not keep an idle worker active");
    session.HandlePage(Command("close"));
    session.HandlePage(Command("open"));
    session.HandlePage(request);
    Check(Eventually([&] {
        const auto message = session.DrainNotification();
        return message && HasText(*message, L"changed lyric");
    }), "close releases lyrics and reopening reads the new sidecar");
    session.SetQueue(imported.tracks);
    auto afterReplace = request;
    afterReplace.session = 2;
    session.HandlePage(afterReplace);
    Check(Eventually([&] {
        const auto message = session.DrainNotification();
        return message && HasText(*message, L"\"type\":\"music-lyrics\"") && HasText(*message, L"\"lines\":[]");
    }), "generic SetQueue clears import authorization and old lyrics");
    session.Shutdown();
    Check(!session.DrainNotification(), "shutdown releases pending lyrics");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void TestInFlightLyricsCancelOnTrackChangeVisibilityAndShutdown() {
    const auto root = std::filesystem::temp_directory_path() / "lume-session-lyrics-flight-test";
    std::filesystem::create_directories(root);
    const auto audio = root / "first.wav", second = root / "second.wav";
    { std::ofstream file(audio); file << "audio fixture"; }
    { std::ofstream file(second); file << "audio fixture"; }
    const auto tracks = music::ImportLocalMusicFiles({audio.wstring(), second.wstring()}).tracks;
    Check(tracks.size() == 2, "in-flight fixture imports tracks");
    if (tracks.size() != 2) return;

    std::atomic<int> reads{0}, cancelledReads{0};
    MusicSessionOptions options;
    options.readLocalLyrics = [&](const music::Track&, const std::function<bool()>& cancelled) {
        ++reads;
        const bool stopped = Eventually(cancelled, 2000);
        if (stopped) ++cancelledReads;
        // Even a reader completing with data after cancellation must be ignored.
        return music::LocalLyricsResult{{{1, 2, "obsolete lyric", {}, {}}}, {}};
    };
    MusicSession session(std::move(options));
    session.ImportLocalFiles({audio.wstring(), second.wstring()});
    session.HandlePage(Command("open"));
    auto request = CommandWithId("loadLyrics", tracks[0].id);
    request.session = 1;
    session.HandlePage(request);
    Check(Eventually([&] { return reads == 1; }), "reader starts only after explicit page request");
    session.HandlePage(CommandWithIndex("playNow", 1, tracks[1].id));
    Check(Eventually([&] { return cancelledReads == 1; }), "changing track cancels in-flight read");
    Check(Eventually([&] {
        const auto message = session.DrainNotification();
        Check(!message || !HasText(*message, L"obsolete lyric"), "stale read cannot repopulate the replacement track");
        return message && HasText(*message, L"\"type\":\"music-lyrics\"") && HasText(*message, L"\"lines\":[]");
    }), "track replacement sends a clear message");

    request.id = tracks[1].id;
    session.HandlePage(request);
    Check(Eventually([&] { return reads == 2; }), "new track accepts a fresh read");
    ui::MusicPageCommand hidden;
    hidden.action = ui::MusicAction::Visibility;
    hidden.flag = false;
    session.HandlePage(hidden);
    Check(Eventually([&] { return cancelledReads == 2; }), "hidden page cancels in-flight read");
    session.HandlePage(request);
    Check(reads == 2, "hidden page never reads again");
    hidden.flag = true;
    session.HandlePage(hidden);
    session.HandlePage(request);
    Check(Eventually([&] { return reads == 3; }), "visible page can request again after cancellation");
    session.Shutdown();
    Check(cancelledReads == 3 && !session.HasWork() && !session.DrainNotification(),
        "shutdown cancels, joins reader and releases pending data");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

struct LyricsFixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / "lume-lyrics-race-test";
    music::Track track;
    LyricsFixture() {
        std::filesystem::create_directories(root);
        { std::ofstream file(root / "song.wav"); file << "audio fixture"; }
        track = music::ImportLocalMusicFiles({(root / "song.wav").wstring()}).tracks.at(0);
    }
    ~LyricsFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void Import(MusicSession& session) { session.ImportLocalFiles({(root / "song.wav").wstring()}); }
    ui::MusicPageCommand Request(std::uint64_t generation = 1) const {
        auto request = CommandWithId("loadLyrics", track.id);
        request.session = generation;
        return request;
    }
};

struct ControlledHostState {
    std::mutex mutex;
    IMusicPlaybackHost::EventHandler handler;
    std::string session;
    std::atomic<bool> started{false}, releaseStart{true}, running{false};
    bool failStart = false;
    void Tick() {
        IMusicPlaybackHost::EventHandler callback;
        music::HostEvent event;
        {
            std::lock_guard lock(mutex);
            callback = handler;
            event.session = session;
        }
        event.sequence = 1;
        event.status = "playing";
        event.playing = true;
        event.duration = 30;
        if (callback && !event.session.empty()) callback(music::EncodeHostEvent(event));
    }
    bool Ready() {
        std::lock_guard lock(mutex);
        return !session.empty();
    }
};

class ControlledHost final : public IMusicPlaybackHost {
public:
    explicit ControlledHost(std::shared_ptr<ControlledHostState> state) : state_(std::move(state)) {}
    bool Start(EventHandler handler) override {
        { std::lock_guard lock(state_->mutex); state_->handler = std::move(handler); }
        state_->started = true;
        Eventually([&] { return state_->releaseStart.load(); }, 2000);
        state_->running = !state_->failStart;
        return state_->running;
    }
    bool Send(std::string frame) override {
        const auto command = music::DecodeHostCommand(frame);
        if (!command) return false;
        EventHandler callback;
        {
            std::lock_guard lock(state_->mutex);
            state_->session = command->session;
            callback = state_->handler;
        }
        music::HostEvent event;
        event.session = command->session;
        event.sequence = command->sequence;
        event.status = command->action == "play" ? "playing" : "ready";
        event.playing = command->action == "play";
        event.duration = 30;
        if (callback) callback(music::EncodeHostEvent(event));
        return true;
    }
    void Stop() override {
        std::lock_guard lock(state_->mutex);
        state_->handler = {};
        state_->running = false;
    }
    bool Running() const noexcept override { return state_->running; }
private:
    std::shared_ptr<ControlledHostState> state_;
};

void TestPendingLyricsWakeAndFairDrainUnderPlaybackTicks() {
    LyricsFixture files;
    auto host = std::make_shared<HostFixture>();
    std::atomic<int> wakes{0};
    MusicSessionOptions options;
    options.createHost = [host] { return std::make_unique<FakeHost>(host); };
    options.wake = [&] { ++wakes; };
    options.readLocalLyrics = [](const auto&, const auto&) {
        return music::LocalLyricsResult{{{1, 2, "fair delivery", {}, {}}}, {}};
    };
    MusicSession session(std::move(options));
    files.Import(session);
    session.HandlePage(Command("open"));
    session.DrainNotification();
    const auto beforeRead = wakes.load();
    session.HandlePage(files.Request());
    Check(Eventually([&] { return wakes > beforeRead; }), "reader wakes message loop when lyrics become pending");
    // Publish lyrics before starting playback so an unrelated host wake cannot
    // be mistaken for completion now that the two owners run independently.
    auto beforeTick = wakes.load();
    session.HandlePage(CommandWithIndex("playNow", 0, files.track.id));
    Check(Eventually([&] { return wakes >= beforeTick + 2 && host->Audio().playing; }),
        "confirmed load and play produce state ticks beside pending lyrics");
    const auto beforeState = wakes.load();
    const auto state = session.DrainNotification();
    Check(state && HasText(*state, L"\"type\":\"music-state\""), "identity state is delivered before lyrics");
    Check(wakes > beforeState, "one-message drain rearms wake for pending lyrics");
    beforeTick = wakes.load();
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return wakes > beforeTick; }), "another fresh response arrives before lyric drain");
    const auto beforeLyrics = wakes.load();
    const auto lyrics = session.DrainNotification();
    Check(lyrics && HasText(*lyrics, L"fair delivery"), "continuous playback ticks cannot starve pending lyrics");
    Check(wakes > beforeLyrics, "lyrics drain rearms wake for the remaining playback state");
    session.DrainNotification();
    Check(!session.DrainNotification(), "drained lyrics are not transmitted twice");
    session.Shutdown();
}

void TestQueuedCancellationAndWorkerReuse() {
    LyricsFixture files;
    auto host = std::make_shared<ControlledHostState>();
    host->releaseStart = false;
    host->failStart = true;
    std::atomic<int> reads{0}, cancellations{0};
    std::atomic<bool> releaseRead{false};
    MusicSessionOptions options;
    options.createHost = [host] { return std::make_unique<ControlledHost>(host); };
    options.readLocalLyrics = [&](const auto&, const auto& cancelled) {
        if (++reads == 1) {
            Eventually([&] { return releaseRead.load(); }, 5000);
            if (cancelled()) ++cancellations;
            return music::LocalLyricsResult{{{1, 2, "cancelled delivery", {}, {}}}, {}};
        }
        return music::LocalLyricsResult{{{1, 2, "fresh delivery", {}, {}}}, {}};
    };
    MusicSession session(std::move(options));
    files.Import(session);
    session.HandlePage(Command("open"));
    session.HandlePage(CommandWithIndex("playNow", 0, files.track.id));
    Check(Eventually([&] { return host->started.load(); }), "host start holds playback worker");
    session.HandlePage(files.Request());
    Check(Eventually([&] { return reads == 1; }), "lyrics worker runs independently of blocked host startup");
    for (int cycle = 0; cycle < 20; ++cycle) {
        session.HandlePage(Command("close"));
        session.HandlePage(Command("open"));
        session.HandlePage(files.Request());
    }
    session.HandlePage(Command("close"));
    Check(reads == 1, "replacement lyric requests cannot start concurrent readers");
    releaseRead = true;
    host->releaseStart = true;
    Check(Eventually([&] { session.DrainNotification(); return !session.HasWork(); }),
        "both workers retire after startup failure and lyric cancellation");
    Check(reads == 1 && cancellations == 1, "close cancels active read and removes the single pending request");
    for (int cycle = 0; cycle < 12; ++cycle) {
        session.HandlePage(Command("open"));
        session.HandlePage(files.Request());
        Check(Eventually([&] {
            const auto message = session.DrainNotification();
            return message && HasText(*message, L"fresh delivery");
        }), "finished worker can be joined and reused for a fresh page generation");
        session.HandlePage(Command("close"));
        Check(Eventually([&] { return !session.HasWork(); }), "page close leaves no active lyrics worker");
    }
    Check(reads == 13, "each reopened generation reads exactly once");
    session.Shutdown();
}

void TestSlowLyricsDoNotBlockPlaybackOrEof() {
    LyricsFixture files;
    auto fixture = std::make_shared<HostFixture>();
    std::atomic<bool> reading{false}, finished{false}, cancelledRead{false};
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    options.readLocalLyrics = [&](const auto&, const auto& cancelled) {
        reading = true;
        cancelledRead = Eventually(cancelled, 5000);
        finished = true;
        return music::LocalLyricsResult{{{1, 2, "late blocked read", {}, {}}}, {}};
    };
    MusicSession session(std::move(options));
    files.Import(session);
    session.HandlePage(Command("open"));
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->Audio().playing; }), "slow-lyrics fixture starts audio");
    session.HandlePage(files.Request());
    Check(Eventually([&] { return reading.load(); }), "slow lyric reader is in flight");
    session.HandlePage(Command("pause"));
    Check(Eventually([&] { return !fixture->Audio().playing; }), "pause reaches host before lyric read returns");
    auto seek = Command("snapshot");
    seek.action = ui::MusicAction::Seek;
    seek.position = 12;
    session.HandlePage(seek);
    Check(Eventually([&] { const auto audio = fixture->Audio(); return !audio.playing && audio.position == 12; }),
        "paused seek reaches host while lyrics remain blocked");
    session.HandlePage(Command("play"));
    Check(Eventually([&] { const auto audio = fixture->Audio(); return audio.playing && audio.position == 12; }),
        "resume reaches host while lyrics remain blocked");
    ui::MusicPageCommand repeat;
    repeat.action = ui::MusicAction::SetRepeat;
    repeat.value = "one";
    session.HandlePage(repeat);
    fixture->FinishTrack();
    Check(Eventually([&] { const auto audio = fixture->Audio(); return audio.playing && audio.position == 0; }),
        "repeat-one EOF restarts actual audio without waiting for the same-track lyric read");
    Check(!finished.load(), "pause, seek, resume and same-track EOF complete during the blocked read");
    repeat.value = "none";
    session.HandlePage(repeat);
    fixture->FinishTrack();
    Check(Eventually([&] { return !session.HostRunning(); }), "finite EOF stops actual audio while lyrics are in flight");
    session.Shutdown();
    Check(finished && cancelledRead && !session.HasWork(), "shutdown cancels and joins the lyric owner");
    Check(!session.DrainNotification(), "cancelled EOF lyrics cannot repopulate the page");
}

void TestLyricsSessionIsRequiredAndStaleCleanupStillReleases() {
    LyricsFixture files;
    std::atomic<int> reads{0}, cancellations{0};
    MusicSessionOptions options;
    options.readLocalLyrics = [&](const auto&, const auto& cancelled) {
        ++reads;
        if (Eventually(cancelled, 2000)) ++cancellations;
        return music::LocalLyricsResult{{{1, 2, "late delivery", {}, {}}}, {}};
    };
    MusicSession session(std::move(options));
    files.Import(session);
    session.DrainNotification();
    session.HandlePage(Command("open"));
    session.HandlePage(files.Request(0));
    Check(!session.HasWork(), "unscoped lyrics request cannot use the current import authorization");
    session.HandlePage(Command("close"));
    session.Shutdown();

    // A close/hide can carry the previous queue session if import and page
    // cleanup cross in the message loop. Cleanup must still revoke the read.
    for (const auto action : {ui::MusicAction::Close, ui::MusicAction::Visibility}) {
        std::atomic<int> entered{0}, stopped{0};
        MusicSessionOptions scoped;
        scoped.readLocalLyrics = [&](const auto&, const auto& cancelled) {
            ++entered;
            if (Eventually(cancelled, 2000)) ++stopped;
            return music::LocalLyricsResult{{{1, 2, "late delivery", {}, {}}}, {}};
        };
        MusicSession current(std::move(scoped));
        files.Import(current);
        files.Import(current);
        current.HandlePage(Command("open"));
        current.DrainNotification();
        current.DrainNotification();
        current.HandlePage(files.Request(1));
        Check(!current.HasWork(), "same track id in an older session cannot authorize a read");
        current.HandlePage(files.Request(2));
        Check(Eventually([&] { return entered == 1; }), "current session can request lyrics");
        ui::MusicPageCommand cleanup;
        cleanup.action = action;
        cleanup.session = 1;
        cleanup.flag = false;
        current.HandlePage(cleanup);
        Check(Eventually([&] { return stopped == 1; }), "stale queue session cannot prevent page cleanup cancellation");
        Check(!current.DrainNotification(), "cleanup drops all pending lyrics");
        current.Shutdown();
    }
}

void TestReplacementAndPendingPageCleanupDropOldLyrics() {
    LyricsFixture files;
    std::atomic<int> reads{0}, cancellations{0}, wakes{0};
    std::atomic<bool> holdFirst{true};
    MusicSessionOptions options;
    options.wake = [&] { ++wakes; };
    options.readLocalLyrics = [&](const auto&, const auto& cancelled) {
        const auto index = ++reads;
        if (index == 1) {
            Eventually([&] { return !holdFirst.load() || cancelled(); }, 2000);
            if (cancelled()) ++cancellations;
        }
        return music::LocalLyricsResult{{{1, 2, index == 1 ? "old queue" : "new queue", {}, {}}}, {}};
    };
    MusicSession session(std::move(options));
    files.Import(session);
    session.HandlePage(Command("open"));
    session.DrainNotification();
    session.HandlePage(files.Request());
    Check(Eventually([&] { return reads == 1; }), "old queue reader starts");
    files.Import(session);
    session.HandlePage(files.Request(2));
    Check(Eventually([&] { return reads == 2 && cancellations == 1; }),
        "reimporting the same source cancels old generation and reads for the new session");
    Check(Eventually([&] {
        const auto message = session.DrainNotification();
        Check(!message || !HasText(*message, L"old queue"), "same-id old-session completion cannot escape");
        return message && HasText(*message, L"new queue") && HasText(*message, L"\"session\":2");
    }), "new queue receives only its own lyrics");
    for (const auto action : {ui::MusicAction::Close, ui::MusicAction::Visibility}) {
        session.HandlePage(Command("close"));
        session.HandlePage(Command("open"));
        const auto before = wakes.load();
        session.HandlePage(files.Request(2));
        Check(Eventually([&] { return wakes > before; }), "lyrics completion is pending before page cleanup");
        ui::MusicPageCommand cleanup;
        cleanup.action = action;
        cleanup.flag = false;
        session.HandlePage(cleanup);
        Check(!session.DrainNotification(), "close or hide releases already completed pending lyrics");
        Check(Eventually([&] { return !session.HasWork(); }), "completed pending lyrics do not keep a hidden page active");
    }
    holdFirst = false;
    session.Shutdown();
}

void TestOnlyLatestPendingLyricsSurvives() {
    LyricsFixture files;
    std::atomic<int> reads{0};
    std::atomic<bool> releaseRead{false}, firstCancelled{false};
    MusicSessionOptions options;
    options.readLocalLyrics = [&](const auto&, const auto& cancelled) {
        if (++reads == 1) {
            Eventually([&] { return releaseRead.load(); }, 5000);
            firstCancelled = cancelled();
            return music::LocalLyricsResult{{{1, 2, "obsolete pending generation", {}, {}}}, {}};
        }
        return music::LocalLyricsResult{{{1, 2, "latest pending generation", {}, {}}}, {}};
    };
    MusicSession session(std::move(options));
    files.Import(session);
    session.HandlePage(Command("open"));
    session.HandlePage(files.Request());
    Check(Eventually([&] { return reads == 1; }), "first reader remains active while requests are replaced");
    for (std::uint64_t generation = 2; generation <= 21; ++generation) {
        files.Import(session);
        session.HandlePage(files.Request(generation));
    }
    Check(reads == 1, "only one lyric read may be in flight despite repeated replacement");
    releaseRead = true;
    Check(Eventually([&] {
        const auto message = session.DrainNotification();
        Check(!message || !HasText(*message, L"obsolete pending generation"), "cancelled read never publishes stale data");
        return message && HasText(*message, L"latest pending generation") && HasText(*message, L"\"session\":21");
    }), "only the latest pending request publishes into its matching session");
    Check(firstCancelled && reads == 2, "intermediate pending generations are replaced without executing");
    Check(Eventually([&] { session.DrainNotification(); return !session.HasWork(); }), "lyrics owner retires once pending work drains");
    session.Shutdown();
}

void TestLyricsDestructionJoinsActiveRead() {
    LyricsFixture files;
    std::atomic<bool> entered{false}, sawCancellation{false}, releaseRead{false}, destroyed{false};
    MusicSessionOptions options;
    options.readLocalLyrics = [&](const auto&, const auto& cancelled) {
        entered = true;
        sawCancellation = Eventually(cancelled, 5000);
        Eventually([&] { return releaseRead.load(); }, 5000);
        return music::LocalLyricsResult{};
    };
    auto session = std::make_unique<MusicSession>(std::move(options));
    files.Import(*session);
    session->HandlePage(Command("open"));
    session->HandlePage(files.Request());
    Check(Eventually([&] { return entered.load(); }), "destruction fixture has an active lyric read");
    std::thread destroy([owned = std::move(session), &destroyed]() mutable {
        owned.reset();
        destroyed = true;
    });
    Check(Eventually([&] { return sawCancellation.load(); }), "destruction revokes the active lyric generation");
    Check(!destroyed.load(), "destruction waits for the cancelled reader to finish");
    releaseRead = true;
    destroy.join();
    Check(destroyed.load(), "destruction joins the lyric worker before releasing the session");
}

void TestQueueCommandsAndActualStop() {
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    MusicSession session(std::move(options));
    music::Track first;
    first.id = "first";
    first.source = "first.wav";
    auto second = first;
    second.id = "second";
    second.source = "second.wav";
    session.SetQueue({first, second});
    session.HandlePage(CommandWithId("removeFromQueue", "second"));
    Check(!HasText(session.State(), L"\"id\":\"second\""), "remove uses the stable queue id");
    session.HandlePage(CommandWithId("enqueue", "second"));
    Check(HasText(session.State(), L"\"id\":\"second\""), "removed native-authorized track can be re-enqueued");
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->sends >= 3; }), "queue fixture starts audio");
    session.HandlePage(CommandWithIndex("playNow", 1, "second"));
    Check(Eventually([&] { return fixture->Audio().trackId == "second.wav" && fixture->Audio().playing; }),
        "re-enqueued native id still resolves to the authorized source");
    session.HandlePage(Command("clearQueue"));
    Check(Eventually([&] { return fixture->stops == 1; }), "clear queue stops the actual playback host");
    Check(!session.HostRunning() && HasText(session.State(), L"\"playing\":false"),
        "clear queue cannot leave background audio running");
    session.Shutdown();
}

music::Track QueueTrack(const std::string& id) {
    music::Track track;
    track.id = id;
    track.source = id + ".wav";
    track.duration = 30;
    return track;
}

nlohmann::json SessionState(const MusicSession& session) {
    return nlohmann::json::parse(session.State());
}

void QueueId(MusicSession& session, const std::string& action, const std::string& id) {
    auto command = CommandWithId(action, id);
    command.session = SessionState(session).at("session").get<std::uint64_t>();
    session.HandlePage(command);
}

void TestPlayNowRequiresMatchingIndexAndId() {
    const auto parse = [](const nlohmann::json& wire) {
        const auto text = wire.dump(-1, ' ', true);
        return ui::ParseMusicPageJson(std::wstring(text.begin(), text.end()));
    };
    nlohmann::json wire{{"version", 1}, {"scope", "music"}, {"action", "playNow"},
        {"session", 1}, {"index", 1}, {"id", "b"}};
    const auto valid = parse(wire);
    Check(valid && valid->action == ui::MusicAction::PlayNow && valid->index == 1 && valid->id == "b",
        "playNow parses both index and stable id");
    auto invalid = wire;
    invalid.erase("id");
    Check(!parse(invalid), "index-only playNow is rejected");
    invalid = wire;
    invalid.erase("index");
    Check(!parse(invalid), "id-only playNow is rejected");
    for (const auto& index : {nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json("1"), nlohmann::json(100001)}) {
        invalid = wire;
        invalid["index"] = index;
        Check(!parse(invalid), "playNow keeps the bounded unsigned index contract");
    }
    for (const auto& id : {nlohmann::json(""), nlohmann::json(7), nlohmann::json(nullptr),
            nlohmann::json(std::string(129, 'x')), nlohmann::json(std::string("b\0c", 3))}) {
        invalid = wire;
        invalid["id"] = id;
        Check(!parse(invalid), "playNow rejects empty, non-string, oversized or NUL-containing ids");
    }
    wire["id"] = std::string(128, 'x');
    Check(parse(wire).has_value(), "playNow accepts the 128-byte id boundary");
    invalid = wire;
    invalid["source"] = "evil.wav";
    Check(!parse(invalid), "playNow cannot attach a page-supplied source");

    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    MusicSession session(std::move(options));
    session.SetQueue({QueueTrack("a"), QueueTrack("b"), QueueTrack("c")});
    auto oldIndex = CommandWithIndex("playNow", 1, "b");
    oldIndex.session = 1;
    QueueId(session, "removeFromQueue", "a");
    const auto afterRemove = SessionState(session);
    session.HandlePage(oldIndex);
    Check(SessionState(session) == afterRemove && fixture->starts == 0,
        "remove A then old index 1/id B cannot select or play C");
    QueueId(session, "enqueue", "a");
    oldIndex.id = "c";
    QueueId(session, "enqueue", "c");
    const auto afterReorder = SessionState(session);
    session.HandlePage(oldIndex);
    Check(SessionState(session) == afterReorder && fixture->starts == 0,
        "moving an entry rejects its old index instead of selecting the replacement row");
    for (const auto& id : {std::string{}, std::string(129, 'x'), std::string("b\0c", 3)}) {
        auto direct = oldIndex;
        direct.index = 0;
        direct.id = id;
        session.HandlePage(direct);
        Check(SessionState(session) == afterReorder, "direct native playNow also requires a valid matching id");
    }
    oldIndex.index = 99;
    oldIndex.id = "b";
    session.HandlePage(oldIndex);
    Check(SessionState(session) == afterReorder, "out-of-range index cannot select a matching id elsewhere");
    session.HandlePage(CommandWithIndex("playNow", 0, "b"));
    Check(Eventually([&] { const auto audio = fixture->Audio(); return audio.playing && audio.trackId == "b.wav"; }),
        "a fresh matching index/id still loads the intended native source");
    session.Shutdown();
}

void TestQueueIdentityAuthorizationAndPause() {
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    MusicSession session(std::move(options));
    session.SetQueue({QueueTrack("a"), QueueTrack("b"), QueueTrack("c")});
    session.HandlePage(CommandWithIndex("playNow", 1, "b"));
    Check(Eventually([&] { return fixture->Audio().playing && fixture->Audio().trackId == "b.wav"; }),
        "playNow loads the selected native source");
    auto seek = Command("snapshot");
    seek.action = ui::MusicAction::Seek;
    seek.position = 12;
    session.HandlePage(seek);
    session.HandlePage(Command("pause"));
    Check(Eventually([&] { const auto audio = fixture->Audio(); return !audio.playing && audio.position == 12; }),
        "coalesced seek then pause preserves both desired position and pause");
    const auto commands = fixture->Commands().size();
    QueueId(session, "removeFromQueue", "a");
    auto state = SessionState(session);
    Check(state["current"]["id"] == "b" && state["currentIndex"] == 0 &&
        state["timeline"]["position"] == 12 && state["status"] == "paused",
        "remove before current preserves current identity and paused timeline");
    Check(fixture->Commands().size() == commands, "non-current removal sends no playback commands");
    QueueId(session, "removeFromQueue", "b");
    Check(Eventually([&] { const auto audio = fixture->Audio(); return audio.trackId == "c.wav" && !audio.playing; }),
        "removing paused current loads successor without playing");
    Check(SessionState(session)["status"] == "paused", "host load acknowledgement does not erase pause intent");
    QueueId(session, "enqueue", "a");
    QueueId(session, "enqueue", "b");
    session.HandlePage(CommandWithIndex("playNow", 2, "b"));
    Check(Eventually([&] { return fixture->Audio().trackId == "b.wav" && fixture->Audio().playing; }),
        "re-enqueued id retains its native source");
    QueueId(session, "removeFromQueue", "b");
    Check(Eventually([&] { return fixture->Audio().trackId == "a.wav" && fixture->Audio().playing; }),
        "removing playing tail switches host to predecessor");
    const auto beforeInvalid = SessionState(session)["queue"];
    QueueId(session, "enqueue", "https://untrusted.invalid/audio.wav");
    QueueId(session, "removeFromQueue", "missing");
    auto unscoped = CommandWithId("removeFromQueue", "a");
    unscoped.session = 0;
    session.HandlePage(unscoped);
    Check(SessionState(session)["queue"] == beforeInvalid, "invalid source/id and unscoped mutation are rejected");
    Check(!ui::ParseMusicPageJson(LR"({"version":1,"scope":"music","action":"enqueue","session":1,"id":"a","source":"evil.wav"})"),
        "page enqueue cannot inject a source alongside its id");
    const auto oldSession = SessionState(session)["session"].get<std::uint64_t>();
    session.SetQueue({QueueTrack("fresh")});
    Check(Eventually([&] { return fixture->Audio().trackId == "fresh.wav" && !fixture->Audio().playing; }),
        "native replacement stops old audio and loads the new ready source");
    auto stale = CommandWithId("enqueue", "a");
    stale.session = oldSession;
    session.HandlePage(stale);
    stale = CommandWithSession("clearQueue", oldSession);
    session.HandlePage(stale);
    QueueId(session, "enqueue", "a");
    Check(SessionState(session)["queue"].size() == 1 && SessionState(session)["current"]["id"] == "fresh",
        "stale commands and revoked native ids cannot alter a replacement queue");
    session.HandlePage(Command("clearQueue"));
    Check(Eventually([&] { return !session.HostRunning(); }), "clear stops actual host");
    QueueId(session, "enqueue", "fresh");
    Check(SessionState(session)["current"]["id"] == "fresh" && !session.HostRunning(),
        "clear retains bounded native authorization and re-enqueue stays lazy");
    session.Shutdown();
}

void TestRapidOperationsAndLateEvents() {
    auto fixture = std::make_shared<HostFixture>();
    fixture->releaseStart = false;
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    options.wake = [fixture] { ++fixture->wakes; };
    MusicSession session(std::move(options));
    session.SetQueue({QueueTrack("a"), QueueTrack("b"), QueueTrack("c")});
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->starts == 1; }), "rapid-operation fixture holds host startup");
    for (int i = 0; i < 200; ++i) {
        session.HandlePage(CommandWithIndex("playNow", static_cast<std::size_t>(i % 3),
            std::string(1, static_cast<char>('a' + i % 3))));
        session.HandlePage(Command("pause"));
    }
    session.HandlePage(Command("clearQueue"));
    QueueId(session, "enqueue", "b");
    session.HandlePage(Command("play"));
    session.HandlePage(Command("pause"));
    const auto beforeAck = fixture->wakes.load();
    fixture->releaseStart = true;
    Check(Eventually([&] { return fixture->wakes >= beforeAck + 2 && fixture->Audio().trackId == "b.wav" &&
        fixture->Audio().status == "paused"; }),
        "rapid operations converge to the final source and pause intent");
    const auto actions = fixture->Actions();
    Check(actions.size() <= 4 && std::find(actions.begin(), actions.end(), "play") == actions.end(),
        "superseded operations stay bounded and cannot briefly play a removed source");
    auto stale = fixture->Audio();
    const auto before = SessionState(session);
    stale.playing = true;
    stale.position = 29;
    stale.duration = 999;
    stale.status = "playing";
    fixture->Emit(stale);
    stale.sequence += 100;
    fixture->Emit(stale);
    stale.session = "foreign-session";
    fixture->Emit(stale);
    Check(SessionState(session) == before, "duplicate, unsolicited and foreign host replies cannot mutate state");
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->Audio().playing; }), "latest paused source can resume");
    session.HandlePage(Command("clearQueue"));
    Check(Eventually([&] { return !session.HostRunning(); }), "clear after rapid operations still stops actual host");
    session.Shutdown();
}

void TestHostEofRepeatShuffleAndFailures() {
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    MusicSession session(std::move(options));
    session.SetQueue({QueueTrack("a"), QueueTrack("b"), QueueTrack("c")});
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->Audio().playing; }), "EOF fixture starts");
    fixture->FinishTrack();
    Check(Eventually([&] { return fixture->Audio().trackId == "b.wav" && fixture->Audio().playing; }),
        "polled EOF advances and loads the real next source");
    ui::MusicPageCommand repeat;
    repeat.action = ui::MusicAction::SetRepeat;
    repeat.value = "one";
    session.HandlePage(repeat);
    fixture->FinishTrack();
    Check(Eventually([&] { return fixture->Audio().trackId == "b.wav" && fixture->Audio().playing && fixture->Audio().position == 0; }),
        "repeat-one EOF restarts the current host source");
    session.HandlePage(Command("next"));
    Check(Eventually([&] { return fixture->Audio().trackId == "c.wav" && fixture->Audio().playing; }),
        "manual next escapes repeat-one without sending unsupported next to host");
    repeat.value = "all";
    session.HandlePage(repeat);
    fixture->FinishTrack();
    Check(Eventually([&] { return fixture->Audio().trackId == "a.wav" && fixture->Audio().playing; }),
        "repeat-all EOF wraps the actual host source");
    ui::MusicPageCommand shuffle;
    shuffle.action = ui::MusicAction::SetShuffle;
    shuffle.flag = true;
    session.HandlePage(shuffle);
    repeat.value = "none";
    session.HandlePage(repeat);
    std::set<std::string> seen{"a.wav"};
    for (int i = 0; i < 2; ++i) {
        const auto prior = fixture->Audio().trackId;
        fixture->FinishTrack();
        Check(Eventually([&] { return fixture->Audio().trackId != prior && fixture->Audio().playing; }),
            "shuffle EOF switches actual host source");
        seen.insert(fixture->Audio().trackId);
    }
    Check(seen.size() == 3, "shuffle visits each actual source exactly once");
    fixture->FinishTrack();
    Check(Eventually([&] { return !session.HostRunning(); }), "finite shuffle EOF closes host at exhaustion");
    Check(SessionState(session)["current"].is_null() && SessionState(session)["playing"] == false,
        "finite EOF cannot leave a fake playing state");
    for (const auto& command : fixture->Commands())
        Check(command.action != "next" && command.action != "previous", "queue navigation stays in shell");
    session.Shutdown();

    for (int failure = 0; failure < 3; ++failure) {
        auto broken = std::make_shared<HostFixture>();
        broken->failLoadSource = failure == 0 ? "broken.wav" : "";
        broken->failPlay = failure == 1;
        broken->failSendAction = failure == 2 ? "play" : "";
        MusicSessionOptions failing;
        failing.createHost = [broken] { return std::make_unique<FakeHost>(broken); };
        MusicSession current(std::move(failing));
        current.SetQueue({QueueTrack("broken"), QueueTrack("good")});
        current.HandlePage(Command("play"));
        Check(Eventually([&] { return SessionState(current)["status"] == "error" && !current.HostRunning(); }),
            "load, play and transport failures roll back playing intent and release host");
        Check(SessionState(current)["playing"] == false && !broken->Audio().playing,
            "failure stops actual playback and its projection");
        { std::lock_guard lock(broken->mutex); broken->failLoadSource.clear(); broken->failPlay = false; broken->failSendAction.clear(); }
        current.HandlePage(CommandWithIndex("playNow", 1, "good"));
        Check(Eventually([&] { return broken->Audio().trackId == "good.wav" && broken->Audio().playing; }),
            "new host session can recover on a playable track");
        Check(SessionState(current)["error"] == "" && broken->starts == 2, "recovery clears the music error");
        current.Shutdown();
    }
}

void TestDelayedAcknowledgementsAndCloseRace() {
    // Send blocks until its response callback finishes, as the production
    // pipe client does. Delivery itself runs on another thread.
    auto fixture = std::make_shared<HostFixture>();
    fixture->heldAction = "load";
    fixture->releaseReply = false;
    fixture->replyOnThread = true;
    fixture->injectStaleReplies = true;
    fixture->failLoadSource = "broken.wav";
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    MusicSession session(std::move(options));
    session.SetQueue({QueueTrack("broken"), QueueTrack("good")});
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->replyHeld.load(); }), "load ACK is delayed after hello");
    const auto waiting = fixture->Actions();
    Check(waiting == std::vector<std::string>{"hello", "load"},
        "no play can overtake an unconfirmed load");
    fixture->releaseReply = true;
    Check(Eventually([&] { return !session.HostRunning() && SessionState(session)["error"] == "load-failed"; }),
        "delayed load failure survives old/future/foreign replies and stops host");
    const auto failed = fixture->Actions();
    Check(std::find(failed.begin(), failed.end(), "play") == failed.end(),
        "load failure never issues play against a previous source");

    fixture->replyHeld = false;
    fixture->releaseReply = false;
    session.HandlePage(CommandWithIndex("playNow", 0, "broken"));
    Check(Eventually([&] { return fixture->replyHeld.load(); }), "second delayed load starts");
    session.HandlePage(CommandWithIndex("playNow", 1, "good"));
    fixture->releaseReply = true;
    Check(Eventually([&] { return fixture->Audio().trackId == "good.wav" && fixture->Audio().playing &&
        SessionState(session)["error"] == ""; }), "obsolete failed load cannot poison the newer playback target");
    {
        std::lock_guard lock(fixture->mutex);
        fixture->heldAction = "close";
    }
    fixture->replyHeld = false;
    fixture->releaseReply = false;
    const auto hostSession = fixture->Audio().session;
    session.HandlePage(Command("clearQueue"));
    Check(Eventually([&] { return fixture->replyHeld.load(); }), "clear is held inside real close operation");
    QueueId(session, "enqueue", "good");
    session.HandlePage(Command("play"));
    fixture->releaseReply = true;
    Check(Eventually([&] { return fixture->Audio().playing && fixture->Audio().trackId == "good.wav" &&
        fixture->Audio().session != hostSession; }), "play queued during close starts only on a fresh host session");
    Check(SessionState(session)["playing"] == true && SessionState(session)["error"] == "",
        "obsolete close ACK cannot stop or error the new projection");
    session.Shutdown();

    auto unconfirmed = std::make_shared<HostFixture>();
    unconfirmed->omitReply = true;
    MusicSessionOptions invalid;
    invalid.createHost = [unconfirmed] { return std::make_unique<FakeHost>(unconfirmed); };
    MusicSession rejected(std::move(invalid));
    rejected.SetQueue({QueueTrack("a")});
    rejected.HandlePage(Command("play"));
    Check(Eventually([&] { return !rejected.HostRunning() &&
        SessionState(rejected)["error"] == "music-host-unconfirmed"; }),
        "Send success without synchronous ACK fails closed instead of assuming load/play success");
    Check(unconfirmed->Actions() == std::vector<std::string>{"hello"},
        "contract-violating asynchronous adapter cannot advance to load/play");
    rejected.Shutdown();
}

class ObservedNativeHost final : public IMusicPlaybackHost {
public:
    ObservedNativeHost(std::wstring executable, std::shared_ptr<HostFixture> fixture)
        : client_({std::move(executable), 1000}), fixture_(std::move(fixture)) {}
    bool Start(EventHandler handler) override {
        ++fixture_->starts;
        return client_.Start([this, handler = std::move(handler)](std::string frame) {
            const auto event = music::DecodeHostEvent(frame);
            if (event) {
                std::lock_guard lock(fixture_->mutex);
                fixture_->audio = *event;
                fixture_->events.push_back(*event);
            }
            handler(std::move(frame));
        });
    }
    bool Send(std::string frame) override {
        if (const auto command = music::DecodeHostCommand(frame)) {
            std::lock_guard lock(fixture_->mutex);
            fixture_->commands.push_back(*command);
        }
        const bool result = client_.Send(std::move(frame));
        ++fixture_->sends;
        return result;
    }
    void Stop() override { client_.Stop(); ++fixture_->stops; }
    bool Running() const noexcept override { return client_.Running(); }
private:
    music::MusicHostClient client_;
    std::shared_ptr<HostFixture> fixture_;
};

void WriteSilence(const std::filesystem::path& path, std::uint32_t milliseconds) {
    std::ofstream file(path, std::ios::binary);
    const auto word = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) file.put(static_cast<char>((value >> (i * 8)) & 255));
    };
    const auto samples = 8000u * milliseconds / 1000u;
    file.write("RIFF", 4); word(36u + samples, 4); file.write("WAVEfmt ", 8);
    word(16, 4); word(1, 2); word(1, 2); word(8000, 4); word(8000, 4);
    word(1, 2); word(8, 2); file.write("data", 4); word(samples, 4);
    const std::string silence(samples, static_cast<char>(128));
    file.write(silence.data(), static_cast<std::streamsize>(silence.size()));
}

void TestRealHostQueueAndEof() {
    wchar_t module[32768]{};
    const auto length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    const auto executable = std::filesystem::path(std::wstring(module, length)).parent_path() / L"LumeMusicHost.exe";
    Check(std::filesystem::exists(executable), "real queue integration requires sibling LumeMusicHost.exe");
    if (!std::filesystem::exists(executable)) return;
    const auto root = std::filesystem::temp_directory_path() /
        ("lume-queue-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto first = root / "first.wav", second = root / "second.wav";
    WriteSilence(first, 600);
    WriteSilence(second, 2500);
    const auto tracks = music::ImportLocalMusicFiles({first.wstring(), second.wstring()}).tracks;
    Check(tracks.size() == 2, "real fixture imports two generated PCM WAV files");
    if (tracks.size() != 2) return;
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture, executable] {
        return std::make_unique<ObservedNativeHost>(executable.wstring(), fixture);
    };
    MusicSession session(std::move(options));
    session.ImportLocalFiles({first.wstring(), second.wstring()});
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->Audio().playing && fixture->Audio().duration > 0.5; }, 2000),
        "independent native host confirms real first WAV playback");
    Check(Eventually([&] { const auto audio = fixture->Audio(); return audio.playing && audio.duration > 2 &&
        SessionState(session)["current"]["id"] == tracks[1].id; }, 2500),
        "real EOF polling switches to the second WAV without page ticks");
    session.HandlePage(Command("pause"));
    Check(Eventually([&] { return fixture->Audio().status == "paused" && !fixture->Audio().playing; }),
        "real host confirms pause");
    QueueId(session, "removeFromQueue", tracks[1].id);
    Check(Eventually([&] { const auto audio = fixture->Audio(); return !audio.playing && audio.duration < 1 &&
        SessionState(session)["current"]["id"] == tracks[0].id; }),
        "deleting paused tail loads the predecessor in the real host without playing");
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->Audio().playing; }), "real predecessor can resume");
    session.HandlePage(Command("clearQueue"));
    Check(Eventually([&] { return !session.HostRunning(); }), "real clear terminates playback host");
    const auto finalAudio = fixture->Audio();
    Check(finalAudio.status == "closed" && !finalAudio.playing && finalAudio.duration == 0,
        "clear receives native close ACK with stopped unloaded engine");
    for (const auto& command : fixture->Commands())
        Check(command.action != "next" && command.action != "previous", "real host receives only single-source commands");
    session.Shutdown();
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

void TestNativeControlsAndAnalysisLifetime() {
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    MusicSession session(std::move(options));
    ui::MusicPageCommand volume;
    volume.action = ui::MusicAction::SetVolume;
    volume.volume = 0.25;
    ui::MusicPageCommand mute;
    mute.action = ui::MusicAction::SetMuted;
    mute.flag = true;
    session.HandlePage(volume);
    session.HandlePage(mute);
    Check(fixture->starts == 0 && SessionState(session)["volume"] == 0.25 &&
        SessionState(session)["muted"] == true, "controls remain native and lazy before playback");
    Check(session.PlayTracks({QueueTrack("a"), QueueTrack("b"), QueueTrack("c")}, "b"),
        "native PlayTracks atomically replaces and selects");
    Check(Eventually([&] { const auto state = fixture->Audio();
        return state.playing && state.trackId == "b.wav" && state.muted && state.volume == 0.25;
    }), "preload controls reach the host before playback");
    const auto before = SessionState(session);
    Check(!session.PlayTracks({QueueTrack("x")}, "unknown") && SessionState(session)["queue"] == before["queue"],
        "invalid PlayTracks id cannot replace authorization or queue");
    const auto commands = fixture->Commands().size();
    QueueId(session, "insertNext", "a");
    Check(SessionState(session)["queue"][1]["id"] == "a" && SessionState(session)["current"]["id"] == "b" &&
        fixture->Commands().size() == commands, "native insert-next only changes queue ordering");
    QueueId(session, "insertNext", "forged.wav");
    Check(SessionState(session)["queue"].size() == 3, "unknown insert-next identity is not authorized");
    session.HandlePage(Command("open"));
    mute.flag = false;
    session.HandlePage(mute);
    Check(Eventually([&] { return SessionState(session)["analysis"]["energy"] == 0.125; }),
        "visible page receives the host's measured analysis");
    session.HandlePage(Command("close"));
    Check(SessionState(session)["analysis"]["energy"] == 0, "closing clears analysis immediately");
    Check(Eventually([&] { return !fixture->Commands().back().analysis; }),
        "hidden playback stops requesting analysis");
    Check(fixture->Audio().playing, "hidden analysis cancellation does not stop playback");
    session.HandlePage(Command("open"));
    Check(Eventually([&] { return SessionState(session)["analysis"]["energy"] == 0.125; }),
        "returning to the page requests fresh analysis");
    session.HandlePage(Command("pause"));
    Check(SessionState(session)["analysis"]["energy"] == 0, "pause clears analysis synchronously");
    Check(session.EnqueueTracks({QueueTrack("d"), QueueTrack("e")}, true), "native append authorizes new tracks");
    const auto snapshot = session.Snapshot();
    Check(snapshot.current->id == "b" && !snapshot.playing &&
        snapshot.queue[1].id == "d" && snapshot.queue[2].id == "e" &&
        snapshot.queue[1].source == "d.wav", "native Snapshot retains sources and plural insert-next preserves order");
    auto forged = QueueTrack("d");
    forged.source = "forged.wav";
    Check(!session.EnqueueTracks({forged}), "native append cannot replace an existing authorized source by id");
    QueueId(session, "removeFromQueue", "e");
    QueueId(session, "enqueue", "e");
    Check(session.Snapshot().queue.back().id == "e", "native append adds bounded page id authorization");
    session.Shutdown();
    Check(!session.Snapshot().playing && session.Snapshot().analysis.energy == 0,
        "shutdown revokes playing and analysis projection");
    MusicSession empty({});
    Check(empty.EnqueueTracks({QueueTrack("first"), QueueTrack("second"), QueueTrack("third")}, true),
        "plural insert-next can initialize an empty queue");
    const auto initialized = empty.Snapshot();
    Check(initialized.current->id == "first" && initialized.queue[1].id == "second" &&
        initialized.queue[2].id == "third" && !initialized.playing,
        "empty insert-next keeps supplied order without starting audio");
}

void TestPausedPlaybackReleasesHostWhenPageCloses() {
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    MusicSession session(std::move(options));
    Check(session.PlayTracks({QueueTrack("resume")}, "resume"), "resume fixture starts selected track");
    Check(Eventually([&] { return session.Snapshot().playing && session.HostRunning(); }),
        "selected track starts in the playback host");

    ui::MusicPageCommand seek;
    seek.action = ui::MusicAction::Seek;
    seek.position = 8.0;
    session.HandlePage(seek);
    Check(Eventually([&] { return session.Snapshot().position == 8.0; }),
        "seek position is confirmed before pause");
    session.HandlePage(Command("pause"));
    Check(Eventually([&] { return !session.Snapshot().playing; }), "playback is paused");
    session.HandlePage(Command("close"));

    Check(Eventually([&] { return !session.HostRunning(); }),
        "closing a paused music page releases its playback host");
    const auto paused = session.Snapshot();
    Check(paused.current && paused.current->id == "resume" && paused.position == 8.0 && !paused.playing,
        "releasing the host preserves the selected track and paused position");

    session.HandlePage(Command("open"));
    Check(!session.HostRunning(), "reopening alone does not restart the playback host");
    session.HandlePage(Command("play"));
    Check(Eventually([&] {
        return session.HostRunning() && session.Snapshot().playing &&
            fixture->Audio().playing && fixture->Audio().trackId == "resume.wav" &&
            fixture->Audio().position == 8.0;
    }), "play after reopening restores the paused track at its saved position");
    session.Shutdown();
}

void TestBackgroundPauseReleasesHostAfterLeavingMusicPage() {
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    MusicSession session(std::move(options));
    Check(session.PlayTracks({QueueTrack("background")}, "background"),
        "background fixture starts selected track");
    Check(Eventually([&] { return fixture->Audio().playing; }), "background track is playing");

    session.HandlePage(Command("close"));
    Check(session.HostRunning() && fixture->Audio().playing,
        "leaving the music page does not interrupt active playback");
    session.HandlePage(Command("pause"));
    Check(Eventually([&] { return !session.HostRunning(); }),
        "pausing after leaving the music page releases the host");
    Check(session.Snapshot().current && session.Snapshot().current->id == "background" &&
        !session.Snapshot().playing, "background pause retains the selected track");
    session.Shutdown();
}

void TestRemoteResolutionAndCancellation() {
    auto fixture = std::make_shared<HostFixture>();
    std::atomic<int> resolutions{0}, cancellations{0};
    const auto caller = std::this_thread::get_id();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    options.resolveSource = [&](const music::Track& track, const std::function<bool()>& cancelled) {
        Check(std::this_thread::get_id() != caller, "provider resolution never runs on the page caller");
        ++resolutions;
        if (track.id == "slow") {
            if (Eventually(cancelled, 2000)) ++cancellations;
            return std::string("https://example.invalid/obsolete?signature=secret");
        }
        if (track.id == "denied") return std::string{};
        if (track.id == "local-injection") return std::string("C:/private.wav");
        if (track.id == "throws") throw std::runtime_error("secret");
        return std::string("https://example.invalid/audio?signature=secret");
    };
    MusicSession session(std::move(options));
    auto remote = QueueTrack("remote");
    remote.source = "online:" + remote.id;
    auto slow = remote; slow.id = "slow"; slow.source = "online:slow";
    Check(session.PlayTracks({slow, remote}, "slow"), "opaque native tracks can be selected");
    Check(Eventually([&] { return resolutions == 1; }), "resolver starts on playback worker");
    Check(fixture->starts == 0, "host remains lazy during provider resolution");
    session.HandlePage(CommandWithIndex("playNow", 1, "remote"));
    Check(Eventually([&] { return cancellations == 1 && fixture->Audio().playing; }),
        "new selection cancels and discards obsolete signed URL");
    Check(resolutions == 2 && fixture->Audio().trackId == "https://example.invalid/audio?signature=secret",
        "only current authorized source reaches host");
    Check(!HasText(session.State(), L"secret") && !HasText(session.WidgetState(), L"secret") &&
        !SessionState(session)["current"].contains("source"), "signed source never enters UI projections");
    session.HandlePage(Command("pause"));
    Check(Eventually([&] { return !fixture->Audio().playing; }), "remote pause reaches host");
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->Audio().playing; }) && resolutions == 2,
        "pause and resume do not repeat entitlement resolution for the loaded track");
    for (const auto* id : {"denied", "local-injection", "throws"}) {
        auto rejected = remote; rejected.id = id; rejected.source = "online:" + rejected.id;
        session.PlayTracks({rejected}, id);
        Check(Eventually([&] { return SessionState(session)["error"] == "music-source-unavailable" &&
            !session.HostRunning(); }), "denial, non-HTTPS result and exception all fail closed");
        Check(!HasText(session.State(), L"secret"), "resolver exception text is never exposed");
    }
    session.PlayTracks({slow}, "slow");
    Check(Eventually([&] { return resolutions == 6; }), "shutdown fixture enters resolver");
    session.Shutdown();
    Check(cancellations == 2 && !session.HasWork(), "shutdown cancels and joins the playback resolver");
}

void TestKugouHttpPlayback() {
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    const std::string url = "http://fs.youthandroid2.kugou.com/song.mp3?key=secret";
    std::string resolved = url;
    options.resolveAudioSource = [&](const auto&, std::string_view quality, const auto&) {
        return music::ResolvedAudioSource{resolved, std::string(quality), 128000};
    };
    MusicSession session(std::move(options));
    auto track = QueueTrack("kugou:" + std::string(32, 'A'));
    track.source = "online:" + track.id;
    Check(session.PlayTracks({track}, track.id), "Kugou HTTP track can be selected");
    Check(Eventually([&] { const auto audio = fixture->Audio(); return audio.playing && audio.trackId == url; }),
        "Kugou authorized HTTP source reaches the audio host unchanged");
    Check(!HasText(session.State(), L"secret") && !HasText(session.WidgetState(), L"secret"),
        "Kugou HTTP source and authorization do not leak to UI projections");
    session.HandlePage(Command("pause"));
    Check(Eventually([&] { return !fixture->Audio().playing; }), "Kugou HTTP pause reaches the loaded host");
    session.HandlePage(Command("play"));
    Check(Eventually([&] { return fixture->Audio().playing; }), "Kugou HTTP source resumes");
    for (const auto* unsafe : {"http://fs.kugou.com.evil.test/song", "http://user@fs.w.kugou.com/song",
            "http://127.0.0.1/song", "http://other.kugou.com/song", "http://fs.w.kugou.com:80/song"}) {
        resolved = unsafe;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return SessionState(session)["error"] == "music-source-unavailable" &&
            !session.HostRunning(); }), "Kugou HTTP playback fails closed for invalid sources");
    }
    resolved = url;
    for (const auto* id : {"qq:songMID", "netease:42"}) {
        track.id = id; track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return SessionState(session)["error"] == "music-source-unavailable" &&
            !session.HostRunning(); }), "other providers cannot use the Kugou HTTP media exception");
    }
    session.Shutdown();
}
void TestOnlineLyricsKeepProviderTimingAndCancel() {
    std::atomic<int> reads{0}, cancelledReads{0};
    const auto caller = std::this_thread::get_id();
    MusicSessionOptions options;
    options.readOnlineLyrics = [&](const music::Track& track, const std::function<bool()>& cancelled) {
        Check(std::this_thread::get_id() != caller, "online lyrics stay off the UI caller");
        ++reads;
        if (track.id == "netease:slow" && Eventually(cancelled, 2000)) ++cancelledReads;
        return music::LocalLyricsResult{{{1, 3, "ab", {{1, 1.5, "a"}, {1.5, 2.5, "b"}}, "translated"}}, {}};
    };
    MusicSession session(std::move(options));
    auto track = QueueTrack("netease:123"); track.source = "online:" + track.id;
    session.SetQueue({track});
    session.HandlePage(Command("open"));
    session.HandlePage(CommandWithId("loadLyrics", track.id));
    Check(Eventually([&] {
        const auto message = session.DrainNotification();
        if (!message) return false;
        const auto json = nlohmann::json::parse(*message);
        return json["type"] == "music-lyrics" && json["lines"].size() == 1 &&
            json["lines"][0]["words"][1]["startTime"] == 1.5 &&
            json["lines"][0]["words"][1]["text"] == "b" &&
            json["lines"][0]["translation"] == "translated";
    }), "online words and translation survive validation and projection unchanged");
    Check(reads == 1 && !HasText(session.State(), L"translated"), "lyrics stay separate from playback state ticks");
    track.id = "netease:slow"; track.source = "online:" + track.id;
    session.SetQueue({track});
    auto request = CommandWithId("loadLyrics", track.id);
    request.session = session.Snapshot().session;
    session.HandlePage(request);
    Check(Eventually([&] { return reads == 2; }), "online slow read starts once");
    session.HandlePage(Command("close"));
    Check(Eventually([&] { return cancelledReads == 1; }), "page close cancels online lyrics");
    session.HandlePage(Command("open"));
    const auto pending = session.DrainNotification();
    Check(!pending || !HasText(*pending, L"translated"), "cancelled provider data cannot repopulate the page");
    session.Shutdown();
}

ui::MusicPageCommand QualityCommand(const std::string& value, std::uint64_t session = 0) {
    auto json = nlohmann::json{{"version", 1}, {"scope", "music"}, {"action", "setQuality"}, {"value", value}};
    if (session) json["session"] = session;
    const auto wire = json.dump();
    const auto parsed = ui::ParseMusicPageJson(std::wstring(wire.begin(), wire.end()));
    Check(parsed.has_value(), "quality command has a valid strict native protocol");
    return parsed.value_or(ui::MusicPageCommand{});
}

void TestQualitySwitchPreservesPlaybackOnFailure() {
    for (const auto* provider : {"netease:42", "qq:songMID", "kugou:0123456789abcdef0123456789abcdef"})
    for (const bool paused : {false, true})
    for (const auto* failure : {"resolve", "start", "hello", "load", "setVolume", "setMuted", "prepare", "play", "unconfirmed"}) {
        auto old = std::make_shared<HostFixture>();
        auto candidate = std::make_shared<HostFixture>();
        const std::string mode = failure;
        candidate->failStart = mode == "start";
        candidate->failLoadSource = mode == "load" ? "https://fixture.invalid/high?signature=secret" : "";
        candidate->failPlay = mode == "play";
        candidate->omitReply = mode == "unconfirmed";
        if (mode == "hello" || mode == "setVolume" || mode == "setMuted" || mode == "prepare")
            candidate->failSendAction = mode;
        int hosts = 0;
        MusicSessionOptions options;
        options.createHost = [&] { return std::make_unique<FakeHost>(hosts++ == 0 ? old : candidate); };
        options.resolveAudioSource = [&](const auto&, std::string_view quality, const auto&) {
            if (quality == "high" && mode == "resolve") throw std::runtime_error("signature=secret");
            return music::ResolvedAudioSource{
                quality == "standard" ? "https://fixture.invalid/standard" : "https://fixture.invalid/high?signature=secret",
                std::string(quality), quality == "standard" ? 128000u : 320000u};
        };
        MusicSession session(std::move(options));
        auto track = QueueTrack(provider); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return old->Audio().playing; }), "quality failure fixture starts original playback");
        Check(SessionState(session).value("audio", nlohmann::json::object()) ==
            nlohmann::json{{"requestedQuality", "standard"}, {"quality", "standard"}, {"bitrate", 128000}, {"switching", false}},
            "first online load publishes actual quality and bitrate without a quality command");
        ui::MusicPageCommand seek; seek.action = ui::MusicAction::Seek; seek.position = 11;
        session.HandlePage(seek);
        Check(Eventually([&] { return old->Audio().position == 11; }), "quality failure fixture seeks old audio");
        if (paused) {
            session.HandlePage(Command("pause"));
            Check(Eventually([&] { return !old->Audio().playing; }), "quality failure fixture pauses original audio");
            if (mode == "play") candidate->failSendAction = "pause";
        }
        const auto initialSession = session.Snapshot().session;
        const auto immediate = session.HandlePage(QualityCommand("high", initialSession));
        Check(immediate && nlohmann::json::parse(*immediate)["audio"]["requestedQuality"] == "high" &&
            nlohmann::json::parse(*immediate)["audio"]["switching"] == true,
            "quality command returns pending requested level synchronously");
        Check(Eventually([&] { return SessionState(session)["error"] != ""; }),
            "quality failure publishes a sanitized asynchronous error");
        if (mode == "resolve") std::this_thread::sleep_for(std::chrono::milliseconds(350));
        const auto state = SessionState(session);
        Check(old->Audio().playing == !paused && old->stops == 0 && session.HostRunning() &&
            state["playing"] == !paused && state["timeline"]["position"] == 11 && state["error"] != "" &&
            state["session"] == initialSession && state["audio"]["requestedQuality"] == "standard" &&
            state["audio"]["quality"] == "standard" && state["audio"]["bitrate"] == 128000 &&
            state["audio"]["switching"] == false,
            "every candidate failure keeps original audio, timeline, identity and actual quality");
        Check(!HasText(session.State(), L"secret") && !HasText(session.WidgetState(), L"secret"),
            "quality failure never exposes candidate URL or exception text");
        if (mode != "resolve" && mode != "start") Check(candidate->stops == 1, "failed candidate is stopped once");
        session.Shutdown();
        Check(old->stops == 1, "original playback is only stopped by shutdown after switch failure");
    }
}

void TestQualitySwitchSuccessAndSupersession() {
    for (const auto* providerTrack : {"netease:42", "qq:songMID", "kugou:0123456789abcdef0123456789abcdef"})
    for (const bool paused : {false, true}) {
        const auto* requested = std::string_view(providerTrack).find("netease:") == 0 ? "jymaster" : "hires";
        auto old = std::make_shared<HostFixture>();
        auto candidate = std::make_shared<HostFixture>();
        candidate->injectStaleReplies = true;
        candidate->replyOnThread = true;
        int hosts = 0;
        std::atomic<int> resolutions{0};
        std::atomic<bool> release{false};
        MusicSessionOptions options;
        options.createHost = [&] { return std::make_unique<FakeHost>(hosts++ == 0 ? old : candidate); };
        options.resolveAudioSource = [&](const auto&, std::string_view quality, const auto& cancelled) {
            ++resolutions;
            if (quality != "standard") Eventually([&] { return release.load() || cancelled(); }, 2000);
            return music::ResolvedAudioSource{"https://fixture.invalid/audio?signature=secret&padding=" + std::string(140, 'x'),
                quality == "standard" ? "standard" : "lossless", quality == "standard" ? 128000u : 900000u};
        };
        MusicSession session(std::move(options));
        auto track = QueueTrack(providerTrack); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return old->Audio().playing; }), "quality success fixture starts original playback");
        ui::MusicPageCommand seek; seek.action = ui::MusicAction::Seek; seek.position = 10;
        session.HandlePage(seek);
        Check(Eventually([&] { return old->Audio().position == 10; }), "quality success fixture has a timeline");
        if (paused) {
            session.HandlePage(Command("pause"));
            Check(Eventually([&] { return !old->Audio().playing; }), "quality success fixture pauses original audio");
        }
        ui::MusicPageCommand volume; volume.action = ui::MusicAction::SetVolume; volume.volume = 0.35;
        session.HandlePage(volume);
        Check(Eventually([&] { return old->Audio().volume == 0.35; }), "quality fixture applies native volume");
        const auto identity = session.Snapshot().session;
        const auto stale = session.HandlePage(QualityCommand("hires", identity + 1));
        Check(stale && resolutions == 1, "stale quality session never schedules resolution");
        session.HandlePage(QualityCommand(requested, identity));
        Check(Eventually([&] { return resolutions == 2; }), "quality switch resolves on worker");
        Check(old->stops == 0 && old->Audio().playing == !paused &&
            SessionState(session)["audio"]["quality"] == "standard", "pending switch retains old audio and actual level");
        {
            std::lock_guard lock(old->mutex);
            old->audio.position = 13;
        }
        release = true;
        Check(Eventually([&] { return old->stops == 1 && SessionState(session)["audio"]["quality"] == "lossless"; }),
            "confirmed candidate commits actual server downgrade");
        const auto state = SessionState(session);
        Check(candidate->Audio().playing == !paused && candidate->Audio().position == 13 &&
            candidate->Audio().volume == 0.35 &&
            state["timeline"]["position"] == 13 && state["session"] == identity && state["playing"] == !paused &&
            state["audio"]["requestedQuality"] == requested && state["audio"]["bitrate"] == 900000,
            "successful switch preserves latest position, pause and queue identity");
        Check(!HasText(session.State(), L"secret"), "long signed URLs never reach page state");
        const auto beforeRetry = candidate->Actions();
        const auto retryAt = std::chrono::steady_clock::now();
        session.HandlePage(QualityCommand(requested, identity));
        Check(Eventually([&] { return resolutions == 3 && SessionState(session)["audio"]["switching"] == false; }),
            "explicit retry of a downgraded quality resolves again and clears pending");
        const auto afterRetry = candidate->Actions();
        Check(hosts == 2 && candidate->stops == 0 &&
            std::count(beforeRetry.begin(), beforeRetry.end(), "load") == std::count(afterRetry.begin(), afterRetry.end(), "load") &&
            std::count(beforeRetry.begin(), beforeRetry.end(), "prepare") == std::count(afterRetry.begin(), afterRetry.end(), "prepare"),
            "same resolved source retry keeps the current host");
        Check(session.Snapshot().position == 13 && std::chrono::steady_clock::now() - retryAt < std::chrono::seconds(1),
            "same source retry avoids a second load and seek without resetting the clock");
        session.Shutdown();
        Check(candidate->stops == 1, "committed candidate shuts down once");
    }
    for (const bool shutdown : {false, true}) {
        auto old = std::make_shared<HostFixture>();
        auto obsolete = std::make_shared<HostFixture>();
        auto latest = std::make_shared<HostFixture>();
        obsolete->heldAction = "load"; obsolete->releaseReply = false; obsolete->cancelHeldReply = true;
        int hosts = 0;
        MusicSessionOptions options;
        options.createHost = [&] {
            const auto fixture = hosts == 0 ? old : hosts == 1 ? obsolete : latest;
            ++hosts; return std::make_unique<FakeHost>(fixture);
        };
        options.resolveAudioSource = [](const auto&, std::string_view quality, const auto&) {
            return music::ResolvedAudioSource{"https://fixture.invalid/" + std::string(quality), std::string(quality), 320000};
        };
        MusicSession session(std::move(options));
        auto track = QueueTrack("netease:42"); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return old->Audio().playing; }), "candidate cancellation fixture starts audio");
        session.HandlePage(QualityCommand("hires"));
        Check(Eventually([&] { return obsolete->replyHeld.load(); }), "candidate load can be held without stopping old audio");
        Check(old->Audio().playing && old->stops == 0, "old audio continues during candidate host I/O");
        if (shutdown) session.Shutdown();
        else {
            session.HandlePage(QualityCommand("sky"));
            Check(Eventually([&] { return latest->Audio().playing && SessionState(session)["audio"]["quality"] == "sky"; }),
                "rapid quality change cancels obsolete candidate and only commits latest");
            session.Shutdown();
        }
        Check(obsolete->cancelledReplies == 1 && obsolete->stops == 1 && old->stops == 1 && !session.HasWork(),
            "candidate supersession and shutdown release all hosts and discard stale ACKs");
    }
}

void TestQualitySwitchWaitsForBufferedAudio() {
    for (const auto* provider : {"netease:42", "qq:songMID", "kugou:0123456789abcdef0123456789abcdef"})
    for (const bool paused : {false, true}) {
        auto old = std::make_shared<HostFixture>();
        auto candidate = std::make_shared<HostFixture>();
        candidate->preparedReady = false;
        std::atomic<int> polls{0};
        candidate->onCommand = [&](const auto& command) {
            if (command.action == "hello") ++polls;
        };
        int hosts = 0;
        MusicSessionOptions options;
        options.createHost = [&] { return std::make_unique<FakeHost>(hosts++ == 0 ? old : candidate); };
        options.resolveAudioSource = [](const auto&, std::string_view quality, const auto&) {
            return music::ResolvedAudioSource{"https://fixture.invalid/" + std::string(quality), std::string(quality), 900000};
        };
        MusicSession session(std::move(options));
        auto track = QueueTrack(provider); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return old->Audio().playing; }), "buffering fixture starts original audio");
        if (paused) {
            session.HandlePage(Command("pause"));
            Check(Eventually([&] { return !old->Audio().playing; }), "buffering fixture pauses");
        }
        session.HandlePage(QualityCommand("lossless"));
        Check(Eventually([&] { return polls >= 3; }), "new quality must wait for decoded buffers");
        Check(old->stops == 0 && old->Audio().playing == !paused && !candidate->Audio().playing &&
            candidate->Audio().muted && SessionState(session)["audio"]["quality"] == "standard",
            "unready SQ never replaces, unmutes or stops live audio");
        {
            std::lock_guard lock(old->mutex);
            old->audio.position = 0.4;
        }
        Check(Eventually([&] { return session.Snapshot().position == 0.4; }),
            "native lyric clock continues following old audio throughout buffering");
        {
            std::lock_guard lock(candidate->mutex);
            candidate->audio.position = 0.4;
        }
        candidate->preparedReady = true;
        Check(Eventually([&] { return old->stops == 1 && SessionState(session)["audio"]["quality"] == "lossless"; }),
            "prepared audio alone may commit the requested platform quality");
        const auto commands = candidate->Commands();
        const auto prepared = std::find_if(commands.begin(), commands.end(), [](const auto& command) {
            return command.action == "prepare";
        });
        Check(prepared != commands.end() && prepared->follow == !paused && candidate->Audio().playing == !paused,
            "preparation follows a playing source and preserves pause intent");
        session.Shutdown();
    }
}

void TestSlowQualityPreparationKeepsLiveClock() {
    for (const auto* provider : {"netease:42", "qq:songMID", "kugou:0123456789abcdef0123456789abcdef"})
    for (const bool slowLoad : {false, true}) {
        auto old = std::make_shared<HostFixture>();
        auto candidate = std::make_shared<HostFixture>();
        if (slowLoad) { candidate->heldAction = "load"; candidate->releaseReply = false; }
        std::atomic<bool> resolving{false}, release{false};
        int hosts = 0;
        MusicSessionOptions options;
        options.createHost = [&] { return std::make_unique<FakeHost>(hosts++ == 0 ? old : candidate); };
        options.resolveAudioSource = [&](const auto&, std::string_view quality, const auto& cancelled) {
            if (quality != "standard" && !slowLoad) {
                resolving = true;
                Eventually([&] { return release.load() || cancelled(); }, 2000);
            }
            return music::ResolvedAudioSource{"https://fixture.invalid/" + std::string(quality), std::string(quality), 320000};
        };
        MusicSession session(std::move(options));
        auto track = QueueTrack(provider); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return old->Audio().playing; }), "slow preparation starts old audio");
        session.HandlePage(QualityCommand("high"));
        Check(Eventually([&] { return slowLoad ? candidate->replyHeld.load() : resolving.load(); }),
            "provider resolution or host load can be held");
        const auto clockAt = std::chrono::steady_clock::now();
        { std::lock_guard lock(old->mutex); old->audio.position = 3; }
        Check(Eventually([&] { return session.Snapshot().position == 3; }, 600) &&
            SessionState(session)["audio"]["switching"] == true && old->stops == 0,
            "all providers keep publishing the live lyric clock during Resolve and Load");
        Check(std::chrono::steady_clock::now() - clockAt < std::chrono::milliseconds(650),
            "live timeline does not wait for slow preparation");
        const auto releaseAt = std::chrono::steady_clock::now();
        release = true; candidate->releaseReply = true;
        Check(Eventually([&] { return SessionState(session)["audio"]["quality"] == "high"; }) &&
            session.Snapshot().position == 3 && SessionState(session)["audio"]["switching"] == false,
            "candidate prepares at the latest clock and clears switching after handover");
        Check(std::chrono::steady_clock::now() - releaseAt < std::chrono::seconds(1),
            "ready source hands over promptly on every platform");
        session.Shutdown();
    }
}

void TestQualityBufferingTimeoutAndCancellation() {
    for (const auto* provider : {"netease:42", "qq:songMID", "kugou:0123456789abcdef0123456789abcdef"})
    for (const auto* outcome : {"timeout", "supersede", "shutdown", "failure"}) {
        auto old = std::make_shared<HostFixture>();
        auto candidate = std::make_shared<HostFixture>();
        auto latest = std::make_shared<HostFixture>();
        const std::string mode = outcome;
        const bool resolveTimeout = mode == "timeout" && std::string_view(provider).rfind("qq:", 0) == 0;
        const bool loadTimeout = mode == "timeout" && std::string_view(provider).rfind("kugou:", 0) == 0;
        std::atomic<bool> resolving{false};
        if (loadTimeout) {
            candidate->heldAction = "load"; candidate->releaseReply = false;
            candidate->cancelHeldReply = true; candidate->holdTimeoutMs = 15000;
        }
        candidate->preparedReady = false;
        std::atomic<int> polls{0};
        std::atomic<bool> failPoll{false};
        candidate->onCommand = [&](const auto& command) {
            if (command.action == "hello") {
                ++polls;
                if (failPoll) candidate->failSendAction = "hello";
            }
        };
        int hosts = 0;
        MusicSessionOptions options;
        options.createHost = [&] {
            return std::make_unique<FakeHost>(hosts++ == 0 ? old : hosts == 2 ? candidate : latest);
        };
        options.resolveAudioSource = [&](const auto&, std::string_view quality, const auto& cancelled) {
            if (quality == "lossless") {
                resolving = true;
                if (resolveTimeout) while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (loadTimeout) std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
            return music::ResolvedAudioSource{"https://fixture.invalid/" + std::string(quality), std::string(quality), 900000};
        };
        MusicSession session(std::move(options));
        auto track = QueueTrack(provider); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return old->Audio().playing; }), "buffering cancellation fixture starts original audio");
        const auto startedAt = std::chrono::steady_clock::now();
        session.HandlePage(QualityCommand("lossless"));
        Check(Eventually([&] { return resolveTimeout ? resolving.load() : loadTimeout ? candidate->replyHeld.load() : polls >= 3; }),
            "candidate enters resolution, load or buffering before cancellation or timeout");
        if (mode == "shutdown") session.Shutdown();
        else if (mode == "supersede") {
            session.HandlePage(QualityCommand("high"));
            Check(Eventually([&] { return SessionState(session)["audio"]["quality"] == "high" && old->stops == 1; }),
                "new quality cancels unready candidate and commits only the latest source");
        } else {
            if (mode == "failure") failPoll = true;
            Check(Eventually([&] { return SessionState(session)["error"] != ""; }, 10000),
                "unready or broken candidate fails within the bounded buffering deadline");
            const auto state = SessionState(session);
            Check(candidate->stops == (resolveTimeout ? 0 : 1) && old->stops == 0 && old->Audio().playing && state["playing"] == true &&
                state["audio"]["quality"] == "standard" && state["audio"]["requestedQuality"] == "standard",
                "buffering timeout and failure preserve original playback and actual quality");
            if (mode == "timeout") Check(state["error"] == "music-quality-buffering-timeout",
                "slow buffering reports a specific recoverable error");
            if (mode == "timeout") Check(std::chrono::steady_clock::now() - startedAt < std::chrono::milliseconds(9500),
                "the single deadline includes resolver and load time, rather than restarting for each stage");
        }
        Check(candidate->stops == (resolveTimeout ? 0 : 1) && !candidate->Audio().playing &&
            (resolveTimeout || loadTimeout || candidate->Audio().muted),
            "cancelled or failed unready source never starts or unmutes");
        if (mode != "shutdown") session.Shutdown();
        Check(old->stops == 1 && !session.HasWork(), "buffering cancellation and shutdown release all work");
    }
}

void TestQualitySwitchAfterSupersededControls() {
    for (const bool cancel : {false, true})
    for (const auto* action : {"hello", "setVolume", "setMuted", "seek"}) {
        auto old = std::make_shared<HostFixture>();
        MusicSessionOptions options;
        options.createHost = [old] { return std::make_unique<FakeHost>(old); };
        options.resolveAudioSource = [](const auto&, std::string_view quality, const auto&) {
            return quality == "standard" ?
                music::ResolvedAudioSource{"https://fixture.invalid/standard", "standard", 128000} :
                music::ResolvedAudioSource{};
        };
        MusicSession session(std::move(options));
        auto track = QueueTrack("netease:42"); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return SessionState(session)["playing"] == true && old->Audio().playing; }),
            "superseded quality fixture starts original playback");
        {
            std::lock_guard lock(old->mutex);
            old->heldAction = action;
            old->cancelHeldReply = cancel;
        }
        old->releaseReply = false;
        if (std::string_view(action) != "hello") {
            ui::MusicPageCommand control;
            control.action = std::string_view(action) == "setVolume" ? ui::MusicAction::SetVolume :
                std::string_view(action) == "setMuted" ? ui::MusicAction::SetMuted : ui::MusicAction::Seek;
            control.volume = 0.4; control.flag = true; control.position = 7;
            session.HandlePage(control);
        }
        Check(Eventually([&] { return old->replyHeld.load(); }), "old poll or control ACK is held");
        session.HandlePage(QualityCommand("high"));
        if (cancel) Check(Eventually([&] { return old->cancelledReplies == 1; }),
            "quality change cancels obsolete poll or control");
        old->releaseReply = true;
        Check(Eventually([&] { return SessionState(session)["error"] == "music-source-unavailable"; }),
            "failed quality resolution reports its error after obsolete control");
        const auto state = SessionState(session);
        Check(old->stops == 0 && old->Audio().playing && state["playing"] == true &&
            state["audio"]["requestedQuality"] == "standard" && state["audio"]["quality"] == "standard" &&
            state["audio"]["bitrate"] == 128000,
            "obsolete poll or control cannot unload original audio before a failed quality switch");
        session.Shutdown();
    }
}

// Live QQ check (2026-10-11): QQ signs Hi-Res links for songs without a
// Hi-Res file and the CDN answers 404, so the host reports load-failed. A new
// track must fall back one level and keep playing instead of failing this and
// every later track; an expired login must say so and never downgrade.
void TestNewTrackQualityFallback() {
    {
        auto host = std::make_shared<HostFixture>();
        host->failLoadSource = "https://fixture.invalid/hires";
        MusicSessionOptions options;
        options.createHost = [host] { return std::make_unique<FakeHost>(host); };
        std::vector<std::string> requested;
        std::mutex requestedMutex;
        options.resolveAudioSource = [&](const auto&, std::string_view quality, const auto&) {
            { std::lock_guard lock(requestedMutex); requested.emplace_back(quality); }
            return music::ResolvedAudioSource{"https://fixture.invalid/" + std::string(quality), std::string(quality), 0};
        };
        MusicSession session(std::move(options));
        session.HandlePage(QualityCommand("hires"));
        auto track = QueueTrack("qq:hires-missing"); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return SessionState(session)["playing"] == true && host->Audio().playing; }),
            "a Hi-Res load failure falls back and keeps playing");
        const auto state = SessionState(session);
        Check(state["error"] == "" && state["audio"]["requestedQuality"] == "lossless" &&
            state["audio"]["quality"] == "lossless", "the fallback level is reported, not the failed one");
        {
            std::lock_guard lock(requestedMutex);
            Check(requested.size() >= 2 && requested.front() == "hires" && requested.back() == "lossless",
                "resolution steps from hires to lossless");
        }
        session.Shutdown();
    }
    {
        auto host = std::make_shared<HostFixture>();
        MusicSessionOptions options;
        options.createHost = [host] { return std::make_unique<FakeHost>(host); };
        std::atomic<int> calls{0};
        options.resolveAudioSource = [&](const auto&, std::string_view, const auto&) -> music::ResolvedAudioSource {
            ++calls;
            throw std::runtime_error("expired");
        };
        MusicSession session(std::move(options));
        session.HandlePage(QualityCommand("lossless"));
        auto track = QueueTrack("qq:expired"); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return SessionState(session)["error"] == "music-source-expired"; }),
            "an expired platform login is reported as such");
        Check(calls == 1 && SessionState(session)["audio"]["requestedQuality"] == "lossless",
            "an expired login is not retried at lower levels");
        session.Shutdown();
    }
}

void TestRevokedAudioSourceAcknowledgements() {
    for (const bool switching : {false, true})
    for (const bool replacement : {false, true})
    for (const bool cancel : {false, true})
    for (const auto* action : {"load", "play"}) {
        auto account = std::make_shared<std::atomic<std::uint64_t>>(1);
        auto old = std::make_shared<HostFixture>();
        auto candidate = std::make_shared<HostFixture>();
        candidate->heldAction = action;
        candidate->releaseReply = false;
        candidate->cancelHeldReply = cancel;
        int hosts = 0;
        MusicSessionOptions options;
        options.createHost = [&] {
            return std::make_unique<FakeHost>(switching && hosts++ == 0 ? old : candidate);
        };
        options.resolveAudioSource = [account](const auto&, std::string_view quality, const auto&) {
            const auto lease = account->load();
            return music::ResolvedAudioSource{"https://fixture.invalid/" + std::string(quality),
                std::string(quality), quality == "standard" ? 128000u : 320000u,
                [account, lease] { return account->load() == lease; }};
        };
        MusicSession session(std::move(options));
        auto track = QueueTrack("netease:42"); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        if (switching) {
            Check(Eventually([&] { return old->Audio().playing; }), "revocation fixture starts original audio");
            ui::MusicPageCommand seek; seek.action = ui::MusicAction::Seek; seek.position = 11;
            session.HandlePage(seek);
            Check(Eventually([&] { return old->Audio().position == 11; }), "original audio has a stable position");
            session.HandlePage(QualityCommand("high"));
        }
        const auto identity = session.Snapshot().session;
        Check(Eventually([&] { return candidate->replyHeld.load(); }),
            "resolved old-account URL reaches held host ACK");
        account->store(replacement ? 2u : 0u);
        if (cancel) Check(Eventually([&] { return candidate->cancelledReplies == 1; }),
            "account revocation reaches host transport cancellation after URL resolution");
        candidate->releaseReply = true;
        Check(Eventually([&] { return candidate->stops == 1 &&
            SessionState(session)["error"] == "music-source-unavailable"; }),
            "revoked candidate is stopped even when transport returns a successful late ACK");
        if (switching) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const auto state = SessionState(session);
            Check(old->stops == 0 && old->Audio().playing && session.HostRunning() &&
                state["playing"] == true && state["timeline"]["position"] == 11 &&
                state["session"] == identity && state["audio"]["requestedQuality"] == "standard" &&
                state["audio"]["quality"] == "standard" && state["audio"]["bitrate"] == 128000,
                "revoked quality candidate preserves original host, position and actual audio through polling");
        } else {
            const auto state = SessionState(session);
            const auto audio = state.value("audio", nlohmann::json::object());
            Check(!session.HostRunning() && !session.Snapshot().playing &&
                audio.value("quality", std::string{}).empty() && audio.value("bitrate", 0) == 0,
                "ordinary online load cannot retain or publish a revoked source");
        }
        session.Shutdown();
    }

    for (const bool replacement : {false, true}) {
        auto account = std::make_shared<std::atomic<std::uint64_t>>(1);
        auto fixture = std::make_shared<HostFixture>();
        fixture->heldAction = "play"; fixture->releaseReply = false;
        fixture->onCommand = [account, replacement](const music::HostCommand& command) {
            if (command.action == "setVolume") account->store(replacement ? 2u : 0u);
        };
        MusicSessionOptions options;
        options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
        options.resolveAudioSource = [account](const auto&, std::string_view quality, const auto&) {
            return music::ResolvedAudioSource{"https://fixture.invalid/old-account", std::string(quality), 128000,
                [account] { return account->load() == 1; }};
        };
        MusicSession session(std::move(options));
        auto track = QueueTrack("netease:42"); track.source = "online:" + track.id;
        session.PlayTracks({track}, track.id);
        Check(Eventually([&] { return fixture->replyHeld.load(); }), "initial play awaits ACK after confirmed load");
        ui::MusicPageCommand volume; volume.action = ui::MusicAction::SetVolume; volume.volume = 0.4;
        session.HandlePage(volume);
        fixture->releaseReply = true;
        Check(Eventually([&] { return fixture->stops == 1 &&
            SessionState(session)["error"] == "music-source-unavailable"; }),
            "superseded first play retains the lease through the resumed control ACK");
        Check(!session.HostRunning() && !session.Snapshot().playing &&
            SessionState(session)["audio"]["quality"] == "",
            "account revocation during resumed controls cannot commit an unconfirmed source");
        session.Shutdown();
    }

    auto account = std::make_shared<std::atomic<std::uint64_t>>(1);
    auto obsolete = std::make_shared<HostFixture>();
    auto latest = std::make_shared<HostFixture>();
    obsolete->heldAction = "load"; obsolete->releaseReply = false;
    int hosts = 0;
    MusicSessionOptions options;
    options.createHost = [&] { return std::make_unique<FakeHost>(hosts++ == 0 ? obsolete : latest); };
    options.resolveAudioSource = [account](const auto& track, std::string_view quality, const auto&) {
        const auto lease = account->load();
        return music::ResolvedAudioSource{"https://fixture.invalid/" + track.id,
            std::string(quality), 128000, [account, lease] { return account->load() == lease; }};
    };
    MusicSession session(std::move(options));
    auto track = QueueTrack("netease:old"); track.source = "online:" + track.id;
    session.PlayTracks({track}, track.id);
    Check(Eventually([&] { return obsolete->replyHeld.load(); }), "old-account initial load awaits ACK");
    account->store(2);
    track.id = "netease:new"; track.source = "online:" + track.id;
    session.PlayTracks({track}, track.id);
    obsolete->releaseReply = true;
    Check(Eventually([&] { return latest->Audio().playing && SessionState(session)["error"] == ""; }),
        "late revoked load cannot fail the new account playback target");
    Check(obsolete->stops == 1 && latest->stops == 0 && session.Snapshot().current->id == track.id &&
        SessionState(session)["audio"]["quality"] == "standard",
        "new-account host alone commits its source and audio metadata");
    session.Shutdown();
}

void TestSupersededControlAcknowledgements() {
    for (const bool volume : {true, false}) {
        auto fixture = std::make_shared<HostFixture>();
        MusicSessionOptions options;
        options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
        MusicSession session(std::move(options));
        session.PlayTracks({QueueTrack("a")}, "a");
        Check(Eventually([&] { return fixture->Audio().playing; }), "control race starts playback");
        {
            std::lock_guard lock(fixture->mutex);
            fixture->heldAction = volume ? "setVolume" : "setMuted";
        }
        fixture->releaseReply = false;
        ui::MusicPageCommand command;
        command.action = volume ? ui::MusicAction::SetVolume : ui::MusicAction::SetMuted;
        command.volume = 0.2;
        command.flag = true;
        session.HandlePage(command);
        Check(Eventually([&] { return fixture->replyHeld.load(); }), "control ACK is held after device mutation");
        command.volume = 1;
        command.flag = false;
        session.HandlePage(command);
        fixture->releaseReply = true;
        Check(Eventually([&] { const auto audio = fixture->Audio();
            return audio.playing && audio.volume == 1 && !audio.muted && SessionState(session)["error"] == "";
        }), "latest control is reapplied even when an obsolete ACK changed the device");
        session.Shutdown();
    }
}

void TestLibraryRestoreAndLyricsAuthorization() {
    std::atomic<int> reads{0};
    auto fixture = std::make_shared<HostFixture>();
    MusicSessionOptions options;
    options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
    options.readLocalLyrics = [&](const auto&, const auto&) {
        ++reads;
        return music::LocalLyricsResult{{{1, 3, "library sidecar", {}, ""}}, {}};
    };
    MusicSession session(std::move(options));
    music::MusicSnapshot saved;
    saved.queue = {QueueTrack("a"), QueueTrack("b")};
    saved.current = saved.queue[1];
    saved.currentIndex = 1;
    saved.position = 12;
    saved.volume = 0.4;
    saved.muted = true;
    saved.repeat = music::RepeatMode::All;
    saved.shuffle = true;
    saved.playing = true;
    session.RestoreQueue(saved, true);
    const auto restored = session.Snapshot();
    Check(restored.current->id == "b" && restored.position == 12 &&
        restored.volume == 0.4 && restored.muted && restored.shuffle &&
        restored.repeat == music::RepeatMode::All, "library restore preserves selection and settings");
    Check(!restored.playing && !session.HostRunning() && fixture->starts == 0,
        "persisted playing state never auto-starts playback");
    session.HandlePage(Command("open"));
    auto request = CommandWithId("loadLyrics", "b");
    request.session = restored.session;
    session.HandlePage(request);
    Check(Eventually([&] { return reads == 1; }), "restored library track authorizes local sidecar");
    session.Shutdown();
}

void TestLoadCancellationPort() {
    for (const bool shutdown : {true, false}) {
        auto fixture = std::make_shared<HostFixture>();
        fixture->heldAction = "load";
        fixture->releaseReply = false;
        fixture->cancelHeldReply = true;
        MusicSessionOptions options;
        options.createHost = [fixture] { return std::make_unique<FakeHost>(fixture); };
        MusicSession session(std::move(options));
        session.PlayTracks({QueueTrack("a")}, "a");
        Check(Eventually([&] { return fixture->replyHeld.load(); }), "load enters cancellable transport");
        const auto start = std::chrono::steady_clock::now();
        if (shutdown) session.Shutdown();
        else {
            session.HandlePage(Command("clearQueue"));
            Check(Eventually([&] { return !session.HostRunning(); }), "queue clear cancels obsolete load");
        }
        Check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(800),
            "shutdown/supersession does not await the load deadline");
        Check(fixture->cancelledReplies == 1, "target invalidation reaches transport cancellation");
        Check(!session.Snapshot().playing && SessionState(session)["error"] == "",
            "cancelled load cannot publish a stale error or playing state");
        fixture->releaseReply = true;
        session.Shutdown();
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "kugou") {
        TestKugouHttpPlayback();
        if (!passed) return 1;
        std::cout << "music Kugou HTTP playback and host boundary regressions passed\n";
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "lease") {
        TestRevokedAudioSourceAcknowledgements();
        if (!passed) return 1;
        std::cout << "music source lease ACK regressions passed\n";
        return 0;
    }
    TestPageLifecycleAndLazyHost();
    TestScoreAnalysisWaitsForLoadAndCaches();
    TestCloseCancelsPageRequestsAndPreservesAccountProjection();
    TestHostFailureDoesNotLeakIntoOtherFeatures();
    TestLocalLyricsAreOnDemandAndScopedToImportedCurrentTrack();
    TestInFlightLyricsCancelOnTrackChangeVisibilityAndShutdown();
    TestPendingLyricsWakeAndFairDrainUnderPlaybackTicks();
    TestQueuedCancellationAndWorkerReuse();
    TestSlowLyricsDoNotBlockPlaybackOrEof();
    TestLyricsSessionIsRequiredAndStaleCleanupStillReleases();
    TestReplacementAndPendingPageCleanupDropOldLyrics();
    TestOnlyLatestPendingLyricsSurvives();
    TestLyricsDestructionJoinsActiveRead();
    TestQueueCommandsAndActualStop();
    TestPlayNowRequiresMatchingIndexAndId();
    TestQueueIdentityAuthorizationAndPause();
    TestRapidOperationsAndLateEvents();
    TestHostEofRepeatShuffleAndFailures();
    TestDelayedAcknowledgementsAndCloseRace();
    TestRealHostQueueAndEof();
    TestNativeControlsAndAnalysisLifetime();
    TestPausedPlaybackReleasesHostWhenPageCloses();
    TestBackgroundPauseReleasesHostAfterLeavingMusicPage();
    TestRemoteResolutionAndCancellation();
    TestKugouHttpPlayback();
    TestQualitySwitchPreservesPlaybackOnFailure();
    TestQualitySwitchSuccessAndSupersession();
    TestQualitySwitchWaitsForBufferedAudio();
    TestSlowQualityPreparationKeepsLiveClock();
    TestQualityBufferingTimeoutAndCancellation();
    TestQualitySwitchAfterSupersededControls();
    TestRevokedAudioSourceAcknowledgements();
    TestNewTrackQualityFallback();
    TestSupersededControlAcknowledgements();
    TestOnlineLyricsKeepProviderTimingAndCancel();
    TestLibraryRestoreAndLyricsAuthorization();
    TestLoadCancellationPort();
    if (!passed) return 1;
    std::cout << "music session lifecycle behavior passed\n";
    return 0;
}
