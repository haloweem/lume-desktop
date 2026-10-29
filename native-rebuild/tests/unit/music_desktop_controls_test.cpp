// This standalone test includes the implementation to check its private COM
// mailbox without exporting test hooks or activating a real media session.
#include "platform/windows/music_desktop_controls.cpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#include <filesystem>

using namespace lume::platform::windows;
using lume::music::MusicSnapshot;
using lume::music::PlaybackStatus;
using lume::ui::MusicAction;

namespace {
void Expect(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
MusicSnapshot Sample() {
    MusicSnapshot snapshot;
    snapshot.session = 42; snapshot.revision = 1; snapshot.status = PlaybackStatus::Playing;
    snapshot.playing = true;
    lume::music::Track track;
    track.id = "native-track"; track.title = "Lume Music - \xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b\xe8\xaf\x95";
    track.artist = "Artist"; track.album = "Album"; track.duration = 180;
    track.source = "must-not-open.wav";
    snapshot.current = track; snapshot.queue.push_back(track); snapshot.position = 25;
    return snapshot;
}
HWND OwnedMediaHost() {
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM value) -> BOOL {
        wchar_t name[64]{};
        if (GetClassNameW(hwnd, name, 64) && std::wstring(name) == L"LumeMusicMediaDispatch" &&
            GetPropW(hwnd, kMediaOwner)) {
            *reinterpret_cast<HWND*>(value) = hwnd; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}
void SaveBitmap(const WidgetBitmap& bitmap, const std::filesystem::path& path) {
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file)+sizeof(info);
    file.bfSize = file.bfOffBits+static_cast<DWORD>(bitmap.pixels.size()*4);
    info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(bitmap.width);
    info.biHeight = -static_cast<LONG>(bitmap.height); info.biPlanes = 1; info.biBitCount = 32;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    // Composite the production transparent foreground over a neutral dark
    // backdrop; this does not claim to verify live desktop glass.
    auto pixels = bitmap.pixels;
    for (auto& p : pixels) {
        const auto a = p >> 24;
        const auto background = 48*(255-a)/255;
        p = 0xff000000u | (std::min(255u, ((p>>16)&255)+background)<<16) |
            (std::min(255u, ((p>>8)&255)+background)<<8) | std::min(255u, (p&255)+background);
    }
    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()*4));
    Expect(output.good(), "离屏位图写入失败");
}

class ButtonArgs final : public Media::ISystemMediaTransportControlsButtonPressedEventArgs {
public:
    Media::SystemMediaTransportControlsButton button = Media::SystemMediaTransportControlsButton_Play;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Button(Media::SystemMediaTransportControlsButton* out) override {
        *out = button; return S_OK;
    }
};
class SeekArgs final : public Media::IPlaybackPositionChangeRequestedEventArgs {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_RequestedPlaybackPosition(Foundation::TimeSpan* out) override {
        out->Duration = 125000000; return S_OK;
    }
};
}

