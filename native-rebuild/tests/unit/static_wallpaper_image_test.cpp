#include "platform/windows/wallpaper/static_image.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <wincodec.h>
#include <wrl/client.h>

namespace {
using namespace lume::platform::windows::wallpaper;
using Microsoft::WRL::ComPtr;

void Expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void Put(std::vector<char>& bytes, std::size_t offset, std::uint32_t value, unsigned count) {
    for (unsigned index = 0; index < count; ++index)
        bytes[offset + index] = static_cast<char>((value >> (index * 8)) & 0xff);
}

std::vector<char> Bmp(unsigned width, unsigned height,
                      const std::vector<std::uint32_t>& pixels) {
    std::vector<char> bytes(54 + pixels.size() * 4, 0);
    bytes[0] = 'B'; bytes[1] = 'M';
    Put(bytes, 2, static_cast<std::uint32_t>(bytes.size()), 4);
    Put(bytes, 10, 54, 4); Put(bytes, 14, 40, 4);
    Put(bytes, 18, width, 4); Put(bytes, 22, height, 4);
    Put(bytes, 26, 1, 2); Put(bytes, 28, 32, 2);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            Put(bytes, 54 + (static_cast<std::size_t>(height - 1 - y) * width + x) * 4,
                pixels[static_cast<std::size_t>(y) * width + x], 4);
    return bytes;
}

StaticImageSource Source(const std::vector<char>& bytes) {
    return {bytes.size(), [&bytes](std::uint64_t offset, char* output,
                                 std::size_t length, std::size_t& count) {
        if (offset > bytes.size() || length > bytes.size() - offset) return false;
        count = length;
        std::memcpy(output, bytes.data() + static_cast<std::size_t>(offset), count);
        return true;
    }};
}

void BmpPixels() {
    const std::vector<std::uint32_t> pixels{0xffff0000u, 0xff00ff00u,
                                          0xff0000ffu, 0xff112233u};
    const auto bytes = Bmp(2, 2, pixels);
    const auto source = Source(bytes);
    StaticImage image;
    std::string error;
    const auto result = LoadStaticImage({}, &source, 2, 2, true, nullptr, image, error);
    Expect(result == StaticImageLoad::Loaded, "owned BMP decodes without a player");
    Expect(image.width == 2 && image.height == 2 && image.canvasWidth == 2 &&
           image.canvasHeight == 2 && image.pixels == pixels,
           "BMP preserves opaque color channels and top-down row order");
}

std::vector<char> Png(unsigned width, unsigned height,
                      const std::vector<std::uint32_t>& pixels) {
    ComPtr<IWICImagingFactory> factory;
    Expect(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
           IID_PPV_ARGS(&factory))), "create fixture PNG encoder factory");
    ComPtr<IStream> stream;
    Expect(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "create fixture stream");
    ComPtr<IWICBitmapEncoder> encoder;
    Expect(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
           SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)),
           "initialize fixture PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    Expect(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
           SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(width, height)),
           "initialize fixture PNG frame");
    auto format = GUID_WICPixelFormat32bppBGRA;
    Expect(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA,
           "fixture PNG preserves straight alpha");
    Expect(SUCCEEDED(frame->WritePixels(height, width * 4,
        static_cast<UINT>(pixels.size() * 4), reinterpret_cast<BYTE*>(
            const_cast<std::uint32_t*>(pixels.data())))) && SUCCEEDED(frame->Commit()) &&
           SUCCEEDED(encoder->Commit()), "encode owned fixture PNG pixels");
    STATSTG stat{};
    HGLOBAL global = nullptr;
    Expect(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) &&
           SUCCEEDED(GetHGlobalFromStream(stream.Get(), &global)), "read PNG fixture size");
    const auto* memory = static_cast<const char*>(GlobalLock(global));
    Expect(memory != nullptr, "lock PNG fixture bytes");
    std::vector<char> bytes(memory, memory + static_cast<std::size_t>(stat.cbSize.QuadPart));
    GlobalUnlock(global);
    return bytes;
}

