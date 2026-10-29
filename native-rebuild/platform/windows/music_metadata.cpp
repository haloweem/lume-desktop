#include "platform/windows/music_metadata.h"
#include "platform/windows/music_local_files.h"

#include <windows.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <initguid.h>
#include <propkey.h>

#include <algorithm>
#include <cwchar>
#include <vector>

namespace lume::music {
namespace {
using Microsoft::WRL::ComPtr;

struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};

struct Variant {
    PROPVARIANT value{};
    ~Variant() { PropVariantClear(&value); }
};

std::string Text(const wchar_t* value) {
    if (!value) return {};
    const auto length = wcsnlen(value, 513);
    if (!length || length > 512) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
        static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (count <= 0 || count > 512) return {};
    std::string text(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length),
        text.data(), count, nullptr, nullptr);
    return text;
}

std::string Property(IPropertyStore* store, REFPROPERTYKEY key) {
    Variant value;
    if (FAILED(store->GetValue(key, &value.value))) return {};
    if (value.value.vt == VT_LPWSTR) return Text(value.value.pwszVal);
    if (value.value.vt == (VT_VECTOR | VT_LPWSTR)) {
        std::string joined;
        const auto& strings = value.value.calpwstr;
        if (strings.cElems > 64) return {};
        for (ULONG i = 0; i < strings.cElems; ++i) {
            const auto part = Text(strings.pElems[i]);
            if (part.empty()) continue;
            if (joined.size() + part.size() + (joined.empty() ? 0 : 2) > 512) break;
            if (!joined.empty()) joined += "; ";
            joined += part;
        }
        return joined;
    }
    return {};
}

std::string Artwork(IStream* input) {
    STATSTG stat{};
    if (!input || FAILED(input->Stat(&stat, STATFLAG_NONAME)) ||
        !stat.cbSize.QuadPart || stat.cbSize.QuadPart > 8 * 1024 * 1024) return {};
    LARGE_INTEGER zero{};
    if (FAILED(input->Seek(zero, STREAM_SEEK_SET, nullptr))) return {};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromStream(input, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame))) return {};
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || !width || !height ||
        width > 8192 || height > 8192 || static_cast<std::uint64_t>(width) * height > 16 * 1024 * 1024)
        return {};
    const auto edge = std::max(width, height);
    const auto outWidth = std::max(1u, edge > kMusicArtworkEdge ? width * kMusicArtworkEdge / edge : width);
    const auto outHeight = std::max(1u, edge > kMusicArtworkEdge ? height * kMusicArtworkEdge / edge : height);
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(frame.Get(), outWidth, outHeight, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    ComPtr<IStream> output;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> encoded;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &output)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(output.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&encoded, nullptr)) || FAILED(encoded->Initialize(nullptr)) ||
        FAILED(encoded->SetSize(outWidth, outHeight))) return {};
    auto format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(encoded->SetPixelFormat(&format)) ||
        FAILED(encoded->WriteSource(converter.Get(), nullptr)) || FAILED(encoded->Commit()) ||
        FAILED(encoder->Commit()) || FAILED(output->Stat(&stat, STATFLAG_NONAME)) ||
        !stat.cbSize.QuadPart || stat.cbSize.QuadPart > kMaxMusicArtworkBytes ||
        FAILED(output->Seek(zero, STREAM_SEEK_SET, nullptr))) return {};
    std::string bytes(static_cast<std::size_t>(stat.cbSize.QuadPart), '\0');
    ULONG read = 0;
    if (FAILED(output->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) || read != bytes.size())
        return {};
    return bytes;
}
}

MusicMetadata ReadMusicMetadata(const std::wstring& path) noexcept {
    MusicMetadata result;
    try {
        const auto file = InspectLocalMusicFile(path);
        if (!file.error.empty()) { result.warning = file.error; return result; }
        Apartment apartment;
        if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) {
            result.warning = "metadata-unavailable"; return result;
        }
        ComPtr<IPropertyStore> store;
        if (FAILED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_BESTEFFORT,
            IID_PPV_ARGS(&store)))) { result.warning = "metadata-unavailable"; return result; }
        result.title = Property(store.Get(), PKEY_Title);
        result.artist = Property(store.Get(), PKEY_Music_Artist);
        result.album = Property(store.Get(), PKEY_Music_AlbumTitle);
        Variant duration;
        if (SUCCEEDED(store->GetValue(PKEY_Media_Duration, &duration.value)) &&
            duration.value.vt == VT_UI8 && duration.value.uhVal.QuadPart <= 10000000000000000ull)
            result.duration = static_cast<double>(duration.value.uhVal.QuadPart) / 10000000.0;
        Variant cover;
        if (SUCCEEDED(store->GetValue(PKEY_ThumbnailStream, &cover.value)) &&
            cover.value.vt == VT_STREAM && cover.value.pStream) {
            result.artworkPng = Artwork(cover.value.pStream);
            if (result.artworkPng.empty()) result.warning = "artwork-invalid-or-too-large";
        }
    } catch (...) { result.warning = "metadata-unavailable"; }
    return result;
}

}  // namespace lume::music
