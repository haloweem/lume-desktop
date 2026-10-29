#include "platform/windows/music_http_flac_decoder.h"
#include "platform/windows/music_http_stream.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <limits>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#pragma warning(push, 0)
#include "vendor/dr-flac/0.13.3/dr_flac.h"
#pragma warning(pop)

namespace lume::music {
using Microsoft::WRL::ComPtr;
struct HttpFlacDecoder::Impl {
    ComPtr<IStream> stream;
    drflac* decoder = nullptr;
    bool failed = false;
    ~Impl() { if (decoder) drflac_close(decoder); }
    static size_t ReadBytes(void* context, void* data, size_t count) {
        auto& self = *static_cast<Impl*>(context);
        ULONG read = 0;
        if (count > std::numeric_limits<ULONG>::max() || FAILED(self.stream->Read(data,
                static_cast<ULONG>(count), &read))) self.failed = true;
        return self.failed ? 0 : read;
    }
    static drflac_bool32 SeekBytes(void* context, int offset, drflac_seek_origin origin) {
        auto& self = *static_cast<Impl*>(context);
        STATSTG state{};
        ULARGE_INTEGER current{};
        LARGE_INTEGER zero{};
        if (FAILED(self.stream->Stat(&state, STATFLAG_NONAME)) ||
            (origin == DRFLAC_SEEK_CUR && FAILED(self.stream->Seek(zero, STREAM_SEEK_CUR, &current)))) {
            self.failed = true;
            return DRFLAC_FALSE;
        }
        const auto length = state.cbSize.QuadPart;
        const auto base = origin == DRFLAC_SEEK_END ? length : current.QuadPart;
        const auto position = static_cast<LONGLONG>(base) + offset;
        // Binary search deliberately probes beyond EOF. IStream permits that,
        // but the decoder needs a rejection to narrow its search bounds.
        if (position < 0 || static_cast<QWORD>(position) > length) return DRFLAC_FALSE;
        LARGE_INTEGER move{};
        move.QuadPart = position;
        const auto hr = self.stream->Seek(move, STREAM_SEEK_SET, nullptr);
        if (FAILED(hr)) self.failed = true;
        return SUCCEEDED(hr);
    }
    static drflac_bool32 Tell(void* context, drflac_int64* position) {
        auto& self = *static_cast<Impl*>(context);
        ULARGE_INTEGER current{};
        LARGE_INTEGER zero{};
        if (FAILED(self.stream->Seek(zero, STREAM_SEEK_CUR, &current))) { self.failed = true; return DRFLAC_FALSE; }
        *position = static_cast<drflac_int64>(current.QuadPart);
        return DRFLAC_TRUE;
    }
};

HttpFlacDecoder::HttpFlacDecoder() : impl_(std::make_unique<Impl>()) {}
HttpFlacDecoder::~HttpFlacDecoder() = default;
bool HttpFlacDecoder::Open(const std::wstring& url) {
    if (impl_->decoder || FAILED(CreateMusicHttpFlacStream(url, &impl_->stream))) return false;
    impl_->decoder = drflac_open(Impl::ReadBytes, Impl::SeekBytes, Impl::Tell, impl_.get(), nullptr);
    const auto* decoder = impl_->decoder;
    return decoder && !impl_->failed && decoder->channels > 0 && decoder->channels <= 8 &&
        decoder->sampleRate >= 8000 && decoder->sampleRate <= 192000 &&
        decoder->bitsPerSample > 0 && decoder->bitsPerSample <= 32 &&
        decoder->totalPCMFrameCount > 0;
}
std::uint32_t HttpFlacDecoder::Channels() const { return impl_->decoder->channels; }
std::uint32_t HttpFlacDecoder::Rate() const { return impl_->decoder->sampleRate; }
std::uint64_t HttpFlacDecoder::Frames() const { return impl_->decoder->totalPCMFrameCount; }
HRESULT HttpFlacDecoder::Seek(double seconds) {
    if (!impl_->decoder || impl_->failed || !std::isfinite(seconds) || seconds < 0) return E_FAIL;
    const auto frame = static_cast<drflac_uint64>(std::min(seconds * Rate(), static_cast<double>(Frames())));
    return drflac_seek_to_pcm_frame(impl_->decoder, frame) && !impl_->failed ? S_OK : E_FAIL;
}
HRESULT HttpFlacDecoder::Read(IMFSample** sample, DWORD* flags, LONGLONG* timestamp) {
    *sample = nullptr;
    *flags = 0;
    if (impl_->failed) return E_FAIL;
    const auto first = impl_->decoder->currentPCMFrame;
    *timestamp = static_cast<LONGLONG>(first * 10000000ull / Rate());
    const auto frames = Rate() / 20;
    ComPtr<IMFMediaBuffer> buffer;
    BYTE* data = nullptr;
    auto hr = MFCreateMemoryBuffer(frames * Channels() * sizeof(std::int16_t), &buffer);
    if (FAILED(hr) || FAILED(hr = buffer->Lock(&data, nullptr, nullptr))) return hr;
    const auto read = drflac_read_pcm_frames_s16(impl_->decoder, frames, reinterpret_cast<drflac_int16*>(data));
    buffer->Unlock();
    if (impl_->failed || (!read && first < Frames())) return E_FAIL;
    if (impl_->decoder->currentPCMFrame >= Frames()) *flags = MF_SOURCE_READERF_ENDOFSTREAM;
    if (!read) return S_OK;
    ComPtr<IMFSample> result;
    if (FAILED(hr = buffer->SetCurrentLength(static_cast<DWORD>(read * Channels() * sizeof(std::int16_t)))) ||
        FAILED(hr = MFCreateSample(&result)) || FAILED(hr = result->AddBuffer(buffer.Get()))) return hr;
    *sample = result.Detach();
    return S_OK;
}
}
