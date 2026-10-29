#include "apps/shell/wallpaper_session.h"
#include "core/wallpaper/library.h"
#include "platform/windows/mpv_process_controller.h"
#include "platform/windows/wallpaper/download.h"
#include "platform/windows/wallpaper/media_server.h"
#include "platform/windows/wallpaper/static_image.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
using Json = nlohmann::json;
using Controller = lume::platform::windows::MpvProcessController;
using Reader = lume::platform::windows::wallpaper::EncryptedMediaReader;
struct CloseObservedProcess {
    void operator()(void* handle) const { CloseHandle(handle); }
};
using ObservedProcess = std::unique_ptr<void, CloseObservedProcess>;

int failures = 0;

void Expect(bool value, const std::string& message, const Controller* controller = nullptr) {
    std::cout << (value ? "PASS: " : "FAIL: ") << message;
    if (!value && controller) std::cout << " [" << controller->LastError() << "]";
    std::cout << '\n';
    if (!value) ++failures;
}

void Pump(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(5);
    }
}

std::string Utf8(const std::wstring& value) {
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                             static_cast<int>(value.size()), result.data(), length,
                             nullptr, nullptr))
        return {};
    return result;
}

Json State(lume::shell::WallpaperSession& session) {
    const auto text = Utf8(session.State());
    return text.empty() ? Json{} : Json::parse(text, nullptr, false);
}

Json Screen(const Json& state, std::uint64_t display) {
    if (!state.is_object() || !state.contains("displays") || !state["displays"].is_array())
        return Json::object();
    for (const auto& item : state["displays"]) {
        if (item.value("id", std::string{}) == std::to_string(display)) return item;
    }
    return Json::object();
}

bool WaitFor(const std::function<bool()>& condition, DWORD timeout = 12000) {
    const auto end = GetTickCount64() + timeout;
    do {
        Pump(5);
        if (condition()) return true;
    } while (GetTickCount64() < end);
    return false;
}

