#include "apps/shell/desktop_application.h"
#include "apps/shell/music_library_session.h"
#include "core/music/music_host_protocol.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/music_desktop_controls.h"
#include "tests/unit/unique_test_directory.h"
#include "vendor/nlohmann/json.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
using namespace lume;
using namespace lume::shell;

struct HostFixture {
    std::atomic<int> starts{0}, loads{0}, stops{0};
    std::atomic<bool> running{false}, playing{false}, invalidSource{false};
    std::string source;
};

// The real shell/worker/protocol run unchanged; only the audio transport is replaced.
class FakeHost final : public IMusicPlaybackHost {
public:
    explicit FakeHost(std::shared_ptr<HostFixture> fixture) : fixture_(std::move(fixture)) {}
    bool Start(EventHandler handler) override {
        handler_ = std::move(handler);
        ++fixture_->starts;
        fixture_->running = true;
        return true;
    }
    bool Send(std::string frame) override {
        const auto command = music::DecodeHostCommand(frame);
        if (!fixture_->running || !command) return false;
        state_.session = command->session;
        state_.sequence = command->sequence;
        if (command->action == "load") {
            ++fixture_->loads;
            if (command->source != fixture_->source) {
                fixture_->invalidSource = true;
                return false;
            }
            state_.trackId = command->source;
            state_.duration = 30;
            state_.position = 0;
            state_.playing = false;
        } else if (command->action == "play") state_.playing = true;
        else if (command->action == "pause") state_.playing = false;
        else if (command->action == "setVolume") state_.volume = command->value;
        else if (command->action == "setMuted") state_.muted = command->flag;
        else if (command->action == "seek") state_.position = command->value;
        else if (command->action == "close") {
            state_.playing = false;
            state_.trackId.clear();
            state_.duration = 0;
        }
        state_.status = state_.playing ? "playing" : "ready";
        state_.canToggle = state_.canSeek = !state_.trackId.empty();
        fixture_->playing = state_.playing;
        handler_(music::EncodeHostEvent(state_));
        return true;
    }
    void Stop() override {
        if (fixture_->running.exchange(false)) ++fixture_->stops;
        fixture_->playing = false;
        handler_ = {};
    }
    bool Running() const noexcept override { return fixture_->running; }
private:
    std::shared_ptr<HostFixture> fixture_;
    EventHandler handler_;
    music::HostEvent state_;
};

void WriteWav(const fs::path& path) {
    std::ofstream stream(path, std::ios::binary);
    const auto word = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) stream.put(static_cast<char>((value >> (8 * i)) & 255));
    };
    constexpr std::uint32_t samples = 8000 * 30;
    stream.write("RIFF", 4); word(36 + samples, 4); stream.write("WAVEfmt ", 8);
    word(16, 4); word(1, 2); word(1, 2); word(8000, 4); word(8000, 4);
    word(1, 2); word(8, 2); stream.write("data", 4); word(samples, 4);
    const std::string silence(samples, static_cast<char>(128));
    stream.write(silence.data(), static_cast<std::streamsize>(silence.size()));
    stream.close();
    if (!stream) throw std::runtime_error("could not create isolated WAV");
}

BOOL CALLBACK CountNativeControls(HWND window, LPARAM parameter) {
    wchar_t name[128]{};
    GetClassNameW(window, name, 128);
    if (std::wstring(name) == L"LumeMusicMediaDispatch" ||
        std::wstring(name) == L"LumeNativeDesktopWidget")
        ++*reinterpret_cast<int*>(parameter);
    return TRUE;
}

struct Probe {
    DesktopApplication* app = nullptr;
    std::shared_ptr<HostFixture> host = std::make_shared<HostFixture>();
    std::string id;
    std::wstring privateDirectory;
    ULONGLONG started = 0, hiddenAt = 0;
    std::uint64_t session = 0, revision = 0;
    int libraryMessages = 0, musicMessages = 0, desktopMessages = 0;
    bool failed = false, opened = false, library = false, playing = false, enabled = false;
    bool closeRequested = false, hidden = false, reopenRequested = false, reopened = false;
    bool resumedState = false, reopenedLibrary = false, paused = false, disabled = false;
    bool corruptQueue = false, restoreError = false, missingError = false;

