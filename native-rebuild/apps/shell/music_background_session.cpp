#include "apps/shell/music_background_session.h"
#include "platform/windows/text_util.h"

#include "core/music/music_background_store.h"
#include "apps/shell/music_background_session_legacy.h"
#include "platform/windows/local_settings_file_store.h"
#include "core/wallpaper/library.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include "platform/windows/wallpaper/download.h"
#include "platform/windows/wallpaper/media_server.h"

#include <shobjidl.h>
#include <wincodec.h>
#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <array>
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <thread>
#include <set>
#include <cstring>

namespace lume::shell {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
using Action = ui::MusicBackgroundAction;
using Result = music::MusicBackgroundResult;
using Storage = music::MusicBackgroundStorage;
using Media = music::MusicBackgroundMedia;
namespace protected_media = platform::windows::wallpaper;
using Reader = protected_media::EncryptedMediaReader;
using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kMaxMediaBytes = 512ull * 1024ull * 1024ull;
constexpr std::size_t kMaxThumbnailBytes = 8 * 1024 * 1024;

using lume::text::Utf8;

std::wstring ChooseWithDialog(HWND owner, IFileOpenDialog*& activeDialog) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog))))
        return {};
    activeDialog = dialog;
    struct DialogScope {
        IFileOpenDialog*& active;
        IFileOpenDialog* dialog;
        ~DialogScope() { active = nullptr; dialog->Release(); }
    } scope{activeDialog, dialog};
    const COMDLG_FILTERSPEC filters[] = {
        {L"音乐背景图片和视频", L"*.jpg;*.jpeg;*.png;*.webp;*.gif;*.mp4;*.webm"}
    };
    dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_DONTADDTORECENT);
    dialog->SetFileTypes(1, filters);
    dialog->SetTitle(L"选择音乐背景");
    if (!activeDialog) return {};
    std::wstring result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    return result;
}

std::string LowerExtension(const fs::path& path) {
    auto extension = path.extension().wstring();
    if (!extension.empty() && extension.front() == L'.') extension.erase(extension.begin());
    for (auto& value : extension) value = static_cast<wchar_t>(towlower(value));
    return Utf8(extension);
}

std::optional<std::pair<music::MusicBackgroundKind, std::string>> Format(std::string_view extension) {
    if (extension == "jpg" || extension == "jpeg") return {{music::MusicBackgroundKind::Image, "image/jpeg"}};
    if (extension == "png") return {{music::MusicBackgroundKind::Image, "image/png"}};
    if (extension == "webp") return {{music::MusicBackgroundKind::Image, "image/webp"}};
    if (extension == "gif") return {{music::MusicBackgroundKind::Image, "image/gif"}};
    if (extension == "mp4") return {{music::MusicBackgroundKind::Video, "video/mp4"}};
    if (extension == "webm") return {{music::MusicBackgroundKind::Video, "video/webm"}};
    return {};
}

bool ReparsePoint(const fs::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool RegularFile(const fs::path& path, std::uint64_t& size) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error || fs::is_symlink(status) || !fs::is_regular_file(status) || ReparsePoint(path)) return false;
    size = fs::file_size(path, error);
    return !error && size > 0 && size <= kMaxMediaBytes + 1024 * 1024;
}

std::string StatePath(const MusicBackgroundSessionOptions& options) {
    if (!options.statePath.empty()) return options.statePath;
    if (options.root.empty()) return {};
    return (fs::u8path(options.root) / "music-background.json").u8string();
}

bool SafePath(const fs::path& path) {
    if (path.empty() || !path.is_absolute()) return false;
    for (auto current = path; !current.empty();) {
        if (ReparsePoint(current)) return false;
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return true;
}

fs::path Absolute(const std::string& value) {
    if (value.empty() || value.find('\0') != std::string::npos) return {};
    return fs::absolute(fs::u8path(value)).lexically_normal();
}

struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::mutex readMutex;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool Open(const fs::path& path) {
        if (!SafePath(path)) return false;
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        return handle != INVALID_HANDLE_VALUE && GetFileType(handle) == FILE_TYPE_DISK &&
            GetFileInformationByHandle(handle, &info) && info.nNumberOfLinks == 1 &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    }
    std::uint64_t Size() const {
        LARGE_INTEGER size{};
        return GetFileSizeEx(handle, &size) && size.QuadPart > 0 ?
            static_cast<std::uint64_t>(size.QuadPart) : 0;
    }
    bool Read(std::uint64_t offset, char* buffer, std::size_t capacity, std::size_t& read) {
        std::lock_guard lock(readMutex);
        read = 0;
        const auto size = Size();
        if ((!buffer && capacity) || offset > size || size > kMaxMediaBytes) return false;
        const auto count = static_cast<DWORD>(std::min<std::uint64_t>(
            std::min<std::size_t>(capacity, 64 * 1024), size - offset));
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        DWORD actual = 0;
        if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) ||
            !ReadFile(handle, buffer, count, &actual, nullptr) || actual != count) return false;
        read = actual;
        return true;
    }
};

struct DirectoryPins {
    std::vector<HANDLE> handles;
    ~DirectoryPins() { for (const auto handle : handles) CloseHandle(handle); }
    bool Pin(const fs::path& directory) {
        if (!directory.is_absolute()) return false;
        fs::path part = directory.root_path();
        std::vector<fs::path> paths{part};
        for (const auto& component : directory.relative_path()) {
            if (component == L".." || component == L".") return false;
            part /= component; paths.push_back(part);
        }
        if (paths.size() > 128) return false;
        for (const auto& path : paths) {
            const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return false;
            handles.push_back(handle);
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info) ||
                !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        }
        return true;
    }
};

bool Nested(const fs::path& left, const fs::path& right) {
    auto a = left.wstring(), b = right.wstring();
    for (auto& c : a) c = static_cast<wchar_t>(towlower(c));
    for (auto& c : b) c = static_cast<wchar_t>(towlower(c));
    return a == b || (a.size() > b.size() && a.compare(0, b.size(), b) == 0 &&
        (a[b.size()] == L'\\' || a[b.size()] == L'/'));
}

std::string AssetId() {
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw std::runtime_error("random");
    const char* digits = "0123456789abcdef";
    std::string id = "background-";
    for (const auto byte : bytes) { id += digits[byte >> 4]; id += digits[byte & 15]; }
    return id;
}

