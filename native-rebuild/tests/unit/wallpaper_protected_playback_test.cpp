#include "platform/windows/mpv_process_controller.h"
#include "platform/windows/wallpaper/download.h"
#include "platform/windows/wallpaper/media_server.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using Json = nlohmann::json;
using Controller = lume::platform::windows::MpvProcessController;
using Reader = lume::platform::windows::wallpaper::EncryptedMediaReader;
using lume::wallpaper::MpvCommand;
int failures = 0;
void Expect(bool value, const char* check, const Controller* controller = nullptr) {
    std::cout << (value ? "PASS: " : "FAIL: ") << check;
    if (!value) {
        ++failures;
        if (controller) std::cout << " [" << controller->LastError() << "]";
    }
    std::cout << std::endl;
}
void Pump(DWORD milliseconds) {
    const auto until = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(5);
    } while (GetTickCount64() < until);
}
std::vector<DWORD> OwnedChildren(HWND parent) {
    std::vector<DWORD> result;
    EnumChildWindows(parent, [](HWND child, LPARAM context) -> BOOL {
        DWORD id = 0; GetWindowThreadProcessId(child, &id);
        auto& values = *reinterpret_cast<std::vector<DWORD>*>(context);
        if (id != GetCurrentProcessId() && std::find(values.begin(), values.end(), id) == values.end())
            values.push_back(id);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
struct Probe {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::string pending;
    std::uint64_t sequence = 190000;
    ~Probe() { if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe); }
    bool Open(DWORD processId) {
        WIN32_FIND_DATAW entry{};
        const auto search = FindFirstFileW(L"\\\\.\\pipe\\lume-native-mpv-*", &entry);
        if (search == INVALID_HANDLE_VALUE) return false;
        do {
            const auto path = L"\\\\.\\pipe\\" + std::wstring(entry.cFileName);
            const auto candidate = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (candidate == INVALID_HANDLE_VALUE) continue;
            ULONG owner = 0;
            if (GetNamedPipeServerProcessId(candidate, &owner) && owner == processId) { pipe = candidate; break; }
            CloseHandle(candidate);
        } while (FindNextFileW(search, &entry));
        FindClose(search); return pipe != INVALID_HANDLE_VALUE;
    }
    bool Transfer(bool write, void* data, DWORD size, DWORD& count) {
        OVERLAPPED state{}; state.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!state.hEvent) return false;
        BOOL done = write ? WriteFile(pipe, data, size, &count, &state) : ReadFile(pipe, data, size, &count, &state);
        if (!done && GetLastError() == ERROR_IO_PENDING) {
            const auto until = GetTickCount64() + 2000;
            while (WaitForSingleObject(state.hEvent, 5) == WAIT_TIMEOUT && GetTickCount64() < until) Pump(1);
            if (WaitForSingleObject(state.hEvent, 0) == WAIT_OBJECT_0)
                done = GetOverlappedResult(pipe, &state, &count, FALSE);
            else {
                CancelIoEx(pipe, &state);
                DWORD ignored = 0; GetOverlappedResult(pipe, &state, &ignored, TRUE);
            }
        }
        CloseHandle(state.hEvent); return done != FALSE;
    }
    Json Request(const Json& command) {
        if (pipe == INVALID_HANDLE_VALUE) return {};
        const auto id = ++sequence;
        auto text = Json{{"command", command}, {"request_id", id}}.dump() + '\n';
        DWORD count = 0;
        if (!Transfer(true, text.data(), static_cast<DWORD>(text.size()), count) || count != text.size()) return {};
        const auto until = GetTickCount64() + 2500;
        while (GetTickCount64() < until) {
            const auto end = pending.find('\n');
            if (end == std::string::npos) {
                std::array<char, 4096> buffer{};
                if (!Transfer(false, buffer.data(), static_cast<DWORD>(buffer.size()), count) || !count) return {};
                pending.append(buffer.data(), count);
                if (pending.size() > 65536) return {};
                continue;
            }
            const auto reply = Json::parse(pending.substr(0, end), nullptr, false);
            pending.erase(0, end + 1);
            if (reply.is_object() && reply.value("request_id", std::uint64_t{0}) == id) return reply;
        }
        return {};
    }
    Json Property(const char* name) {
        auto reply = Request(Json::array({"get_property", name}));
        return reply.is_object() && reply.value("error", std::string{}) == "success" &&
            reply.contains("data") ? reply["data"] : Json{};
    }
};
bool WorkspaceOutput(const std::filesystem::path& value) {
    if (!value.is_absolute()) return false;
    // This acceptance probe is forbidden from writing outside workspace build artifacts.
    const auto root = std::filesystem::weakly_canonical(
        std::filesystem::path(L"H:\\refactor\\workspace\\lume1.0.3\\native-rebuild\\build-artifacts\\wallpaper-library-audit"));
    const auto actual = std::filesystem::weakly_canonical(value);
    auto expected = root.begin(), given = actual.begin();
    for (; expected != root.end(); ++expected, ++given)
        if (given == actual.end() || *given != *expected) return false;
    return actual != root;
}
void CryptoBoundaryChecks(const std::filesystem::path& root, const std::filesystem::path& media,
    const std::filesystem::path& output) {
    const auto keyPath = root / "cache-key.bin";
    const auto originalKey = [&] {
        std::ifstream input(keyPath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }();
    Expect(!originalKey.empty(), "existing DPAPI key is present");
    for (const auto position : {12u, 16u, 24u, 32u, 48u, 60u, 64u}) {
        const auto damaged = output / ("header-tamper-" + std::to_string(position) + ".apk");
        std::filesystem::copy_file(media, damaged, std::filesystem::copy_options::overwrite_existing);
        {
            std::fstream file(damaged, std::ios::in | std::ios::out | std::ios::binary);
            file.seekg(position); char byte = 0; file.read(&byte, 1); byte ^= 1;
            file.seekp(position); file.write(&byte, 1);
        }
        std::string error;
        Expect(!Reader::Open(root.u8string(), damaged.u8string(), error),
            "header length nonce salt flags reserved bytes and tag are authenticated");
    }
    const auto damaged = output / "second-record-tamper.apk";
    std::filesystem::copy_file(media, damaged, std::filesystem::copy_options::overwrite_existing);
    {
        std::fstream file(damaged, std::ios::in | std::ios::out | std::ios::binary);
        constexpr std::streamoff offset = 80 + 65536 + 16 + 8;
        file.seekg(offset); char byte = 0; file.read(&byte, 1); byte ^= 1;
        file.seekp(offset); file.write(&byte, 1);
    }
    std::string error;
    auto reader = Reader::Open(root.u8string(), damaged.u8string(), error);
    std::array<char, 65537> bytes{}; bytes.fill(static_cast<char>(0x5a));
    std::size_t read = 0;
    Expect(reader && !reader->Read(0, bytes.data(), bytes.size(), read) && read == 0 &&
        std::all_of(bytes.begin(), bytes.begin() + 65536, [](char byte) { return byte == 0; }),
        "later GCM record failure wipes previously decrypted caller bytes");
    reader.reset();
    const auto remainingKey = [&] {
        std::ifstream input(keyPath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }();
    Expect(originalKey == remainingKey, "opening or rejecting media never overwrites existing DPAPI key");
    const auto raceRoot = output / ("key-race-" + std::to_string(GetCurrentProcessId()) +
        "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(raceRoot / "media");
    auto factory = lume::platform::windows::wallpaper::MakeProtectedWriterFactory(raceRoot.u8string());
    std::atomic<bool> begin{false};
    std::atomic<unsigned> written{0};
    std::vector<std::thread> workers;
    for (unsigned index = 0; index < 8; ++index) {
        workers.emplace_back([&, index] {
            while (!begin.load()) Sleep(1);
            std::string reason;
            const auto path = raceRoot / "media" / ("race-" + std::to_string(index) + ".apk");
            auto writer = factory(path.u8string(), reason);
            const std::array<std::uint8_t, 4> plain{{1, 2, 3, 4}};
            if (writer && writer->Append(plain.data(), plain.size(), reason) && writer->Finish(reason))
                ++written;
        });
    }
    begin = true;
    for (auto& worker : workers) worker.join();
    Expect(written == 8, "concurrent first writers atomically share one DPAPI cache key");
    unsigned opened = 0;
    for (unsigned index = 0; index < 8; ++index) {
        std::string reason;
        auto testReader = Reader::Open(raceRoot.u8string(),
            (raceRoot / "media" / ("race-" + std::to_string(index) + ".apk")).u8string(), reason);
        std::array<char, 4> plain{};
        std::size_t count = 0;
        if (testReader && testReader->Read(0, plain.data(), plain.size(), count) &&
            count == plain.size() && plain == std::array<char, 4>{{1, 2, 3, 4}}) ++opened;
    }
    Expect(opened == 8, "all concurrently encrypted files decrypt with the winning published key");
}
}

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: protected-playback-test mpv.exe library-root encrypted.apk output-directory\n";
        return 2;
    }
    const auto executable = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    const auto root = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    const auto media = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
    const auto output = std::filesystem::absolute(std::filesystem::u8path(argv[4]));
    if (!WorkspaceOutput(output)) { std::cerr << "output must remain inside owned audit build-artifacts\n"; return 2; }
    std::filesystem::create_directories(output);
    CryptoBoundaryChecks(root, media, output);
    Json evidence{{"verification_only", true}, {"executable", executable.u8string()},
        {"encrypted_media", media.u8string()}, {"plaintext_staging", false}};
    std::string error;
    auto reader = Reader::Open(root.u8string(), media.u8string(), error);
    Expect(bool(reader), "open existing official encrypted download without migration");
    if (!reader) { std::cerr << error << '\n'; return 1; }
    evidence["verified_plaintext_bytes"] = reader->Size();
    std::weak_ptr<Reader> weakReader = reader;
    std::atomic<std::uint64_t> readCalls{0}, bytesRead{0};
    lume::platform::windows::WallpaperMediaSource source;
    source.bytes = reader->Size(); source.contentType = "video/mp4"; source.extension = "mp4";
    source.read = [reader, &readCalls, &bytesRead](std::uint64_t offset, char* buffer,
        std::size_t capacity, std::size_t& read) {
        ++readCalls;
        const bool ok = reader->Read(offset, buffer, capacity, read);
        if (ok) bytesRead += read;
        return ok;
    };
    lume::platform::windows::WallpaperMediaServer server;
    Expect(server.Origin().empty(), "media server is absent before first lease");
    const auto url = server.Register("official-verification", std::move(source));
    reader.reset();
    const auto lastSlash = url.find_last_of('/');
    const auto prefix = lastSlash == std::string::npos ? std::string{} : url.substr(0, lastSlash + 1);
    Expect(!url.empty() && !weakReader.expired(), "opaque lease owns only authenticated range reader");
    const auto displays = lume::platform::windows::EnumerateMpvDisplays();
    Expect(!displays.empty(), "display geometry can be enumerated without desktop mutation");
    if (displays.empty()) return 1;
    const auto parent = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC",
        L"Lume protected playback isolated verification", WS_POPUP, -30000, -30000, 640, 360,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Expect(parent != nullptr, "create owned off-screen parent rather than Explorer attachment");
    if (!parent) return 1;
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    {
        Controller controller(executable.wstring(), parent);
        std::atomic<bool> cancel{false}; controller.SetCancellation(&cancel);
        const auto id = displays.front().id;
        Expect(controller.AllowMediaUrlPrefix(prefix), "allow exact opaque capability prefix");
        const auto began = GetTickCount64();
        Expect(controller.Start(id, url), "real mpv decrypts and renders official first frame", &controller);
        evidence["start_elapsed_ms"] = GetTickCount64() - began;
        if (controller.IsRunning(id)) {
            auto children = OwnedChildren(parent);
            Expect(children.size() == 1, "exactly one owned mpv child in isolated container");
            Probe probe;
            Expect(!children.empty() && probe.Open(children.front()), "independent IPC verifies owned mpv PID");
            const auto path = probe.Property("path");
            Expect(path.is_string() && path.get<std::string>().rfind("lume://", 0) == 0,
                "protected media is read over the stream plugin's pipe, not loopback HTTP");
            evidence["player_path_scheme"] = path.is_string() ? path.get<std::string>().substr(0, 7) : "";
            const auto frames = probe.Property("video-params");
            Expect(frames.is_object() && frames.value("w", 0) > 0 && frames.value("h", 0) > 0,
                "decoder reports actual nonzero video frame dimensions");
            const auto first = probe.Property("time-pos");
            Pump(500);
            const auto second = probe.Property("time-pos");
            Expect(first.is_number() && second.is_number() && first != second,
                "official encrypted video timeline actually advances");
            Expect(controller.Command(id, MpvCommand::Pause), "pause official playback", &controller);
            const auto paused = probe.Property("time-pos");
            Pump(350);
            Expect(probe.Property("pause") == Json(true) && paused == probe.Property("time-pos"),
                "pause acknowledged and timeline remains fixed");
            Expect(controller.Command(id, MpvCommand::Resume), "resume official playback", &controller);
            Pump(350);
            Expect(probe.Property("pause") == Json(false) && paused != probe.Property("time-pos"),
                "resume acknowledged and timeline advances");
            Expect(probe.Property("mute") == Json(true), "original silent default retained");
            Expect(controller.EnsureAttached(id), "off-screen parent and IPC remain healthy", &controller);
            evidence["video_params"] = frames;
            evidence["hwdec_current"] = probe.Property("hwdec-current");
            evidence["current_vo"] = probe.Property("current-vo");
            evidence["pid"] = children.empty() ? 0 : children.front();
            // A decoded screenshot is an explicitly requested acceptance artifact,
            // not a cleartext media cache or a playback staging file.
            const auto screenshot = output / "official-protected-frame.png";
            auto result = probe.Request(Json::array({"screenshot-to-file", screenshot.u8string(), "video"}));
            Expect(result.is_object() && result.value("error", std::string{}) == "success" &&
                std::filesystem::is_regular_file(screenshot), "capture actual decoded official frame evidence");
            const HANDLE process = children.empty() ? nullptr : OpenProcess(SYNCHRONIZE, FALSE, children.front());
            controller.Stop(id);
            Expect(!controller.IsRunning(id) && controller.ActiveDisplays().empty(), "stop clears player ownership");
            if (process) {
                Expect(WaitForSingleObject(process, 1500) == WAIT_OBJECT_0, "stop terminates the owned mpv process");
                CloseHandle(process);
            }
        }
    }
    Expect(GetWindow(parent, GW_CHILD) == nullptr, "off-screen parent has no remaining player container");
    DestroyWindow(parent);
    const auto before = GetTickCount64();
    server.Remove("official-verification");
    Expect(server.Origin().empty() && server.Clients() == 0 && weakReader.expired(),
        "last lease releases listener clients and encrypted reader");
    evidence["lease_cleanup_ms"] = GetTickCount64() - before;
    evidence["read_calls"] = readCalls.load(); evidence["decrypted_bytes_served"] = bytesRead.load();
    evidence["failures"] = failures;
    server.Stop();
    std::ofstream(output / "protected-playback-evidence.json") << evidence.dump(2);
    return failures ? 1 : 0;
}