std::vector<char> Tiff(unsigned width, unsigned height,
                       const std::vector<std::uint32_t>& pixels, unsigned orientation) {
    constexpr unsigned entries = 11;
    constexpr unsigned sampleOffset = 8 + 2 + entries * 12 + 4;
    constexpr unsigned pixelOffset = sampleOffset + 6;
    std::vector<char> bytes(pixelOffset + pixels.size() * 3, 0);
    bytes[0] = 'I'; bytes[1] = 'I'; Put(bytes, 2, 42, 2); Put(bytes, 4, 8, 4);
    Put(bytes, 8, entries, 2);
    unsigned entry = 0;
    const auto tag = [&](unsigned id, unsigned type, unsigned count, unsigned value) {
        const auto offset = 10 + static_cast<std::size_t>(entry++) * 12;
        Put(bytes, offset, id, 2); Put(bytes, offset + 2, type, 2);
        Put(bytes, offset + 4, count, 4); Put(bytes, offset + 8, value, 4);
    };
    tag(256, 4, 1, width); tag(257, 4, 1, height);
    tag(258, 3, 3, sampleOffset); tag(259, 3, 1, 1); tag(262, 3, 1, 2);
    tag(273, 4, 1, pixelOffset); tag(274, 3, 1, orientation); tag(277, 3, 1, 3);
    tag(278, 4, 1, height); tag(279, 4, 1, static_cast<unsigned>(pixels.size() * 3));
    tag(284, 3, 1, 1);
    for (unsigned channel = 0; channel < 3; ++channel)
        Put(bytes, sampleOffset + channel * 2, 8, 2);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        bytes[pixelOffset + index * 3] = static_cast<char>(pixels[index] >> 16);
        bytes[pixelOffset + index * 3 + 1] = static_cast<char>(pixels[index] >> 8);
        bytes[pixelOffset + index * 3 + 2] = static_cast<char>(pixels[index]);
    }
    return bytes;
}

StaticImage Decode(const std::vector<char>& bytes, int width, int height, bool cover) {
    const auto source = Source(bytes);
    StaticImage image;
    std::string error;
    const auto result = LoadStaticImage({}, &source, width, height, cover, nullptr, image, error);
    if (result != StaticImageLoad::Loaded)
        throw std::runtime_error("fixture decode failed: " + error);
    Expect(error.empty(), "successful decode clears a prior error");
    return image;
}

void Aspect() {
    const std::vector<std::uint32_t> row{0xffff0000u, 0xffff0000u, 0xff00ff00u,
                                       0xff00ff00u, 0xff0000ffu, 0xff0000ffu};
    auto pixels = row;
    pixels.insert(pixels.end(), row.begin(), row.end());
    const auto bytes = Bmp(6, 2, pixels);
    const auto contain = Decode(bytes, 6, 6, false);
    Expect(contain.width == 6 && contain.height == 2 && contain.canvasWidth == 6 &&
           contain.canvasHeight == 6 && contain.pixels == pixels,
           "contain fits wide image without stretching or source-sized canvas");
    const auto cover = Decode(bytes, 2, 2, true);
    Expect(cover.width == 2 && cover.height == 2 &&
           cover.pixels == std::vector<std::uint32_t>(4, 0xff00ff00u),
           "cover crops the centered square before decoding target pixels");
    const auto scaled = Decode(bytes, 3, 1, false);
    Expect(scaled.width == 3 && scaled.height == 1 && scaled.pixels.size() == 3,
           "retained pixels have target dimensions rather than source dimensions");
    const auto thin = Decode(bytes, 1, 32, false);
    Expect(thin.width == 1 && thin.height == 1, "extreme contain keeps a nonempty bitmap");
}