struct PlainBuffer {
    std::array<unsigned char, 64 * 1024> bytes{};
    ~PlainBuffer() { SecureZeroMemory(bytes.data(), bytes.size()); }
};

template<class T> struct Com {
    T* value = nullptr;
    ~Com() { if (value) value->Release(); }
    T** Put() { return &value; }
    T* operator->() const { return value; }
};

// Decoders get a seekable, read-only capability, never a pathname. The shared
// budget also covers clones used by the native codecs.
class PreviewStream final : public IStream {
public:
    struct Input {
        std::uint64_t size = 0;
        std::function<bool(std::uint64_t, char*, std::size_t, std::size_t&)> read;
        const std::atomic<bool>* cancel = nullptr;
        Clock::time_point deadline;
        std::atomic<std::uint64_t> remaining{128ull * 1024 * 1024};
    };
    explicit PreviewStream(std::shared_ptr<Input> input) : input_(std::move(input)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IStream && iid != IID_ISequentialStream) return E_NOINTERFACE;
        *out = static_cast<IStream*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Read(void* destination, ULONG capacity, ULONG* actual) override {
        std::lock_guard lock(mutex_);
        if (actual) *actual = 0;
        if (!destination && capacity) return STG_E_INVALIDPOINTER;
        ULONG done = 0;
        while (done < capacity && position_ < input_->size) {
            if ((input_->cancel && *input_->cancel) || Clock::now() >= input_->deadline)
                return STG_E_READFAULT;
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                std::min<ULONG>(capacity - done, 65536), input_->size - position_));
            auto budget = input_->remaining.load();
            do { if (count > budget) return STG_E_READFAULT; }
            while (!input_->remaining.compare_exchange_weak(budget, budget - count));
            std::size_t read = 0;
            if (!input_->read(position_, static_cast<char*>(destination) + done, count, read) ||
                !read || read > count) return STG_E_READFAULT;
            position_ += read; done += static_cast<ULONG>(read);
            if (actual) *actual = done;
        }
        return done == capacity ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* result) override {
        std::lock_guard lock(mutex_);
        const auto base = origin == STREAM_SEEK_SET ? 0 :
            origin == STREAM_SEEK_CUR ? position_ : input_->size;
        if (origin > STREAM_SEEK_END || move.QuadPart < -static_cast<LONGLONG>(base) ||
            move.QuadPart > static_cast<LONGLONG>(input_->size - base)) return STG_E_INVALIDFUNCTION;
        position_ = static_cast<std::uint64_t>(static_cast<LONGLONG>(base) + move.QuadPart);
        if (result) result->QuadPart = position_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
        if (!stat) return STG_E_INVALIDPOINTER;
        *stat = {}; stat->type = STGTY_STREAM; stat->cbSize.QuadPart = input_->size;
        stat->grfMode = STGM_READ; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** output) override {
        if (!output) return E_POINTER;
        std::lock_guard lock(mutex_);
        auto clone = new PreviewStream(input_); clone->position_ = position_;
        *output = clone; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
private:
    std::atomic<ULONG> references_{1};
    std::shared_ptr<Input> input_;
    std::uint64_t position_ = 0;
    std::mutex mutex_;
};

template<class Source>
PreviewStream* PreviewInput(std::shared_ptr<Source> source, const std::atomic<bool>& cancel,
    Clock::time_point deadline) {
    if (!source || !source->Size() || source->Size() > kMaxMediaBytes) return nullptr;
    auto input = std::make_shared<PreviewStream::Input>();
    input->size = source->Size(); input->cancel = &cancel;
    input->deadline = deadline;
    input->read = [source](std::uint64_t offset, char* buffer, std::size_t count, std::size_t& read) {
        return source->Read(offset, buffer, count, read);
    };
    return new PreviewStream(std::move(input));
}

std::shared_ptr<std::vector<unsigned char>> EncodeThumbnail(
    IWICImagingFactory* factory, IWICBitmapSource* frame) {
    Com<IWICBitmapScaler> scaler;
    Com<IWICBitmapEncoder> encoder;
    Com<IWICBitmapFrameEncode> output;
    Com<IStream> stream;
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || !width || !height ||
        width > 16384 || height > 16384 || std::uint64_t(width) * height > 40000000)
        return {};
    const auto scale = std::min({1.0, 320.0 / width, 180.0 / height});
    width = std::max(1u, static_cast<unsigned>(width * scale));
    height = std::max(1u, static_cast<unsigned>(height * scale));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(factory->CreateBitmapScaler(scaler.Put())) ||
        FAILED(scaler->Initialize(frame, width, height, WICBitmapInterpolationModeFant)) ||
        FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.Put())) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.Put())) ||
        FAILED(encoder->Initialize(stream.value, WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(output.Put(), nullptr)) ||
        FAILED(output->Initialize(nullptr)) || FAILED(output->SetSize(width, height)) ||
        FAILED(output->SetPixelFormat(&format)) || FAILED(output->WriteSource(scaler.value, nullptr)) ||
        FAILED(output->Commit()) || FAILED(encoder->Commit())) return {};
    STATSTG stat{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) ||
        !stat.cbSize.QuadPart || stat.cbSize.QuadPart > 256 * 1024) return {};
    auto bytes = std::make_shared<std::vector<unsigned char>>(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER start{};
    ULONG read = 0;
    if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) ||
        FAILED(stream->Read(bytes->data(), static_cast<ULONG>(bytes->size()), &read)) ||
        read != bytes->size()) return {};
    return bytes;
}

} // namespace

