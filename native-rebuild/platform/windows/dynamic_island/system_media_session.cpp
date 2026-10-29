#include "platform/windows/dynamic_island/system_media_session.h"
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wincrypt.h>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lume::platform::windows {
namespace {
using namespace winrt::Windows::Media::Control;
std::wstring Safe(winrt::hstring value) { return value.c_str() ? std::wstring(value.c_str()) : std::wstring{}; }
std::string Thumbnail(GlobalSystemMediaTransportControlsSessionMediaProperties const& properties) {
    try {
        auto reference = properties.Thumbnail();
        if (!reference) return {};
        auto stream = reference.OpenReadAsync().get();
        // Some players report Size() 0 for their thumbnail stream; read in chunks.
        auto reader = winrt::Windows::Storage::Streams::DataReader(stream.GetInputStreamAt(0));
        reader.InputStreamOptions(winrt::Windows::Storage::Streams::InputStreamOptions::Partial);
        std::vector<std::uint8_t> bytes;
        for (;;) {
            const auto count = reader.LoadAsync(64 * 1024).get();
            if (!count) break;
            const auto offset = bytes.size();
            if (offset + count > 2 * 1024 * 1024) return {};
            bytes.resize(offset + count);
            reader.ReadBytes(winrt::array_view<std::uint8_t>(bytes.data() + offset, bytes.data() + offset + count));
        }
        if (bytes.size() < 8) return {};
        DWORD encoded = 0;
        if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                  nullptr, &encoded))
            return {};
        std::string text(encoded, '\0');
        if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                  text.data(), &encoded))
            return {};
        text.resize(encoded);
        const bool png = bytes[0] == 0x89 && bytes[1] == 'P';
        return std::string(png ? "data:image/png;base64," : "data:image/jpeg;base64,") + text;
    } catch (...) { return {}; }
}
}  // namespace

// Windows media sessions are queried on a private MTA worker: the WinRT calls
// block, and must never stall the UI thread or a game's input.
struct SystemMediaSession::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<dynamic_island::Media> latest;
    struct Request { dynamic_island::Action action; int play = -1; };  // play: -1 toggle, 0 pause, 1 play
    std::deque<Request> controls;
    // After a control the player changes state within a few hundred ms:
    // read every 100 ms for a while so the island confirms it at once.
    ULONGLONG fastUntil = 0;
    bool stop = false, done = false;
    // Queries run only while the island asks for snapshots (it is enabled).
    ULONGLONG requested = 0;
    ULONGLONG latestAt = 0;  // when latest was read

    void Run() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        {
            GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
            GlobalSystemMediaTransportControlsSession session{nullptr};
            std::wstring artworkKey;
            std::string artwork;
            for (;;) {
                std::deque<Request> pending;
                {
                    std::unique_lock lock(mutex);
                    const auto interval = GetTickCount64() < fastUntil ? std::chrono::milliseconds(100) : std::chrono::milliseconds(1000);
                    wake.wait_for(lock, interval, [this] { return stop || !controls.empty(); });
                    if (stop) break;
                    pending.swap(controls);
                    if (pending.empty() && GetTickCount64() - requested > 3000) { latest.reset(); continue; }
                }
                try {
                    if (!manager) manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                    session = manager ? manager.GetCurrentSession() : nullptr;
                    for (const auto& request : pending) Control(session, request);
                    if (!pending.empty()) { std::lock_guard lock(mutex); fastUntil = GetTickCount64() + 1500; }
                    auto media = Read(session, artworkKey, artwork);
                    std::lock_guard lock(mutex);
                    latest = std::move(media);
                    latestAt = GetTickCount64();
                } catch (...) {
                    manager = nullptr;
                    std::lock_guard lock(mutex);
                    latest.reset();
                }
            }
        }
        winrt::uninit_apartment();
    }

    static std::optional<dynamic_island::Media> Read(const GlobalSystemMediaTransportControlsSession& session,
                                                      std::wstring& artworkKey, std::string& artwork) {
        if (!session) return {};
        using Status = GlobalSystemMediaTransportControlsSessionPlaybackStatus;
        const auto status = session.GetPlaybackInfo().PlaybackStatus();
        if (status != Status::Playing && status != Status::Paused) return {};
        const auto properties = session.TryGetMediaPropertiesAsync().get();
        const auto timeline = session.GetTimelineProperties();
        dynamic_island::Media media;
        const auto app = Safe(session.SourceAppUserModelId());
        media.session = std::hash<std::wstring>{}(app);
        if (!app.empty() && app.size() < 1024) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, app.data(), static_cast<int>(app.size()), nullptr, 0, nullptr, nullptr);
            media.app.resize(bytes > 0 ? static_cast<std::size_t>(bytes) : 0);
            if (bytes > 0)
                WideCharToMultiByte(CP_UTF8, 0, app.data(), static_cast<int>(app.size()), media.app.data(), bytes, nullptr, nullptr);
        }
        media.title = Safe(properties.Title());
        media.artist = Safe(properties.Artist());
        media.available = !media.title.empty();
        if (!media.available) return {};
        media.id = "system:" + std::to_string(media.session) + ":" + std::to_string(std::hash<std::wstring>{}(media.title));
        // Artwork is read once per track (players may publish it late, so retry while missing).
        const auto key = app + L"\n" + media.title + L"\n" + media.artist;
        if (key != artworkKey || artwork.empty()) { artworkKey = key; artwork = Thumbnail(properties); }
        media.artwork = artwork;
        media.playing = status == Status::Playing;
        media.duration = std::chrono::duration<double>(timeline.EndTime() - timeline.StartTime()).count();
        double position = std::chrono::duration<double>(timeline.Position() - timeline.StartTime()).count();
        // Players report the position only on changes; advance it while playing.
        if (media.playing) position += std::chrono::duration<double>(winrt::clock::now() - timeline.LastUpdatedTime()).count();
        media.position = media.duration > 0 ? std::clamp(position, 0.0, media.duration) : std::max(0.0, position);
        return media;
    }

    static void Control(const GlobalSystemMediaTransportControlsSession& session, const Request& request) {
        if (!session) return;
        using dynamic_island::Action;
        using Status = GlobalSystemMediaTransportControlsSessionPlaybackStatus;
        const auto action = request.action;
        if (action == Action::Toggle) {
            const bool play = request.play >= 0 ? request.play == 1
                : session.GetPlaybackInfo().PlaybackStatus() != Status::Playing;
            if (play) session.TryPlayAsync().get();
            else session.TryPauseAsync().get();
        } else if (action == Action::Previous) session.TrySkipPreviousAsync().get();
        else if (action == Action::Next) session.TrySkipNextAsync().get();
    }
};

