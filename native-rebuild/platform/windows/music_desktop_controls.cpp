#include "platform/windows/music_desktop_controls.h"
#include "platform/windows/music_metadata.h"
#include "platform/windows/music_provider_http.h"
#include "platform/windows/widget_ui/direct_write_tooltip.h"

#include <windowsx.h>
#ifdef __MINGW32__
// WIDL 11.5 emits both BYTE and boolean as IReference<unsigned char>.
// SMTC uses neither boxed type; suppress only the unused BYTE specialization.
#define ____FIReference_1_BYTE_INTERFACE_DEFINED__
#endif
#include <windows.media.h>
#include <systemmediatransportcontrolsinterop.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <shcore.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace lume::platform::windows {
namespace music_desktop_detail {
namespace Media = ABI::Windows::Media;
namespace Foundation = ABI::Windows::Foundation;
namespace Streams = ABI::Windows::Storage::Streams;
using Microsoft::WRL::ComPtr;
using Action = ui::MusicAction;
constexpr UINT kDrain = WM_APP + 0x6d1;
constexpr wchar_t kMediaOwner[] = L"LumeMusicSMTCOwner";
constexpr std::size_t kArtworkLimit = 2800000;
constexpr std::size_t kMaxPending = 32;

std::wstring Wide(const std::string& input, std::size_t max = 1024) {
    if (input.empty() || input.size() > max || input.find('\0') != std::string::npos) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), nullptr, 0);
    if (!count) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), result.data(), count);
    for (auto& ch : result) if (ch < L' ' || ch == 0x7f) ch = L' ';
    return result;
}

struct HString {
    HSTRING value = nullptr;
    explicit HString(const std::wstring& text) {
        const auto hr = WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), &value);
        if (FAILED(hr)) throw hr;
    }
    ~HString() { WindowsDeleteString(value); }
    HString(const HString&) = delete;
    HString& operator=(const HString&) = delete;
};

void Check(HRESULT hr) { if (FAILED(hr)) throw hr; }

std::wstring Time(double seconds) {
    const auto total = static_cast<unsigned>(std::clamp(seconds, 0.0, 1e9));
    const auto tail = total % 60;
    return std::to_wstring(total / 60) + L":" + (tail < 10 ? L"0" : L"") + std::to_wstring(tail);
}

struct Layout {
    WidgetRect artwork, title, artist, elapsed, remaining, seek, buttons[3];
    float titleSize, artistSize, timeSize, iconSize;
};

Layout Arrange(float w, float h) {
    const int tier = w >= 560 && h >= 280 ? 2 : w >= 400 && h >= 220 ? 1 : 0;
    const float px[] = {18, 26, 36}, py[] = {14, 22, 30}, pb[] = {10, 16, 24};
    const float cover[] = {48, 64, 88}, gap[] = {6, 12, 16};
    const float row[] = {24, 28, 32}, button[] = {40, 48, 64}, spacing[] = {28, 38, 54};
    const float metaGap[] = {12, 16, 20};
    const float titleSize[] = {16, 20, 25}, artistSize[] = {13, 15, 19}, timeSize[] = {11, 12, 14};
    const float iconSize[] = {26, 30, 38};
    const float x = px[tier], y = py[tier], b = button[tier], content = w - 2*x;
    const float by = h - pb[tier] - b, sy = by - gap[tier] - row[tier];
    const float cy = y + (sy - gap[tier] - y - cover[tier]) / 2;
    const float tx = x + cover[tier] + metaGap[tier], tw = w - x - tx - 16;
    const float timeWidth = tier == 2 ? 64.f : 54.f;
    Layout result{{x, cy, cover[tier], cover[tier]}, {tx, cy, tw, cover[tier]/2},
        {tx, cy+cover[tier]/2, tw, cover[tier]/2},
        {x, sy, timeWidth, row[tier]}, {w-x-timeWidth, sy, timeWidth, row[tier]},
        {x+timeWidth+9, sy, content-2*timeWidth-18, row[tier]}, {},
        titleSize[tier], artistSize[tier], timeSize[tier], iconSize[tier]};
    for (int i = 0; i < 3; ++i)
        result.buttons[i] = {w/2-b/2+(i-1)*(b+spacing[tier]), by, b, b};
    return result;
}

bool Contains(WidgetRect r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x+r.width && y < r.y+r.height;
}

std::optional<Action> AppCommandAction(LPARAM value) {
    switch (GET_APPCOMMAND_LPARAM(value)) {
    case APPCOMMAND_MEDIA_PLAY_PAUSE: return Action::Toggle;
    case APPCOMMAND_MEDIA_PLAY: return Action::Play;
    case APPCOMMAND_MEDIA_PAUSE: return Action::Pause;
    case APPCOMMAND_MEDIA_PREVIOUSTRACK: return Action::Previous;
    case APPCOMMAND_MEDIA_NEXTTRACK: return Action::Next;
    default: return {};
    }
}

void Ellipsis(WidgetCanvas& canvas, const std::wstring& text, WidgetRect rect,
              float size, float alpha, bool bold = false) {
    auto value = text;
    if (canvas.MeasureText(value, size, bold) > rect.width) {
        std::size_t low = 0, high = value.size();
        while (low < high) {
            const auto mid = (low+high+1)/2;
            if (canvas.MeasureText(value.substr(0, mid)+L"\u2026", size, bold) <= rect.width) low = mid;
            else high = mid-1;
        }
        if (low && value[low-1] >= 0xd800 && value[low-1] <= 0xdbff) --low;
        value = value.substr(0, low)+L"\u2026";
    }
    canvas.Text(value, rect, size, 0xffffff, alpha, bold);
}

void Triangle(WidgetCanvas& canvas, float cx, float cy, float width, float height,
              bool left, float alpha) {
    for (float x = 0; x < width; x += .5f) {
        const float half = height*.5f*(1-x/width);
        const float px = cx+(left ? width*.5f-x : x-width*.5f);
        canvas.Line(px, cy-half, px, cy+half, 0xffffff, alpha, .6f);
    }
}