    void Check(bool value, const char* reason) {
        if (!value) { failed = true; std::cerr << "FAIL: " << reason << '\n'; }
    }
    void NoNativeControls() {
        int count = 0;
        EnumThreadWindows(GetCurrentThreadId(), CountNativeControls, reinterpret_cast<LPARAM>(&count));
        Check(count == 0, "verification creates neither desktop widget nor SMTC dispatch window");
    }
    void Message(const std::wstring& text) {
        const auto json = Json::parse(text, nullptr, false);
        if (!json.is_object()) { Check(false, "native observer receives valid JSON"); return; }
        const auto type = json.value("type", "");
        if (type != "music-library-state" && type != "music-state" && type != "music-desktop-state") return;
        Check(json.value("version", 0) == 1, "music composition messages use envelope v1");
        Check(text.find(privateDirectory) == text.npos, "public messages never expose fixture storage path");
        if (type == "music-library-state") {
            ++libraryMessages;
            const auto error = json.value("error", "");
            if (error == "queue-unreadable") restoreError = true;
            if (error == "track-unavailable") missingError = true;
            Check(error.empty() || error == "track-unavailable" || (corruptQueue && error == "queue-unreadable"),
                "library reports only the deliberately injected failures");
            Check(json.size() == 9 && json.value("loaded", false) &&
                json.value("total", 0) == 1, "explicit library root restored and projected");
            const auto& tracks = json.at("tracks");
            Check(tracks.size() == 1 && tracks[0].value("id", "") == id &&
                tracks[0].value("source", "missing") == "" && tracks[0].value("available", false),
                "only imported stable ID and redacted source reach WebView");
            const auto next = json.at("revision").get<std::uint64_t>();
            Check(next > revision, "library revisions increase across manager lifetimes");
            revision = next;
            library = true;
            if (reopenRequested && app->ManagerReady()) reopenedLibrary = true;
        } else if (type == "music-desktop-state") {
            ++desktopMessages;
            Check(!json.at("running").get<bool>() && !json.at("stopped").get<bool>() &&
                !json.at("smtc").at("registered").get<bool>() &&
                json.at("smtc").at("status") == "verification" && json.at("error") == "",
                "desktop preferences operate without widget visibility or SMTC registration");
            NoNativeControls();
            if (json.at("result") == "applied") {
                Check(json.at("saved") == true, "desktop enable/disable persisted to explicit fixture path");
                if (json.at("enabled") == true) {
                    Check(library && missingError && playing && !closeRequested,
                        "script verifies missing-track error and cleared UI pending before valid playback");
                    enabled = true;
                } else {
                    Check(reopened && resumedState && reopenedLibrary && paused,
                        "script disables desktop only after reopened queue and pause roundtrip");
                    disabled = true;
                }
            }
        } else {
            ++musicMessages;
            Check(json.value("error", "") == "", "composition playback has no error");
            if (!json.contains("current") || !json["current"].is_object() || json["current"].value("id", "") != id)
                return;
            Check(json["queue"].size() == 1 && !json["current"].contains("source"),
                "playback queue keeps one imported identity without its native path");
            const auto currentSession = json.at("session").get<std::uint64_t>();
            if (json.at("playing") == true) {
                playing = true;
                if (!session) session = currentSession;
            }
            if (reopenRequested && app->ManagerReady()) {
                reopened = true;
                Check(currentSession == session && host->starts == 1 && host->loads == 1 && host->stops == 0,
                    "manager reopening retains the same native playback session and host");
                if (json.at("playing") == true) resumedState = true;
                if (resumedState && json.at("playing") == false && !host->playing) paused = true;
            }
        }
    }
    void Tick() {
        if (failed || host->invalidSource || GetTickCount64() - started > 25000) {
            Check(!host->invalidSource, "FakeHost only accepts the imported temporary WAV");
            if (!failed) {
                std::cerr << "progress: library=" << library << " playing=" << playing << " enabled=" << enabled
                    << " hidden=" << hidden << " reopened=" << reopened << " paused=" << paused
                    << " disabled=" << disabled << " corruptQueue=" << corruptQueue
                    << " restoreError=" << restoreError << " missingError=" << missingError << '\n';
                Check(false, "bounded real WebView2 composition deadline");
            }
            app->Exit();
            return;
        }
        NoNativeControls();
        if (!opened) {
            opened = true;
            app->Open(ManagerPage::Music);
        } else if (enabled && playing && !closeRequested) {
            Check(app->ManagerReady() && host->running && host->playing, "close starts with real WebView and fake playback");
            closeRequested = true;
            app->CloseManager();
        } else if (closeRequested && !hidden && !app->ManagerHandle()) {
            hidden = true; hiddenAt = GetTickCount64();
            Check(!app->ManagerReady() && host->running && host->playing && host->stops == 0,
                "closing manager does not stop background session");
        } else if (hidden && !reopenRequested && GetTickCount64() - hiddenAt >= 300) {
            Check(host->running && host->playing && host->starts == 1 && host->loads == 1,
                "background native composition survives without WebView");
            reopenRequested = true;
            app->Open(ManagerPage::Music);
        } else if (disabled && paused) {
            Check(host->running && host->stops == 0, "pause retains host until explicit exit");
            app->Exit();
            app->Exit();
        }
    }
};
Probe* active = nullptr;
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) { if (active) active->Tick(); }

