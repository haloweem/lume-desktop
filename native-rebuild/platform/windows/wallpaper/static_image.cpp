#include "platform/windows/wallpaper/static_image.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>
#include <utility>

namespace lume::platform::windows::wallpaper {
namespace {
using Microsoft::WRL::ComPtr;
constexpr std::uint64_t MaximumPixels = 64ull * 1024 * 1024;
constexpr std::size_t MaximumRead = 65536;

bool Cancelled(const std::atomic<bool>* cancellation) {
    return cancellation && cancellation->load(std::memory_order_relaxed);
}

bool ValidPixels(std::uint64_t width, std::uint64_t height) {
    return width && height && width <= static_cast<std::uint64_t>((std::numeric_limits<int>::max)()) &&
        height <= static_cast<std::uint64_t>((std::numeric_limits<int>::max)()) &&
        width <= MaximumPixels / height;
}

struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};

struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};

struct ReadState {
    StaticImageSource source;
    const std::atomic<bool>* cancellation = nullptr;
    std::atomic<bool> failed{false};
    std::mutex mutex;
};

// This stream owns no plaintext buffer. Even codecs requesting a large Read
// receive data through bounded random-read calls; clones share the capability.
class SourceStream final : public IStream {
public:
    explicit SourceStream(std::shared_ptr<ReadState> state, std::uint64_t position = 0)
        : state_(std::move(state)), position_(position) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (id != IID_IUnknown && id != IID_ISequentialStream && id != IID_IStream)
            return E_NOINTERFACE;
        *output = static_cast<IStream*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references_;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Read(void* destination, ULONG requested, ULONG* transferred) override {
        if (transferred) *transferred = 0;
        if (!destination && requested) return STG_E_INVALIDPOINTER;
        std::lock_guard<std::mutex> lock(state_->mutex);
        ULONG done = 0;
        auto* output = static_cast<char*>(destination);
        while (done < requested && position_ < state_->source.bytes) {
            if (Cancelled(state_->cancellation)) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            const auto length = static_cast<std::size_t>((std::min)(
                (std::min)(static_cast<std::uint64_t>(requested - done),
                           state_->source.bytes - position_), static_cast<std::uint64_t>(MaximumRead)));
            std::size_t count = 0;
            bool success = false;
            try { success = state_->source.read(position_, output + done, length, count); }
            catch (...) { success = false; }
            if (!success || !count || count > length) {
                state_->failed.store(true, std::memory_order_relaxed);
                return STG_E_READFAULT;
            }
            position_ += count;
            done += static_cast<ULONG>(count);
            if (transferred) *transferred = done;
        }
        if (Cancelled(state_->cancellation)) return HRESULT_FROM_WIN32(ERROR_CANCELLED);
        return done == requested ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG* written) override {
        if (written) *written = 0;
        return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER movement, DWORD origin,
                                   ULARGE_INTEGER* newPosition) override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        std::uint64_t base = 0;
        if (origin == STREAM_SEEK_CUR) base = position_;
        else if (origin == STREAM_SEEK_END) base = state_->source.bytes;
        else if (origin != STREAM_SEEK_SET) return STG_E_INVALIDFUNCTION;
        std::uint64_t next = 0;
        if (movement.QuadPart >= 0) {
            const auto distance = static_cast<std::uint64_t>(movement.QuadPart);
            if (distance > state_->source.bytes - base) return STG_E_INVALIDFUNCTION;
            next = base + distance;
        } else {
            const auto distance = static_cast<std::uint64_t>(-(movement.QuadPart + 1)) + 1;
            if (distance > base) return STG_E_INVALIDFUNCTION;
            next = base - distance;
        }
        position_ = next;
        if (newPosition) newPosition->QuadPart = next;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*,
                                     ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override {
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (!stat) return E_POINTER;
        *stat = {};
        stat->type = STGTY_STREAM;
        stat->cbSize.QuadPart = state_->source.bytes;
        stat->grfMode = STGM_READ;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** clone) override {
        if (!clone) return E_POINTER;
        std::lock_guard<std::mutex> lock(state_->mutex);
        *clone = new (std::nothrow) SourceStream(state_, position_);
        return *clone ? S_OK : E_OUTOFMEMORY;
    }

private:
    ~SourceStream() = default;
    std::atomic<ULONG> references_{1};
    std::shared_ptr<ReadState> state_;
    std::uint64_t position_ = 0;
};