void DrawTransportIcon(WidgetCanvas& canvas, WidgetRect r, float iconSize,
                       int index, bool playing, bool enabled) {
    const float alpha = enabled ? 1.f : .3f;
    const float cx = r.x+r.width/2, cy = r.y+r.height/2, size = iconSize*.42f;
    if (index == 1 && playing) {
        canvas.Rounded({cx-size*.65f, cy-size, size*.4f, size*2}, 1, 0xffffff, alpha);
        canvas.Rounded({cx+size*.25f, cy-size, size*.4f, size*2}, 1, 0xffffff, alpha);
    } else if (index == 1) {
        Triangle(canvas, cx+1, cy, size*1.6f, size*2, false, alpha);
    } else {
        Triangle(canvas, cx-size*.55f, cy, size*1.05f, size*1.8f, index == 0, alpha);
        Triangle(canvas, cx+size*.55f, cy, size*1.05f, size*1.8f, index == 0, alpha);
    }
}

// Only in-memory PNG/JPEG data is accepted. No source-path, URL or account I/O.
std::vector<BYTE> ArtworkBytes(const std::string& value) {
    const std::string png = "data:image/png;base64,", jpeg = "data:image/jpeg;base64,";
    const auto prefix = value.compare(0, png.size(), png) == 0 ? png.size() :
        value.compare(0, jpeg.size(), jpeg) == 0 ? jpeg.size() : 0;
    if (!prefix || value.size() > kArtworkLimit || value.size() <= prefix) return {};
    if (value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=", prefix)
        != std::string::npos) return {};
    DWORD length = 0;
    const auto count = static_cast<DWORD>(value.size()-prefix);
    if (!CryptStringToBinaryA(value.data()+prefix, count, CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        nullptr, &length, nullptr, nullptr) || !length || length > 2*1024*1024) return {};
    std::vector<BYTE> bytes(length);
    if (!CryptStringToBinaryA(value.data()+prefix, count, CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        bytes.data(), &length, nullptr, nullptr)) return {};
    bytes.resize(length);
    return bytes;
}

std::vector<BYTE> FetchCover(const music::Track& track, const music::auth::Cancelled& cancelled,
                             const music::auth::Exchange& exchange = music::auth::SendHttp) {
    if (cancelled && cancelled()) return {};
    const auto& url = track.artwork;
    if (url == "music-artwork:" + track.id && !track.source.empty() &&
        track.source.size() <= 1024) {
        const auto png = music::ReadMusicMetadata(Wide(track.source)).artworkPng;
        return {png.begin(), png.end()};
    }
    if (!music::auth::PublicImageUrl(url) || url.empty()) return {};
    music::auth::Provider provider;
    const auto hostEnd = url.find_first_of("/?", 8);
    const std::string_view host(url.data() + 8, hostEnd == url.npos ? url.size() - 8 : hostEnd - 8);
    if (host == "p1.music.126.net" || host == "p2.music.126.net" ||
        host == "p3.music.126.net" || host == "p4.music.126.net")
        provider = music::auth::Provider::Netease;
    else if (host == "y.gtimg.cn") provider = music::auth::Provider::QQ;
    else if (host == "imge.kugou.com" || host == "imgessl.kugou.com")
        provider = music::auth::Provider::Kugou;
    else return {};
    music::auth::HttpRequest request{provider, "GET", url, {}, {}, 1024 * 1024};
    if (!music::auth::ValidHttpRequest(request)) return {};
    try {
        const auto response = exchange(request, cancelled);
        if (response.status != 200 || response.body.size() > request.maxResponse ||
            (response.contentType.compare(0, 9, "image/png") != 0 &&
             response.contentType.compare(0, 10, "image/jpeg") != 0)) return {};
        return {response.body.begin(), response.body.end()};
    } catch (...) { return {}; }
}

class CoverWorker {
public:
    explicit CoverWorker(std::function<void()> wake) : wake_(std::move(wake)) {}
    ~CoverWorker() { Stop(); }
    void Request(const music::Track& track) {
        std::lock_guard lock(mutex_);
        if (stopping_ || track.artwork.empty()) return;
        const auto key = track.artwork + '\0' + track.source;
        if (key == key_) return;
        if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
        key_ = key;
        ++revision_;
        pending_ = track;
        result_.reset();
        ready_.notify_one();
    }
    void Cancel() {
        std::lock_guard lock(mutex_);
        ++revision_;
        key_.clear(); pending_.reset(); result_.reset();
    }
    std::optional<std::vector<BYTE>> Take(const music::Track& track) {
        std::lock_guard lock(mutex_);
        if (!result_ || result_->first != track.artwork + '\0' + track.source) return {};
        auto bytes = std::move(result_->second);
        result_.reset();
        return bytes;
    }
    void Stop() {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return;
            stopping_ = true; ++revision_;
            pending_.reset(); result_.reset();
        }
        ready_.notify_one();
        if (thread_.joinable()) thread_.join();
    }
private:
    void Run() {
        for (;;) {
            music::Track track;
            std::string key;
            std::uint64_t revision = 0;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                if (stopping_) return;
                track = std::move(*pending_);
                pending_.reset();
                key = key_;
                revision = revision_.load();
            }
            auto bytes = FetchCover(track, [this, revision] {
                return stopping_.load() || revision_.load() != revision;
            });
            {
                std::lock_guard lock(mutex_);
                if (stopping_ || revision_ != revision) continue;
                result_ = std::make_pair(std::move(key), std::move(bytes));
            }
            if (wake_) wake_();
        }
    }
    std::function<void()> wake_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::thread thread_;
    std::optional<music::Track> pending_;
    std::optional<std::pair<std::string, std::vector<BYTE>>> result_;
    std::string key_;
    std::atomic<bool> stopping_{false};
    std::atomic<std::uint64_t> revision_{0};
};