void CoverScale() {
    std::vector<std::uint32_t> pixels;
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            pixels.push_back(0xff000000u | ((x * 70) << 16) | ((y * 70) << 8) | ((x + y) * 30));
    ComPtr<IWICImagingFactory> factory;
    Expect(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
           IID_PPV_ARGS(&factory))), "create independent cover reference factory");
    ComPtr<IWICBitmap> bitmap;
    Expect(SUCCEEDED(factory->CreateBitmapFromMemory(4, 4, GUID_WICPixelFormat32bppPBGRA,
           16, static_cast<UINT>(pixels.size() * 4), reinterpret_cast<BYTE*>(pixels.data()),
           &bitmap)), "create independent opaque cover reference bitmap");
    ComPtr<IWICBitmapScaler> scaler;
    Expect(SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
           SUCCEEDED(scaler->Initialize(bitmap.Get(), 5, 5, WICBitmapInterpolationModeFant)),
           "uniformly scale the complete square to an independent 5x5 reference");
    const auto bytes = Bmp(4, 4, pixels);
    for (const auto target : std::array<std::pair<int, int>, 2>{{{3, 5}, {5, 3}}}) {
        const WICRect crop{(5 - target.first) / 2, (5 - target.second) / 2,
                           target.first, target.second};
        std::vector<std::uint32_t> expected(static_cast<std::size_t>(target.first * target.second));
        Expect(SUCCEEDED(scaler->CopyPixels(&crop, static_cast<UINT>(target.first * 4),
               static_cast<UINT>(expected.size() * 4), reinterpret_cast<BYTE*>(expected.data()))),
               "read only the independent uniformly scaled center crop");
        const auto image = Decode(bytes, target.first, target.second, true);
        Expect(image.width == static_cast<unsigned>(target.first) &&
               image.height == static_cast<unsigned>(target.second) && image.pixels == expected,
               "cover preserves uniform geometry before clipping either canvas axis");
    }
}

void Alpha() {
    const auto image = Decode(Png(3, 1, {0x00ffffffu, 0x80ff0000u, 0xff00ff00u}), 3, 1, false);
    Expect(image.pixels == std::vector<std::uint32_t>{0xff000000u, 0xff800000u, 0xff00ff00u},
           "PNG straight alpha is composited over black without hidden RGB bleed");
}

void Orientations() {
    const std::vector<std::uint32_t> pixels{0xff110000u, 0xff002200u, 0xff000033u,
                                          0xff440000u, 0xff005500u, 0xff000066u};
    const std::array<std::vector<std::uint32_t>, 8> expected{{
        pixels,
        {pixels[2], pixels[1], pixels[0], pixels[5], pixels[4], pixels[3]},
        {pixels[5], pixels[4], pixels[3], pixels[2], pixels[1], pixels[0]},
        {pixels[3], pixels[4], pixels[5], pixels[0], pixels[1], pixels[2]},
        {pixels[0], pixels[3], pixels[1], pixels[4], pixels[2], pixels[5]},
        {pixels[3], pixels[0], pixels[4], pixels[1], pixels[5], pixels[2]},
        {pixels[5], pixels[2], pixels[4], pixels[1], pixels[3], pixels[0]},
        {pixels[2], pixels[5], pixels[1], pixels[4], pixels[0], pixels[3]}
    }};
    for (unsigned orientation = 1; orientation <= 8; ++orientation) {
        const bool swapped = orientation >= 5;
        const auto image = Decode(Tiff(3, 2, pixels, orientation), swapped ? 2 : 3,
                                  swapped ? 3 : 2, false);
        Expect(image.width == (swapped ? 2u : 3u) && image.height == (swapped ? 3u : 2u) &&
               image.pixels == expected[orientation - 1],
               "all eight EXIF orientations preserve colors and reflected axes");
    }
    const auto rotated = Tiff(3, 2, pixels, 6);
    const auto contain = Decode(rotated, 6, 6, false);
    Expect(contain.width == 4 && contain.height == 6,
           "contain computes aspect after EXIF swaps the source axes");
    const auto cover = Decode(rotated, 2, 3, true);
    Expect(cover.pixels == expected[5], "cover uses oriented aspect before target scaling");
    for (unsigned orientation = 1; orientation <= 8; ++orientation) {
        const unsigned orientedWidth = orientation >= 5 ? 2u : 3u;
        const unsigned orientedHeight = orientation >= 5 ? 3u : 2u;
        const unsigned left = (orientedWidth - 2) / 2;
        const unsigned top = (orientedHeight - 2) / 2;
        std::vector<std::uint32_t> cropped;
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 2; ++x)
                cropped.push_back(expected[orientation - 1][(top + y) * orientedWidth + left + x]);
        Expect(Decode(Tiff(3, 2, pixels, orientation), 2, 2, true).pixels == cropped,
               "odd center crop is positioned in EXIF-oriented coordinates before reflection");
    }
}