std::vector<DWORD> PlayerProcesses(HWND parent) {
    std::vector<DWORD> result;
    EnumChildWindows(parent, [](HWND child, LPARAM context) -> BOOL {
        DWORD processId = 0;
        GetWindowThreadProcessId(child, &processId);
        auto& values = *reinterpret_cast<std::vector<DWORD>*>(context);
        if (processId != GetCurrentProcessId() &&
            std::find(values.begin(), values.end(), processId) == values.end())
            values.push_back(processId);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

struct Probe {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::string pending;
    std::uint64_t sequence = 760000;

    ~Probe() {
        if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    }

    bool Open(DWORD processId) {
        WIN32_FIND_DATAW entry{};
        const auto search = FindFirstFileW(L"\\\\.\\pipe\\lume-native-mpv-*", &entry);
        if (search == INVALID_HANDLE_VALUE) return false;
        do {
            const auto path = L"\\\\.\\pipe\\" + std::wstring(entry.cFileName);
            const auto candidate = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (candidate == INVALID_HANDLE_VALUE) continue;
            ULONG owner = 0;
            if (GetNamedPipeServerProcessId(candidate, &owner) && owner == processId) {
                pipe = candidate;
                break;
            }
            CloseHandle(candidate);
        } while (FindNextFileW(search, &entry));
        FindClose(search);
        return pipe != INVALID_HANDLE_VALUE;
    }

    bool Transfer(bool write, void* data, DWORD size, DWORD& transferred) {
        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!operation.hEvent) return false;
        auto done = write
            ? WriteFile(pipe, data, size, &transferred, &operation)
            : ReadFile(pipe, data, size, &transferred, &operation);
        if (!done && GetLastError() == ERROR_IO_PENDING) {
            const auto end = GetTickCount64() + 2000;
            while (WaitForSingleObject(operation.hEvent, 5) == WAIT_TIMEOUT &&
                   GetTickCount64() < end)
                Pump(1);
            if (WaitForSingleObject(operation.hEvent, 0) == WAIT_OBJECT_0) {
                done = GetOverlappedResult(pipe, &operation, &transferred, FALSE);
            } else {
                CancelIoEx(pipe, &operation);
                DWORD ignored = 0;
                GetOverlappedResult(pipe, &operation, &ignored, TRUE);
            }
        }
        CloseHandle(operation.hEvent);
        return done != FALSE;
    }

    Json Property(const char* name) {
        if (pipe == INVALID_HANDLE_VALUE) return {};
        const auto requestId = ++sequence;
        const auto request = Json{
            {"command", Json::array({"get_property", name})},
            {"request_id", requestId}
        }.dump() + '\n';
        DWORD written = 0;
        if (!Transfer(true, const_cast<char*>(request.data()),
                      static_cast<DWORD>(request.size()), written) ||
            written != request.size())
            return {};
        const auto end = GetTickCount64() + 2500;
        while (GetTickCount64() < end) {
            const auto newline = pending.find('\n');
            if (newline == std::string::npos) {
                std::array<char, 4096> buffer{};
                DWORD read = 0;
                if (!Transfer(false, buffer.data(), static_cast<DWORD>(buffer.size()), read) ||
                    !read)
                    return {};
                pending.append(buffer.data(), read);
                if (pending.size() > 65536) return {};
                continue;
            }
            const auto reply = Json::parse(pending.substr(0, newline), nullptr, false);
            pending.erase(0, newline + 1);
            if (reply.is_object() &&
                reply.value("request_id", std::uint64_t{0}) == requestId)
                return reply.value("error", std::string{}) == "success" &&
                    reply.contains("data") ? reply["data"] : Json{};
        }
        return {};
    }
};

std::string ContentType(const std::string& extension) {
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "png") return "image/png";
    if (extension == "webp") return "image/webp";
    if (extension == "bmp") return "image/bmp";
    if (extension == "mp4") return "video/mp4";
    return {};
}

bool HasBalancedFilter(const Json& filters) {
    if (!filters.is_array()) return false;
    return std::any_of(filters.begin(), filters.end(), [](const auto& filter) {
        return filter.is_object() && filter.value("label", std::string{}) == "lume-balanced";
    });
}

bool ApplyPlaybackDefaults(Controller& controller, std::uint64_t display,
                            const std::string& label) {
    const auto fit = controller.SetFit(display, true);
    Expect(fit, label + " applies cover fit", &controller);
    const auto volume = controller.SetVolume(display, 0);
    Expect(volume, label + " applies silent volume", &controller);
    const auto resumed = controller.Command(display, lume::wallpaper::MpvCommand::Resume);
    Expect(resumed, label + " applies the playing state", &controller);
    return fit && volume && resumed;
}

bool NativeFrame(HWND parent,const std::wstring& path,
                 const lume::platform::windows::wallpaper::StaticImageSource* source) {
    const auto host=GetWindow(parent,GW_CHILD);
    RECT bounds{};DWORD process=0;
    GetWindowThreadProcessId(host,&process);
    if(!host||process!=GetCurrentProcessId()||!GetClientRect(host,&bounds)||
        bounds.right<=0||bounds.bottom<=0||!PlayerProcesses(parent).empty())return false;
    lume::platform::windows::wallpaper::StaticImage expected;
    std::string error;
    if(lume::platform::windows::wallpaper::LoadStaticImage(path,source,bounds.right,bounds.bottom,
        true,nullptr,expected,error)!=lume::platform::windows::wallpaper::StaticImageLoad::Loaded)return false;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=bounds.right;info.bmiHeader.biHeight=-bounds.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;
    const auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    const auto dc=CreateCompatibleDC(nullptr);
    if(!bitmap||!dc||!pixels) {
        if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);return false;
    }
    const auto old=SelectObject(dc,bitmap);
    std::fill_n(static_cast<std::uint32_t*>(pixels),expected.pixels.size(),0x00ff00ffu);
    SendMessageW(host,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    GdiFlush();
    const bool matches=std::equal(expected.pixels.begin(),expected.pixels.end(),
        static_cast<const std::uint32_t*>(pixels),[](auto left,auto right){
            return (left&0xffffffu)==(right&0xffffffu);
        });
    SelectObject(dc,old);DeleteDC(dc);DeleteObject(bitmap);return matches;
}

bool RunLocal(const std::filesystem::path& executable,
              const std::filesystem::path& image,
              HWND parent,
              std::uint64_t display,
              bool balanced) {
    Controller controller(executable.wstring(), parent);
    if (balanced) {
        const auto configured = controller.SetBalancedMode(true);
        Expect(configured, "balanced local static image accepts the playback policy", &controller);
        if (!configured) return false;
    }
    const auto media = image.u8string();
    const auto started = controller.Start(display, media);
    Expect(started, balanced
        ? "balanced local imported static image starts natively"
        : "local imported static image starts natively", &controller);
    if (!started) return false;
    ApplyPlaybackDefaults(controller, display, balanced
        ? "balanced local static image" : "local static image");

    Expect(PlayerProcesses(parent).empty(),"local static image starts no external player");
    Expect(NativeFrame(parent,image.wstring(),nullptr),"local static image paints the decoded frame");
    for (const bool enabled : {true,false,true}) {
        Expect(controller.SetBalancedMode(enabled),"native static image accepts sampling policy changes",&controller);
        Expect(PlayerProcesses(parent).empty()&&NativeFrame(parent,image.wstring(),nullptr),
            "native static sampling changes preserve pixels without starting a player");
    }
    controller.Stop(display);
    Expect(!controller.IsRunning(display), "local static image stops cleanly");
    return started;
}

bool RunProtected(const std::filesystem::path& executable,
                  const std::filesystem::path& root,
                  const std::filesystem::path& encrypted,
                  const std::string& extension,
                  HWND parent,
                  std::uint64_t display,
                  bool balanced) {
    const bool video = extension == "mp4";
    std::string error;
    auto reader = Reader::Open(root.u8string(), encrypted.u8string(), error);
    Expect(bool(reader), "official encrypted static image opens through the protected reader");
    if (!reader) {
        std::cout << "  reader error: " << error << '\n';
        return false;
    }
    const auto contentType = ContentType(extension);
    Expect(!contentType.empty(), "static protected fixture has a supported content type");
    if (contentType.empty()) return false;

    lume::platform::windows::WallpaperMediaServer server;
    lume::platform::windows::WallpaperMediaSource source;
    source.bytes = reader->Size();
    source.contentType = contentType;
    source.extension = extension;
    source.read = [reader](std::uint64_t offset, char* buffer, std::size_t capacity,
                           std::size_t& read) {
        return reader->Read(offset, buffer, capacity, read);
    };
    const lume::platform::windows::wallpaper::StaticImageSource staticSource{source.bytes,source.read};
    const auto url = server.Register("static-protected-test", std::move(source));
    Expect(!url.empty(), "official encrypted static image receives a loopback capability");
    if (url.empty()) return false;

    const auto slash = url.find_last_of('/');
    const auto prefix = slash == std::string::npos ? std::string{} : url.substr(0, slash + 1);
    Controller controller(executable.wstring(), parent);
    if (balanced) {
        const auto configured = controller.SetBalancedMode(true);
        Expect(configured, "balanced protected static image accepts the playback policy", &controller);
        if (!configured) {
            server.Remove("static-protected-test");
            return false;
        }
    }
    Expect(controller.AllowMediaUrlPrefix(prefix),
        balanced ? "balanced static protected image capability is accepted by the real mpv controller"
                 : "static protected image capability is accepted by the real mpv controller");
    if(!video)Expect(controller.AllowStaticImageSource(url,staticSource),
        "protected static image registers its exact in-memory reader");
    const auto started = controller.Start(display, url);
    Expect(started, balanced
        ? "balanced official encrypted static image starts through media server"
        : "official encrypted static image starts through media server", &controller);
    if (started&&!video) {
        ApplyPlaybackDefaults(controller,display,"native protected static image");
        Expect(PlayerProcesses(parent).empty(),"protected static image starts no external player");
        Expect(NativeFrame(parent,std::filesystem::u8path(url).wstring(),&staticSource),
            "protected static image paints the decrypted frame without a cleartext file");
    } else if (started) {
        ApplyPlaybackDefaults(controller, display, balanced
            ? "balanced protected static image" : "protected static image");
        const auto players = PlayerProcesses(parent);
        Expect(players.size() == 1, "protected static image owns exactly one embedded player");
        Probe probe;
        const bool probed = players.size() == 1 && probe.Open(players.front());
        Expect(probed, "protected static image exposes the owned mpv IPC", &controller);
        if (probed) {
            const auto path = probe.Property("path");
            const auto frame = probe.Property("video-frame-info");
            Expect(path.is_string() && path.get<std::string>() == url,
                balanced ? "balanced protected static image reaches mpv through the loopback URL"
                         : "protected static image reaches mpv through the loopback URL");
            const auto filters = probe.Property("vf");
            Expect(filters.is_array() && HasBalancedFilter(filters) == (balanced && video),
                "protected media receives a frame-rate filter only for balanced video");
            std::cout << "  " << (balanced ? "balanced " : "") << "protected video-frame-info: "
                      << frame.dump() << '\n';
            Expect(frame.is_object() && !frame.empty(),
                balanced ? "balanced protected static image reports a decoded frame"
                         : "protected static image reports a decoded frame");
            if (video) {
                const auto first = probe.Property("time-pos");
                Pump(300);
                const auto later = probe.Property("time-pos");
                Expect(first.is_number() && later.is_number() &&
                    first.get<double>() != later.get<double>(), "protected video timeline advances");
            }
        }
    }
    controller.Stop(display);
    controller.RevokeMediaUrlPrefix(prefix);
    server.Remove("static-protected-test");
    Expect(!controller.IsRunning(display), "protected static image stops cleanly");
    return started;
}

bool EncryptFixture(const std::filesystem::path& input, const std::filesystem::path& root,
                    const std::filesystem::path& encrypted) {
    std::filesystem::create_directories(encrypted.parent_path());
    std::string error;
    const auto factory = lume::platform::windows::wallpaper::MakeProtectedWriterFactory(root.u8string());
    auto writer = factory(encrypted.u8string(), error);
    std::ifstream source(input, std::ios::binary);
    bool ok = writer && source;
    std::array<std::uint8_t, 65536> buffer{};
    while (ok && source) {
        source.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto count = source.gcount();
        if (count) ok = writer->Append(buffer.data(), static_cast<std::size_t>(count), error);
    }
    ok = ok && source.eof() && writer->Finish(error);
    Expect(ok, "create an isolated encrypted fixture using the production writer: " + error);
    return ok;
}

void RunSwitches(const std::filesystem::path& executable, const std::filesystem::path& image,
                 const std::filesystem::path& video, HWND parent, std::uint64_t display) {
    Controller controller(executable.wstring(), parent);
    Expect(controller.SetBalancedMode(true), "switch fixture enables balanced mode");
    if (!controller.Start(display, image.u8string())) {
        Expect(false, "real switch fixture starts the static image", &controller);
        return;
    }
    Expect(PlayerProcesses(parent).empty(),"real switch starts with a native image and no player");
    for (const bool enabled : {true, false, true}) {
        std::vector<ObservedProcess> observedPlayers;
        Expect(controller.SetBalancedMode(enabled), "switch fixture changes policy", &controller);
        for (const auto& media : {video, image}) {
            const auto started = controller.Start(display, media.u8string());
            Expect(started, "real player switches to " + media.extension().u8string(), &controller);
            if (!started) return;
            if(media==image) {
                Expect(PlayerProcesses(parent).empty()&&NativeFrame(parent,image.wstring(),nullptr),
                    "switching back to static releases the player and paints native pixels");
                for(const auto& process:observedPlayers)
                    Expect(WaitForSingleObject(process.get(),2000)==WAIT_OBJECT_0,
                        "the switched-out video process exits even if its window has detached");
                observedPlayers.clear();
            } else {
                const auto players=PlayerProcesses(parent);Probe probe;
                for(const auto id:players) {
                    ObservedProcess process(OpenProcess(SYNCHRONIZE,FALSE,id));
                    Expect(bool(process),"retain the exact video process before switching back to native");
                    if(!process)return;
                    observedPlayers.push_back(std::move(process));
                }
                const bool opened=players.size()==1&&probe.Open(players.front());
                Expect(opened,"switching to video starts one owned real player");
                if(!opened)return;
                const auto filters=probe.Property("vf");
                Expect(filters.is_array()&&HasBalancedFilter(filters)==enabled,
                    "real switched video preserves filter policy");
                const auto frame=probe.Property("video-frame-info");
                Expect(frame.is_object()&&!frame.empty(),"real switched video has a decoded frame");
                Expect(probe.Property("path")==media.u8string(),"real switched video matches selected file");
            }
        }
    }
}

bool RunSession(const std::filesystem::path& executable,
                const std::filesystem::path& localImage,
                const std::filesystem::path& output,
                HWND parent,
                std::uint64_t display,
                bool desktop,
                bool balanced) {
    const auto root = output / (balanced ? "session-balanced" : "session");
    std::filesystem::create_directories(root);
    lume::wallpaper::Library library(root.u8string());
    Expect(library.Load().ok, "static session fixture loads an isolated library");
    const auto imported = library.ImportLocal(localImage.u8string());
    Expect(imported.ok, "static session fixture registers the imported PNG");
    if (!imported.ok) return false;

    lume::shell::WallpaperSessionOptions options;
    options.root = root.u8string();
    options.executable = executable.wstring();
    options.verification = !desktop;
    options.verificationParent = desktop ? nullptr : parent;
    options.balancedMode = balanced;
    lume::shell::WallpaperSession session(GetModuleHandleW(nullptr), options, {});
    const auto restored = session.Restore();
    Expect(restored, "static session restores through the production actor");
    if (!restored) return false;

    const auto loaded = WaitFor([&] {
        const auto state = State(session);
        return state.is_object() && Screen(state, display).is_object() &&
            state.value("library", Json::array()).size() == 1;
    });
    Expect(loaded, "static session publishes the imported library and display");
    if (!loaded) {
        std::cout << "  session state: " << State(session).dump() << '\n';
        session.Shutdown();
        return false;
    }

    lume::ui::WallpaperPageCommand apply;
    apply.action = lume::ui::WallpaperAction::Apply;
    apply.id = imported.id;
    apply.display = display;
    session.HandlePage(apply);
    const auto playing = WaitFor([&] {
        const auto state = State(session);
        const auto screen = Screen(state, display);
        return screen.is_object() &&
            screen.value("wallpaperId", std::string{}) == imported.id &&
            screen.value("state", std::string{}) == "playing";
    });
    const auto finalState = State(session);
    Expect(playing, balanced
        ? "production WallpaperSession applies the imported static PNG with balanced playback"
        : "production WallpaperSession applies the imported static PNG", nullptr);
    if (!playing) {
        std::cout << "  failed session state: " << finalState.dump() << '\n';
    }
    Expect(finalState.value("error", std::string{}).empty(),
        balanced ? "balanced static session apply leaves no playback error"
                 : "static session apply leaves no playback error");

    lume::ui::WallpaperPageCommand stop;
    stop.action = lume::ui::WallpaperAction::StopAll;
    session.HandlePage(stop);
    Expect(WaitFor([&] {
        const auto state = State(session);
        return Screen(state, display).value("state", std::string{}) == "stopped";
    }), "static session stops the imported wallpaper cleanly");
    session.Shutdown();
    return playing;
}
}