std::shared_ptr<const WidgetBitmap> DecodeCover(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return {};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapClipper> clip;
    ComPtr<IWICBitmapScaler> scale;
    ComPtr<IWICFormatConverter> convert;
    UINT w = 0, h = 0;
    GUID format{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetContainerFormat(&format)) ||
        (format != GUID_ContainerFormatPng && format != GUID_ContainerFormatJpeg) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) ||
        !w || !h || w > 2048 || h > 2048) return {};
    const auto side = std::min(w, h);
    WICRect crop{static_cast<INT>((w-side)/2), static_cast<INT>((h-side)/2),
                 static_cast<INT>(side), static_cast<INT>(side)};
    const auto output = std::min(256u, side);
    if (FAILED(factory->CreateBitmapClipper(&clip)) || FAILED(clip->Initialize(frame.Get(), &crop)) ||
        FAILED(factory->CreateBitmapScaler(&scale)) ||
        FAILED(scale->Initialize(clip.Get(), output, output, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&convert)) ||
        FAILED(convert->Initialize(scale.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    auto bitmap = std::make_shared<WidgetBitmap>();
    bitmap->width = bitmap->height = output;
    bitmap->pixels.resize(static_cast<std::size_t>(output)*output);
    if (FAILED(convert->CopyPixels(nullptr, output*4, output*output*4,
        reinterpret_cast<BYTE*>(bitmap->pixels.data())))) return {};
    return bitmap;
}

struct PendingCommand { Action action; double position; std::uint64_t generation; };
struct MediaInbox {
    std::mutex mutex;
    HWND dispatcher = nullptr;
    bool active = false;
    std::uint64_t generation = 0;
    std::deque<PendingCommand> pending;
    void Push(Action action, double position = 0) {
        std::lock_guard lock(mutex);
        if (!active || !dispatcher || pending.size() >= kMaxPending) return;
        const bool wake = pending.empty();
        pending.push_back({action, position, generation});
        if (wake && !PostMessageW(dispatcher, kDrain, 0, 0)) pending.clear();
    }
};

// The handlers own only a refcounted mailbox, never this adapter or its callback.
// In-flight WinRT calls are safe after unsubscription and destruction.
template<class Interface> class MediaHandler : public Interface {
public:
    explicit MediaHandler(std::shared_ptr<MediaInbox> inbox) : inbox_(std::move(inbox)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(Interface) || iid == __uuidof(IAgileObject)) {
            *out = static_cast<Interface*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references_;
        if (!count) delete this;
        return count;
    }
protected:
    virtual ~MediaHandler() = default;
    std::shared_ptr<MediaInbox> inbox_;
private:
    std::atomic<ULONG> references_{1};
};
using ButtonInterface = Foundation::ITypedEventHandler<Media::SystemMediaTransportControls*,
    Media::SystemMediaTransportControlsButtonPressedEventArgs*>;
class ButtonHandler final : public MediaHandler<ButtonInterface> {
public:
    using MediaHandler::MediaHandler;
    HRESULT STDMETHODCALLTYPE Invoke(Media::ISystemMediaTransportControls*,
        Media::ISystemMediaTransportControlsButtonPressedEventArgs* args) override {
        if (!args) return E_POINTER;
        Media::SystemMediaTransportControlsButton button{};
        const auto hr = args->get_Button(&button);
        if (FAILED(hr)) return hr;
        try {
            switch (button) {
            case Media::SystemMediaTransportControlsButton_Play: inbox_->Push(Action::Play); break;
            case Media::SystemMediaTransportControlsButton_Pause: inbox_->Push(Action::Pause); break;
            case Media::SystemMediaTransportControlsButton_Previous: inbox_->Push(Action::Previous); break;
            case Media::SystemMediaTransportControlsButton_Next: inbox_->Push(Action::Next); break;
            default: break;
            }
        } catch (...) { return E_OUTOFMEMORY; }
        return S_OK;
    }
};
using SeekInterface = Foundation::ITypedEventHandler<Media::SystemMediaTransportControls*,
    Media::PlaybackPositionChangeRequestedEventArgs*>;
class SeekHandler final : public MediaHandler<SeekInterface> {
public:
    using MediaHandler::MediaHandler;
    HRESULT STDMETHODCALLTYPE Invoke(Media::ISystemMediaTransportControls*,
        Media::IPlaybackPositionChangeRequestedEventArgs* args) override {
        if (!args) return E_POINTER;
        Foundation::TimeSpan value{};
        const auto hr = args->get_RequestedPlaybackPosition(&value);
        if (FAILED(hr)) return hr;
        try { inbox_->Push(Action::Seek, static_cast<double>(value.Duration)/10000000.0); }
        catch (...) { return E_OUTOFMEMORY; }
        return S_OK;
    }
};
} // namespace music_desktop_detail
using namespace music_desktop_detail;

MusicDesktopState ProjectMusicDesktopState(const music::MusicSnapshot& snapshot) {
    MusicDesktopState state;
    state.session = snapshot.session;
    state.status = snapshot.status;
    const bool valid = snapshot.session != 0 && snapshot.current &&
        !snapshot.current->id.empty() && snapshot.current->id.size() <= 128 &&
        snapshot.current->id.find('\0') == std::string::npos;
    const bool ready = snapshot.status == music::PlaybackStatus::Ready ||
        snapshot.status == music::PlaybackStatus::Playing || snapshot.status == music::PlaybackStatus::Paused;
    if (valid) {
        const auto& track = *snapshot.current;
        state.trackId = track.id;
        state.title = Wide(track.title);
        state.artist = Wide(track.artist);
        state.album = Wide(track.album);
        state.duration = std::isfinite(track.duration) && track.duration > 0 && track.duration <= 1e9 ?
            track.duration : 0;
        state.position = std::isfinite(snapshot.position) ? std::clamp(snapshot.position, 0.0, state.duration) : 0;
    }
    state.canToggle = valid && ready;
    state.canSkip = state.canToggle && !snapshot.queue.empty();
    state.canSeek = state.canToggle && state.duration > 0;
    state.playing = state.canToggle && snapshot.playing;
    if (!ready || !valid) state.title = snapshot.status == music::PlaybackStatus::Error ?
        L"\u97f3\u4e50\u4fe1\u606f\u6682\u4e0d\u53ef\u7528" : L"\u6682\u65e0\u64ad\u653e";
    else if (state.title.empty()) state.title = L"\u672a\u77e5\u6b4c\u66f2";
    return state;
}

std::optional<ui::MusicPageCommand> MusicDesktopCommand(
    const MusicDesktopState& state, Action action, double position) {
    if (!state.canToggle || !state.session || state.trackId.empty()) return {};
    switch (action) {
    case Action::Play: if (state.playing) return {}; break;
    case Action::Pause: if (!state.playing) return {}; break;
    case Action::Toggle: break;
    case Action::Previous: case Action::Next: if (!state.canSkip) return {}; break;
    case Action::Seek:
        if (!state.canSeek || !std::isfinite(position) || position < 0) return {};
        break;
    default: return {};
    }
    ui::MusicPageCommand command;
    command.action = action; command.session = state.session; command.id = state.trackId;
    if (action == Action::Seek) command.position = std::min(position, state.duration);
    return command;
}

struct MusicDesktopControls::Impl {
    Impl(HINSTANCE instance, CommandHandler handler, WidgetHandler widgetHandler, WakeHandler wake)
        : instance(instance), handler(std::move(handler)), widgetHandler(std::move(widgetHandler)),
          coverWorker(std::move(wake)) {
        WidgetWindowPorts ports;
        ports.draw = [this](WidgetCanvas& canvas, float w, float h) { Draw(canvas, w, h); };
        ports.interactiveAt = [this](float x, float y) { return Hit(x, y) >= 0; };
        ports.event = [this](HWND hwnd, UINT msg, WPARAM w, LPARAM l) { return Input(hwnd, msg, w, l); };
        ports.action = [this](WidgetAction action, const widget::Preferences& bounds) {
            prefs = bounds;
            switch (action) {
            case WidgetAction::Close: Hide(); break;
            case WidgetAction::Lock: prefs.locked = !prefs.locked; window->Configure(prefs); break;
            case WidgetAction::Liquid: prefs.frosted = false; window->Configure(prefs); break;
            case WidgetAction::Frosted: prefs.frosted = true; window->Configure(prefs); break;
            case WidgetAction::Reset:
                prefs = widget::Defaults(MusicDesktopControls::Limits);
                prefs.enabled = true; window->Configure(prefs); break;
            default: break;
            }
            const auto callback = this->widgetHandler;
            if (callback) {
                try { callback(action, prefs); } catch (...) {}
            }
        };
        window = std::make_unique<NativeWidgetWindow>(instance, L"Lume Music",
            MusicDesktopControls::Limits, std::move(ports));
    }
    ~Impl() { Shutdown(); }

    static LRESULT CALLBACK DispatchProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && msg == kDrain) { self->Drain(); return 0; }
        if (msg == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return DefWindowProcW(hwnd, msg, w, l);
    }
    static LRESULT CALLBACK WidgetProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l,
                                      UINT_PTR id, DWORD_PTR context) {
        auto* self = reinterpret_cast<Impl*>(context);
        if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, WidgetProc, id);
        if (msg == WM_CANCELMODE) { self->CancelGesture(); self->window->Paint(); }
        if (msg == WM_APPCOMMAND && !self->controls) {
            const auto action = AppCommandAction(l);
            if (action && self->Dispatch(*action)) return TRUE;
        }
        return DefSubclassProc(hwnd, msg, w, l);
    }
    bool Dispatcher() {
        if (dispatcher) return true;
        WNDCLASSW wc{};
        wc.hInstance = instance; wc.lpfnWndProc = DispatchProc; wc.lpszClassName = L"LumeMusicMediaDispatch";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        // SMTC GetForWindow requires a top-level HWND, not HWND_MESSAGE.
        // No WS_VISIBLE, owner, taskbar entry, activation or desktop material.
        dispatcher = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"Lume Music Media", WS_POPUP, 0, 0, 1, 1,
            nullptr, nullptr, instance, this);
        return dispatcher != nullptr;
    }
    void Drain() {
        const auto source = inbox;
        std::deque<PendingCommand> pending;
        {
            std::lock_guard lock(inbox->mutex);
            pending.swap(inbox->pending);
        }
        // One batch is bounded; callbacks may update the snapshot or shut down.
        const auto life = alive;
        for (const auto& item : pending) {
            if (!*life) return;
            if (stopped || !controls || inbox != source) return;
            if (item.generation != generation) continue;
            Dispatch(item.action, item.position);
        }
    }
    bool Dispatch(Action action, double position = 0) {
        const auto command = MusicDesktopCommand(state, action, position);
        if (stopped || !command || !handler) return false;
        const auto callback = handler;
        try { callback(*command); }
        catch (...) { return false; }
        return true;
    }
    void Update(const music::MusicSnapshot& snapshot) {
        if (stopped) return;
        auto next = ProjectMusicDesktopState(snapshot);
        if (next.session != state.session || next.trackId != state.trackId ||
            next.canToggle != state.canToggle || next.canSeek != state.canSeek) {
            ++generation;
            CancelGesture();
            std::lock_guard lock(inbox->mutex);
            inbox->generation = generation;
            inbox->pending.clear();
        }
        const bool changedTrack = next.session != state.session || next.trackId != state.trackId ||
            (snapshot.current && currentTrack &&
                (snapshot.current->artwork != currentTrack->artwork ||
                 snapshot.current->source != currentTrack->source));
        state = std::move(next);
        currentTrack = snapshot.current;
        if (changedTrack) {
            coverBytes.clear();
            cover.reset();
            coverAttempted = false;
            metadataDirty = true;
        }
        if (seeking) preview = std::clamp(preview, 0.0, state.duration);
        const std::string* artwork = snapshot.current && state.canToggle ? &snapshot.current->artwork : nullptr;
        const bool accepted = artwork && artwork->size() <= kArtworkLimit &&
            (artwork->compare(0, 22, "data:image/png;base64,") == 0 ||
             artwork->compare(0, 23, "data:image/jpeg;base64,") == 0);
        const std::string_view nextArtwork = accepted ? std::string_view(*artwork) : std::string_view{};
        if (nextArtwork != artworkData) {
            artworkData = nextArtwork;
            cover.reset(); coverAttempted = false; metadataDirty = true;
        }
        if (snapshot.current && state.canToggle && !accepted && window->Handle()) {
            coverWorker.Request(*snapshot.current);
            if (auto bytes = coverWorker.Take(*snapshot.current)) {
                coverBytes = std::move(*bytes);
                cover = DecodeCover(coverBytes);
                coverAttempted = true;
                metadataDirty = true;
            }
        } else if (!snapshot.current || !state.canToggle || accepted) {
            coverWorker.Cancel();
            coverBytes.clear();
        }
        if (state.title != published.title || state.artist != published.artist ||
            state.album != published.album || state.trackId != published.trackId) metadataDirty = true;
        if (controls) Publish();
        if (window->Handle()) { UpdateTooltips(); window->Paint(); }
    }
    bool Show() {
        if (stopped) return false;
        prefs.enabled = true;
        if (!window->Configure(prefs) || !window->Start()) { prefs.enabled = false; return false; }
        if (!SetWindowSubclass(window->Handle(), WidgetProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
            Hide(); return false;
        }
        if (!tooltip) {
            INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES};
            InitCommonControlsEx(&common);
            tooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, window->Handle(), nullptr, instance, nullptr);
            if (tooltip && !direct_write_tooltip::Attach(tooltip)) { DestroyWindow(tooltip); tooltip = nullptr; }
            if (tooltip) {
                SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 420);
                for (UINT_PTR id = 1; id <= 5; ++id) {
                    TOOLINFOW tool{}; tool.cbSize = sizeof(tool); tool.hwnd = window->Handle();
                    tool.uId = id; tool.uFlags = TTF_SUBCLASS; tool.lpszText = const_cast<wchar_t*>(L"");
                    SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
                }
            }
        }
        UpdateTooltips();
        if (state.canToggle && artworkData.empty()) {
            // Update is called by the shell on a worker completion; Show starts
            // the current cover even when no playback notification follows.
            if (currentTrack) coverWorker.Request(*currentTrack);
        }
        return true;
    }
    void UpdateTooltips() {
        if (!tooltip || !window->Handle()) return;
        const auto layout = Geometry();
        const auto dpi = GetDpiForWindow(window->Handle());
        tooltipText = {
            L"\u4e0a\u4e00\u9996", state.playing ? L"\u6682\u505c" : L"\u64ad\u653e",
            L"\u4e0b\u4e00\u9996", L"\u64ad\u653e\u8fdb\u5ea6",
            state.title + L"\n" + state.artist + L"\n" + state.album};
        SetWindowTextW(window->Handle(), (L"Lume Music: "+state.title).c_str());
        for (UINT_PTR id = 1; id <= 5; ++id) {
            const auto r = id <= 3 ? layout.buttons[id-1] : id == 4 ? layout.seek :
                WidgetRect{layout.title.x, layout.title.y, layout.title.width,
                    layout.title.height+layout.artist.height};
            TOOLINFOW tool{}; tool.cbSize = sizeof(tool); tool.hwnd = window->Handle(); tool.uId = id;
            tool.rect = {static_cast<LONG>(r.x*dpi/96), static_cast<LONG>(r.y*dpi/96),
                static_cast<LONG>((r.x+r.width)*dpi/96), static_cast<LONG>((r.y+r.height)*dpi/96)};
            tool.lpszText = tooltipText[id-1].data();
            SendMessageW(tooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
            SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
        }
    }
    void CancelGesture() {
        pressed = -1; seeking = false;
        if (window && window->Handle() && GetCapture() == window->Handle()) ReleaseCapture();
    }
    void Hide() {
        CancelGesture();
        if (window->Handle()) prefs = window->Bounds();
        prefs.enabled = false;
        if (tooltip) { DestroyWindow(tooltip); tooltip = nullptr; }
        if (window->Handle()) RemoveWindowSubclass(window->Handle(), WidgetProc, 1);
        window->Stop();
        coverWorker.Cancel();
        coverBytes.clear();
        cover.reset(); coverAttempted = false; focus = -1; hover = -1;
    }
    void Disable() {
        {
            std::lock_guard lock(inbox->mutex);
            inbox->active = false; inbox->dispatcher = nullptr; inbox->pending.clear();
        }
        if (controls) {
            controls->put_IsEnabled(false);
            controls->put_PlaybackStatus(Media::MediaPlaybackStatus_Closed);
            if (buttonRegistered) controls->remove_ButtonPressed(buttonToken);
            if (seekRegistered && controls2) controls2->remove_PlaybackPositionChangeRequested(seekToken);
            if (updater) { updater->ClearAll(); updater->Update(); }
        }
        buttonRegistered = seekRegistered = false;
        timeline.Reset(); properties.Reset(); updater.Reset(); controls2.Reset(); controls.Reset();
        if (owner && GetPropW(owner, kMediaOwner) == reinterpret_cast<HANDLE>(this))
            RemovePropW(owner, kMediaOwner);
        if (dispatcher) { DestroyWindow(dispatcher); dispatcher = nullptr; }
        if (roInitialized) { RoUninitialize(); roInitialized = false; }
        owner = nullptr; ownsOwner = false;
    }
    HRESULT Enable(HWND nextOwner, bool internal = false) {
        if (stopped) return result = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        DWORD process = 0;
        if (!internal && (!nextOwner || GetWindowThreadProcessId(nextOwner, &process) != GetCurrentThreadId() ||
            process != GetCurrentProcessId() || GetAncestor(nextOwner, GA_ROOT) != nextOwner ||
            GetParent(nextOwner) == HWND_MESSAGE)) return result = E_INVALIDARG;
        if (!internal && GetPropW(nextOwner, kMediaOwner) &&
            GetPropW(nextOwner, kMediaOwner) != reinterpret_cast<HANDLE>(this))
            return result = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (controls && ((internal && ownsOwner) || (!internal && owner == nextOwner))) return result;
        Disable();
        inbox = std::make_shared<MediaInbox>();
        try {
            const auto hr = RoInitialize(RO_INIT_SINGLETHREADED);
            Check(hr); roInitialized = true;
            if (!Dispatcher()) {
                const auto error = GetLastError();
                Check(error ? HRESULT_FROM_WIN32(error) : E_FAIL);
            }
            if (internal) nextOwner = dispatcher;
            if (!SetPropW(nextOwner, kMediaOwner, reinterpret_cast<HANDLE>(this))) {
                const auto error = GetLastError();
                Check(error ? HRESULT_FROM_WIN32(error) : E_FAIL);
            }
            owner = nextOwner; ownsOwner = internal;
            ComPtr<ISystemMediaTransportControlsInterop> factory;
            HString name(L"Windows.Media.SystemMediaTransportControls");
            Check(RoGetActivationFactory(name.value, IID_PPV_ARGS(factory.GetAddressOf())));
            Check(factory->GetForWindow(nextOwner, IID_PPV_ARGS(controls.GetAddressOf())));
            Check(controls->put_IsEnabled(false));
            Check(controls.As(&controls2));
            Check(controls->get_DisplayUpdater(&updater));
            ComPtr<IInspectable> object;
            HString timelineName(L"Windows.Media.SystemMediaTransportControlsTimelineProperties");
            Check(RoActivateInstance(timelineName.value, &object));
            Check(object.As(&timeline));
            ComPtr<ButtonInterface> buttons; buttons.Attach(new ButtonHandler(inbox));
            ComPtr<SeekInterface> seek; seek.Attach(new SeekHandler(inbox));
            Check(controls->add_ButtonPressed(buttons.Get(), &buttonToken)); buttonRegistered = true;
            Check(controls2->add_PlaybackPositionChangeRequested(seek.Get(), &seekToken)); seekRegistered = true;
            Check(controls->put_IsStopEnabled(false));
            Check(controls->put_IsRecordEnabled(false));
            Check(controls->put_IsFastForwardEnabled(false));
            Check(controls->put_IsRewindEnabled(false));
            Check(controls->put_IsChannelUpEnabled(false));
            Check(controls->put_IsChannelDownEnabled(false));
            {
                std::lock_guard lock(inbox->mutex);
                inbox->dispatcher = dispatcher; inbox->generation = generation; inbox->active = true;
            }
            metadataDirty = true; Publish();
            return result;
        } catch (HRESULT hr) { result = hr; }
          catch (...) { result = E_FAIL; }
        Disable();
        return result;
    }
    void Thumbnail() {
        Check(updater->put_Thumbnail(nullptr));
        const auto bytes = artworkData.empty() ? coverBytes : ArtworkBytes(artworkData);
        // Decode once to reject oversized/malformed images before handing to Windows.
        if (!DecodeCover(bytes)) return;
        ComPtr<IStream> stream;
        Check(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
        ULONG written = 0;
        Check(stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written));
        if (written != bytes.size()) return;
        LARGE_INTEGER zero{}; Check(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
        ComPtr<Streams::IRandomAccessStream> random;
        Check(CreateRandomAccessStreamOverStream(stream.Get(), BSOS_DEFAULT,
            IID_PPV_ARGS(random.GetAddressOf())));
        ComPtr<Streams::IRandomAccessStreamReferenceStatics> factory;
        HString name(L"Windows.Storage.Streams.RandomAccessStreamReference");
        Check(RoGetActivationFactory(name.value, IID_PPV_ARGS(factory.GetAddressOf())));
        ComPtr<Streams::IRandomAccessStreamReference> reference;
        Check(factory->CreateFromStream(random.Get(), &reference));
        Check(updater->put_Thumbnail(reference.Get()));
    }
    void Publish() {
        if (!owner || GetPropW(owner, kMediaOwner) != reinterpret_cast<HANDLE>(this)) {
            result = HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE); Disable(); return;
        }
        try {
            Check(controls->put_IsPlayEnabled(state.canToggle));
            Check(controls->put_IsPauseEnabled(state.canToggle));
            Check(controls->put_IsNextEnabled(state.canSkip));
            Check(controls->put_IsPreviousEnabled(state.canSkip));
            const auto status = !state.canToggle ? Media::MediaPlaybackStatus_Closed :
                state.playing ? Media::MediaPlaybackStatus_Playing :
                state.status == music::PlaybackStatus::Paused ? Media::MediaPlaybackStatus_Paused :
                Media::MediaPlaybackStatus_Stopped;
            Check(controls->put_PlaybackStatus(status));
            if (!state.canToggle) {
                Check(controls->put_IsEnabled(false));
                Check(updater->ClearAll()); Check(updater->Update()); metadataDirty = true;
            } else {
                if (metadataDirty) {
                    Check(updater->put_Type(Media::MediaPlaybackType_Music));
                    properties.Reset();
                    Check(updater->get_MusicProperties(&properties));
                    HString title(state.title), artist(state.artist), id(Wide(state.trackId, 128));
                    Check(properties->put_Title(title.value)); Check(properties->put_Artist(artist.value));
                    Check(updater->put_AppMediaId(id.value));
                    ComPtr<Media::IMusicDisplayProperties2> album;
                    if (SUCCEEDED(properties.As(&album))) {
                        HString text(state.album); Check(album->put_AlbumTitle(text.value));
                    }
                    Thumbnail();
                    Check(updater->Update()); published = state; metadataDirty = false;
                }
                Check(controls->put_IsEnabled(true));
            }
            const Foundation::TimeSpan zero{0};
            const Foundation::TimeSpan end{static_cast<INT64>(state.duration*10000000)};
            Check(timeline->put_StartTime(zero)); Check(timeline->put_MinSeekTime(zero));
            Check(timeline->put_EndTime(end)); Check(timeline->put_MaxSeekTime(state.canSeek ? end : zero));
            Check(timeline->put_Position({static_cast<INT64>(state.position*10000000)}));
            Check(controls2->UpdateTimelineProperties(timeline.Get()));
            result = S_OK;
        } catch (HRESULT hr) { result = hr; Disable(); }
          catch (...) { result = E_FAIL; Disable(); }
    }
    void Shutdown() {
        if (stopped) return;
        stopped = true; *alive = false;
        coverWorker.Stop();
        Disable(); Hide();
        artworkData.clear(); handler = {}; widgetHandler = {}; state = {};
    }
    Layout Geometry() const {
        RECT rect{}; const auto hwnd = window->Handle();
        GetClientRect(hwnd, &rect);
        const auto dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
        return Arrange(static_cast<float>(rect.right)*96/std::max(1u, dpi),
            static_cast<float>(rect.bottom)*96/std::max(1u, dpi));
    }
    bool Enabled(int control) const { return control == 3 ? state.canSeek :
        control == 1 ? state.canToggle : (control == 0 || control == 2) && state.canSkip; }
    int Hit(float x, float y) const {
        const auto layout = Geometry();
        for (int i = 0; i < 3; ++i) if (Contains(layout.buttons[i], x, y)) return i;
        return Contains(layout.seek, x, y) ? 3 : -1;
    }
    void SeekAt(float x) {
        const auto rect = Geometry().seek;
        preview = std::clamp(static_cast<double>((x-rect.x)/rect.width), 0.0, 1.0)*state.duration;
    }
    void Activate(int control) {
        if (control == 3) Dispatch(Action::Seek, preview);
        else if (control >= 0 && control < 3) Dispatch(control == 0 ? Action::Previous :
            control == 1 ? Action::Toggle : Action::Next);
    }
    std::optional<LRESULT> Input(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_SIZE || msg == WM_DPICHANGED) { UpdateTooltips(); return {}; }
        const auto dpi = std::max(1u, GetDpiForWindow(hwnd));
        const float x = static_cast<float>(GET_X_LPARAM(l))*96/dpi;
        const float y = static_cast<float>(GET_Y_LPARAM(l))*96/dpi;
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
            const auto hit = Hit(x, y);
            if (hit < 0) return {};
            if (!Enabled(hit)) return 0;
            SetFocus(hwnd); focus = pressed = hit; gestureGeneration = generation;
            SetCapture(hwnd);
            seeking = hit == 3;
            if (seeking) SeekAt(x);
            window->Paint(); return 0;
        }
        if (msg == WM_MOUSEMOVE) {
            const int hit = Hit(x, y);
            if (seeking) { SeekAt(x); window->Paint(); return 0; }
            if (hover != hit) { hover = hit; window->Paint(); }
            return {};
        }
        if (msg == WM_MOUSELEAVE) { hover = -1; window->Paint(); return {}; }
        if (msg == WM_LBUTTONUP && pressed >= 0) {
            const int control = pressed;
            const bool accept = gestureGeneration == generation && Enabled(control) &&
                (seeking || Hit(x, y) == control);
            if (seeking) SeekAt(x);
            CancelGesture(); window->Paint();
            if (accept) Activate(control);
            return 0;
        }
        if (msg == WM_CAPTURECHANGED || msg == WM_KILLFOCUS) {
            CancelGesture();
            if (msg == WM_KILLFOCUS) focus = -1;
            window->Paint(); return {};
        }
        if (msg == WM_KEYDOWN) {
            if (w == VK_ESCAPE) { CancelGesture(); window->Paint(); return 0; }
            if (w == VK_TAB) {
                const int step = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
                if (focus < 0 && step < 0) focus = 0;
                for (int i = 0; i < 4; ++i) {
                    focus = (focus+step+4)%4;
                    if (Enabled(focus)) break;
                }
                window->Paint(); return 0;
            }
            if ((l & (1LL << 30)) != 0) return 0;
            if ((w == VK_SPACE || w == VK_RETURN) && focus != 3) {
                Activate(focus < 0 ? 1 : focus); return 0;
            }
            if (focus == 3 && state.canSeek) {
                if (w == VK_LEFT || w == VK_RIGHT || w == VK_HOME || w == VK_END) {
                    const double position = w == VK_HOME ? 0 : w == VK_END ? state.duration :
                        std::clamp(state.position+(w == VK_LEFT ? -5 : 5), 0.0, state.duration);
                    Dispatch(Action::Seek, position); return 0;
                }
            }
        }
        return {};
    }
    void Draw(WidgetCanvas& canvas, float w, float h) {
        if (stopped || !std::isfinite(w) || !std::isfinite(h) || w < 320 || h < 152) return;
        const auto layout = Arrange(w, h);
        if (!coverAttempted) {
            cover = DecodeCover(artworkData.empty() ? coverBytes : ArtworkBytes(artworkData));
            coverAttempted = true;
        }
        const auto art = layout.artwork;
        canvas.Rounded(art, 6, 0xffffff, .08f);
        if (cover) canvas.ImageBitmap(cover, art);
        else {
            const float x = art.x+art.width*.45f, y = art.y+art.height*.28f;
            canvas.Line(x, y, x, y+art.height*.4f, 0xffffff, .5f, 2);
            canvas.Line(x, y, x+art.width*.22f, y+art.height*.1f, 0xffffff, .5f, 2);
            canvas.Rounded({x-8, y+art.height*.35f, 9, 6}, 3, 0xffffff, .5f);
        }
        Ellipsis(canvas, state.title.empty() ? L"\u6682\u65e0\u64ad\u653e" : state.title,
            layout.title, layout.titleSize, 1, true);
        Ellipsis(canvas, state.canToggle ? (state.artist.empty() ? state.album : state.artist) : L"",
            layout.artist, layout.artistSize, .7f);
        const auto position = seeking ? preview : state.position;
        const auto elapsed = state.canSeek ? Time(position) : L"--:--";
        const auto remaining = state.canSeek ? L"-"+Time(state.duration-position) : L"--:--";
        const auto textWidth = std::max(canvas.MeasureText(elapsed, layout.timeSize),
            canvas.MeasureText(remaining, layout.timeSize));
        const float timeSize = layout.timeSize*std::min(1.f, layout.elapsed.width/std::max(1.f, textWidth));
        canvas.Text(elapsed, layout.elapsed, timeSize, 0xffffff, .72f);
        canvas.Text(remaining, layout.remaining, timeSize, 0xffffff, .72f, false, WidgetTextAlign::Right);
        const auto seek = layout.seek;
        const float progress = state.canSeek ? static_cast<float>(position/state.duration) : 0;
        canvas.Rounded({seek.x, seek.y+seek.height/2-2.5f, seek.width, 5}, 2.5f, 0xffffff, .25f);
        if (progress > 0) canvas.Rounded({seek.x, seek.y+seek.height/2-2.5f, seek.width*progress, 5}, 2.5f, 0xffffff);
        if (state.canSeek) canvas.Rounded({seek.x+seek.width*progress-5, seek.y+seek.height/2-5, 10, 10}, 5, 0xffffff);
        if (focus == 3) canvas.Line(seek.x, seek.y+seek.height-1, seek.x+seek.width, seek.y+seek.height-1, 0xffffff);
        for (int i = 0; i < 3; ++i)
            DrawTransportIcon(canvas, layout.buttons[i], layout.iconSize, i, state.playing, Enabled(i));
    }

    HINSTANCE instance;
    CommandHandler handler;
    WidgetHandler widgetHandler;
    std::unique_ptr<NativeWidgetWindow> window;
    widget::Preferences prefs = widget::Defaults(MusicDesktopControls::Limits);
    MusicDesktopState state, published;
    std::string artworkData;
    std::optional<music::Track> currentTrack;
    CoverWorker coverWorker;
    std::vector<BYTE> coverBytes;
    std::shared_ptr<const WidgetBitmap> cover;
    std::array<std::wstring, 5> tooltipText;
    bool coverAttempted = false, metadataDirty = true, stopped = false;
    bool seeking = false;
    int pressed = -1, hover = -1, focus = -1;
    double preview = 0;
    std::uint64_t generation = 0, gestureGeneration = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::shared_ptr<MediaInbox> inbox = std::make_shared<MediaInbox>();
    HWND owner = nullptr, dispatcher = nullptr, tooltip = nullptr;
    ComPtr<Media::ISystemMediaTransportControls> controls;
    ComPtr<Media::ISystemMediaTransportControls2> controls2;
    ComPtr<Media::ISystemMediaTransportControlsDisplayUpdater> updater;
    ComPtr<Media::IMusicDisplayProperties> properties;
    ComPtr<Media::ISystemMediaTransportControlsTimelineProperties> timeline;
    EventRegistrationToken buttonToken{}, seekToken{};
    bool buttonRegistered = false, seekRegistered = false, roInitialized = false;
    bool ownsOwner = false;
    HRESULT result = S_FALSE;
};

