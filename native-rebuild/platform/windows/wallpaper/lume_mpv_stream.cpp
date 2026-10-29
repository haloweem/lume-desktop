// mpv C plugin (loaded with --scripts=lume-mpv-stream.dll). Registers the
// lume:// protocol, which reads Lume's protected wallpapers from the media
// server's named pipe instead of a loopback HTTP connection, so security
// software that filters network access of unsigned programs cannot stall
// playback. See wallpaper_pipe.h for the protocol.
#define MPV_CPLUGIN_DYNAMIC_SYM
#include <windows.h>
#include <mpv/client.h>
#include <mpv/stream_cb.h>
#include "platform/windows/wallpaper/wallpaper_pipe.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace pipe = lume::platform::windows::wallpaper_pipe;
namespace {
struct Stream {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::uint64_t size = 0, position = 0;
};
bool Exchange(HANDLE handle, bool write, void* data, DWORD size) {
    auto* bytes = static_cast<char*>(data);
    while (size) {
        DWORD done = 0;
        const BOOL ok = write ? WriteFile(handle, bytes, size, &done, nullptr) : ReadFile(handle, bytes, size, &done, nullptr);
        if (!ok || !done) return false;
        bytes += done;
        size -= done;
    }
    return true;
}
int64_t Read(void* cookie, char* buffer, uint64_t count) {
    auto& s = *static_cast<Stream*>(cookie);
    if (s.position >= s.size || !count) return 0;
    pipe::Request request{s.position, static_cast<std::uint32_t>(std::min<uint64_t>(count, pipe::MaxRead))};
    std::uint32_t got = 0;
    if (!Exchange(s.handle, true, &request, sizeof(request)) || !Exchange(s.handle, false, &got, 4) ||
        got > request.length || (got && !Exchange(s.handle, false, buffer, got)))
        return -1;
    s.position += got;
    return got;
}
int64_t Seek(void* cookie, int64_t offset) {
    auto& s = *static_cast<Stream*>(cookie);
    if (offset < 0 || static_cast<uint64_t>(offset) > s.size) return MPV_ERROR_GENERIC;
    s.position = static_cast<uint64_t>(offset);
    return offset;
}
int64_t Size(void* cookie) { return static_cast<int64_t>(static_cast<Stream*>(cookie)->size); }
void Close(void* cookie) {
    auto* s = static_cast<Stream*>(cookie);
    if (s->handle != INVALID_HANDLE_VALUE) CloseHandle(s->handle);
    delete s;
}
// lume://<port>/<token>/media.<ext>
int Open(void*, char* uri, mpv_stream_cb_info* info) {
    const std::string url(uri ? uri : "");
    constexpr char scheme[] = "lume://";
    if (url.compare(0, sizeof(scheme) - 1, scheme) != 0) return MPV_ERROR_LOADING_FAILED;
    const auto slash = url.find('/', sizeof(scheme) - 1);
    if (slash == std::string::npos || slash == sizeof(scheme) - 1 || slash - (sizeof(scheme) - 1) > 5)
        return MPV_ERROR_LOADING_FAILED;
    unsigned port = 0;
    for (auto i = sizeof(scheme) - 1; i < slash; ++i) {
        if (url[i] < '0' || url[i] > '9') return MPV_ERROR_LOADING_FAILED;
        port = port * 10 + static_cast<unsigned>(url[i] - '0');
    }
    const auto path = url.substr(slash);
    if (!port || port > 65535 || path.size() > pipe::MaxPath) return MPV_ERROR_LOADING_FAILED;
    const auto name = pipe::Name(port);
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 50 && handle == INVALID_HANDLE_VALUE; ++attempt) {
        handle = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_PIPE_BUSY) return MPV_ERROR_LOADING_FAILED;
            WaitNamedPipeW(name.c_str(), 100);
        }
    }
    if (handle == INVALID_HANDLE_VALUE) return MPV_ERROR_LOADING_FAILED;
    auto* s = new Stream{handle};
    std::uint32_t length = static_cast<std::uint32_t>(path.size());
    if (!Exchange(handle, true, const_cast<char*>(pipe::Magic), 4) || !Exchange(handle, true, &length, 4) ||
        !Exchange(handle, true, const_cast<char*>(path.data()), length) || !Exchange(handle, false, &s->size, 8) ||
        !s->size) {
        Close(s);
        return MPV_ERROR_LOADING_FAILED;
    }
    info->cookie = s;
    info->read_fn = Read;
    info->seek_fn = Seek;
    info->size_fn = Size;
    info->close_fn = Close;
    return 0;
}
}  // namespace

extern "C" __declspec(dllexport) int mpv_open_cplugin(mpv_handle* handle) {
    if (mpv_stream_cb_add_ro(handle, "lume", nullptr, Open) < 0) return -1;
    while (mpv_wait_event(handle, -1)->event_id != MPV_EVENT_SHUTDOWN) {}
    return 0;
}