StaticImage Previous() { return {1, 1, 4, 5, {0xff123456u}}; }

void Same(const StaticImage& value, const StaticImage& previous) {
    Expect(value.width == previous.width && value.height == previous.height &&
           value.canvasWidth == previous.canvasWidth && value.canvasHeight == previous.canvasHeight &&
           value.pixels == previous.pixels, "failed/unsupported load preserves previous bitmap");
}

void Streams() {
    const auto bytes = Bmp(2, 2, {0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xff112233u});
    bool outOfBounds = false;
    std::size_t calls = 0;
    StaticImageSource shortSource{bytes.size(), [&](std::uint64_t offset, char* output,
                          std::size_t length, std::size_t& count) {
        ++calls;
        if (offset >= bytes.size() || !length || length > bytes.size() - offset) {
            outOfBounds = true; return false;
        }
        count = 1;
        *output = bytes[static_cast<std::size_t>(offset)];
        return true;
    }};
    StaticImage image;
    std::string error;
    Expect(LoadStaticImage({}, &shortSource, 2, 2, true, nullptr, image, error) ==
           StaticImageLoad::Loaded && !outOfBounds && calls > 50,
           "seekable callback retries short reads without crossing declared bounds");
    for (unsigned mode = 0; mode < 4; ++mode) {
        StaticImageSource failed{bytes.size(), [&, mode](std::uint64_t offset, char* output,
                                      std::size_t length, std::size_t& count) {
            if (mode == 0) return false;
            if (mode == 1) { count = 0; return true; }
            if (mode == 2) { count = length + 1; return true; }
            if (offset >= 54) throw std::runtime_error("owned callback failure");
            count = (std::min)(length, static_cast<std::size_t>(54 - offset));
            std::memcpy(output, bytes.data() + static_cast<std::size_t>(offset), count);
            return true;
        }};
        image = Previous();
        Expect(LoadStaticImage({}, &failed, 2, 2, true, nullptr, image, error) ==
               StaticImageLoad::Failed && !error.empty(),
               "callback failure, stalled/oversized reads and exceptions fail safely");
        Same(image, Previous());
    }
    const auto large = Bmp(400, 400, std::vector<std::uint32_t>(160000, 0xff789abcu));
    std::size_t maximum = 0;
    StaticImageSource bounded{large.size(), [&](std::uint64_t offset, char* output,
                                std::size_t length, std::size_t& count) {
        maximum = (std::max)(maximum, length);
        if (offset > large.size() || length > large.size() - offset) return false;
        count = length;
        std::memcpy(output, large.data() + static_cast<std::size_t>(offset), count);
        return true;
    }};
    Expect(LoadStaticImage({}, &bounded, 10, 10, true, nullptr, image, error) ==
           StaticImageLoad::Loaded && maximum <= 65536 && image.pixels.size() == 100,
           "large source uses bounded callback chunks and retains only target pixels");
}