struct MusicCoverResolver::Impl {
    explicit Impl(std::function<void()> wake) : worker(std::move(wake)) {}
    CoverWorker worker;
    std::string key, url;
};
MusicCoverResolver::MusicCoverResolver(std::function<void()> wake) : impl_(std::make_unique<Impl>(std::move(wake))) {}
MusicCoverResolver::~MusicCoverResolver() { impl_->worker.Stop(); }
std::string MusicCoverResolver::Resolve(const music::Track& track) {
    if (track.artwork.rfind("data:image/", 0) == 0) return track.artwork;
    if (track.artwork.empty()) return {};
    const auto key = track.artwork + '\0' + track.source;
    if (key == impl_->key) return impl_->url;
    impl_->worker.Request(track);
    const auto bytes = impl_->worker.Take(track);
    if (!bytes) return {};
    impl_->key = key;
    impl_->url.clear();
    DWORD length = 0;
    if (!bytes->empty() && bytes->size() <= 2 * 1024 * 1024 &&
        CryptBinaryToStringA(bytes->data(), static_cast<DWORD>(bytes->size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                             nullptr, &length)) {
        std::string text(length, '\0');
        if (CryptBinaryToStringA(bytes->data(), static_cast<DWORD>(bytes->size()),
                                 CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &length)) {
            text.resize(length);
            const bool png = bytes->size() > 4 && (*bytes)[0] == 0x89 && (*bytes)[1] == 'P';
            impl_->url = std::string(png ? "data:image/png;base64," : "data:image/jpeg;base64,") + text;
        }
    }
    return impl_->url;
}

MusicDesktopControls::MusicDesktopControls(HINSTANCE instance, CommandHandler callback,
                                            WidgetHandler widgetCallback, WakeHandler wake)
    : impl_(std::make_unique<Impl>(instance, std::move(callback), std::move(widgetCallback), std::move(wake))) {}
MusicDesktopControls::~MusicDesktopControls() = default;
void MusicDesktopControls::Update(const music::MusicSnapshot& snapshot) { impl_->Update(snapshot); }
bool MusicDesktopControls::Configure(const widget::Preferences& prefs) {
    if (impl_->stopped || !impl_->window->Configure(prefs)) return false;
    impl_->prefs = prefs; return true;
}
bool MusicDesktopControls::Show() { return impl_->Show(); }
void MusicDesktopControls::Hide() { impl_->Hide(); }
bool MusicDesktopControls::Visible() const { return impl_->window->Handle() != nullptr; }
HWND MusicDesktopControls::Handle() const { return impl_->window->Handle(); }
widget::Preferences MusicDesktopControls::Bounds() const {
    return Handle() ? impl_->window->Bounds() : impl_->prefs;
}
const MusicDesktopState& MusicDesktopControls::State() const { return impl_->state; }
HRESULT MusicDesktopControls::EnableSystemMediaControls(HWND owner) { return impl_->Enable(owner); }
HRESULT MusicDesktopControls::EnableSystemMediaControls() { return impl_->Enable(nullptr, true); }
void MusicDesktopControls::DisableSystemMediaControls() { impl_->Disable(); }
bool MusicDesktopControls::SystemMediaControlsEnabled() const { return impl_->controls != nullptr; }
HRESULT MusicDesktopControls::SystemMediaResult() const { return impl_->result; }
void MusicDesktopControls::Shutdown() { impl_->Shutdown(); }
void MusicDesktopControls::Draw(WidgetCanvas& canvas, float w, float h) { impl_->Draw(canvas, w, h); }
bool MusicDesktopControls::PreTranslateMessage(const MSG& msg) {
    if (!Handle() || msg.hwnd != Handle() || msg.message != WM_KEYDOWN || msg.wParam != VK_TAB) return false;
    return impl_->Input(msg.hwnd, msg.message, msg.wParam, msg.lParam).has_value();
}
std::optional<LRESULT> MusicDesktopControls::ProcessMessage(HWND hwnd, UINT msg, WPARAM, LPARAM l) {
    if (msg == WM_NCDESTROY && hwnd == impl_->owner) { impl_->Disable(); return {}; }
    if (impl_->stopped || msg != WM_APPCOMMAND || impl_->controls) return {};
    DWORD process = 0;
    if (!hwnd || GetWindowThreadProcessId(hwnd, &process) != GetCurrentThreadId() ||
        process != GetCurrentProcessId()) return {};
    const auto action = AppCommandAction(l);
    return action && impl_->Dispatch(*action) ? std::optional<LRESULT>(TRUE) : std::nullopt;
}

} // namespace lume::platform::windows
