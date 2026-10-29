#include "platform/windows/account/avatar.h"
#include <wincodec.h>
#include <shobjidl.h>
#include <cstring>

namespace {
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** put() { return &p; }
    T* operator->() const { return p; }
};
struct Apartment {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
void Check(HRESULT hr) { if (FAILED(hr)) throw lume::account::Failure(lume::account::Error::InvalidAvatar); }
}
namespace lume::account {
Avatar ValidateAvatar(Bytes bytes) {
    if (bytes.size() > MaxAvatarBytes) throw Failure(Error::AvatarTooLarge);
    if (bytes.size() < 16) throw Failure(Error::InvalidAvatar);
    std::string mime;
    if (std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) == 0) mime = "image/png";
    else if (bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) mime = "image/jpeg";
    else if (std::memcmp(bytes.data(), "RIFF", 4) == 0 && std::memcmp(bytes.data() + 8, "WEBP", 4) == 0) {
        const auto declared = static_cast<std::uint32_t>(bytes[4]) | (static_cast<std::uint32_t>(bytes[5]) << 8) |
            (static_cast<std::uint32_t>(bytes[6]) << 16) | (static_cast<std::uint32_t>(bytes[7]) << 24);
        if (declared != bytes.size() - 8) throw Failure(Error::InvalidAvatar);
        mime = "image/webp";
    } else throw Failure(Error::InvalidAvatar);
    Apartment apartment;
    if (FAILED(apartment.hr) && apartment.hr != RPC_E_CHANGED_MODE) throw Failure(Error::InvalidAvatar);
    Com<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())));
    Com<IWICStream> stream; Check(factory->CreateStream(stream.put()));
    Check(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size())));
    Com<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnLoad, decoder.put()));
    UINT frames = 0; Check(decoder->GetFrameCount(&frames));
    if (frames != 1) throw Failure(Error::InvalidAvatar);
    Com<IWICBitmapFrameDecode> frame; Check(decoder->GetFrame(0, frame.put()));
    UINT width = 0, height = 0; Check(frame->GetSize(&width, &height));
    if (!width || !height || width > 4096 || height > 4096 || static_cast<std::uint64_t>(width) * height > 16 * 1024 * 1024)
        throw Failure(Error::InvalidAvatar);
    Com<IWICFormatConverter> converted; Check(factory->CreateFormatConverter(converted.put()));
    Check(converted->Initialize(frame.p, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    // Force full native decoding: matching file signatures alone is not validation.
    Bytes pixels(static_cast<std::size_t>(width) * height * 4);
    Check(converted->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    return {std::move(bytes), std::move(mime)};
}
std::string AvatarDataUrl(const Avatar& image) {
    const auto checked = ValidateAvatar(image.bytes);
    if (checked.mime != image.mime) throw Failure(Error::InvalidAvatar);
    return "data:" + checked.mime + ";base64," + Base64(checked.bytes, false);
}
}
namespace lume::platform::windows::account {
using namespace lume::account;
std::optional<std::wstring> ChooseAvatarFile(HWND owner) {
    Com<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put()))))
        throw Failure(Error::InvalidAvatar);
    const COMDLG_FILTERSPEC filters[] = {{L"JPG, PNG, WebP", L"*.jpg;*.jpeg;*.png;*.webp"}};
    if (FAILED(dialog->SetFileTypes(1, filters)) || FAILED(dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
        FOS_PATHMUSTEXIST | FOS_DONTADDTORECENT | FOS_NOCHANGEDIR))) throw Failure(Error::InvalidAvatar);
    auto hr = dialog->Show(owner);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    if (FAILED(hr)) throw Failure(Error::InvalidAvatar);
    Com<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put()))) throw Failure(Error::InvalidAvatar);
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) throw Failure(Error::InvalidAvatar);
    std::wstring result(path); CoTaskMemFree(path); return result;
}
Avatar ReadAvatarFile(std::wstring_view path, const Cancellation& cancel) {
    cancel.Check();
    if (path.empty() || path.size() > 32767 || path.find(L'\0') != path.npos) throw Failure(Error::InvalidAvatar);
    auto file = CreateFileW(std::wstring(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw Failure(Error::InvalidAvatar);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(MaxAvatarBytes)) {
        CloseHandle(file);
        throw Failure(size.QuadPart > static_cast<LONGLONG>(MaxAvatarBytes) ? Error::AvatarTooLarge : Error::InvalidAvatar);
    }
    Bytes bytes(static_cast<std::size_t>(size.QuadPart)); DWORD read = 0;
    auto success = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file); cancel.Check();
    if (!success || read != bytes.size()) throw Failure(Error::InvalidAvatar);
    return ValidateAvatar(std::move(bytes));
}
}