bool KnownRaster(const std::array<char, 16>& bytes, ULONG count) {
    const auto begins = [&](const char* signature, std::size_t length) {
        return count >= length && std::memcmp(bytes.data(), signature, length) == 0;
    };
    return begins("BM", 2) || begins("\x89PNG\r\n\x1a\n", 8) ||
        begins("\xff\xd8", 2) || begins("GIF87a", 6) || begins("GIF89a", 6) ||
        begins("II\x2a\0", 4) || begins("MM\0\x2a", 4) || begins("\0\0\x01\0", 4);
}

bool KnownRasterHint(const std::wstring& name, bool callbackHint) {
    auto path = std::wstring_view(name);
    const auto scheme = path.find(L"://");
    if (callbackHint && scheme != std::wstring_view::npos && scheme > 1)
        path = path.substr(0, path.find_first_of(L"?#"));
    const auto dot = path.find_last_of(L'.');
    const auto separator = path.find_last_of(L"/\\");
    if (dot == std::wstring_view::npos ||
        (separator != std::wstring_view::npos && dot <= separator)) return false;
    const auto extension = path.substr(dot);
    for (const auto expected : {std::wstring_view(L".bmp"), std::wstring_view(L".dib"),
            std::wstring_view(L".png"), std::wstring_view(L".jpg"), std::wstring_view(L".jpeg"),
            std::wstring_view(L".jpe"), std::wstring_view(L".jfif"), std::wstring_view(L".gif"),
            std::wstring_view(L".tif"), std::wstring_view(L".tiff"), std::wstring_view(L".ico")}) {
        if (extension.size() == expected.size() && std::equal(extension.begin(), extension.end(),
            expected.begin(), [](wchar_t actual, wchar_t wanted) {
                if (actual >= L'A' && actual <= L'Z')
                    actual = static_cast<wchar_t>(actual + (L'a' - L'A'));
                return actual == wanted;
            })) return true;
    }
    return false;
}

unsigned Orientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> metadata;
    if (FAILED(frame->GetMetadataQueryReader(&metadata))) return 1;
    for (const auto* query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
        PROPVARIANT value{};
        const auto result = metadata->GetMetadataByName(query, &value);
        unsigned orientation = 1;
        if (SUCCEEDED(result)) {
            if (value.vt == VT_UI2) orientation = value.uiVal;
            else if (value.vt == VT_UI4) orientation = value.ulVal;
        }
        PropVariantClear(&value);
        if (SUCCEEDED(result) && orientation >= 1 && orientation <= 8) return orientation;
    }
    return 1;
}

// Scatters one band of source rows (original orientation, `width` pixels per
// row, first row at `top`) to the EXIF-displayed position in `output`.
void ScatterOrientedRows(const std::uint32_t* band, unsigned top, unsigned rows,
    unsigned width, unsigned height, unsigned orientation, std::uint32_t* output) {
    const auto outputWidth = orientation >= 5 ? height : width;
    for (unsigned row = 0; row < rows; ++row) {
        const auto y = top + row;
        const auto* source = band + static_cast<std::size_t>(row) * width;
        for (unsigned x = 0; x < width; ++x) {
            unsigned destinationX = x, destinationY = y;
            switch (orientation) {
            case 2: destinationX = width - 1 - x; break;
            case 3: destinationX = width - 1 - x; destinationY = height - 1 - y; break;
            case 4: destinationY = height - 1 - y; break;
            case 5: destinationX = y; destinationY = x; break;
            case 6: destinationX = height - 1 - y; destinationY = x; break;
            case 7: destinationX = height - 1 - y; destinationY = width - 1 - x; break;
            case 8: destinationX = y; destinationY = width - 1 - x; break;
            default: break;
            }
            output[static_cast<std::size_t>(destinationY) * outputWidth + destinationX] = source[x];
        }
    }
}

