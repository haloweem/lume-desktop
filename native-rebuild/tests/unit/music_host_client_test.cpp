#include "platform/windows/music_host_client.h"

#include <cstdlib>
#include <iostream>
#include <windows.h>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool Exact(HANDLE pipe, void* bytes, DWORD size, bool write) {
    auto* cursor = static_cast<char*>(bytes);
    while (size) {
        DWORD transferred = 0;
        const bool ok = write ? WriteFile(pipe, cursor, size, &transferred, nullptr) :
            ReadFile(pipe, cursor, size, &transferred, nullptr);
        if (!ok || !transferred) return false;
        cursor += transferred; size -= transferred;
    }
    return true;
}

// This executable doubles as a controlled, credential-free host process.
int Child(const char* name) {
    HANDLE pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return 2;
    DWORD size = 0;
    if (!Exact(pipe, &size, sizeof(size), false) || size > lume::music::kMaxMusicHostFrameBytes) return 3;
    std::string frame(size, '\0');
    if (!Exact(pipe, frame.data(), size, false)) return 4;
    const auto command = lume::music::DecodeHostCommand(frame);
    if (!command) return 5;
    if (command->session == "crash") return 0;
    if (command->session == "stall") Sleep(30000);
    if (command->session == "slow") Sleep(1200);
    std::string reply = "fixture-response";
    size = static_cast<DWORD>(reply.size());
    if (!Exact(pipe, &size, sizeof(size), true)) return 6;
    for (auto& byte : reply) {
        if (command->session == "drip") Sleep(80);
        if (!Exact(pipe, &byte, 1, true)) return 7;
    }
    // Stay alive until the owner closes the pipe, just like the real host.
    char byte{};
    DWORD read = 0;
    ReadFile(pipe, &byte, 1, &read, nullptr);
    CloseHandle(pipe);
    return 0;
}

std::string Frame(const std::string& action, const std::string& session) {
    lume::music::HostCommand command;
    command.session = session;
    command.sequence = 1;
    command.action = action;
    if (action == "load") command.source = "https://fixture.invalid/tone.wav";
    return lume::music::EncodeHostCommand(command);
}
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--pipe") == 0) return Child(argv[2]);
    lume::music::MusicHostClient client({L"Z:\\lume\\missing\\LumeMusicHost.exe", 40});
    Expect(!client.Start([](std::string) {}), "a missing host cannot start");
    Expect(!client.Running(), "a failed host is not running");
    Expect(!client.Send(R"({"version":1})"), "a failed host rejects commands");
    client.Stop();
    wchar_t module[32768]{};
    const auto length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    const std::wstring executable(module, length);
    const auto run = [&](const char* action, const char* scenario, DWORD loadTimeout,
                         bool cancel, bool expected, long long minimum, long long maximum) {
        lume::music::MusicHostClient fixture({executable, 1000, loadTimeout});
        int callbacks = 0;
        Expect(fixture.Start([&](std::string reply) {
            ++callbacks; Expect(reply == "fixture-response", "entire reply delivered");
        }), "controlled host starts");
        const auto start = std::chrono::steady_clock::now();
        const auto cancelled = [&] {
            return cancel && std::chrono::steady_clock::now() - start > std::chrono::milliseconds(100);
        };
        Expect(fixture.Send(Frame(action, scenario), cancelled) == expected, "controlled request result");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        Expect(elapsed >= minimum && elapsed < maximum, "request total deadline and cancellation are bounded");
        Expect(callbacks == (expected ? 1 : 0), "failed/cancelled request never delivers late callback");
        if (!expected) Expect(!fixture.Running(), "failed request kills independent host");
        fixture.Stop();
        std::cout << action << "/" << scenario << " elapsedMs=" << elapsed << '\n';
    };
    run("load", "slow", 3000, false, true, 1100, 2500);
    run("hello", "slow", 3000, false, false, 900, 1900);
    run("load", "stall", 10000, true, false, 90, 900);
    run("load", "stall", 250, false, false, 200, 1100);
    run("load", "drip", 300, false, false, 250, 1100);
    run("load", "crash", 10000, false, false, 0, 900);
    run("hello", "ready", 10000, false, true, 0, 900);
    client.Stop();
    std::cout << "music host client failure behavior passed\n";
    return 0;
}
