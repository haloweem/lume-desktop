#include "platform/windows/music_local_files.h"
#include "platform/windows/text_util.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <set>
#include <system_error>

namespace lume::music {
namespace {

constexpr std::size_t kMaxFiles = 32;
constexpr std::uintmax_t kMaxFileBytes = 512ull * 1024ull * 1024ull;

using lume::text::Utf8;

bool Supported(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".mp3" || extension == L".wav" || extension == L".flac" ||
        extension == L".m4a" || extension == L".aac" || extension == L".ogg" || extension == L".wma";
}

std::string Id(const std::string& source) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : source) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return "local-" + std::to_string(hash);
}

struct ReadLease {
    std::vector<HANDLE> handles;
    ~ReadLease() { for (auto handle : handles) CloseHandle(handle); }
    HANDLE Open(const std::filesystem::path& path, bool directory) {
        const auto handle = CreateFileW(path.c_str(), directory ? FILE_READ_ATTRIBUTES : GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
        if (handle == INVALID_HANDLE_VALUE) return handle;
        handles.push_back(handle);
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory)
            return INVALID_HANDLE_VALUE;
        return handle;
    }
};

}  // namespace

bool IsLocalMusicPath(std::wstring_view path) noexcept {
    try {
        if (path.size() < 4 || path.size() > 4096 || path.find(L'\0') != path.npos ||
            !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
            path[1] != L':' || (path[2] != L'\\' && path[2] != L'/') ||
            path.find_first_of(L":*?\"<>|", 2) != path.npos) return false;
        const std::filesystem::path parsed(path);
        for (const auto& component : parsed.relative_path()) {
            const auto part = component.wstring();
            if (part != L"." && part != L".." && !part.empty() &&
                (part.back() == L'.' || part.back() == L' ')) return false;
            for (auto c : part) if (c < 32) return false;
        }
        const auto type = GetDriveTypeW(parsed.root_path().c_str());
        return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_RAMDISK;
    } catch (...) { return false; }
}

LocalMusicFileInfo InspectLocalMusicFile(const std::wstring& input) noexcept {
    LocalMusicFileInfo result;
    try {
        if (!IsLocalMusicPath(input)) { result.error = "invalid-path"; return result; }
        const auto path = std::filesystem::path(input).lexically_normal();
        if (!Supported(path)) { result.error = "unsupported-type"; return result; }
        auto lease = std::make_shared<ReadLease>();
        auto directory = path.root_path();
        if (lease->Open(directory, true) == INVALID_HANDLE_VALUE) {
            result.error = "invalid-path"; return result;
        }
        for (const auto& component : path.parent_path().relative_path()) {
            directory /= component;
            if (lease->Open(directory, true) == INVALID_HANDLE_VALUE) {
                result.error = "invalid-path"; return result;
            }
        }
        const auto handle = lease->Open(path, false);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            result.error = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ?
                "missing" : "access-denied";
            return result;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        LARGE_INTEGER size{};
        if (!GetFileInformationByHandle(handle, &info) || !GetFileSizeEx(handle, &size)) {
            result.error = "access-denied"; return result;
        }
        if (size.QuadPart <= 0 || static_cast<std::uint64_t>(size.QuadPart) > kMaxFileBytes) {
            result.error = "file-too-large"; return result;
        }
        std::wstring resolved(4097, L'\0');
        const auto length = GetFinalPathNameByHandleW(handle, resolved.data(),
            static_cast<DWORD>(resolved.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!length || length >= resolved.size()) { result.error = "invalid-path"; return result; }
        resolved.resize(length);
        if (resolved.rfind(L"\\\\?\\", 0) == 0) resolved.erase(0, 4);
        if (!IsLocalMusicPath(resolved)) { result.error = "invalid-path"; return result; }
        const auto source = Utf8(resolved);
        const auto title = Utf8(std::filesystem::path(resolved).stem().wstring());
        if (source.empty() || source.size() > 4096 || title.empty() || title.size() > 512) {
            result.error = "invalid-path"; return result;
        }
        result.identity = std::to_string(info.dwVolumeSerialNumber) + "-" +
            std::to_string((static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow) + "-" +
            std::to_string((static_cast<std::uint64_t>(info.ftCreationTime.dwHighDateTime) << 32) |
                info.ftCreationTime.dwLowDateTime);
        result.track.id = Id(result.identity);
        result.track.title = title;
        result.track.source = source;
        result.bytes = static_cast<std::uint64_t>(size.QuadPart);
        result.modified = (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) |
            info.ftLastWriteTime.dwLowDateTime;
        result.readLease = std::move(lease);
    } catch (...) { result.error = "invalid-path"; }
    return result;
}

LocalMusicImportResult ImportLocalMusicFiles(const std::vector<std::wstring>& paths) noexcept {
    try {
        if (paths.empty()) return {{}, "empty"};
        if (paths.size() > kMaxFiles) return {{}, "too-many-files"};
        LocalMusicImportResult result;
        result.tracks.reserve(paths.size());
        std::set<std::string> identities;
        for (const auto& input : paths) {
            auto info = InspectLocalMusicFile(input);
            if (!info.error.empty()) return {{}, info.error};
            if (identities.insert(info.identity).second) result.tracks.push_back(std::move(info.track));
        }
        return result;
    } catch (...) {
        return {{}, "invalid-path"};
    }
}

}  // namespace lume::music
