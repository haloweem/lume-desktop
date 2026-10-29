#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace lume::platform::windows {
// A server lease exposes one already-approved media object. It never resolves
// URL paths on disk and never receives a cache key or arbitrary filename.
struct WallpaperMediaSource {
    std::uint64_t bytes=0;
    std::string contentType;
    std::string extension;
    // Reads must be bounded local operations and may not reenter this server's
    // lifecycle methods. Remove/Stop wait for an in-progress read to release
    // its reader/key, after interrupting the associated network socket.
    std::function<bool(std::uint64_t,char*,std::size_t,std::size_t&)> read;
};
class WallpaperMediaServer final {
public:
    // Empty disables CORS. Otherwise only https://lume.local or an explicitly
    // injected http://127.0.0.1:<port> test origin is accepted; invalid values throw.
    explicit WallpaperMediaServer(std::string allowedOrigin={});
    ~WallpaperMediaServer();
    WallpaperMediaServer(const WallpaperMediaServer&)=delete;
    WallpaperMediaServer& operator=(const WallpaperMediaServer&)=delete;
    std::string Register(const std::string& id,WallpaperMediaSource source);
    void Remove(const std::string& id);
    void Stop();
    std::string Origin()const;
    unsigned Clients()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
