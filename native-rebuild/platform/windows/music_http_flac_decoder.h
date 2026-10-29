#pragma once

#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

struct IMFSample;

namespace lume::music {
// Only HTTP FLAC uses this decoder. Frame numbers come from the FLAC stream,
// avoiding estimated timestamps when the Windows decoder seeks over HTTP.
class HttpFlacDecoder {
public:
    HttpFlacDecoder();
    ~HttpFlacDecoder();
    bool Open(const std::wstring& url);
    std::uint32_t Channels() const;
    std::uint32_t Rate() const;
    std::uint64_t Frames() const;
    HRESULT Seek(double seconds);
    HRESULT Read(IMFSample** sample, DWORD* flags, LONGLONG* timestamp);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