void CancellationAndLimits() {
    const auto bytes = Bmp(2, 2, std::vector<std::uint32_t>(4, 0xff123456u));
    const auto source = Source(bytes);
    auto image = Previous();
    std::string error;
    std::atomic<bool> cancel{true};
    Expect(LoadStaticImage({}, &source, 2, 2, true, &cancel, image, error) ==
           StaticImageLoad::Failed && !error.empty(), "pre-cancelled load stops before decoding");
    Same(image, Previous());
    cancel.store(false);
    auto cancelling = source;
    cancelling.read = [&](std::uint64_t offset, char* output, std::size_t length, std::size_t& count) {
        const bool result = source.read(offset, output, length, count);
        cancel.store(true);
        return result;
    };
    Expect(LoadStaticImage({}, &cancelling, 2, 2, true, &cancel, image, error) ==
           StaticImageLoad::Failed, "cancellation during bounded reads stops the decode");
    Same(image, Previous());
    for (const auto size : std::array<std::pair<int, int>, 4>{{{0, 2}, {-1, 2},
           {8193, 8192}, {(std::numeric_limits<int>::max)(), (std::numeric_limits<int>::max)()}}}) {
        Expect(LoadStaticImage({}, &source, size.first, size.second, true, nullptr, image, error) ==
               StaticImageLoad::Failed, "invalid or over-64M target dimensions fail before allocation");
        Same(image, Previous());
    }
    const auto huge = Tiff(8193, 8192, {0xff123456u}, 1);
    const auto hugeSource = Source(huge);
    Expect(LoadStaticImage({}, &hugeSource, 2, 2, true, nullptr, image, error) ==
           StaticImageLoad::Failed && error.find("pixel") != std::string::npos,
           "over-64M source is rejected from frame dimensions before pixel decode");
    Same(image, Previous());
    const auto wide = Bmp(65536, 1, std::vector<std::uint32_t>(65536, 0xff123456u));
    const auto wideSource = Source(wide);
    for (const auto height : {32768, 65536}) {
        Expect(LoadStaticImage({}, &wideSource, 1, height, true, nullptr, image, error) ==
               StaticImageLoad::Failed && error.find("scaled dimensions") != std::string::npos,
               "cover rejects intermediate scale dimensions beyond INT/UINT before pixel copying");
        Same(image, Previous());
    }
    StaticImageSource invalid{(std::numeric_limits<std::uint64_t>::max)(), source.read};
    Expect(LoadStaticImage({}, &invalid, 2, 2, true, nullptr, image, error) ==
           StaticImageLoad::Failed, "unrepresentable stream length fails safely");
    Same(image, Previous());
}

void UnsupportedAndCorrupt() {
    const std::vector<char> unknown(32, 'x');
    auto source = Source(unknown);
    auto image = Previous();
    std::string error;
    Expect(LoadStaticImage({}, &source, 2, 2, true, nullptr, image, error) ==
           StaticImageLoad::Unsupported, "no image codec may use the compatibility player");
    Same(image, Previous());
    const std::array<std::vector<char>, 3> corrupt{{
        {'B', 'M', 0, 0, 0, 0},
        {static_cast<char>(0x89), 'P', 'N', 'G', '\r', '\n', static_cast<char>(0x1a), '\n', 0},
        {static_cast<char>(0xff), static_cast<char>(0xd8), static_cast<char>(0xff), 0}
    }};
    for (const auto& bytes : corrupt) {
        source = Source(bytes);
        Expect(LoadStaticImage({}, &source, 2, 2, true, nullptr, image, error) ==
               StaticImageLoad::Failed, "corrupt known raster signature is never unsupported");
        Same(image, Previous());
    }
    const unsigned char gif[] = {'G','I','F','8','9','a',1,0,1,0,0x80,0,0,
        0,0,0,255,255,255,
        0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,
        0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,0x3b};
    const std::vector<char> animated(reinterpret_cast<const char*>(gif),
        reinterpret_cast<const char*>(gif) + sizeof(gif));
    source = Source(animated);
    Expect(LoadStaticImage({}, &source, 2, 2, true, nullptr, image, error) ==
           StaticImageLoad::Unsupported, "multiframe GIF retains the compatibility player");
    Same(image, Previous());
    StaticImageSource empty{};
    Expect(LoadStaticImage({}, &empty, 2, 2, true, nullptr, image, error) == StaticImageLoad::Failed,
           "missing callback cannot be treated as a codec fallback");
}

