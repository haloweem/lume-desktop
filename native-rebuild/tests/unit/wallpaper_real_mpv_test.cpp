#include "platform/windows/mpv_process_controller.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;
using Controller = lume::platform::windows::MpvProcessController;
using lume::wallpaper::MpvCommand;
int failures = 0;

void Expect(bool result, const char* check, const Controller* controller = nullptr) {
    std::cout << (result ? "PASS: " : "FAIL: ") << check;
    if (!result) {
        ++failures;
        if (controller) std::cout << " [" << controller->LastError() << "]";
    }
    std::cout << std::endl;
}

std::vector<DWORD> PlayerProcesses(HWND parent) {
    std::vector<DWORD> result;
    EnumChildWindows(parent, [](HWND child, LPARAM context) -> BOOL {
        DWORD id = 0;
        GetWindowThreadProcessId(child, &id);
        auto& values = *reinterpret_cast<std::vector<DWORD>*>(context);
        if (id != GetCurrentProcessId() && std::find(values.begin(), values.end(), id) == values.end())
            values.push_back(id);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

void Pump(DWORD duration) {
    const auto end = GetTickCount64() + duration;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

bool WriteBitmap(const std::filesystem::path& path, bool alternate) {
    constexpr LONG width = 320;
    constexpr LONG height = 180;
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + width * height * 4;
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = -height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height));
    for (LONG y = 0; y < height; ++y) {
        for (LONG x = 0; x < width; ++x) {
            pixels[static_cast<std::size_t>(y * width + x)] =
                alternate ? (x < width / 2 ? 0x001C38F0u : 0x00EE9B19u)
                          : (x < width / 2 ? 0x00EB3023u : 0x001FD08Du);
        }
    }
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
    stream.write(reinterpret_cast<const char*>(&info), sizeof(info));
    stream.write(reinterpret_cast<const char*>(pixels.data()),
                 static_cast<std::streamsize>(pixels.size() * sizeof(pixels.front())));
    return static_cast<bool>(stream);
}

struct ProbePipe {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::string pending;
    std::uint64_t sequence = 90000;
    ~ProbePipe() { if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe); }
    bool Open(DWORD processId) {
        WIN32_FIND_DATAW entry{};
        const auto search = FindFirstFileW(L"\\\\.\\pipe\\lume-native-mpv-*", &entry);
        if (search == INVALID_HANDLE_VALUE) return false;
        do {
            const auto name = L"\\\\.\\pipe\\" + std::wstring(entry.cFileName);
            const auto candidate = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
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
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) return false;
        auto done = write ? WriteFile(pipe, data, size, &transferred, &overlapped)
                          : ReadFile(pipe, data, size, &transferred, &overlapped);
        if (!done && GetLastError() == ERROR_IO_PENDING) {
            const auto end = GetTickCount64() + 2000;
            while (WaitForSingleObject(overlapped.hEvent, 10) == WAIT_TIMEOUT && GetTickCount64() < end)
                Pump(1);
            if (WaitForSingleObject(overlapped.hEvent, 0) != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &overlapped);
                DWORD ignored = 0;
                GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
            } else {
                done = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
            }
        }
        CloseHandle(overlapped.hEvent);
        return done != FALSE;
    }
    Json Request(const Json& command) {
        if (pipe == INVALID_HANDLE_VALUE) return {};
        const auto id = ++sequence;
        auto message = Json{{"command", command}, {"request_id", id}}.dump() + "\n";
        DWORD written = 0;
        if (!Transfer(true, message.data(), static_cast<DWORD>(message.size()), written)) return {};
        const auto end = GetTickCount64() + 3000;
        while (GetTickCount64() < end) {
            const auto newline = pending.find('\n');
            if (newline == std::string::npos) {
                std::array<char, 4096> bytes{};
                DWORD read = 0;
                if (!Transfer(false, bytes.data(), static_cast<DWORD>(bytes.size()), read) || !read) return {};
                pending.append(bytes.data(), read);
                if (pending.size() > 65536) return {};
                continue;
            }
            const auto reply = Json::parse(pending.substr(0, newline), nullptr, false);
            pending.erase(0, newline + 1);
            if (reply.is_object() && reply.value("request_id", std::uint64_t{0}) == id) return reply;
        }
        return {};
    }
    Json Property(const char* name) {
        const auto reply = Request(Json::array({"get_property", name}));
        return reply.is_object() && reply.value("error", std::string{}) == "success" && reply.contains("data")
            ? reply["data"] : Json{};
    }
};
}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: wallpaper-real-mpv-test mpv.exe output-directory generated-test-video.mp4\n";
        return 2;
    }
    const auto executable = std::filesystem::u8path(argv[1]);
    const auto output = std::filesystem::u8path(argv[2]);
    const auto video = std::filesystem::u8path(argv[3]);
    std::filesystem::create_directories(output);
    const auto first = output / L"first.bmp";
    const auto second = output / L"second.bmp";
    Expect(WriteBitmap(first, false) && WriteBitmap(second, true), "generated static fixtures");
    const auto displays = lume::platform::windows::EnumerateMpvDisplays();
    Expect(!displays.empty(), "one display enumerated");
    if (displays.empty()) return 1;
    const auto window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
                                        L"Lume wallpaper isolated verification", WS_POPUP,
                                        -30000, -30000, 640, 360, nullptr, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
    Expect(window != nullptr, "owned off-screen verification window");
    if (!window) return 1;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    Json evidence{{"executable", executable.u8string()}, {"verification_only", true},
                  {"display_id", displays.front().id}};
    {
        Controller controller(executable.wstring(), window);
        const auto id = displays.front().id;
        Expect(controller.Start(id, first.u8string()), "real mpv static first frame", &controller);
        if (controller.IsRunning(id)) {
            const auto children = PlayerProcesses(window);
            Expect(children.size() == 1, "real player embedded in owned container only");
            Expect(controller.EnsureAttached(id), "real attachment and IPC health", &controller);
            Expect(controller.Command(id, MpvCommand::Pause), "real static pause", &controller);
            Expect(controller.Command(id, MpvCommand::Resume), "real static resume", &controller);
            Expect(controller.Start(id, second.u8string()), "real static loadfile switch", &controller);
            Expect(PlayerProcesses(window) == children, "real static switch reuses player");
            controller.Stop(id);
            Expect(!controller.IsRunning(id), "real static player stopped");
        }
        Expect(controller.Start(id, video.u8string()), "real generated video first frame", &controller);
        if (controller.IsRunning(id)) {
            const auto children = PlayerProcesses(window);
            Expect(children.size() == 1, "real video player embedded");
            ProbePipe probe;
            Expect(!children.empty() && probe.Open(children.front()), "independent probe validates owned player PID");
            const auto start = probe.Property("time-pos");
            Pump(450);
            const auto advancing = probe.Property("time-pos");
            Expect(start.is_number() && advancing.is_number() &&
                   advancing.get<double>() != start.get<double>(), "video timeline really advances");
            Expect(controller.Command(id, MpvCommand::Pause), "real video pause", &controller);
            const auto paused = probe.Property("time-pos");
            Pump(350);
            Expect(paused == probe.Property("time-pos"), "pause actually stops timeline");
            Expect(controller.Command(id, MpvCommand::Resume), "real video resume", &controller);
            Expect(controller.SetFit(id, false), "real contain fit", &controller);
            Expect(probe.Property("panscan") == Json(0.0), "contain reaches actual mpv property");
            Expect(controller.SetFit(id, true), "real cover fit", &controller);
            Expect(probe.Property("panscan") == Json(1.0), "cover reaches actual mpv property");
            // Keep the test acoustically silent while exercising the audio-track API.
            Expect(probe.Request(Json::array({"set_property", "ao", "null"})).value("error", std::string{}) == "success",
                   "test-only null audio output");
            Expect(controller.SetVolume(id, 25), "positive volume enables real audio", &controller);
            Expect(probe.Property("volume") == Json(25.0), "volume reaches real mpv property");
            Expect(controller.SetVolume(id, 0), "zero volume disables real audio", &controller);
            Expect(probe.Property("mute") == Json(true), "zero volume remains muted");
            evidence["hwdec-current"] = probe.Property("hwdec-current");
            evidence["video-params"] = probe.Property("video-params");
            evidence["audio-track-after-zero"] = probe.Property("aid");
            evidence["vo"] = probe.Property("current-vo");
            evidence["pid"] = children.empty() ? 0 : children.front();
            const auto screenshot = output / L"real-video-frame.png";
            const auto capture = probe.Request(Json::array({"screenshot-to-file", screenshot.u8string(), "video"}));
            Expect(capture.is_object() && capture.value("error", std::string{}) == "success" &&
                   std::filesystem::is_regular_file(screenshot), "own decoded video frame captured");
            Expect(controller.Start(id, first.u8string()), "video to image reuses real decoder process", &controller);
            Expect(PlayerProcesses(window) == children, "real video to image uses same player PID");
            HANDLE process = children.empty() ? nullptr : OpenProcess(SYNCHRONIZE, FALSE, children.front());
            controller.Stop(id);
            Expect(!controller.IsRunning(id) && controller.ActiveDisplays().empty(), "real Stop clears owned state");
            if (process) {
                Expect(WaitForSingleObject(process, 1500) == WAIT_OBJECT_0, "real Stop exits mpv process");
                CloseHandle(process);
            }
        }
    }
    Expect(GetWindow(window, GW_CHILD) == nullptr, "real tests leave no child container");
    DestroyWindow(window);
    evidence["failures"] = failures;
    std::ofstream(output / L"real-mpv-evidence.json") << evidence.dump(2);
    return failures == 0 ? 0 : 1;
}
