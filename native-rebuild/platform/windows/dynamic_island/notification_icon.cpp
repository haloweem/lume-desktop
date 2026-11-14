#include "platform/windows/dynamic_island/notification_icon.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace lume::platform::windows {
namespace {
using Microsoft::WRL::ComPtr;
constexpr std::array<std::string_view, NotificationIconCache::Capacity> Keys{
    "qq", "wechat", "water", "stand", "sleep", "severe", "rain", "temperature", "preview", "off-work", "off-work-squidward"};
constexpr std::size_t MaxFileBytes = 512 * 1024;
constexpr UINT MaxDimension = 256;

std::shared_ptr<const WidgetBitmap> Decode(std::string_view key) {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!length || length >= std::size(executable)) return {};
    const auto path = std::filesystem::path(executable).parent_path() / L"ui" / L"assets" / L"notifications" /
        (std::wstring(key.begin(), key.end()) + L".png");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto lengthBytes = file.tellg();
    if (lengthBytes <= 0 || lengthBytes > static_cast<std::streamoff>(MaxFileBytes)) return {};
    std::vector<BYTE> bytes(static_cast<std::size_t>(lengthBytes));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
    // Reject other formats before allowing WIC to select a decoder.
    constexpr std::array<BYTE, 8> signature{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    if (bytes.size() < signature.size() || !std::equal(signature.begin(), signature.end(), bytes.begin())) return {};
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT width = 0, height = 0, frames = 0; GUID format{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetContainerFormat(&format)) || !IsEqualGUID(format, GUID_ContainerFormatPng) ||
        FAILED(decoder->GetFrameCount(&frames)) || frames != 1 ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) ||
        !width || !height || width > MaxDimension || height > MaxDimension ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    auto bitmap = std::make_shared<WidgetBitmap>();
    bitmap->width = width; bitmap->height = height;
    bitmap->pixels.resize(static_cast<std::size_t>(width) * height);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, width * height * 4,
        reinterpret_cast<BYTE*>(bitmap->pixels.data())))) return {};
    return bitmap;
}
} // namespace

std::shared_ptr<const WidgetBitmap> NotificationIconCache::Load(std::string_view key) {
    const auto found = std::find(Keys.begin(), Keys.end(), key);
    if (found == Keys.end()) return {};
    const auto index = static_cast<std::size_t>(found - Keys.begin());
    if (!attempted_[index]) {
        attempted_[index] = true;
        bitmaps_[index] = Decode(*found);
    }
    return bitmaps_[index];
}
void NotificationIconCache::Clear() {
    bitmaps_ = {}; attempted_ = {};
}
} // namespace lume::platform::windows
