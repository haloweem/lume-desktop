#include "platform/windows/application_settings/application_settings_ports.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <utility>

namespace lume::platform::windows {
namespace {
using namespace application_settings;
constexpr DWORD MaxBytes = 65536;
struct FileHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit FileHandle(HANDLE handle) : value(handle) {}
    ~FileHandle() { Close(); }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    void Close() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
};
std::filesystem::path Path(const std::string& text) {
    if (text.empty() || text.size() > 32760 || text.find('\0') != text.npos) return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), wide.data(), size)) return {};
    const std::filesystem::path path(wide);
    return path.is_absolute() && path.has_filename() ? path : std::filesystem::path{};
}
class PreferencesFile final : public IPreferencesFile {
public:
    explicit PreferencesFile(const std::string& path) : path_(Path(path)) {}
    FileRead Read() override {
        if (path_.empty()) return {ReadStatus::Failed, {}};
        FileHandle file(CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.value == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            return {error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                        ? ReadStatus::Missing : ReadStatus::Failed, {}};
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.value, &size) || size.QuadPart < 0 || size.QuadPart > MaxBytes)
            return {ReadStatus::Failed, {}};
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD count = 0;
        if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) ||
            count != bytes.size()) return {ReadStatus::Failed, {}};
        return {ReadStatus::Loaded, std::move(bytes)};
    }
    bool WriteAtomically(const std::string& bytes) override {
        if (path_.empty() || bytes.empty() || bytes.size() > MaxBytes) return false;
        std::error_code error;
        std::filesystem::create_directories(path_.parent_path(), error);
        if (error) return false;
        static std::atomic<unsigned long long> serial{0};
        std::filesystem::path temporary;
        HANDLE handle = INVALID_HANDLE_VALUE;
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            temporary = path_;
            temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                         std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial);
            handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle != INVALID_HANDLE_VALUE) break;
            if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        }
        if (handle == INVALID_HANDLE_VALUE) return false;
        FileHandle file(handle);
        DWORD written = 0;
        const bool flushed = WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                             written == bytes.size() && FlushFileBuffers(file.value);
        file.Close();
        const bool replaced = flushed && MoveFileExW(temporary.c_str(), path_.c_str(),
                                                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!replaced) DeleteFileW(temporary.c_str());
        return replaced;
    }
private:
    std::filesystem::path path_;
};
}
std::shared_ptr<application_settings::IPreferencesFile> CreateApplicationSettingsFile(std::string path) {
    return std::make_shared<PreferencesFile>(path);
}
}
