#pragma once
#include <cstdint>
#include <string>

// How the wallpaper player reads protected media without a network
// connection. The media server's loopback URL
//     http://127.0.0.1:<port>/<token>/media.<ext>
// becomes, for mpv,
//     lume://<port>/<token>/media.<ext>
// which the lume-mpv-stream plugin (inside mpv.exe) serves from the pipe
// \\.\pipe\lume-wallpaper-<port>. The port is held exclusively by Lume's
// media server, so the name is unique while it runs.
//
// Byte protocol, little endian:
//   client: "LWP1", u32 path length, path ("/<token>/media.<ext>")
//   server: u64 size (0: unknown or revoked; the server then hangs up)
//   repeat: client Request; server u32 count, then count bytes (0: end or error)
namespace lume::platform::windows::wallpaper_pipe {
constexpr char Magic[4] = {'L', 'W', 'P', '1'};
constexpr std::uint32_t MaxPath = 512, MaxRead = 1u << 20;
#pragma pack(push, 1)
struct Request { std::uint64_t offset; std::uint32_t length; };
#pragma pack(pop)
inline std::wstring Name(unsigned port) { return L"\\\\.\\pipe\\lume-wallpaper-" + std::to_wstring(port); }
// The mpv URL for a media server URL; empty when it is not one.
inline std::string PlayerUrl(const std::string& url) {
    constexpr char origin[] = "http://127.0.0.1:";
    if (url.compare(0, sizeof(origin) - 1, origin) != 0) return {};
    return "lume://" + url.substr(sizeof(origin) - 1);
}
}  // namespace lume::platform::windows::wallpaper_pipe
