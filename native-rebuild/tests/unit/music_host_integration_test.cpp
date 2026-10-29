#include "platform/windows/music_host_client.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <chrono>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main(int argc, char** argv) {
    Expect(argc >= 2, "the host executable path is required");
    std::string latest;
    std::wstring executable;
    executable.reserve(std::strlen(argv[1]));
    for (const auto* cursor = argv[1]; *cursor; ++cursor) executable.push_back(static_cast<unsigned char>(*cursor));
    lume::music::MusicHostClient client({std::move(executable), 1000});
    Expect(client.Start([&](std::string event) { latest = std::move(event); }), "the host starts");
    Expect(client.Send(R"({"version":1,"session":"integration","sequence":1,"action":"hello"})"),
        "hello reaches the host");
    Expect(latest.find("\"status\":\"empty\"") != std::string::npos, "hello returns empty status");
    if (argc >= 4) {
        lume::music::HostCommand command;
        command.session = "integration";
        command.sequence = 2;
        command.action = "load";
        command.source = argv[2];
        const auto start = std::chrono::steady_clock::now();
        Expect(client.Send(lume::music::EncodeHostCommand(command)), "real MF delayed HTTP load outlives ordinary timeout");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        const auto event = lume::music::DecodeHostEvent(latest);
        Expect(event && event->error.empty() && event->duration > 1, "real delayed MF load returns decoded duration");
        Expect(elapsed >= 1100 && elapsed < 9000, "real network load has a bounded independent budget");
        std::cout << "MF delayed HTTP load elapsedMs=" << elapsed << '\n';
        command.sequence = 3;
        command.source = argv[3];
        const auto cancelStart = std::chrono::steady_clock::now();
        latest.clear();
        Expect(!client.Send(lume::music::EncodeHostCommand(command), [&] {
            return std::chrono::steady_clock::now() - cancelStart >= std::chrono::milliseconds(150);
        }), "cancel actual MF connection");
        const auto cancelledMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cancelStart).count();
        Expect(cancelledMs < 1000 && !client.Running() && latest.empty(),
            "stalled MF network host is killed promptly with no late callback");
        std::cout << "MF stalled HTTP cancellation elapsedMs=" << cancelledMs << '\n';
        client.Stop();
        return 0;
    }
    Expect(client.Send(R"({"version":1,"session":"integration","sequence":2,"action":"close"})"),
        "close reaches the host");
    Expect(latest.find("\"status\":\"closed\"") != std::string::npos, "close returns closed status");
    client.Stop();
    std::cout << "music host process integration passed\n";
    return 0;
}