int main(int argc, char** argv) {
    Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM 初始化失败");
    {
        auto sample = Sample();
        const auto state = ProjectMusicDesktopState(sample);
        Expect(state.session == 42 && state.trackId == "native-track" && state.position == 25 &&
            state.duration == 180 && state.canSeek && state.playing, "当前状态投影错误");
        Expect(state.title.find(L"\u4e2d\u6587") != std::wstring::npos, "中文元数据转换失败");
        auto command = MusicDesktopCommand(state, MusicAction::Seek, 400);
        Expect(command && command->position == 180 && command->session == 42 &&
            command->id == "native-track", "跳转未夹紧或丢失歌曲身份");
        Expect(!MusicDesktopCommand(state, MusicAction::Seek, -1) &&
            !MusicDesktopCommand(state, MusicAction::Seek, std::numeric_limits<double>::quiet_NaN()) &&
            !MusicDesktopCommand(state, MusicAction::Login) &&
            !MusicDesktopCommand(state, MusicAction::ClearQueue), "非法操作未拒绝");
        Expect(!MusicDesktopCommand(state, MusicAction::Play) &&
            MusicDesktopCommand(state, MusicAction::Pause), "播放与暂停幂等映射错误");
        sample.session = 0;
        Expect(!ProjectMusicDesktopState(sample).canToggle, "零会话不得发出通配操作");
        sample = Sample(); sample.current->duration = std::numeric_limits<double>::infinity();
        Expect(!ProjectMusicDesktopState(sample).canSeek, "未知时长不得跳转");
        sample = Sample(); sample.current->title.assign(1025, 'x');
        Expect(ProjectMusicDesktopState(sample).title.size() < 1025, "元数据超过上限");
        sample = Sample(); sample.position = std::numeric_limits<double>::quiet_NaN();
        Expect(ProjectMusicDesktopState(sample).position == 0, "非法进度未规范化");
        sample.status = PlaybackStatus::Error;
        Expect(!ProjectMusicDesktopState(sample).canToggle, "错误状态仍可操作");

        // Hidden, test-owned HWND. No Show() and no real SMTC in the default run.
        const HWND owner = CreateWindowExW(0, L"STATIC", L"T13 isolated owner", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Expect(owner && !IsWindowVisible(owner), "隔离隐藏 HWND 创建失败");
        if (argc == 2 && std::string(argv[1]) == "--smtc-smoke") {
            const auto probe = [](HRESULT hr, const char* step) {
                std::cout << step << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr) << '\n';
                return hr;
            };
            const auto initialized = RoInitialize(RO_INIT_SINGLETHREADED);
            Expect(SUCCEEDED(initialized), "探测 WinRT 初始化失败");
            ComPtr<ISystemMediaTransportControlsInterop> diagnosticFactory;
            HString diagnosticName(L"Windows.Media.SystemMediaTransportControls");
            Expect(SUCCEEDED(probe(RoGetActivationFactory(diagnosticName.value,
                IID_PPV_ARGS(diagnosticFactory.GetAddressOf())), "RoGetActivationFactory")), "探测工厂失败");
            ComPtr<Media::ISystemMediaTransportControls> diagnostic;
            Expect(SUCCEEDED(probe(diagnosticFactory->GetForWindow(owner,
                IID_PPV_ARGS(diagnostic.GetAddressOf())), "GetForWindow")), "探测窗口绑定失败");
            probe(diagnostic->put_IsEnabled(false), "IsEnabled(false)");
            probe(diagnostic->put_IsStopEnabled(false), "IsStopEnabled(false)");
            probe(diagnostic->put_IsRecordEnabled(false), "IsRecordEnabled(false)");
            probe(diagnostic->put_IsFastForwardEnabled(false), "IsFastForwardEnabled(false)");
            probe(diagnostic->put_IsRewindEnabled(false), "IsRewindEnabled(false)");
            probe(diagnostic->put_IsChannelUpEnabled(false), "IsChannelUpEnabled(false)");
            probe(diagnostic->put_IsChannelDownEnabled(false), "IsChannelDownEnabled(false)");
            ComPtr<Media::ISystemMediaTransportControls2> diagnostic2;
            probe(diagnostic.As(&diagnostic2), "ISystemMediaTransportControls2");
            auto box = std::make_shared<MediaInbox>();
            ComPtr<ButtonInterface> buttonProbe; buttonProbe.Attach(new ButtonHandler(box));
            ComPtr<SeekInterface> seekProbe; seekProbe.Attach(new SeekHandler(box));
            EventRegistrationToken bt{}, st{};
            const auto br = probe(diagnostic->add_ButtonPressed(buttonProbe.Get(), &bt), "add_ButtonPressed");
            const auto sr = probe(diagnostic2->add_PlaybackPositionChangeRequested(seekProbe.Get(), &st),
                "add_PlaybackPositionChangeRequested");
            if (SUCCEEDED(br)) diagnostic->remove_ButtonPressed(bt);
            if (SUCCEEDED(sr)) diagnostic2->remove_PlaybackPositionChangeRequested(st);
            ComPtr<IInspectable> timelineProbe;
            HString timeName(L"Windows.Media.SystemMediaTransportControlsTimelineProperties");
            probe(RoActivateInstance(timeName.value, &timelineProbe), "RoActivateInstance(timeline)");
            ComPtr<Media::ISystemMediaTransportControlsTimelineProperties> time;
            probe(timelineProbe.As(&time), "timeline interface");
            probe(time->put_StartTime({0}), "StartTime");
            probe(time->put_EndTime({0}), "EndTime");
            probe(time->put_MinSeekTime({0}), "MinSeekTime");
            probe(time->put_MaxSeekTime({0}), "MaxSeekTime");
            probe(time->put_Position({0}), "Position");
            probe(diagnostic2->UpdateTimelineProperties(time.Get()), "UpdateTimelineProperties");
            probe(diagnostic->put_PlaybackStatus(Media::MediaPlaybackStatus_Closed), "PlaybackStatus(Closed)");
            ComPtr<Media::ISystemMediaTransportControlsDisplayUpdater> display;
            probe(diagnostic->get_DisplayUpdater(&display), "DisplayUpdater");
            probe(display->put_Type(Media::MediaPlaybackType_Music), "Type(Music)");
            ComPtr<Media::IMusicDisplayProperties> properties;
            probe(display->get_MusicProperties(&properties), "MusicProperties");
            probe(display->ClearAll(), "ClearAll");
            probe(display->Update(), "Update");
            properties.Reset(); display.Reset(); time.Reset(); timelineProbe.Reset(); diagnostic2.Reset();
            diagnostic.Reset(); diagnosticFactory.Reset(); RoUninitialize();
            MusicDesktopControls dormant(GetModuleHandleW(nullptr), {});
            const auto registered = dormant.EnableSystemMediaControls(owner);
            std::cout << "SMTC 注册 HRESULT=0x" << std::hex << static_cast<unsigned long>(registered) << '\n';
            Expect(SUCCEEDED(registered), "真实 SMTC 空态注册失败");
            MusicDesktopControls second(GetModuleHandleW(nullptr), {});
            Expect(second.EnableSystemMediaControls(owner) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),
                "同一 HWND 被第二个组件重复注册");
            ComPtr<ISystemMediaTransportControlsInterop> factory;
            HString name(L"Windows.Media.SystemMediaTransportControls");
            Expect(SUCCEEDED(RoGetActivationFactory(name.value, IID_PPV_ARGS(factory.GetAddressOf()))),
                "SMTC 工厂查询失败");
            ComPtr<Media::ISystemMediaTransportControls> system;
            Expect(SUCCEEDED(factory->GetForWindow(owner, IID_PPV_ARGS(system.GetAddressOf()))),
                "SMTC 关联窗口查询失败");
            boolean enabled = true;
            Expect(SUCCEEDED(system->get_IsEnabled(&enabled)) && !enabled && !dormant.Handle(),
                "空态注册不应启用系统媒体会话或显示组件");
            dormant.DisableSystemMediaControls();
            Expect(!dormant.SystemMediaControlsEnabled() &&
                SUCCEEDED(system->get_IsEnabled(&enabled)) && !enabled, "撤销后系统媒体控制仍启用");
            Expect(SUCCEEDED(dormant.EnableSystemMediaControls(owner)), "SMTC 二次注册失败");
            dormant.Update(Sample());
            Expect(SUCCEEDED(dormant.SystemMediaResult()) && dormant.SystemMediaControlsEnabled(),
                "真实 SMTC 元数据发布失败");
            Expect(SUCCEEDED(system->get_IsEnabled(&enabled)) && enabled, "有效当前曲目未发布到 SMTC");
            ComPtr<Media::ISystemMediaTransportControlsDisplayUpdater> readback;
            ComPtr<Media::IMusicDisplayProperties> musicReadback;
            Expect(SUCCEEDED(system->get_DisplayUpdater(&readback)) &&
                SUCCEEDED(readback->get_MusicProperties(&musicReadback)), "发布的元数据不可读");
            HSTRING title = nullptr;
            Expect(SUCCEEDED(musicReadback->get_Title(&title)), "发布的标题不可读");
            const auto titleText = std::wstring(WindowsGetStringRawBuffer(title, nullptr));
            WindowsDeleteString(title);
            Expect(titleText == ProjectMusicDesktopState(Sample()).title, "SMTC 标题与原生状态不一致");
            auto artworkSample = Sample();
            artworkSample.current->artwork = "data:image/png;base64,"
                "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=";
            dormant.Update(artworkSample);
            Expect(SUCCEEDED(dormant.SystemMediaResult()), "真实 SMTC 内存封面发布失败");
            ComPtr<Streams::IRandomAccessStreamReference> thumbnail;
            Expect(SUCCEEDED(readback->get_Thumbnail(&thumbnail)) && thumbnail,
                "系统媒体缩略图为空，封面并未真正发布");
            thumbnail.Reset();
            const auto foreground = GetForegroundWindow();
            dormant.DisableSystemMediaControls();
            Expect(SUCCEEDED(dormant.EnableSystemMediaControls()), "独立隐藏宿主注册失败");
            const auto host = OwnedMediaHost();
            Expect(host && !IsWindowVisible(host) && GetAncestor(host, GA_ROOT) == host &&
                GetForegroundWindow() == foreground && !dormant.Handle(),
                "媒体宿主显示、抢焦点或依赖桌面组件");
            dormant.Hide();
            DestroyWindow(owner);
            Expect(dormant.SystemMediaControlsEnabled() && IsWindow(host),
                "管理窗口销毁或组件隐藏导致媒体宿主失效");
            dormant.Update({});
            Expect(SUCCEEDED(dormant.SystemMediaResult()), "清空当前曲目失败");
            dormant.Update(Sample());
            Expect(SUCCEEDED(dormant.SystemMediaResult()), "空态恢复后元数据发布失败");
            for (int i = 0; i < 5; ++i) {
                dormant.DisableSystemMediaControls();
                Expect(SUCCEEDED(dormant.EnableSystemMediaControls()), "重复启停泄漏注册资源");
            }
            const auto finalHost = OwnedMediaHost();
            dormant.Shutdown();
            Expect(!dormant.SystemMediaControlsEnabled() && !dormant.Show() && !IsWindow(finalHost),
                "退出后仍持有 SMTC 或隐藏宿主");
            musicReadback.Reset(); readback.Reset();
            system.Reset(); factory.Reset();
            Expect(!OwnedMediaHost(), "关闭后仍有本线程媒体宿主残留");
            std::cout << "T13: 真实 SMTC 注册、元数据回读、撤销、独立隐藏宿主及退出通过；未显示组件或播放音频\n";
            CoUninitialize();
            return 0;
        }
        std::vector<lume::ui::MusicPageCommand> commands;
        MusicDesktopControls controls(GetModuleHandleW(nullptr),
            [&](const auto& value) { commands.push_back(value); });
        controls.Update(Sample());
        Expect(!controls.Handle() && !controls.Visible() && !controls.SystemMediaControlsEnabled(),
            "Update 不得创建桌面窗口或注册媒体控制");
        Expect(controls.EnableSystemMediaControls(nullptr) == E_INVALIDARG, "无效 HWND 未拒绝");
        const auto media = [&](int action) {
            return controls.ProcessMessage(owner, WM_APPCOMMAND, 0, static_cast<LPARAM>(action)<<16);
        };
        Expect(media(APPCOMMAND_MEDIA_NEXTTRACK) == TRUE && commands.size() == 1 &&
            commands.back().id == "native-track" && commands.back().session == 42, "媒体键命令未携带身份");
        Expect(!media(APPCOMMAND_VOLUME_UP) && commands.size() == 1, "抢占了无关媒体键");
        sample = Sample(); sample.session = 43; sample.current->id = "new-track";
        controls.Update(sample);
        media(APPCOMMAND_MEDIA_PLAY_PAUSE);
        Expect(commands.back().session == 43 && commands.back().id == "new-track", "命令仍使用旧身份");
        controls.Hide();
        Expect(!controls.Visible() && media(APPCOMMAND_MEDIA_PAUSE), "隐藏组件不应停止本地媒体操作");
        controls.Update({});
        Expect(!media(APPCOMMAND_MEDIA_NEXTTRACK), "空态抢占媒体键");

        auto inbox = std::make_shared<MediaInbox>();
        inbox->dispatcher = owner; inbox->active = true; inbox->generation = 12;
        ComPtr<ButtonInterface> handler; handler.Attach(new ButtonHandler(inbox));
        ButtonArgs args;
        std::thread worker([&] {
            for (int i = 0; i < 1000; ++i) Expect(SUCCEEDED(handler->Invoke(nullptr, &args)), "事件调用失败");
        });
        worker.join();
        Expect(inbox->pending.size() == 32 && inbox->pending.front().generation == 12,
            "事件队列未限制或丢失代数");
        MSG message{}; unsigned wakes = 0;
        while (PeekMessageW(&message, owner, kDrain, kDrain, PM_REMOVE)) ++wakes;
        Expect(wakes == 1, "一个事件批次发送了过多唤醒消息");
        inbox->pending.clear();
        ComPtr<SeekInterface> seeker; seeker.Attach(new SeekHandler(inbox));
        SeekArgs seekArgs;
        Expect(SUCCEEDED(seeker->Invoke(nullptr, &seekArgs)) &&
            inbox->pending.front().position == 12.5, "SMTC 百纳秒时间转换错误");
        inbox->active = false; inbox->pending.clear(); inbox->dispatcher = nullptr;
        Expect(SUCCEEDED(handler->Invoke(nullptr, &args)) && inbox->pending.empty(),
            "撤销后迟到的回调仍投递");
        void* agile = nullptr;
        Expect(SUCCEEDED(handler->QueryInterface(__uuidof(IAgileObject), &agile)), "事件代理不支持跨线程");
        static_cast<IUnknown*>(agile)->Release();
        Expect(handler->Invoke(nullptr, nullptr) == E_POINTER, "无效 WinRT 参数未拒绝");
        Expect(ArtworkBytes("https://example.invalid/cover.png").empty() &&
            ArtworkBytes("file:///C:/private.png").empty() &&
            ArtworkBytes("data:image/png;base64,%%%%").empty(), "封面触发了越界资源读取");

        // WIC-generated 8x8 BMP is deliberately not an allowed artwork format.
        Expect(!DecodeCover({1, 2, 3, 4}), "坏图片未拒绝");
        const auto png = std::string("data:image/png;base64,") +
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=";
        Expect(!ArtworkBytes(png).empty(), "合法 base64 被拒绝");
        const auto image = DecodeCover(ArtworkBytes(png));
        Expect(image && image->width == 1, "内存 PNG 解码失败");
        const auto bytes = ArtworkBytes(png);
        for (const auto& [url, provider] : {
                 std::pair{"https://p1.music.126.net/song.jpg?param=256y256", lume::music::auth::Provider::Netease},
                 std::pair{"https://y.gtimg.cn/music/photo_new/cover.jpg", lume::music::auth::Provider::QQ},
                 std::pair{"https://imgessl.kugou.com/stdmusic/cover.jpg", lume::music::auth::Provider::Kugou}}) {
            auto track = *Sample().current;
            track.artwork = url;
            bool requested = false;
            const auto fetched = FetchCover(track, {}, [&](const auto& request, const auto&) {
                requested = request.provider == provider && request.method == "GET" &&
                    request.url == url && request.headers.empty() &&
                    request.maxResponse <= 1024 * 1024;
                lume::music::auth::HttpResponse response;
                response.status = 200; response.contentType = "image/png";
                response.body.assign(bytes.begin(), bytes.end());
                return response;
            });
            Expect(requested && fetched == bytes && DecodeCover(fetched),
                "online provider cover did not reach the desktop decoder");
            track.artwork = std::string(url);
            track.artwork.insert(track.artwork.find('/', 8), ".evil.test");
            Expect(FetchCover(track, {}, [&](const auto&, const auto&) {
                Expect(false, "invalid cover reached transport");
                return lume::music::auth::HttpResponse{};
            }).empty(), "invalid cover host accepted");
        }

        const auto button = [&](bool playing) {
            WidgetCanvas canvas; WidgetBitmap bitmap;
            Expect(canvas.Begin(48, 48, 96), "button canvas");
            DrawTransportIcon(canvas, {0, 0, 48, 48}, 30, 1, playing, true);
            Expect(canvas.Finish(bitmap), "button render");
            return bitmap;
        };
        const auto pausedButton = button(false), playingButton = button(true);
        const auto at = [](const WidgetBitmap& b, int x, int y) { return b.pixels[static_cast<std::size_t>(y)*48+x] >> 24; };
        Expect(at(pausedButton, 24, 4) == 0 && at(playingButton, 24, 4) == 0 &&
            at(pausedButton, 24, 45) == 0 && at(playingButton, 24, 45) == 0,
            "play/pause icon has a circular base or underline");
        Expect(pausedButton.pixels != playingButton.pixels, "play/pause icons did not switch");

        if (argc == 2) std::filesystem::create_directories(argv[1]);
        for (const auto status : {PlaybackStatus::Empty, PlaybackStatus::Playing, PlaybackStatus::Paused,
                                 PlaybackStatus::Error, PlaybackStatus::Ready}) {
            sample = Sample(); sample.status = status; sample.playing = status == PlaybackStatus::Playing;
            if (status == PlaybackStatus::Empty) sample.current.reset();
            if (status == PlaybackStatus::Paused) sample.current->artwork = png;
            controls.Update(sample);
            for (unsigned dpi : {96u, 144u, 192u}) {
                for (const auto size : {SIZE{320, 152}, SIZE{440, 230}, SIZE{600, 300}}) {
                    WidgetCanvas canvas; WidgetBitmap bitmap;
                    Expect(canvas.Begin(static_cast<float>(size.cx), static_cast<float>(size.cy), dpi), "离屏画布失败");
                    controls.Draw(canvas, static_cast<float>(size.cx), static_cast<float>(size.cy));
                    Expect(canvas.Finish(bitmap) && bitmap.width == static_cast<unsigned>(MulDiv(size.cx, dpi, 96)),
                        "离屏绘制或 DPI 尺寸错误");
                    const auto count = std::count_if(bitmap.pixels.begin(), bitmap.pixels.end(),
                        [](auto pixel) { return (pixel>>24) > 32; });
                    Expect(count > 300, "音乐画布为空");
                    Expect(!controls.Handle(), "离屏测试意外创建桌面组件");
                    if (argc == 2 && dpi == 96) SaveBitmap(bitmap, std::filesystem::path(argv[1]) /
                        ("music-"+std::to_string(static_cast<int>(status))+"-"+std::to_string(size.cx)+".bmp"));
                }
            }
        }
        controls.Shutdown(); controls.Shutdown(); controls.Update(Sample());
        Expect(!controls.Show() && !controls.Handle() && !media(APPCOMMAND_MEDIA_PLAY_PAUSE),
            "退出后仍可启用窗口或发送命令");
        DestroyWindow(owner);
    }
    CoUninitialize();
    std::cout << "T13: 状态、命令、媒体事件队列、退出和 45 组离屏布局测试通过\n";
}
