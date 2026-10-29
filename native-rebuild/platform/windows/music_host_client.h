#pragma once

#include "core/music/music_host_protocol.h"

#include <cstdint>
#include <chrono>
#include <functional>
#include <string>

namespace lume::music {

struct MusicHostClientOptions {
    std::wstring executable;
    std::uint32_t timeoutMs = 1000;
    std::uint32_t loadTimeoutMs = 10000;
};

class MusicHostClient {
public:
    using EventHandler = std::function<void(std::string)>;

    explicit MusicHostClient(MusicHostClientOptions options);
    ~MusicHostClient();

    bool Start(EventHandler handler);
    bool Send(std::string frame);
    // Called on the owning worker; cancellation must be thread-safe and nonthrowing.
    bool Send(std::string frame, const std::function<bool()>& cancelled);
    void Stop();
    bool Running() const noexcept;

private:
    bool Transfer(bool write, void* buffer, std::uint32_t bytes,
                  std::chrono::steady_clock::time_point deadline,
                  const std::function<bool()>& cancelled);
    bool Connect();
    void StopProcess();

    MusicHostClientOptions options_;
    EventHandler handler_;
    void* pipe_ = reinterpret_cast<void*>(-1);
    void* process_ = nullptr;
};

}  // namespace lume::music