int main(int argc, char** argv) {
    if (argc != 5 && argc != 7 && argc != 8) {
        std::cerr << "Usage: wallpaper-static-playback-test "
                     "mpv.exe local-image new-output-directory local-video\n"
                     "or mpv.exe local-image output-directory protected-root protected-apk extension [--desktop]\n";
        return 2;
    }
    const auto executable = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    const auto localImage = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    const auto output = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
    const bool generated = argc == 5;
    const auto root = generated ? output / "protected" :
        std::filesystem::absolute(std::filesystem::u8path(argv[4]));
    const auto encrypted = generated ? root / "media" / "image.apk" :
        std::filesystem::absolute(std::filesystem::u8path(argv[5]));
    auto extension = generated ? localImage.extension().u8string() :
        std::filesystem::u8path(argv[6]).u8string();
    if (!extension.empty() && extension.front() == '.') extension.erase(0, 1);
    const bool desktop = argc == 8 && std::string(argv[7]) == "--desktop";
    if (argc == 8 && !desktop) {
        std::cerr << "unknown optional mode\n";
        return 2;
    }
    if (!std::filesystem::is_regular_file(executable) ||
        !std::filesystem::is_regular_file(localImage) ||
        (!generated && !std::filesystem::is_regular_file(encrypted)) ||
        extension.empty()) {
        std::cerr << "input fixture is missing\n";
        return 2;
    }
    if (generated && std::filesystem::exists(output)) {
        std::cerr << "generated fixtures require a new output directory\n";
        return 2;
    }
    std::filesystem::create_directories(output);
    if (generated && !EncryptFixture(localImage, root, encrypted)) return 1;

    const auto displays = lume::platform::windows::EnumerateMpvDisplays();
    Expect(!displays.empty(), "static playback test enumerates a display");
    if (displays.empty()) return 1;

    const auto parent = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
        L"Lume static wallpaper isolated verification", WS_POPUP, -30000, -30000,
        640, 360, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Expect(parent != nullptr, "static playback test creates an owned off-screen parent");
    if (!parent) return 1;
    ShowWindow(parent, SW_SHOWNOACTIVATE);

    RunLocal(executable, localImage, parent, displays.front().id, false);
    RunLocal(executable, localImage, parent, displays.front().id, true);
    RunProtected(executable, root, encrypted, extension, parent, displays.front().id, false);
    RunProtected(executable, root, encrypted, extension, parent, displays.front().id, true);
    if (generated) {
        const auto video = std::filesystem::absolute(std::filesystem::u8path(argv[4]));
        RunSwitches(executable, localImage, video, parent, displays.front().id);
        const auto protectedVideo = root / "media" / "video.apk";
        if (EncryptFixture(video, root, protectedVideo)) {
            std::cout << "Protected VIDEO regression:\n";
            RunProtected(executable, root, protectedVideo, "mp4", parent, displays.front().id, false);
            RunProtected(executable, root, protectedVideo, "mp4", parent, displays.front().id, true);
        }
    }
    RunSession(executable, localImage, output, parent, displays.front().id, desktop, false);
    RunSession(executable, localImage, output, parent, displays.front().id, desktop, true);

    Expect(GetWindow(parent, GW_CHILD) == nullptr,
        "static playback test leaves no native player container");
    DestroyWindow(parent);
    return failures == 0 ? 0 : 1;
}
