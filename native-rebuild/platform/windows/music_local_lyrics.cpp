#include "platform/windows/music_local_lyrics.h"

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <propkey.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

namespace lume::music {
namespace {

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
};

bool SameResolvedPath(HANDLE file, const std::filesystem::path& expected) {
    std::wstring resolved(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(file, resolved.data(),
        static_cast<DWORD>(resolved.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= resolved.size()) return false;
    resolved.resize(length);
    if (resolved.substr(0, 4) == L"\\\\?\\") resolved.erase(0, 4);
    return CompareStringOrdinal(resolved.c_str(), -1, expected.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool PlainDiskFile(HANDLE file, const std::filesystem::path& expected) {
    BY_HANDLE_FILE_INFORMATION info{};
    return file != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        GetFileType(file) == FILE_TYPE_DISK && SameResolvedPath(file, expected);
}

std::string Utf8(const wchar_t* text, int length) {
    if (length <= 0) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string output(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, output.data(), size, nullptr, nullptr);
    return output;
}

std::string FromCodePage(const std::string& bytes, UINT codePage) {
    const auto length = static_cast<int>(bytes.size());
    const auto size = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), length, nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data(), length, wide.data(), size);
    return Utf8(wide.data(), size);
}

// Sidecar .lrc files from Chinese download sites are often GBK or UTF-16 rather
// than UTF-8. Return UTF-8 text, or the input unchanged when no decoding fits.
std::string LyricsText(std::string bytes) {
    const auto utf16 = [&](bool bigEndian) {
        std::wstring wide;
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            const auto a = static_cast<unsigned char>(bytes[i]), b = static_cast<unsigned char>(bytes[i + 1]);
            wide.push_back(static_cast<wchar_t>(bigEndian ? (a << 8) | b : (b << 8) | a));
        }
        return Utf8(wide.data(), static_cast<int>(wide.size()));
    };
    if (bytes.size() >= 2 && bytes[0] == '\xFF' && bytes[1] == '\xFE') return utf16(false);
    if (bytes.size() >= 2 && bytes[0] == '\xFE' && bytes[1] == '\xFF') return utf16(true);
    if (bytes.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0) > 0) return bytes;
    for (const UINT codePage : {54936u /* GB18030, a GBK superset */, 950u /* Big5 */}) {
        auto text = FromCodePage(bytes, codePage);
        if (!text.empty()) return text;
    }
    return bytes;
}

// Lyrics embedded in the audio file's tags (ID3 USLT, Vorbis LYRICS, MP4 lyr),
// read through the Windows property system. Only timed (LRC) text is usable.
std::string EmbeddedLyrics(const std::filesystem::path& source) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string text;
    {
        Microsoft::WRL::ComPtr<IPropertyStore> store;
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(SHGetPropertyStoreFromParsingName(source.c_str(), nullptr, GPS_DEFAULT,
                IID_PPV_ARGS(&store))) && SUCCEEDED(store->GetValue(PKEY_Music_Lyrics, &value)) &&
            value.vt == VT_LPWSTR && value.pwszVal) {
            const auto length = static_cast<int>(wcsnlen(value.pwszVal, kLyricsMaxFileBytes));
            text = Utf8(value.pwszVal, length);
        }
        PropVariantClear(&value);
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return text;
}

}  // namespace

LocalLyricsResult LoadLocalLyrics(const Track& track, const std::function<bool()>& cancelled) noexcept {
    try {
        const auto stop = [&] { return cancelled && cancelled(); };
        if (stop()) return {{}, "cancelled"};
        if (track.id.rfind("local-", 0) != 0 || track.source.empty()) return {{}, "not-local"};
        const auto source = std::filesystem::u8path(track.source);
        const auto root = source.root_path().wstring();
        // Do not turn a local lyric request into network or device I/O.
        if (!source.is_absolute() || root.size() != 3 || root[1] != L':' ||
            source.wstring().find(L':', 2) != std::wstring::npos ||
            GetDriveTypeW(root.c_str()) == DRIVE_REMOTE) return {{}, "not-local"};
        // Pin the authorized audio entry until parsing completes; path-only
        // checks permit its replacement between validation and sidecar access.
        Handle audio{CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (!PlainDiskFile(audio.value, source)) return {{}, "invalid-path"};
        const auto path = std::filesystem::path(source).replace_extension(L".lrc");
        Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_OVERLAPPED, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) return {{}, "missing"};
            auto embedded = EmbeddedLyrics(source);
            if (embedded.empty() || stop() || !SameResolvedPath(audio.value, source)) return {{}, "missing"};
            auto parsed = ParseLrc(embedded, track.duration);
            if (parsed.lines.empty()) return {{}, "missing"};
            return parsed;
        }
        LARGE_INTEGER size{};
        if (!PlainDiskFile(file.value, path) || !SameResolvedPath(audio.value, source) ||
            !GetFileSizeEx(file.value, &size) || size.QuadPart < 0 ||
            static_cast<unsigned long long>(size.QuadPart) > kLyricsMaxFileBytes) return {{}, "file-limit"};
        Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        if (!event.value) return {{}, "read"};
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            if (stop() || std::chrono::steady_clock::now() >= deadline) return {{}, "cancelled"};
            OVERLAPPED overlapped{};
            overlapped.hEvent = event.value;
            overlapped.Offset = static_cast<DWORD>(offset);
            ResetEvent(event.value);
            DWORD received = 0;
            const auto count = static_cast<DWORD>(std::min<std::size_t>(16384, bytes.size() - offset));
            if (!ReadFile(file.value, bytes.data() + offset, count, &received, &overlapped)) {
                if (GetLastError() != ERROR_IO_PENDING) return {{}, "read"};
                while (WaitForSingleObject(event.value, 10) == WAIT_TIMEOUT) {
                    if (stop() || std::chrono::steady_clock::now() >= deadline) {
                        CancelIoEx(file.value, &overlapped);
                        GetOverlappedResult(file.value, &overlapped, &received, TRUE);
                        return {{}, "cancelled"};
                    }
                }
                if (!GetOverlappedResult(file.value, &overlapped, &received, TRUE)) return {{}, "read"};
            }
            if (!received) return {{}, "read"};
            offset += received;
        }
        if (stop()) return {{}, "cancelled"};
        if (!SameResolvedPath(audio.value, source) || !SameResolvedPath(file.value, path)) return {{}, "invalid-path"};
        return ParseLrc(LyricsText(std::move(bytes)), track.duration);
    } catch (...) {
        return {{}, "invalid"};
    }
}

}  // namespace lume::music