namespace background_preview {
// Internal frame conversion shared by the MF path and isolated pixel regression.
std::shared_ptr<std::vector<unsigned char>> VideoFrameThumbnail(
    UINT width, UINT height, UINT displayWidth, UINT displayHeight,
    LONG signedStride, BYTE* pixels, DWORD bytes) {
    const auto stride = static_cast<std::uint64_t>(signedStride < 0 ?
        -static_cast<std::int64_t>(signedStride) : signedStride);
    if (!pixels || !width || !height || width > 8192 || height > 8192 ||
        std::uint64_t(width) * height > 16000000 || !displayWidth || !displayHeight ||
        displayWidth > width || displayHeight > height ||
        stride < std::uint64_t(width) * 4 || stride * height > bytes) return {};
    Com<IWICImagingFactory> factory;
    Com<IWICBitmap> bitmap;
    Com<IWICBitmapFlipRotator> flipped;
    Com<IWICBitmapClipper> clipped;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.Put()))) ||
        FAILED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGR,
            static_cast<UINT>(stride), bytes, pixels, bitmap.Put()))) return {};
    IWICBitmapSource* source = bitmap.value;
    if (signedStride < 0) {
        if (FAILED(factory->CreateBitmapFlipRotator(flipped.Put())) ||
            FAILED(flipped->Initialize(source, WICBitmapTransformFlipVertical))) return {};
        source = flipped.value;
    }
    const WICRect region{0, 0, static_cast<INT>(displayWidth), static_cast<INT>(displayHeight)};
    if (FAILED(factory->CreateBitmapClipper(clipped.Put())) ||
        FAILED(clipped->Initialize(source, &region))) return {};
    return EncodeThumbnail(factory.value, clipped.value);
}
} // namespace background_preview

namespace {

std::shared_ptr<std::vector<unsigned char>> Thumbnail(IStream* input) {
    if (!input) return {};
    Com<IWICImagingFactory> factory;
    Com<IWICBitmapDecoder> decoder;
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.Put()))) ||
        FAILED(factory->CreateDecoderFromStream(input, nullptr,
            WICDecodeMetadataCacheOnDemand, decoder.Put())) ||
        FAILED(decoder->GetFrame(0, frame.Put()))) return {};
    return EncodeThumbnail(factory.value, frame.value);
}

// Load system MF entry points locally: this optional catalog preview must not
// add a wallpaper owner or alter the shell's shared link/runtime configuration.
struct MediaFoundation {
    HMODULE platform = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE reader = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    template<class T> static T Function(HMODULE module, const char* name) {
        T result = nullptr;
        const auto address = module ? GetProcAddress(module, name) : nullptr;
        static_assert(sizeof(result) == sizeof(address));
        std::memcpy(&result, &address, sizeof(result)); return result;
    }
    decltype(&MFStartup) startup = Function<decltype(&MFStartup)>(platform, "MFStartup");
    decltype(&MFShutdown) shutdown = Function<decltype(&MFShutdown)>(platform, "MFShutdown");
    decltype(&MFCreateAttributes) attributes = Function<decltype(&MFCreateAttributes)>(platform, "MFCreateAttributes");
    decltype(&MFCreateMediaType) mediaType = Function<decltype(&MFCreateMediaType)>(platform, "MFCreateMediaType");
    using WrapStream = HRESULT (WINAPI*)(IStream*, IMFByteStream**);
    WrapStream byteStream = Function<WrapStream>(platform, "MFCreateMFByteStreamOnStream");
    decltype(&MFCreateSourceReaderFromByteStream) sourceReader =
        Function<decltype(&MFCreateSourceReaderFromByteStream)>(reader, "MFCreateSourceReaderFromByteStream");
    bool started = false;
    ~MediaFoundation() {
        if (started) shutdown();
        if (reader) FreeLibrary(reader);
        if (platform) FreeLibrary(platform);
    }
    bool Start() {
        return startup && shutdown && attributes && mediaType && byteStream && sourceReader &&
            (started = SUCCEEDED(startup(MF_VERSION, MFSTARTUP_LITE)));
    }
};

std::shared_ptr<std::vector<unsigned char>> VideoThumbnail(IStream* input, const std::atomic<bool>& cancel,
    Clock::time_point deadline) {
    if (!input || cancel || Clock::now() >= deadline) return {};
    MediaFoundation mf;
    if (!mf.Start()) return {};
    Com<IMFByteStream> stream;
    Com<IMFAttributes> options;
    Com<IMFSourceReader> reader;
    Com<IMFMediaType> native, output;
    if (FAILED(mf.byteStream(input, stream.Put())) ||
        FAILED(mf.attributes(options.Put(), 2)) ||
        FAILED(options->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)) ||
        FAILED(options->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE)) ||
        FAILED(mf.sourceReader(stream.value, options.value, reader.Put())) ||
        FAILED(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE)) ||
        FAILED(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE)) ||
        FAILED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, native.Put()))) return {};
    UINT64 frameSize = 0;
    if (FAILED(native->GetUINT64(MF_MT_FRAME_SIZE, &frameSize))) return {};
    UINT width = static_cast<UINT>(frameSize >> 32), height = static_cast<UINT>(frameSize);
    const UINT displayWidth = width, displayHeight = height;
    if (!width || !height || width > 8192 || height > 8192 ||
        std::uint64_t(width) * height > 16000000 ||
        FAILED(mf.mediaType(output.Put())) ||
        FAILED(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
        FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, output.value))) return {};
    PROPVARIANT start{};
    start.vt = VT_I8;
    if (FAILED(reader->SetCurrentPosition(GUID_NULL, start))) return {};
    for (unsigned attempt = 0; attempt < 8 && !cancel && Clock::now() < deadline; ++attempt) {
        Com<IMFSample> sample;
        DWORD flags = 0;
        const auto sampleResult = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags,
            nullptr, sample.Put());
        if (FAILED(sampleResult) || (flags & MF_SOURCE_READERF_ERROR) ||
            (!sample.value && (flags & MF_SOURCE_READERF_ENDOFSTREAM))) break;
        if (!sample.value) continue;
        Com<IMFMediaType> current;
        Com<IMFMediaBuffer> buffer;
        if (FAILED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), current.Put())) ||
            FAILED(current->GetUINT64(MF_MT_FRAME_SIZE, &frameSize))) break;
        width = static_cast<UINT>(frameSize >> 32); height = static_cast<UINT>(frameSize);
        UINT32 stride = 0;
        if (!width || !height || width > 8192 || height > 8192 ||
            std::uint64_t(width) * height > 16000000 ||
            FAILED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ||
            FAILED(sample->ConvertToContiguousBuffer(buffer.Put()))) break;
        BYTE* pixels = nullptr;
        DWORD bytes = 0;
        if (FAILED(buffer->Lock(&pixels, nullptr, &bytes))) break;
        struct Unlock {
            IMFMediaBuffer* buffer; BYTE* pixels; DWORD bytes;
            ~Unlock() { SecureZeroMemory(pixels, bytes); buffer->Unlock(); }
        } unlock{buffer.value, pixels, bytes};
        if (cancel || Clock::now() >= deadline) break;
        return background_preview::VideoFrameThumbnail(width, height, displayWidth, displayHeight,
            static_cast<LONG>(stride), pixels, bytes);
    }
    return {};
}