void FormatHints() {
    GUID id{};
    wchar_t name[40]{};
    Expect(SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(id, name, 40) > 0,
           "create owned damaged-image fixture names");
    const auto base = std::filesystem::temp_directory_path() /
                      (std::wstring(L"lume-damaged-static-") + name);
    struct Cleanup {
        std::array<std::filesystem::path, 4> paths;
        ~Cleanup() {
            for (const auto& path : paths) {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        }
    } cleanup{{base.wstring() + L".png", base.wstring() + L".bmp",
               base.wstring() + L"#1.png", base.wstring() + L"#1.bmp"}};
    const std::vector<char> damaged(32, 'x');
    const auto source = Source(damaged);
    std::string error;
    auto image = Previous();
    bool localHintsFailed = true, extendedHintsFailed = true, filenameHintsFailed = true;
    for (const auto& path : cleanup.paths) {
        {
            std::ofstream file(path, std::ios::binary);
            file.write(damaged.data(), static_cast<std::streamsize>(damaged.size()));
            Expect(file.good(), "write owned damaged raster fixture");
        }
        localHintsFailed &= LoadStaticImage(path.wstring(), nullptr, 2, 2, true,
            nullptr, image, error) == StaticImageLoad::Failed;
        Same(image, Previous());
        const auto extended = std::wstring(L"\\\\?\\") + path.wstring();
        const auto file = CreateFileW(extended.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Expect(file != INVALID_HANDLE_VALUE, "extended Win32 path opens the owned raster fixture");
        CloseHandle(file);
        extendedHintsFailed &= LoadStaticImage(extended, nullptr, 2, 2, true,
            nullptr, image, error) == StaticImageLoad::Failed;
        Same(image, Previous());
        filenameHintsFailed &= LoadStaticImage(path.filename().wstring(), &source, 2, 2, true,
            nullptr, image, error) == StaticImageLoad::Failed;
        Same(image, Previous());
        const auto hint = L"http://127.0.0.1:1234/approved/fixture" + path.extension().wstring();
        Expect(LoadStaticImage(hint, &source, 2, 2, true, nullptr, image, error) ==
               StaticImageLoad::Failed,
               "approved callback URL raster hint rejects completely damaged signature");
        Same(image, Previous());
    }
    Expect(LoadStaticImage(L"http://127.0.0.1:1234/approved/image.PNG?token=owned#preview",
        &source, 2, 2, true, nullptr, image, error) == StaticImageLoad::Failed,
        "raster URL hint is case-insensitive and excludes query/fragment");
    Same(image, Previous());
    for (const auto* hint : {L"image.png#preview", L"image.bmp?preview"}) {
        Expect(LoadStaticImage(hint, &source, 2, 2, true, nullptr, image, error) ==
               StaticImageLoad::Unsupported,
               "plain callback filename hint keeps suffix characters as part of the name");
        Same(image, Previous());
    }
    for (const auto* hint : {L"http://127.0.0.1:1234/approved/image.webp",
                             L"http://127.0.0.1:1234/approved/image.avif"}) {
        Expect(LoadStaticImage(hint, &source, 2, 2, true, nullptr, image, error) ==
               StaticImageLoad::Unsupported,
               "WebP/AVIF hints do not turn a missing codec result into raster corruption");
        Same(image, Previous());
    }
    if (!localHintsFailed || !extendedHintsFailed || !filenameHintsFailed)
        throw std::runtime_error(std::string("damaged PNG/BMP hint classification: local ") +
            (localHintsFailed ? "Failed" : "not Failed") + ", extended " +
            (extendedHintsFailed ? "Failed" : "not Failed") + ", callback filename " +
            (filenameHintsFailed ? "Failed" : "not Failed"));
}

void PaintAndFile() {
    GUID id{};
    wchar_t name[40]{};
    Expect(SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(id, name, 40) > 0,
           "create owned local image fixture name");
    const auto path = std::filesystem::temp_directory_path() /
                      (std::wstring(L"lume-static-image-") + name + L".bmp");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{path};
    const auto bytes = Bmp(2, 1, {0xffff0000u, 0xff0000ffu});
    {
        std::ofstream file(path, std::ios::binary);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        Expect(file.good(), "write owned local BMP fixture");
    }
    StaticImage image;
    std::string error;
    Expect(LoadStaticImage(path.wstring(), nullptr, 4, 4, false, nullptr, image, error) ==
           StaticImageLoad::Loaded && image.width == 4 && image.height == 2,
           "local raster uses the same target-fitted decode as callback source");
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = 4; bitmap.bmiHeader.biHeight = -4;
    bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    const auto dib = CreateDIBSection(nullptr, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
    const auto dc = CreateCompatibleDC(nullptr);
    Expect(dib && dc && pixels, "create owned offscreen paint target");
    const auto old = SelectObject(dc, dib);
    std::fill_n(static_cast<std::uint32_t*>(pixels), 16, 0xffff00ffu);
    const RECT bounds{0, 0, 4, 4};
    PaintStaticImage(dc, bounds, image);
    GdiFlush();
    const auto* painted = static_cast<const std::uint32_t*>(pixels);
    const bool correct = (painted[0] & 0xffffffu) == 0 &&
        (painted[4] & 0xffffffu) == 0xff0000u && (painted[7] & 0xffffffu) == 0x0000ffu &&
        (painted[12] & 0xffffffu) == 0;
    SelectObject(dc, old); DeleteDC(dc); DeleteObject(dib);
    Expect(correct, "GDI paint draws centered content with black contain letterboxes");
    {
        // A window larger than the prepared canvas exercises the stretch path.
        BITMAPINFO large = bitmap;
        large.bmiHeader.biWidth = 8; large.bmiHeader.biHeight = -8;
        void* largePixels = nullptr;
        const auto largeDib = CreateDIBSection(nullptr, &large, DIB_RGB_COLORS, &largePixels, nullptr, 0);
        const auto largeDc = CreateCompatibleDC(nullptr);
        Expect(largeDib && largeDc && largePixels, "create owned stretched paint target");
        const auto previousBitmap = SelectObject(largeDc, largeDib);
        std::fill_n(static_cast<std::uint32_t*>(largePixels), 64, 0xffff00ffu);
        const RECT largeBounds{0, 0, 8, 8};
        PaintStaticImage(largeDc, largeBounds, image);
        GdiFlush();
        const auto* stretched = static_cast<const std::uint32_t*>(largePixels);
        bool letterboxed = true;
        for (const auto row : {0, 1, 6, 7})
            for (int column = 0; column < 8; ++column)
                letterboxed = letterboxed && (stretched[row * 8 + column] & 0xffffffu) == 0;
        const bool scaled = (stretched[2 * 8] & 0xffffffu) == 0xff0000u &&
            (stretched[5 * 8 + 7] & 0xffffffu) == 0x0000ffu;
        SelectObject(largeDc, previousBitmap); DeleteDC(largeDc); DeleteObject(largeDib);
        Expect(letterboxed && scaled,
               "stretched GDI paint scales content and fills only black letterbox margins");
    }
    const auto previous = image;
    Expect(LoadStaticImage(path.wstring() + std::wstring(1, L'\0') + L"suffix", nullptr,
        4, 4, false, nullptr, image, error) == StaticImageLoad::Failed,
        "embedded NUL local path cannot select a different file");
    Same(image, previous);
    Expect(std::filesystem::remove(path), "decoder releases the local file after load");
}
}  // namespace

int main() {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 2;
    int failures = 0;
    const std::array<std::pair<const char*, void (*)()>, 10> tests{{
        {"BMP pixels", BmpPixels}, {"aspect and crop", Aspect}, {"uniform cover scale", CoverScale},
        {"PNG alpha", Alpha},
        {"EXIF orientations", Orientations}, {"bounded random stream", Streams},
        {"cancellation and limits", CancellationAndLimits},
        {"unsupported and corrupt", UnsupportedAndCorrupt}, {"format hints", FormatHints},
        {"local file and paint", PaintAndFile}
    }};
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
        }
    }
    CoUninitialize();
    std::cout << tests.size() - static_cast<std::size_t>(failures) << '/' << tests.size()
              << " groups passed (owned fixtures only)\n";
    return failures ? 1 : 0;
}