std::wstring ReadyScript(const std::string& id, bool corruptQueue) {
    if (id.size() <= 6 || id.rfind("local-", 0) != 0 ||
        id.find_first_not_of("0123456789", 6) != id.npos)
        throw std::runtime_error("fixture requires a numeric local ID");
    const auto selected = L"\"" + std::wstring(id.begin(), id.end()) + L"\"";
    return L"(async () => { const selected=" + selected +
        L", corruptQueue=" + (corruptQueue ? L"true" : L"false") + LR"JS(;
        const [{createBridge},{createMusicLibrary}]=await Promise.all([
            import('./bridge.js'),import('./music-library.js')]);
        const bridge=createBridge();
        const local=createMusicLibrary({send:(action,fields)=>bridge.musicLibrary(action,fields)});
        const post=(scope,action,fields={})=>scope==='music-desktop'
            ? bridge.musicDesktop(action,fields) : bridge.music(action,fields);
        let phase=null, library=false, music=null, sent=false, completed=false;
        let missingSent=false, missingSettled=false;
        function advance() {
            if(phase===null||!library||!music||completed)return;
            if(phase==='initial'&&!missingSettled) {
                if(!missingSent) {
                    missingSent=true;
                    if(!local.play('local-999999999999999999999999')||!local.state.pending)
                        throw new Error('missing-track request did not enter pending');
                }
                return;
            }
            if(!sent) {
                sent=true;
                if(phase==='initial')local.play(selected);
                else if(music.current?.id===selected&&music.playing)
                    post('music','pause',{session:music.session});
                else sent=false;
                return;
            }
            if(music.current?.id!==selected)return;
            if(phase==='initial'&&music.playing) {
                completed=true;
                post('music-desktop','setEnabled',{value:true});
            } else if(phase==='reopened'&&!music.playing) {
                completed=true;
                post('music-desktop','setEnabled',{value:false});
            }
        }
        bridge.subscribe(data=>{
            if(data.type==='music-desktop-state'&&phase===null) {
                phase=data.enabled?'reopened':'initial';
                post('music','open');
                local.load();
                post('music','snapshot');
            } else if(data.type==='music-library-state') {
                if(!local.receive(data))return;
                if(data.loaded&&data.tracks.some(track=>track.id===selected)&&
                    (phase==='reopened'||data.error===(corruptQueue?'queue-unreadable':'')))library=true;
                if(missingSent&&data.error==='track-unavailable') {
                    if(local.state.pending||local.state.error!=='track-unavailable')
                        throw new Error('error ACK failed to settle real library controller');
                    missingSettled=true;
                }
            }
            else if(data.type==='music-state')music=data;
            advance();
        });
        post('music-desktop','snapshot');
    })();)JS";
}
} // namespace