class BackgroundFiles final : public settings::IFileStore {
public:
    explicit BackgroundFiles(settings::IFileStore& files) : files_(files) {}
    bool Read(const std::string& path, std::string& bytes) const override { return files_.Read(path, bytes); }
    bool ReadBounded(const std::string& path, std::size_t limit, std::string& bytes) const override {
        return files_.ReadBounded(path, limit, bytes);
    }
    bool Exists(const std::string& path) const override { return files_.Exists(path); }
    bool Rename(const std::string& from, const std::string& to) override { return files_.Rename(from, to); }
    bool Remove(const std::string& path) override { return files_.Remove(path); }
    bool Write(const std::string& path, const std::string& bytes) override {
        fs::path temporary;
        bool created = false;
        try {
            const auto target = fs::u8path(path);
            if (bytes.size() > music::kMusicBackgroundMaxRecordBytes || !SafePath(target)) return false;
            temporary = target.wstring() + L"." + fs::u8path(AssetId()).wstring() + L".tmp";
            {
                // Never open the store's predictable staging names with CREATE_ALWAYS.
                File output;
                output.handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (output.handle == INVALID_HANDLE_VALUE) return false;
                created = true;
                BY_HANDLE_FILE_INFORMATION info{};
                DWORD written = 0;
                if (!GetFileInformationByHandle(output.handle, &info) || info.nNumberOfLinks != 1 ||
                    (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
                    !WriteFile(output.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
                    written != bytes.size() || !FlushFileBuffers(output.handle))
                    throw std::runtime_error("background metadata write");
            }
            if (!SafePath(target) || !MoveFileExW(temporary.c_str(), target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("background metadata replace");
            return true;
        } catch (...) {
            if (created) DeleteFileW(temporary.c_str());
            return false;
        }
    }
private:
    settings::IFileStore& files_;
};

}  // namespace

struct MusicBackgroundSession::Impl {
    Impl(MusicBackgroundSessionOptions options, settings::IFileStore& files)
        : options(std::move(options)), root(Absolute(this->options.root)),
          mediaDirectory(root.empty() ? fs::path{} : root / "media"),
          protectedRoot(root.empty() ? fs::path{} : root / "protected"),
          libraryRoot(Absolute(this->options.wallpaperLibraryRoot)),
          metadataFiles(files), store_(StatePath(this->options), metadataFiles),
          visible(this->options.initiallyVisible) {}

    bool Restore() {
        if (stopped || restored || !SafePath(root) || !SafePath(mediaDirectory) ||
            !SafePath(protectedRoot / "media") ||
            (!libraryRoot.empty() && (Nested(root, libraryRoot) || Nested(libraryRoot, root)))) return restored;
        try {
            fs::create_directories(mediaDirectory);
            fs::create_directories(protectedRoot / "media");
            if (!rootPins.Pin(mediaDirectory) || !rootPins.Pin(protectedRoot / "media") ||
                !rootPins.Pin(fs::u8path(StatePath(options)).parent_path()) ||
                !SafePath(fs::u8path(StatePath(options))) ||
                !SafePath(fs::u8path(StatePath(options) + ".bak"))) return false;
            loadResult = store_.Load();
            restored = loadResult == Result::Ready;
            if (!restored) return false;
            Collect();
            if (!Activate()) loadResult = Result::Load;
            if (visible) StartWorker();
            return true;
        } catch (...) { loadResult = Result::Load; return false; }
    }

    std::wstring HandlePage(const ui::MusicBackgroundCommand& command, HWND owner) {
        if (stopped) return command.action == Action::Catalog ?
            CatalogMessage(command.request, Result::Load) : Message(0, Result::Load);
        if (command.action == Action::Snapshot) return State();
        if (!restored) return command.action == Action::Catalog ?
            CatalogMessage(command.request, loadResult) : Message(command.request, loadResult);
        if (!command.request || command.request > music::kMusicBackgroundMaxSafeInteger ||
            command.revision > music::kMusicBackgroundMaxSafeInteger) return Message(0, Result::Invalid);
        if (command.action == Action::Catalog &&
            (!command.limit || command.limit > ui::kMusicBackgroundMaxCatalogItems ||
                command.offset > wallpaper::kMaxLibraryItems)) return CatalogMessage(command.request, Result::Invalid);
        if (command.action == Action::Select && !music::ValidMusicBackgroundSourceId(command.id))
            return Message(command.request, Result::Invalid);
        if (!visible) return command.action == Action::Catalog ?
            CatalogMessage(command.request, Result::Busy) : Message(command.request, Result::Busy);
        if (busy) return command.action == Action::Catalog ?
            CatalogMessage(command.request, Result::Busy) : Message(command.request, Result::Busy);
        if (command.action != Action::Catalog && command.revision != store_.revision())
            return Message(command.request, Result::Conflict);
        if (command.action == Action::Clear) return Clear(command);
        if (command.action != Action::Choose && command.action != Action::Select &&
            command.action != Action::Catalog) return Message(command.request, Result::Invalid);
        std::wstring selected;
        if (command.action == Action::Choose) {
            // Show pumps messages: reserve the operation and keep its pre-modal generation.
            const auto chooserGeneration = generation;
            busy = true;
            try {
                // Keep the hook alive across reentrant close, just like the native dialog.
                const auto picker = options.chooseMedia;
                if (picker) selected = picker(owner);
                else if (!options.verification) selected = ChooseWithDialog(owner, chooser);
            } catch (...) {
                busy = false;
                return Message(command.request, Result::Load);
            }
            busy = false;
            if (stopped) return Message(command.request, Result::Load);
            if (!visible || chooserGeneration != generation) return Message(command.request, loadResult);
            if (selected.empty()) return Message(command.request, Result::Ready);
        }
        try { StartWorker(); }
        catch (...) { return Message(command.request, Result::Load); }
        cancel = false;
        busy = true;
        {
            std::lock_guard lock(mutex);
            job = Job{command, std::move(selected), generation};
        }
        condition.notify_all();
        return command.action == Action::Catalog ?
            CatalogMessage(command.request, Result::Busy) : Message(command.request, Result::Busy);
    }

    std::wstring State() const {
        return Message(0, stopped ? Result::Load : !restored ? loadResult : busy ? Result::Busy : loadResult);
    }

    std::wstring MediaDirectory() const { return mediaDirectory.wstring(); }

    bool HasWork() const noexcept { return !stopped && busy; }

    std::vector<std::wstring> Drain() {
        std::vector<std::wstring> messages;
        if (stopped) return messages;
        if (!outgoing.empty() && Clock::now() >= outgoingUntil) { ReleaseOutgoing(); Collect(); }
        std::optional<Completed> result;
        {
            std::lock_guard lock(mutex);
            result = std::move(completed);
            completed.reset();
        }
        if (!result) return messages;
        busy = false;
        if (result->generation != generation || !visible) {
            Discard(*result);
            messages.push_back(result->command.action == Action::Catalog ?
                CatalogMessage(result->command.request, Result::Ready) :
                Message(result->command.request, loadResult));
            return messages;
        }
        if (result->command.action == Action::Catalog) {
            ui::MusicBackgroundCatalogMessage message;
            message.request = result->command.request;
            message.result = result->result;
            message.total = result->total;
            message.items = std::move(result->items);
            message.selectedId = store_.sourceWallpaperId();
            // Validate catalog metadata first; only this native owner can attach minted capabilities.
            auto encoded = Json::parse(ui::EncodeMusicBackgroundCatalog(message));
            if (result->result == Result::Ready) {
                if (!result->command.offset) ReleaseThumbnails();
                for (std::size_t i = 0; i < result->thumbnails.size(); ++i) {
                    const auto& bytes = result->thumbnails[i];
                    // ponytail: bounded previews; viewport-driven leases can extend large galleries later.
                    if (!bytes || thumbnailIds.size() >= ui::kMusicBackgroundMaxCatalogItems ||
                        bytes->size() > kMaxThumbnailBytes - thumbnailBytes) continue;
                    const auto id = AssetId();
                    platform::windows::WallpaperMediaSource source;
                    source.bytes = bytes->size(); source.contentType = "image/png"; source.extension = "png";
                    source.read = [bytes](std::uint64_t offset, char* buffer, std::size_t capacity, std::size_t& read) {
                        read = 0;
                        if ((!buffer && capacity) || offset > bytes->size()) return false;
                        read = std::min({capacity, std::size_t(65536), bytes->size() - static_cast<std::size_t>(offset)});
                        if (read) std::memcpy(buffer, bytes->data() + offset, read);
                        return true;
                    };
                    const auto url = server.Register(id, std::move(source));
                    if (!url.empty()) {
                        thumbnailIds.push_back(id);
                        thumbnailBytes += bytes->size();
                        encoded["items"][i]["thumbnailUrl"] = url;
                    }
                }
            }
            const auto text = encoded.dump(-1, ' ', true);
            messages.emplace_back(text.begin(), text.end());
            return messages;
        }
        auto status = result->result;
        if (status == Result::Ready && result->media) {
            const auto nextUrl = result->storage == Storage::Protected ?
                Register(*result->media, std::move(result->reader)) :
                Register(*result->media, std::move(result->plainReader));
            if (nextUrl.empty()) status = Result::Load;
            else {
                status = SaveAsset(result->media, result->storage, result->sourceId, result->command.revision);
                if (status == Result::Ready) {
                    RetireCurrent();
                    currentId = result->media->id;
                    currentPath = result->path;
                    currentUrl = nextUrl;
                    visibleMedia = store_.media();
                    loadResult = Result::Ready;
                    Collect();
                } else if (!nextUrl.empty()) server.Remove(result->media->id);
            }
        }
        if (status != Result::Ready) Discard(*result);
        messages.push_back(Message(result->command.request, status));
        return messages;
    }

    void CancelActive() {
        ++generation;
        cancel = true;
        // Clear before Close can pump messages; the Show scope still owns the COM reference.
        if (auto* dialog = std::exchange(chooser, nullptr))
            dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        CancelWorkerIo();
        condition.notify_all();
    }

    void SetVisible(bool value) {
        if (stopped || visible == value) return;
        visible = value;
        if (!visible) {
            CancelActive();
            server.Stop();
            thumbnailIds.clear();
            thumbnailBytes = 0;
            currentId.clear(); currentUrl.clear(); outgoing.clear();
            currentPath.clear(); outgoingPath.clear();
            visibleMedia.reset();
            {
                std::lock_guard lock(mutex);
                deadline = Clock::time_point::max();
            }
            condition.notify_all();
        } else if (restored) {
            try {
                StartWorker();
                loadResult = Activate() ? Result::Ready : Result::Load;
            } catch (...) { loadResult = Result::Load; }
        }
    }

    void Shutdown() noexcept {
        if (stopped) return;
        stopped = true;
        CancelActive();
        {
            std::lock_guard lock(mutex);
            closing = true;
        }
        CancelWorkerIo();
        condition.notify_all();
        if (worker.joinable()) worker.join();
        if (workerIoHandle) { CloseHandle(workerIoHandle); workerIoHandle = nullptr; }
        if (completed) Discard(*completed);
        completed.reset();
        server.Stop();
        thumbnailIds.clear();
        thumbnailBytes = 0;
        busy = false;
        visibleMedia.reset();
    }

private:
    struct Job {
        ui::MusicBackgroundCommand command;
        std::wstring selected;
        std::uint64_t generation;
    };
    struct Completed {
        ui::MusicBackgroundCommand command;
        std::uint64_t generation = 0;
        Result result = Result::Invalid;
        std::optional<Media> media;
        Storage storage = Storage::Plain;
        std::uint64_t expectedBytes = 0;
        std::string sourceId;
        std::shared_ptr<Reader> reader;
        std::shared_ptr<File> plainReader;
        fs::path path;
        std::uint64_t total = 0;
        std::vector<ui::MusicBackgroundCatalogItem> items;
        std::vector<std::shared_ptr<std::vector<unsigned char>>> thumbnails;
    };

    void ReleaseThumbnails() {
        for (const auto& id : thumbnailIds) server.Remove(id);
        thumbnailIds.clear();
        thumbnailBytes = 0;
    }

    std::wstring Message(std::uint64_t request, Result result) const {
        ui::MusicBackgroundMessage message;
        message.revision = store_.revision();
        message.request = request;
        message.result = result;
        message.media = visibleMedia;
        message.mediaUrl = currentUrl;
        return ui::EncodeMusicBackgroundPageState(message);
    }

    std::wstring CatalogMessage(std::uint64_t request, Result result) const {
        ui::MusicBackgroundCatalogMessage message;
        message.request = request; message.result = result;
        message.selectedId = store_.sourceWallpaperId();
        return ui::EncodeMusicBackgroundCatalog(message);
    }

    fs::path AssetPath(const Media& media, Storage storage) const {
        return storage == Storage::Protected ? protectedRoot / "media" / (media.id + ".apk") :
            mediaDirectory / (media.id + "." + media.extension);
    }

    template<class MediaReader>
    std::string Register(const Media& media, std::shared_ptr<MediaReader> reader) {
        if (!reader) return {};
        platform::windows::WallpaperMediaSource source;
        source.bytes = reader->Size(); source.contentType = media.mimeType; source.extension = media.extension;
        source.read = [reader](std::uint64_t offset, char* buffer, std::size_t capacity, std::size_t& read) {
            return reader->Read(offset, buffer, capacity, read);
        };
        return server.Register(media.id, std::move(source));
    }

    bool Activate() {
        visibleMedia.reset();
        currentId.clear(); currentUrl.clear();
        currentPath.clear();
        if (!visible || !store_.media()) return true;
        const auto& media = *store_.media();
        const auto path = AssetPath(media, store_.storage());
        std::uint64_t size = 0;
        if (!SafePath(path) || !RegularFile(path, size) ||
            (store_.storage() == Storage::Plain && size > kMaxMediaBytes)) return false;
        auto file = std::make_shared<File>();
        if (!file->Open(path)) return false;
        if (store_.storage() == Storage::Protected) {
            if (!SafePath(protectedRoot / "cache-key.bin")) return false;
            std::string error;
            auto reader = Reader::Open(protectedRoot.u8string(), path.u8string(), error);
            if (!reader || reader->Size() > kMaxMediaBytes) return false;
            currentUrl = Register(media, std::move(reader));
        } else {
            if (!file->Size() || file->Size() > kMaxMediaBytes) return false;
            currentUrl = Register(media, std::move(file));
        }
        if (currentUrl.empty()) return false;
        currentId = media.id;
        currentPath = path;
        visibleMedia = media;
        return true;
    }

    void RetireCurrent() {
        ReleaseOutgoing();
        outgoing = std::move(currentId);
        outgoingPath = std::move(currentPath);
        currentId.clear();
        currentPath.clear();
        if (!outgoing.empty()) {
            outgoingUntil = Clock::now() + std::chrono::seconds(30);
            {
                std::lock_guard lock(mutex);
                deadline = outgoingUntil;
            }
            condition.notify_all();
        }
    }

    void ReleaseOutgoing() {
        if (!outgoing.empty()) server.Remove(outgoing);
        outgoing.clear();
        outgoingPath.clear();
    }

    void Discard(Completed& result) noexcept {
        result.reader.reset();
        result.plainReader.reset();
        if (result.path.empty()) return;
        try {
            if (result.path.parent_path() == mediaDirectory ||
                result.path.parent_path() == protectedRoot / "media") {
                std::error_code error;
                if (SafePath(result.path)) fs::remove(result.path, error);
            }
        } catch (...) {}
    }

    void Collect() noexcept {
        if (busy) return;
        try {
        // Keep both persisted generations. Never prune unrelated files or keys.
        std::set<fs::path> keep;
        if (store_.media()) keep.insert(AssetPath(*store_.media(), store_.storage()));
        if (store_.previous()) keep.insert(AssetPath(*store_.previous(), store_.previousStorage()));
        if (!outgoing.empty()) keep.insert(outgoingPath);
        for (const auto& directory : {mediaDirectory, protectedRoot / "media"}) {
            if (!SafePath(directory)) continue;
            std::error_code error;
            for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
                const auto path = it->path();
                const auto name = path.filename().u8string();
                if (name.rfind("background-", 0) != 0 || keep.count(path) || !SafePath(path)) continue;
                const auto extension = LowerExtension(path);
                if (!Format(extension) && extension != "apk" && extension != "tmp") continue;
                const auto dot = name.find('.');
                const auto id = name.substr(11, dot == name.npos ? name.size() : dot - 11);
                if ((id.size() != 16 && id.size() != 32) ||
                    id.find_first_not_of("0123456789abcdef") != std::string::npos) continue;
                if (it->is_regular_file(error)) fs::remove(path, error);
            }
        }
        } catch (...) {}
    }

    Result SaveAsset(const std::optional<Media>& media, Storage storage,
        const std::string& sourceId, std::uint64_t revision) {
        for (const auto* suffix : {"", ".bak", ".tmp", ".bak.tmp"})
            if (!SafePath(fs::u8path(StatePath(options) + suffix))) return Result::Save;
        return store_.SaveAsset(media, storage, sourceId, revision);
    }

    void StartWorker() {
        if (worker.joinable()) return;
        worker = std::thread([this] {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            struct Apartment {
                HRESULT result;
                ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
            } apartment{initialized};
            {
                std::lock_guard lock(mutex);
                DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                    &workerIoHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);
            }
            for (;;) {
                std::optional<Job> next;
                {
                    std::unique_lock lock(mutex);
                    const auto wakeAt = deadline;
                    if (!closing && !job)
                        condition.wait_until(lock, wakeAt);
                    if (closing) return;
                    if (job) { next = std::move(job); job.reset(); }
                    else if (Clock::now() >= deadline) deadline = Clock::time_point::max();
                    else continue;
                }
                if (next) {
                    auto result = Execute(*next);
                    std::lock_guard lock(mutex);
                    completed = std::move(result);
                }
                if (options.wake) {
                    try { options.wake(); } catch (...) {}
                }
            }
        });
    }

    void CancelWorkerIo() {
        std::lock_guard lock(mutex);
        // Best effort for a pending synchronous call; chunk checks remain authoritative.
        if (workerIoHandle) CancelSynchronousIo(workerIoHandle);
    }

    std::optional<Media> EntryMedia(const wallpaper::LibraryEntry& entry) const {
        const auto path = fs::u8path(entry.localPath).lexically_normal();
        if (path.parent_path() != libraryRoot / "media" || !SafePath(path)) return {};
        std::uint64_t physical = 0;
        if (!RegularFile(path, physical)) return {};
        auto extension = entry.protectedMedia ? wallpaper::MediaExtension(entry.item) : path.extension().u8string();
        if (!extension.empty() && extension.front() == '.') extension.erase(0, 1);
        const auto format = Format(extension);
        if (!format || !entry.item.sizeBytes || entry.item.sizeBytes > kMaxMediaBytes ||
            (entry.imported == entry.protectedMedia)) return {};
        Media media{"catalog", entry.item.title, extension, format->first, format->second};
        Media normalized;
        if (!music::NormalizeMusicBackgroundMedia(media, normalized)) return {};
        return normalized;
    }

    std::shared_ptr<std::vector<unsigned char>> EntryThumbnail(
        const wallpaper::LibraryEntry& entry, const Media& media, const std::string& fallback,
        Clock::time_point previewDeadline) {
        if (cancel || Clock::now() >= previewDeadline) return {};
        previewDeadline = std::min(previewDeadline, Clock::now() + std::chrono::seconds(3));
        auto file = std::make_shared<File>();
        if (!file->Open(fs::u8path(entry.localPath))) return {};
        // v1 wallpaperLibraryStore.posterPathFor uses this exact sidecar name.
        // Never follow a manifest-supplied path, link, or arbitrary thumbnail URL.
        if (wallpaper::IsSafeWallpaperId(entry.item.id)) {
            const auto poster = libraryRoot / "posters" / fs::u8path(entry.item.id + "-poster.png");
            DirectoryPins pins;
            auto posterFile = std::make_shared<File>();
            if (pins.Pin(poster.parent_path()) && posterFile->Open(poster) &&
                posterFile->Size() && posterFile->Size() <= 8 * 1024 * 1024) {
                Com<IStream> input;
                input.value = PreviewInput(posterFile, cancel, previewDeadline);
                if (auto thumbnail = Thumbnail(input.value)) return thumbnail;
            }
        }
        // Keep the original validated manifest fallback; do not fetch external
        // images or decode a video that already has its official poster.
        if (!fallback.empty() || cancel || Clock::now() >= previewDeadline) return {};
        Com<IStream> input;
        if (entry.protectedMedia) {
            File key;
            if (!key.Open(libraryRoot / "cache-key.bin")) return {};
            std::string error;
            auto reader = Reader::Open(libraryRoot.u8string(), entry.localPath, error);
            if (!reader || reader->Size() != entry.item.sizeBytes) return {};
            input.value = PreviewInput(std::move(reader), cancel, previewDeadline);
        } else {
            input.value = PreviewInput(std::move(file), cancel, previewDeadline);
        }
        return media.kind == music::MusicBackgroundKind::Image ? Thumbnail(input.value) :
            VideoThumbnail(input.value, cancel, previewDeadline);
    }

    Completed Execute(const Job& work) noexcept {
        Completed result;
        result.command = work.command; result.generation = work.generation;
        // ponytail: cooperative page budget below the UI's 10 s query timeout;
        // native codec calls cannot be forcibly interrupted inside this process.
        const auto previewDeadline = Clock::now() + std::chrono::seconds(5);
        fs::path staging;
        try {
            if (cancel) return result;
            DirectoryPins sourcePins;
            fs::path input;
            std::optional<Media> media;
            if (work.command.action == Action::Choose) {
                input = fs::path(work.selected).lexically_normal();
                const auto extension = LowerExtension(input), name = Utf8(input.filename().wstring());
                const auto format = Format(extension);
                if (!format) { result.result = Result::Unsupported; return result; }
                media = Media{"catalog", name, extension, format->first, format->second};
            } else {
                if (!sourcePins.Pin(libraryRoot / "media") || !SafePath(libraryRoot / "library.json") ||
                    !SafePath(libraryRoot / "library.json.bak") || !SafePath(libraryRoot / "media")) {
                    result.result = Result::Load; return result;
                }
                wallpaper::Library library(libraryRoot.u8string());
                if (!library.Load().ok) { result.result = Result::Load; return result; }
                if (work.command.action == Action::Catalog) {
                    std::size_t thumbnailSize = 0;
                    for (const auto& entry : library.Items()) {
                        if (cancel) return result;
                        const auto candidate = EntryMedia(entry);
                        if (!candidate) continue;
                        if (result.total++ < work.command.offset || result.items.size() >= work.command.limit) continue;
                        auto fallback = wallpaper::NormalizeOfficialUrl(entry.item.thumbnailUrl);
                        if (fallback.empty())
                            fallback = wallpaper::NormalizeOfficialUrl(entry.item.heroThumbnailUrl);
                        result.items.push_back({entry.item.id, entry.item.title,
                            candidate->kind == music::MusicBackgroundKind::Image ? "image" : "video", fallback});
                        std::shared_ptr<std::vector<unsigned char>> thumbnail;
                        if (thumbnailSize < kMaxThumbnailBytes) {
                            thumbnail = EntryThumbnail(entry, *candidate, fallback, previewDeadline);
                            if (thumbnail && thumbnail->size() > kMaxThumbnailBytes - thumbnailSize)
                                thumbnail.reset();
                            if (thumbnail) thumbnailSize += thumbnail->size();
                        }
                        result.thumbnails.push_back(std::move(thumbnail));
                    }
                    result.result = Result::Ready;
                    return result;
                }
                const auto entry = library.Find(work.command.id);
                if (!entry || !(media = EntryMedia(*entry))) return result;
                result.storage = entry->protectedMedia ? Storage::Protected : Storage::Plain;
                result.expectedBytes = entry->item.sizeBytes;
                result.sourceId = entry->item.id;
                input = fs::u8path(entry->localPath);
            }
            Media normalized;
            if (!media || !music::NormalizeMusicBackgroundMedia(*media, normalized)) return result;
            media->id = AssetId();
            if (!sourcePins.Pin(input.parent_path())) return result;
            File file;
            if (!file.Open(input)) return result;
            const auto physical = file.Size();
            if (!physical || physical > kMaxMediaBytes + 1024 * 1024) {
                result.result = Result::TooLarge; return result;
            }
            std::shared_ptr<Reader> reader;
            bool legacy = false;
            std::string error;
            auto bytes = physical;
            if (result.storage == Storage::Protected) {
                if (!SafePath(libraryRoot / "cache-key.bin")) return result;
                std::array<char, 8> magic{};
                legacy = background_legacy::Read(file.handle, 0, magic.data(), static_cast<DWORD>(magic.size())) &&
                    std::memcmp(magic.data(), "LUMEWP01", magic.size()) == 0;
                if (legacy) bytes = result.expectedBytes;
                else {
                    reader = Reader::Open(libraryRoot.u8string(), input.u8string(), error);
                    if (!reader) { result.result = Result::Unsupported; return result; }
                    bytes = reader->Size();
                }
            }
            if (bytes > kMaxMediaBytes) { result.result = Result::TooLarge; return result; }
            const auto target = AssetPath(*media, result.storage);
            staging = target.wstring() + L".tmp";
            if (!SafePath(target) || !SafePath(protectedRoot / "cache-key.bin")) return result;
            PlainBuffer buffer;
            if (legacy) {
                File key;
                if (!key.Open(libraryRoot / "cache-key.bin")) return result;
                auto writer = protected_media::MakeProtectedWriterFactory(protectedRoot.u8string())(staging.u8string(), error);
                if (!writer || !background_legacy::Reencrypt(file.handle, key.handle, physical,
                    bytes, *writer, cancel, error)) throw std::runtime_error("legacy authentication");
                writer.reset();
            } else if (reader) {
                auto writer = protected_media::MakeProtectedWriterFactory(protectedRoot.u8string())(staging.u8string(), error);
                if (!writer) throw std::runtime_error("writer");
                for (std::uint64_t offset = 0; offset < bytes;) {
                    if (cancel) throw std::runtime_error("cancelled");
                    std::size_t read = 0;
                    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.bytes.size(), bytes - offset));
                    if (!reader->Read(offset, reinterpret_cast<char*>(buffer.bytes.data()), count, read) ||
                        read != count || !writer->Append(buffer.bytes.data(), read, error)) throw std::runtime_error("copy");
                    SecureZeroMemory(buffer.bytes.data(), buffer.bytes.size());
                    offset += read;
                }
                if (cancel || !writer->Finish(error)) throw std::runtime_error("finish");
                writer.reset(); reader.reset();
            } else {
                File output;
                output.handle = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (output.handle == INVALID_HANDLE_VALUE) throw std::runtime_error("create");
                for (std::uint64_t offset = 0; offset < bytes;) {
                    if (cancel) throw std::runtime_error("cancelled");
                    DWORD read = 0, written = 0;
                    const auto count = static_cast<DWORD>(std::min<std::uint64_t>(buffer.bytes.size(), bytes - offset));
                    if (!ReadFile(file.handle, buffer.bytes.data(), count, &read, nullptr) || read != count ||
                        !WriteFile(output.handle, buffer.bytes.data(), read, &written, nullptr) || written != read)
                        throw std::runtime_error("copy");
                    offset += read;
                }
                if (!FlushFileBuffers(output.handle)) throw std::runtime_error("flush");
            }
            if (cancel || !MoveFileExW(staging.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("publish");
            result.path = target;
            if (result.storage == Storage::Protected) {
                result.reader = Reader::Open(protectedRoot.u8string(), target.u8string(), error);
                if (!result.reader) throw std::runtime_error("verify");
            } else {
                result.plainReader = std::make_shared<File>();
                if (!result.plainReader->Open(target) || result.plainReader->Size() != bytes)
                    throw std::runtime_error("verify");
            }
            result.media = std::move(media);
            result.result = Result::Ready;
        } catch (...) {
            result.result = Result::Save;
            Discard(result);
            std::error_code error;
            if (!staging.empty() && SafePath(staging)) fs::remove(staging, error);
        }
        return result;
    }

    MusicBackgroundSessionOptions options;
    fs::path root;
    fs::path mediaDirectory;
    fs::path protectedRoot, libraryRoot;
    DirectoryPins rootPins;
    BackgroundFiles metadataFiles;
    music::MusicBackgroundStore store_;
    std::optional<music::MusicBackgroundMedia> visibleMedia;
    Result loadResult = Result::Load;
    platform::windows::WallpaperMediaServer server{"https://lume.local"};
    std::string currentId, currentUrl, outgoing;
    std::vector<std::string> thumbnailIds;
    std::size_t thumbnailBytes = 0;
    fs::path currentPath, outgoingPath;
    IFileOpenDialog* chooser = nullptr;
    Clock::time_point outgoingUntil{}, deadline = Clock::time_point::max();
    std::thread worker;
    HANDLE workerIoHandle = nullptr;
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> cancel{false};
    std::optional<Job> job;
    std::optional<Completed> completed;
    std::uint64_t generation = 0;
    bool busy = false, closing = false, visible = true;
    bool restored = false;
    bool stopped = false;

    std::wstring Clear(const ui::MusicBackgroundCommand& command) {
        const auto saved = SaveAsset(std::nullopt, Storage::Plain, {}, command.revision);
        if (saved != Result::Ready) return Message(command.request, saved);
        visibleMedia.reset();
        RetireCurrent();
        currentUrl.clear();
        loadResult = Result::Ready;
        Collect();
        return Message(command.request, Result::Ready);
    }
};

MusicBackgroundSession::MusicBackgroundSession(MusicBackgroundSessionOptions options,
    settings::IFileStore& files)
    : impl_(std::make_unique<Impl>(std::move(options), files)) {}

MusicBackgroundSession::~MusicBackgroundSession() { Shutdown(); }

bool MusicBackgroundSession::Restore() { return impl_->Restore(); }

std::wstring MusicBackgroundSession::HandlePage(const ui::MusicBackgroundCommand& command, HWND owner) {
    return impl_->HandlePage(command, owner);
}

std::wstring MusicBackgroundSession::State() const { return impl_->State(); }
std::wstring MusicBackgroundSession::MediaDirectory() const { return impl_->MediaDirectory(); }
bool MusicBackgroundSession::HasWork() const noexcept { return impl_->HasWork(); }
std::vector<std::wstring> MusicBackgroundSession::Drain() { return impl_->Drain(); }
std::optional<std::wstring> MusicBackgroundSession::DrainNotification() {
    auto messages = impl_->Drain();
    if (messages.empty()) return {};
    return std::move(messages.front());
}
void MusicBackgroundSession::SetVisible(bool visible) { impl_->SetVisible(visible); }
void MusicBackgroundSession::CancelActive() { impl_->CancelActive(); }
void MusicBackgroundSession::Shutdown() { impl_->Shutdown(); }

}  // namespace lume::shell