// Copies the cropped source into `output` in displayed orientation. Reoriented
// images are pulled in bounded top-down bands and scattered directly, so the
// peak stays at one output frame plus about 1 MiB instead of two full frames.
bool CopyOriented(IWICBitmapSource* source, unsigned orientation, unsigned width,
    unsigned height, std::vector<std::uint32_t>& output, const std::atomic<bool>* cancellation) {
    const auto stride = width * 4u;  // width <= 64M pixels, so this fits UINT.
    if (orientation < 2 || orientation > 8)
        return SUCCEEDED(source->CopyPixels(nullptr, stride,
            static_cast<UINT>(output.size() * sizeof(std::uint32_t)),
            reinterpret_cast<BYTE*>(output.data())));
    constexpr unsigned BandBytes = 1024 * 1024;
    const auto rows = (std::max)(1u, (std::min)(height, BandBytes / stride));
    std::vector<std::uint32_t> band(static_cast<std::size_t>(rows) * width);
    for (unsigned top = 0; top < height; top += rows) {
        if (Cancelled(cancellation)) return false;
        const auto count = (std::min)(rows, height - top);
        const WICRect rect{0, static_cast<INT>(top), static_cast<INT>(width), static_cast<INT>(count)};
        if (FAILED(source->CopyPixels(&rect, stride, count * stride,
                                      reinterpret_cast<BYTE*>(band.data()))))
            return false;
        ScatterOrientedRows(band.data(), top, count, width, height, orientation, output.data());
    }
    return true;
}

