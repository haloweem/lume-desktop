#include "platform/windows/application_settings/application_settings_ports.h"
#include "platform/windows/application_settings/startup_task.h"
#include <windows.h>
#include <utility>

namespace lume::platform::windows {
namespace {
using namespace application_settings;
constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t RunValue[] = L"Lume";
constexpr DWORD MaxBytes = 65536;
struct RegistryKey {
    HKEY value = nullptr;
    ~RegistryKey() { if (value) RegCloseKey(value); }
};
class StartupRegistration final : public IStartupRegistration {
public:
    StartupRegistration(bool verification, StartupTaskHooks task)
        : verification_(verification), executable_(verification ? L"" : ApplicationSettingsExecutablePath()),
          task_(std::move(task)) {}
    bool Read(StartupEntry& entry) override {
        entry = {};
        if (verification_) return true;
        RegistryKey key;
        const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0,
                                          KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.value);
        if (opened == ERROR_FILE_NOT_FOUND) { SyncTask(entry); return true; }
        if (opened != ERROR_SUCCESS) return false;
        if (!ReadValue(key.value, entry)) return false;
        // Each launch reads the setting: bring the logon task in line with it,
        // which also adds the task for installs upgraded from Run-key only.
        SyncTask(entry);
        return true;
    }
    bool Write(const StartupEntry& entry) override {
        if (verification_ || entry.bytes.size() > MaxBytes || !Owned(entry)) return false;
        RegistryKey key;
        if (!entry.exists) {
            const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0,
                                              KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &key.value);
            if (opened == ERROR_FILE_NOT_FOUND) { SyncTask(entry); return true; }
            if (opened != ERROR_SUCCESS) return false;
        } else if (RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                  KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr,
                                  &key.value, nullptr) != ERROR_SUCCESS) return false;
        StartupEntry previous;
        // 写入和回滚共用归属检查，不能因服务层先前读到旧值就覆盖后来出现的其他程序。
        if (!ReadValue(key.value, previous) || !Owned(previous)) return false;
        if (SameStartupEntry(previous, entry)) { SyncTask(entry); return true; }
        if (!entry.exists) {
            const auto deleted = RegDeleteValueW(key.value, RunValue);
            const bool ok = deleted == ERROR_SUCCESS || deleted == ERROR_FILE_NOT_FOUND;
            if (ok) SyncTask(entry);
            return ok;
        }
        const bool ok = RegSetValueExW(key.value, RunValue, 0, entry.type, entry.bytes.data(),
                                       static_cast<DWORD>(entry.bytes.size())) == ERROR_SUCCESS;
        if (ok) SyncTask(entry);
        return ok;
    }
private:
    // Best effort: the Run entry stays the setting; a task failure changes nothing.
    void SyncTask(const StartupEntry& entry) const {
        if (verification_ || !task_.ensure || !task_.remove) return;
        const bool current = ClassifyStartupEntry(entry, executable_) == StartupEntryKind::Current;
        const bool disabled = task_.disabledByUser && task_.disabledByUser();
        if (current && !disabled) task_.ensure(executable_, L"--tray-only");
        else task_.remove();
    }
    bool Owned(const StartupEntry& entry) const {
        if (!StartupCommand(executable_)) return false;
        const auto kind = ClassifyStartupEntry(entry, executable_);
        return kind == StartupEntryKind::Missing || kind == StartupEntryKind::Current || kind == StartupEntryKind::Legacy;
    }
    static bool ReadValue(HKEY key, StartupEntry& entry) {
        entry = {};
        DWORD type = 0, size = 0;
        const auto queried = RegQueryValueExW(key, RunValue, nullptr, &type, nullptr, &size);
        if (queried == ERROR_FILE_NOT_FOUND) return true;
        if (queried != ERROR_SUCCESS || size > MaxBytes) return false;
        StartupEntry result{true, type, std::vector<std::uint8_t>(size)};
        if (RegQueryValueExW(key, RunValue, nullptr, &type, result.bytes.data(), &size) != ERROR_SUCCESS ||
            size > result.bytes.size()) return false;
        result.type = type;
        result.bytes.resize(size);
        entry = std::move(result);
        return true;
    }
    bool verification_ = false;
    std::wstring executable_;
    StartupTaskHooks task_;
};
}
StartupTaskHooks SystemStartupTaskHooks() {
    return {EnsureStartupTask, RemoveStartupTask, RunEntryDisabledByUser};
}
std::shared_ptr<application_settings::IStartupRegistration> CreateApplicationStartupRegistration(
    bool verification, StartupTaskHooks task) {
    return std::make_shared<StartupRegistration>(verification, std::move(task));
}
std::wstring ApplicationSettingsExecutablePath() {
    for (DWORD size = 512; size <= 32768; size *= 2) {
        std::wstring path(size, L'\0');
        const auto copied = GetModuleFileNameW(nullptr, path.data(), size);
        if (!copied) return {};
        if (copied < size) { path.resize(copied); return path; }
    }
    return {};
}
}