int RunCase(bool corruptQueue) {
    fs::path root;
    try {
        const auto parent = fs::absolute(fs::temp_directory_path()).lexically_normal();
        root = UniqueTestDirectory(parent, "lume-desktop-music-library-");
        const auto libraryRoot = root / "library";
        const auto widgetPath = root / "music-widget.json";
        const auto wav = root / "fixture.wav";
        WriteWav(wav);
        Probe probe;
        probe.corruptQueue = corruptQueue;
        probe.privateDirectory = root.filename().wstring();
        {
            MusicLibrarySession library({libraryRoot});
            const auto imported = library.Import({wav.wstring()});
            if (!imported.error.empty() || imported.added != 1 || !imported.failures.empty())
                throw std::runtime_error("isolated WAV library import failed");
            const auto tracks = library.Tracks();
            if (tracks.size() != 1) throw std::runtime_error("isolated library index is empty");
            probe.id = tracks[0].id;
            probe.host->source = tracks[0].source;
        }
        platform::windows::LocalSettingsFileStore files;
        const auto queuePath = (libraryRoot / "queue.json").u8string();
        const std::string damagedQueue = "{\"version\":1,\"queue\":[broken-regression-fixture";
        if (corruptQueue && !files.Write(queuePath, damagedQueue))
            throw std::runtime_error("could not create isolated corrupt queue");
        DesktopApplicationOptions options;
        options.verification = true;
        options.startHidden = true;
        options.clockPreferencesPath = (root / "clock.json").u8string();
        options.calendarPreferencesPath = (root / "calendar.json").u8string();
        options.performancePreferencesPath = (root / "performance.json").u8string();
        options.almanacPreferencesPath = (root / "almanac.json").u8string();
        options.weatherPreferencesPath = (root / "weather.json").u8string();
        options.organizerPreferencesPath = (root / "organizer.json").u8string();
        options.desktopIconsPreferencesPath = (root / "icons.json").u8string();
        options.minimizePreferencesPath = (root / "minimize.json").u8string();
        options.taskbarPaths.stateFile = (root / "taskbar.json").u8string();
        options.taskbarPaths.cacheRoot = root / "taskbar-cache";
        options.taskbarPaths.payloadRoot = root / "taskbar-payload";
        options.applicationSettings.preferencesPath = (root / "application.json").u8string();
        options.updateNotification.statePath = (root / "updates.json").u8string();
        options.accountOptions.storageDirectory = (root / "account").u8string();
        options.wallpaperOptions.root = (root / "wallpapers").u8string();
        options.musicPreferencesPath = (root / "music.json").u8string();
        options.musicBackground.root = (root / "music-background").u8string();
        options.musicBackground.statePath = (root / "music-background.json").u8string();
        options.musicAccounts.root = root / "music-accounts";
        options.musicFonts.root = root / "music-fonts";
        options.musicLibrary.root = libraryRoot;
        options.musicDesktop.path = widgetPath.u8string();
        options.musicDesktop.verification = true;
        options.music.createHost = [host = probe.host] { return std::make_unique<FakeHost>(host); };
        options.readyScript = ReadyScript(probe.id, corruptQueue);
        options.onMessage = [&](const auto& message) {
            try { probe.Message(message); }
            catch (const std::exception& error) {
                probe.Check(false, "unexpected composition state schema");
                std::cerr << error.what() << '\n';
            }
        };
        {
            DesktopApplication app(GetModuleHandleW(nullptr), std::move(options));
            probe.app = &app; probe.started = GetTickCount64(); active = &probe;
            const auto timer = SetTimer(nullptr, 0, 100, Tick);
            probe.Check(timer != 0, "integration deadline timer created");
            if (timer) probe.Check(app.Run() == 0, "isolated desktop composition exits safely");
            if (timer) KillTimer(nullptr, timer);
            active = nullptr;
            probe.Check(!app.ManagerHandle() && !probe.host->running && probe.host->stops == 1,
                "Exit closes WebView and joins/stops the only fake audio host");
        }
        probe.NoNativeControls();
        probe.Check(probe.library && probe.playing && probe.enabled && probe.hidden && probe.reopened &&
            probe.resumedState && probe.reopenedLibrary && probe.paused && probe.disabled && probe.missingError,
            "real WebView snapshot/play/enable, manager close/reopen, pause/disable all completed");
        probe.Check(probe.libraryMessages > 1 && probe.musicMessages > 1 && probe.desktopMessages > 1 &&
            probe.host->starts == 1 && probe.host->loads == 1 && !probe.host->invalidSource,
            "all three state channels traversed onMessage without real audio or replacement host");
        if (corruptQueue) {
            std::string afterExit;
            probe.Check(probe.restoreError, "corrupt queue restore reports queue-unreadable");
            probe.Check(files.ReadBounded(queuePath, 4096, afterExit) && afterExit == damagedQueue,
                "failed queue restore is never overwritten by drain, exit or destruction");
        } else {
            MusicLibrarySession restored({libraryRoot});
            const auto saved = restored.RestoredQueue();
            probe.Check(saved && saved->queue.size() == 1 && saved->queue[0].id == probe.id &&
                !saved->playing, "safe exit flushed the fixture queue without autoplay");
        }
        widget::PreferencesStore store(files, platform::windows::MusicDesktopControls::Limits);
        const auto desktop = store.Load(widgetPath.u8string());
        probe.Check(desktop.state == widget::LoadState::Loaded && !desktop.value.enabled,
            "desktop disable persisted at explicit temporary path");
        if (probe.failed) {
            std::cerr << "Fixture retained: " << root.u8string() << '\n';
            return 1;
        }
        // Only remove the unique, direct child reserved by this test.
        if (root.parent_path() == parent && root.filename().u8string().rfind("lume-desktop-music-library-", 0) == 0)
            fs::remove_all(root);
        std::cout << "PASS: " << (corruptQueue ? "corrupt queue preserved" : "valid queue persisted")
            << ", real WebView2 missing-track ACK/UI pending, playback and manager close/reopen\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\nFixture retained: " << root.u8string() << '\n';
        return 1;
    }
}

int main() {
    const auto normal = RunCase(false);
    const auto corrupt = RunCase(true);
    return normal || corrupt ? 1 : 0;
}