StaticImageLoad Decode(const std::wstring& localPath, const StaticImageSource* source,
    int width, int height, bool cover, const std::atomic<bool>* cancellation,
    StaticImage& output, std::string& error) {
    const auto fail = [&](const char* message) {
        error = Cancelled(cancellation) ? "Static image loading was cancelled" : message;
        return StaticImageLoad::Failed;
    };
    if (Cancelled(cancellation)) return fail("Static image loading was cancelled");
    if (width <= 0 || height <= 0 || !ValidPixels(static_cast<unsigned>(width),
                                               static_cast<unsigned>(height)))
        return fail("Static image canvas exceeds the 64M pixel limit or has invalid dimensions");

    auto state = std::make_shared<ReadState>();
    state->cancellation = cancellation;
    if (source) {
        state->source = *source;
    } else {
        if (localPath.empty() || localPath.find(L'\0') != std::wstring::npos)
            return fail("Invalid static image file path");
        auto file = std::make_shared<File>();
        file->handle = CreateFileW(localPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size{};
        if (file->handle == INVALID_HANDLE_VALUE || !GetFileSizeEx(file->handle, &size) ||
            size.QuadPart <= 0) return fail("Cannot open static image file");
        state->source.bytes = static_cast<std::uint64_t>(size.QuadPart);
        state->source.read = [file](std::uint64_t offset, char* destination,
                                   std::size_t length, std::size_t& count) {
            count = 0;
            LARGE_INTEGER position{};
            position.QuadPart = static_cast<LONGLONG>(offset);
            DWORD transferred = 0;
            if (!SetFilePointerEx(file->handle, position, nullptr, FILE_BEGIN) ||
                !ReadFile(file->handle, destination, static_cast<DWORD>(length), &transferred, nullptr))
                return false;
            count = transferred;
            return true;
        };
    }
    if (!state->source.read || !state->source.bytes || state->source.bytes >
        static_cast<std::uint64_t>((std::numeric_limits<LONGLONG>::max)()))
        return fail("Invalid static image source size or reader");

    Apartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
        return fail("Cannot initialize static image decoder apartment");
    ComPtr<IStream> stream;
    stream.Attach(new SourceStream(state));
    std::array<char, 16> signature{};
    ULONG read = 0;
    const auto signatureSize = static_cast<ULONG>((std::min)(state->source.bytes,
                                                            std::uint64_t{16}));
    if (FAILED(stream->Read(signature.data(), signatureSize, &read)))
        return fail("Cannot read static image source");
    LARGE_INTEGER start{};
    if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)))
        return fail("Cannot seek static image source");
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&factory))))
        return fail("Cannot create static image decoder");
    ComPtr<IWICBitmapDecoder> decoder;
    const auto decoded = factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                                          WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(decoded)) {
        if (Cancelled(cancellation) || state->failed.load(std::memory_order_relaxed))
            return fail("Cannot read static image source");
        if ((decoded == WINCODEC_ERR_COMPONENTNOTFOUND || decoded == WINCODEC_ERR_UNKNOWNIMAGEFORMAT) &&
            !KnownRaster(signature, read) && !KnownRasterHint(localPath, source != nullptr)) {
            error = "No system decoder is available for this image format";
            return StaticImageLoad::Unsupported;
        }
        return fail("Static image is corrupt or cannot be decoded");
    }
    UINT frames = 0;
    if (FAILED(decoder->GetFrameCount(&frames)) || !frames)
        return fail("Cannot read static image frame count");
    if (Cancelled(cancellation) || state->failed.load(std::memory_order_relaxed))
        return fail("Cannot read static image source");
    if (frames != 1) {
        error = "Animated or multiframe images require the compatibility player";
        return StaticImageLoad::Unsupported;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    UINT sourceWidth = 0, sourceHeight = 0;
    if (FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(frame->GetSize(&sourceWidth, &sourceHeight)))
        return fail("Cannot read static image dimensions");
    if (!ValidPixels(sourceWidth, sourceHeight))
        return fail("Static image source exceeds the 64M pixel limit or has invalid dimensions");
    const auto orientation = Orientation(frame.Get());
    const bool swapAxes = orientation >= 5;
    const auto orientedWidth = swapAxes ? sourceHeight : sourceWidth;
    const auto orientedHeight = swapAxes ? sourceWidth : sourceHeight;
    auto targetWidth = static_cast<unsigned>(width), targetHeight = static_cast<unsigned>(height);
    const bool wider = static_cast<std::uint64_t>(orientedWidth) * targetHeight >
                       static_cast<std::uint64_t>(orientedHeight) * targetWidth;
    if (!cover) {
        if (wider) targetHeight = (std::max)(1u, static_cast<unsigned>(
            static_cast<std::uint64_t>(orientedHeight) * targetWidth / orientedWidth));
        else targetWidth = (std::max)(1u, static_cast<unsigned>(
            static_cast<std::uint64_t>(orientedWidth) * targetHeight / orientedHeight));
    }
    // Valid source/canvas dimensions are at most 64M each, so these products
    // and the ceiling additions fit in uint64_t without narrowing first.
    const auto scaledWidth = cover && wider ?
        (static_cast<std::uint64_t>(orientedWidth) * targetHeight + orientedHeight - 1) /
            orientedHeight : static_cast<std::uint64_t>(targetWidth);
    const auto scaledHeight = cover && !wider ?
        (static_cast<std::uint64_t>(orientedHeight) * targetWidth + orientedWidth - 1) /
            orientedWidth : static_cast<std::uint64_t>(targetHeight);
    if (scaledWidth > static_cast<std::uint64_t>((std::numeric_limits<INT>::max)()) ||
        scaledHeight > static_cast<std::uint64_t>((std::numeric_limits<INT>::max)()))
        return fail("Static image scaled dimensions exceed decoder limits");
    const auto originalScaledWidth = static_cast<UINT>(swapAxes ? scaledHeight : scaledWidth);
    const auto originalScaledHeight = static_cast<UINT>(swapAxes ? scaledWidth : scaledHeight);
    const auto originalCropWidth = swapAxes ? targetHeight : targetWidth;
    const auto originalCropHeight = swapAxes ? targetWidth : targetHeight;
    const auto orientedLeft = static_cast<UINT>((scaledWidth - targetWidth) / 2);
    const auto orientedTop = static_cast<UINT>((scaledHeight - targetHeight) / 2);
    auto originalLeft = swapAxes ? orientedTop : orientedLeft;
    auto originalTop = swapAxes ? orientedLeft : orientedTop;
    // Crop in the displayed coordinate system. Reflections change which side
    // receives the extra pixel when an odd number of scaled pixels is removed.
    if (orientation == 2 || orientation == 3 || orientation == 7 || orientation == 8)
        originalLeft = originalScaledWidth - originalCropWidth - originalLeft;
    if (orientation == 3 || orientation == 4 || orientation == 6 || orientation == 7)
        originalTop = originalScaledHeight - originalCropHeight - originalTop;
    WICRect crop{static_cast<INT>(originalLeft), static_cast<INT>(originalTop),
                 static_cast<INT>(originalCropWidth), static_cast<INT>(originalCropHeight)};
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return fail("Cannot convert static image pixels");
    StaticImage image;
    image.width = swapAxes ? targetHeight : targetWidth;
    image.height = swapAxes ? targetWidth : targetHeight;
    image.canvasWidth = width;
    image.canvasHeight = height;
    const auto pixelCount = static_cast<std::uint64_t>(image.width) * image.height;
    if (!ValidPixels(image.width, image.height) || pixelCount >
        (std::numeric_limits<UINT>::max)() / sizeof(std::uint32_t))
        return fail("Static image output exceeds the pixel buffer limit");
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(converter.Get(), originalScaledWidth, originalScaledHeight,
                                  WICBitmapInterpolationModeFant)))
        return fail("Cannot scale static image pixels");
    ComPtr<IWICBitmapClipper> clipper;
    if (FAILED(factory->CreateBitmapClipper(&clipper)) ||
        FAILED(clipper->Initialize(scaler.Get(), &crop)))
        return fail("Cannot crop static image pixels");
    if (Cancelled(cancellation)) return fail("Static image loading was cancelled");
    image.pixels.resize(static_cast<std::size_t>(pixelCount));
    if (!CopyOriented(clipper.Get(), orientation, image.width, image.height, image.pixels,
                      cancellation) ||
        state->failed.load(std::memory_order_relaxed))
        return fail("Cannot decode static image pixels");
    if (swapAxes) std::swap(image.width, image.height);
    for (std::size_t index = 0; index < image.pixels.size(); ++index) {
        if ((index % 65536 == 0) && Cancelled(cancellation))
            return fail("Static image loading was cancelled");
        // Premultiplied RGB already is the source composited over black.
        image.pixels[index] |= 0xff000000u;
    }
    if (Cancelled(cancellation)) return fail("Static image loading was cancelled");
    output = std::move(image);
    error.clear();
    return StaticImageLoad::Loaded;
}
}  // namespace