SystemMediaSession::SystemMediaSession() : impl_(std::make_shared<Impl>()) {
    worker_ = std::thread([state = impl_] {
        state->Run();
        { std::lock_guard lock(state->mutex); state->done = true; }
        state->wake.notify_all();
    });
}
SystemMediaSession::~SystemMediaSession() {
    std::unique_lock lock(impl_->mutex);
    impl_->stop = true;
    impl_->wake.notify_all();
    // WinRT media calls have no timeout. If the media service hangs, leave the
    // worker (it owns its state) rather than stall exit.
    const bool finished = impl_->wake.wait_for(lock, std::chrono::milliseconds(1500), [this] { return impl_->done; });
    lock.unlock();
    if (finished) worker_.join();
    else worker_.detach();
}
std::optional<dynamic_island::Media> SystemMediaSession::Snapshot() {
    std::lock_guard lock(impl_->mutex);
    impl_->requested = GetTickCount64();
    auto media = impl_->latest;
    // Read up to a second ago: carry a playing position forward to now.
    if (media && media->playing && media->duration > 0)
        media->position = std::min(media->duration, media->position + static_cast<double>(impl_->requested - impl_->latestAt) / 1000);
    // QQ Music and the like publish no timeline: read it from their window.
    if (media && media->duration <= 0)
        if (const auto reading = position_.Read(media->app, media->title, media->playing)) {
            media->position = reading->position;
            media->duration = reading->duration;
        }
    return media;
}
void SystemMediaSession::Control(dynamic_island::Action action) {
    { std::lock_guard lock(impl_->mutex); impl_->controls.push_back({action, -1}); }
    impl_->wake.notify_all();
}
void SystemMediaSession::SetPlaying(bool playing) {
    { std::lock_guard lock(impl_->mutex); impl_->controls.push_back({dynamic_island::Action::Toggle, playing ? 1 : 0}); }
    impl_->wake.notify_all();
}
}
