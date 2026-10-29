#include "platform/windows/music_provider_credentials.h"
#include "platform/windows/music_provider_http.h"
#include <windows.h>
#include <wincrypt.h>
#include <memory>
#include <mutex>
#include <vector>

namespace lume::music::auth {
namespace {
constexpr std::size_t Limit = 64 * 1024;
std::mutex StoreMutex;
[[noreturn]] void Fail() { throw std::runtime_error("music credential storage error"); }
void Require(bool ok) { if (!ok) Fail(); }
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Secret {
    std::string value;
    ~Secret() { SecureZeroMemory(value.data(), value.size()); }
};
struct Blob {
    DATA_BLOB value{};
    ~Blob() {
        if (value.pbData) { SecureZeroMemory(value.pbData, value.cbData); LocalFree(value.pbData); }
    }
};
bool Missing() { const auto error = GetLastError(); return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }
std::filesystem::path Path(const std::filesystem::path& root, Provider provider) {
    Require(*Name(provider) && !root.empty());
    const auto source = root.wstring();
    Require(source.find(L'\0') == source.npos);
    auto absolute = std::filesystem::absolute(root).lexically_normal();
    const auto name = absolute.wstring();
    // Local drive paths only: reject device namespaces, UNC, ADS and ambiguous names.
    Require(name.size() >= 3 && name.size() < 240 && name[1] == L':' &&
        ((name[0] >= L'A' && name[0] <= L'Z') || (name[0] >= L'a' && name[0] <= L'z')) &&
        (name[2] == L'\\' || name[2] == L'/') && name.find(L':', 2) == name.npos);
    for (const auto& part : absolute.relative_path()) {
        const auto text = part.wstring();
        Require(text.empty() || (text.back() != L'.' && text.back() != L' '));
    }
    return absolute / (std::string("music-") + Name(provider) + ".credentials");
}
struct Directories {
    std::vector<std::unique_ptr<Handle>> held;
    bool Open(const std::filesystem::path& root, bool create) {
        auto current = root.root_path();
        if (!Hold(current, create)) return false;
        for (const auto& part : root.relative_path()) {
            if (part.empty()) continue;
            current /= part;
            if (!Hold(current, create)) return false;
        }
        return true;
    }
    bool Hold(const std::filesystem::path& path, bool create) {
        auto handle = std::make_unique<Handle>();
        handle->value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle->value == INVALID_HANDLE_VALUE && Missing()) {
            if (!create) return false;
            Require(CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS);
            handle->value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        }
        BY_HANDLE_FILE_INFORMATION info{};
        Require(handle->value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(handle->value, &info) &&
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT));
        // No FILE_SHARE_DELETE: checked ancestors cannot be swapped while in use.
        held.push_back(std::move(handle));
        return true;
    }
};
void Regular(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    Require(GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        info.nNumberOfLinks == 1);
}
void CheckDestination(const std::filesystem::path& path) {
    Handle file{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE && Missing()) return;
    Require(file.value != INVALID_HANDLE_VALUE);
    Regular(file.value);
}
std::string Entropy(const std::filesystem::path& path, Provider provider) {
    auto lower = path.wstring();
    CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
    std::string value = std::string("Lume.Music.Auth.v1|") + Name(provider) + "|";
    value.append(reinterpret_cast<const char*>(lower.data()), lower.size() * sizeof(wchar_t));
    return HashHex(value, "SHA256");
}
std::string Protect(std::string_view input, const std::string& entropy, bool decrypt) {
    Require(!input.empty() && input.size() <= Limit);
    DATA_BLOB source{static_cast<DWORD>(input.size()), reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.data()))};
    Blob output;
    Require(decrypt
        ? CryptUnprotectData(&source, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.value)
        : CryptProtectData(&source, L"Lume music credentials", &extra, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &output.value));
    Require(output.value.cbData && output.value.cbData <= Limit);
    return {reinterpret_cast<const char*>(output.value.pbData), output.value.cbData};
}
Json ReadJson(const std::string& plain) {
    auto result = Json::parse(plain, [](int depth, Json::parse_event_t, Json&) {
        Require(depth <= 32);
        return true;
    });
    Require(result.is_object());
    return result;
}
}  // namespace

Json LoadCredentials(const std::filesystem::path& root, Provider provider) {
    try {
        std::lock_guard lock(StoreMutex);
        const auto path = Path(root, provider);
        Directories directories;
        if (!directories.Open(path.parent_path(), false)) return Json::object();
        Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE && Missing()) return Json::object();
        Require(file.value != INVALID_HANDLE_VALUE);
        Regular(file.value);
        LARGE_INTEGER size{};
        Require(GetFileSizeEx(file.value, &size) && size.QuadPart > 0 &&
            static_cast<unsigned long long>(size.QuadPart) <= Limit);
        std::string cipher(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        Require(ReadFile(file.value, cipher.data(), static_cast<DWORD>(cipher.size()), &read, nullptr) &&
            read == cipher.size());
        Secret plain{Protect(cipher, Entropy(path, provider), true)};
        auto envelope = ReadJson(plain.value);
        Require(envelope.size() == 3 && envelope.at("version") == 1 &&
            envelope.at("provider") == Name(provider) && envelope.at("secrets").is_object());
        return std::move(envelope.at("secrets"));
    } catch (...) { Fail(); }
}
void SaveCredentials(const std::filesystem::path& root, Provider provider, const Json& secrets) {
    try {
        std::lock_guard lock(StoreMutex);
        const auto path = Path(root, provider);
        Require(secrets.is_object());
        Secret plain{Json{{"version", 1}, {"provider", Name(provider)}, {"secrets", secrets}}.dump()};
        Require(plain.value.size() <= Limit);
        ReadJson(plain.value);
        const auto cipher = Protect(plain.value, Entropy(path, provider), false);
        Directories directories;
        Require(directories.Open(path.parent_path(), true));
        CheckDestination(path);
        auto temp = path;
        temp += "." + RandomHex(16) + ".tmp";
        bool created = false;
        try {
            {
                Handle file{CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
                Require(file.value != INVALID_HANDLE_VALUE);
                created = true;
                Regular(file.value);
                DWORD written = 0;
                Require(WriteFile(file.value, cipher.data(), static_cast<DWORD>(cipher.size()), &written, nullptr) &&
                    written == cipher.size() && FlushFileBuffers(file.value));
            }
            CheckDestination(path);
            Require(MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
        } catch (...) {
            if (created) DeleteFileW(temp.c_str());
            throw;
        }
    } catch (...) { Fail(); }
}
void ClearCredentials(const std::filesystem::path& root, Provider provider) {
    try {
        std::lock_guard lock(StoreMutex);
        const auto path = Path(root, provider);
        Directories directories;
        if (!directories.Open(path.parent_path(), false)) return;
        Handle file{CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, 0, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE && Missing()) return;
        Require(file.value != INVALID_HANDLE_VALUE);
        Regular(file.value);
        FILE_DISPOSITION_INFO disposition{TRUE};
        Require(SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition)));
    } catch (...) { Fail(); }
}
}  // namespace lume::music::auth