StaticImageLoad LoadStaticImage(const std::wstring& localPath, const StaticImageSource* source,
    int width, int height, bool cover, const std::atomic<bool>* cancellation,
    StaticImage& output, std::string& error) {
    try { return Decode(localPath, source, width, height, cover, cancellation, output, error); }
    catch (...) {
        error = Cancelled(cancellation) ? "Static image loading was cancelled" :
                                        "Static image decoder failed or exhausted memory";
        return StaticImageLoad::Failed;
    }
}

void PaintStaticImage(HDC dc, const RECT& bounds, const StaticImage& image) {
    if (!dc) return;
    const auto black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    const auto width = static_cast<std::int64_t>(bounds.right) - bounds.left;
    const auto height = static_cast<std::int64_t>(bounds.bottom) - bounds.top;
    if (width <= 0 || height <= 0 || width > (std::numeric_limits<int>::max)() ||
        height > (std::numeric_limits<int>::max)() || !ValidPixels(image.width, image.height) ||
        image.canvasWidth <= 0 || image.canvasHeight <= 0 ||
        image.width > static_cast<unsigned>(image.canvasWidth) ||
        image.height > static_cast<unsigned>(image.canvasHeight) ||
        image.pixels.size() != static_cast<std::uint64_t>(image.width) * image.height) {
        FillRect(dc, &bounds, black);
        return;
    }
    const auto drawWidth = (std::max)(std::int64_t{1}, width * image.width / image.canvasWidth);
    const auto drawHeight = (std::max)(std::int64_t{1}, height * image.height / image.canvasHeight);
    const auto x = static_cast<int>(static_cast<std::int64_t>(bounds.left) + (width - drawWidth) / 2);
    const auto y = static_cast<int>(static_cast<std::int64_t>(bounds.top) + (height - drawHeight) / 2);
    const auto right = static_cast<int>(x + drawWidth);
    const auto bottom = static_cast<int>(y + drawHeight);
    // Fill only the letterbox margins; the image area is written exactly once.
    const RECT margins[] = {
        {bounds.left, bounds.top, bounds.right, y}, {bounds.left, bottom, bounds.right, bounds.bottom},
        {bounds.left, y, x, bottom}, {right, y, bounds.right, bottom}};
    for (const auto& margin : margins)
        if (margin.right > margin.left && margin.bottom > margin.top) FillRect(dc, &margin, black);
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = static_cast<LONG>(image.width);
    bitmap.bmiHeader.biHeight = -static_cast<LONG>(image.height);
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    int copied = 0;
    if (drawWidth == static_cast<std::int64_t>(image.width) &&
        drawHeight == static_cast<std::int64_t>(image.height)) {
        // Cover/contain bitmaps are prepared at the target size, so the common
        // repaint is a direct copy without GDI's stretching path.
        copied = SetDIBitsToDevice(dc, x, y, image.width, image.height, 0, 0, 0, image.height,
                                   image.pixels.data(), &bitmap, DIB_RGB_COLORS);
    } else {
        const auto previousMode = SetStretchBltMode(dc, COLORONCOLOR);
        copied = StretchDIBits(dc, x, y, static_cast<int>(drawWidth), static_cast<int>(drawHeight),
            0, 0, static_cast<int>(image.width), static_cast<int>(image.height), image.pixels.data(),
            &bitmap, DIB_RGB_COLORS, SRCCOPY);
        if (previousMode) SetStretchBltMode(dc, previousMode);
    }
    if (!copied) {
        const RECT area{x, y, right, bottom};
        FillRect(dc, &area, black);
    }
}
bool SaveStaticImageJpeg(const StaticImage& image, const std::wstring& path) {
    if (!image.width || !image.height || image.pixels.size() < static_cast<std::size_t>(image.width) * image.height) return false;
    Apartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppBGR, image.width * 4,
            static_cast<UINT>(image.pixels.size() * sizeof(std::uint32_t)),
            reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())), &bitmap)) ||
        FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, &options))) return false;
    PROPBAG2 quality{};
    quality.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value{};
    value.vt = VT_R4;
    value.fltVal = 0.92f;
    if (options) options->Write(1, &quality, &value);
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    return SUCCEEDED(frame->Initialize(options.Get())) && SUCCEEDED(frame->SetSize(image.width, image.height)) &&
           SUCCEEDED(frame->SetPixelFormat(&format)) && SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
}  // namespace lume::platform::windows::wallpaper
