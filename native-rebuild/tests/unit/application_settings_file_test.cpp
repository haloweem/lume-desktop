#include "platform/windows/application_settings/application_settings_ports.h"
#include <windows.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
void Require(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}
struct RegistryKey {
    HKEY value = nullptr;
    ~RegistryKey() { if (value) RegCloseKey(value); }
};
class IsolatedCurrentUser {
public:
    IsolatedCurrentUser() {
        FILETIME time{};
        GetSystemTimeAsFileTime(&time);
        path_ = L"Software\\LumeAutostartTest-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(time.dwHighDateTime) + L"-" + std::to_wstring(time.dwLowDateTime);
        Require(RegOpenCurrentUser(KEY_ALL_ACCESS, &user_.value) == ERROR_SUCCESS, "打开隔离夹具父键");
        DWORD disposition = 0;
        Require(RegCreateKeyExW(user_.value, path_.c_str(), 0, nullptr, REG_OPTION_VOLATILE,
                KEY_ALL_ACCESS, nullptr, &isolated_.value, &disposition) == ERROR_SUCCESS &&
                disposition == REG_CREATED_NEW_KEY, "仅使用本次测试新建的临时注册表根");
        if (RegOverridePredefKey(HKEY_CURRENT_USER, isolated_.value) != ERROR_SUCCESS) {
            RegDeleteTreeW(user_.value, path_.c_str());
            throw std::runtime_error("进程内注册表重定向失败，禁止测试生产适配器");
        }
    }
    ~IsolatedCurrentUser() {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegDeleteTreeW(user_.value, path_.c_str());
    }
    void VerifyRegistration() {
        using namespace lume::application_settings;
        using namespace lume::platform::windows;
        constexpr auto runPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        RegistryKey run;
        Require(RegCreateKeyExW(isolated_.value, runPath, 0, nullptr, REG_OPTION_VOLATILE,
                KEY_ALL_ACCESS | KEY_WOW64_64KEY, nullptr, &run.value, nullptr) == ERROR_SUCCESS,
                "夹具只在隔离根下创建 Run");
        const auto seed = [&](const wchar_t* name, const StartupEntry& value) {
            Require(RegSetValueExW(run.value, name, 0, value.type, value.bytes.data(),
                    static_cast<DWORD>(value.bytes.size())) == ERROR_SUCCESS, "写入隔离夹具");
        };
        const auto foreign = EnabledStartupEntry(L"\"C:\\Other\\Other.exe\"");
        seed(L"OtherProgram", foreign);
        // The logon task is faked: tests must never add a real scheduled task.
        int ensured = 0, removed = 0; bool userDisabled = false; std::wstring taskExecutable, taskArguments;
        StartupTaskHooks hooks{[&](const std::wstring& path, const std::wstring& arguments) {
                                   ++ensured; taskExecutable = path; taskArguments = arguments; return true; },
                               [&] { ++removed; return true; }, [&] { return userDisabled; }};
        const auto port = CreateApplicationStartupRegistration(false, hooks);
        const auto executable = ApplicationSettingsExecutablePath();
        const auto command = StartupCommand(executable);
        Require(command.has_value(), "测试可执行路径可构造命令");
        const auto enabled = EnabledStartupEntry(*command);
        StartupEntry actual;
        Require(port->Read(actual) && !actual.exists && port->Write(enabled) &&
                port->Read(actual) && SameStartupEntry(actual, enabled), "生产适配器在隔离注册表真实写入并读回");
        Require(ensured >= 2 && taskExecutable == executable && taskArguments == L"--tray-only",
                "开启开机启动时同步登录计划任务，参数与 Run 入口一致");
        userDisabled = true; removed = 0;
        Require(port->Read(actual) && removed == 1, "任务管理器禁用 Run 入口后移除登录计划任务");
        userDisabled = false; removed = 0;
        Require(port->Write({}) && port->Read(actual) && !actual.exists, "生产适配器删除本实例入口");
        Require(removed >= 1, "关闭开机启动时移除登录计划任务");
        const auto legacy = EnabledStartupEntry(L"\"" + executable.substr(0, executable.find_last_of(L"\\/") + 1) +
                                                 L"lume.exe\" -tray-only");
        seed(L"Lume", legacy);
        Require(port->Write(enabled) && port->Read(actual) && SameStartupEntry(actual, enabled),
                "同目录旧入口可迁移为原生命令");
        Require(port->Write(legacy) && port->Read(actual) && SameStartupEntry(actual, legacy),
                "回滚可以恢复同目录旧命令的原始字节");
        Require(port->Write({}) && port->Read(actual) && !actual.exists, "明确关闭删除同目录旧入口");
        for (const auto& unknown : {foreign, StartupEntry{true, REG_BINARY, {0, 1, 2, 255}},
                                   EnabledStartupEntry(L"\"C:\\Other\\lume.exe\" -tray-only")}) {
            seed(L"Lume", unknown);
            Require(!port->Write(enabled) && port->Read(actual) && SameStartupEntry(actual, unknown),
                    "生产适配器不能覆盖同名无关程序");
            Require(!port->Write({}) && port->Read(actual) && SameStartupEntry(actual, unknown),
                    "生产适配器不能删除同名无关程序");
        }
        const auto verification = CreateApplicationStartupRegistration(true);
        StartupEntry untouched;
        Require(port->Read(untouched) && verification->Read(actual) && !actual.exists &&
                !verification->Write({}) && port->Read(actual) && SameStartupEntry(actual, untouched),
                "验证适配器既不泄露隔离 Run 值，也不能触发删除");
        DWORD type = 0, size = static_cast<DWORD>(foreign.bytes.size());
        std::vector<std::uint8_t> other(size);
        Require(RegQueryValueExW(run.value, L"OtherProgram", nullptr, &type, other.data(), &size) == ERROR_SUCCESS &&
                type == foreign.type && other == foreign.bytes, "其他名称的启动项逐字节保持不变");
    }
private:
    RegistryKey user_, isolated_;
    std::wstring path_;
};
}
int main(int argc, char** argv) {
    using namespace lume::application_settings;
    using namespace lume::platform::windows;
    namespace fs = std::filesystem;
    Check(argc == 2, "explicit project build-artifacts directory required");
    const auto base = fs::weakly_canonical(fs::u8path(argv[1]));
    bool isolated = false;
    for (const auto& component : base) if (component == "build-artifacts") isolated = true;
    Check(isolated && base.is_absolute(), "test data stays under project build-artifacts");
    const auto root = base / ("application-settings-files-" + std::to_string(GetCurrentProcessId()) + "-" +
                              std::to_string(GetTickCount64()));
    fs::create_directories(root);
    const auto file = CreateApplicationSettingsFile((root / "preferences.json").u8string());
    Check(file && file->Read().status == ReadStatus::Missing, "missing preferences are read without creating a file");
    Check(!fs::exists(root / "preferences.json"), "read does not materialize preferences");
    Check(file->WriteAtomically(R"({"version":1,"language":"en","balancedMode":false,"autostart":false})"),
          "atomic write succeeds");
    const auto before = file->Read().bytes;
    Check(DecodePreferences(before).preferences.language == "en", "UTF8 preferences read back");
    HANDLE locked = CreateFileW((root / "preferences.json").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "fixture locks destination without allowing replacement");
    Check(!file->WriteAtomically("{}"), "failed replacement is reported");
    CloseHandle(locked);
    Check(file->Read().bytes == before, "failed atomic replacement preserves original bytes");
    for (const auto& entry : fs::directory_iterator(root))
        Check(entry.path().filename() == "preferences.json", "temporary file cleaned on failure");
    const auto nested = root / L"\u8bbe\u7f6e" / L"preferences.json";
    const auto unicode = CreateApplicationSettingsFile(nested.u8string());
    Check(unicode->Read().status == ReadStatus::Missing && !fs::exists(nested.parent_path()),
          "reading an absent UTF8 directory is side-effect free");
    Check(unicode->WriteAtomically(before) && unicode->Read().bytes == before, "atomic write handles Unicode paths");
    Check(CreateApplicationSettingsFile("")->Read().status == ReadStatus::Failed &&
          !CreateApplicationSettingsFile("preferences.json")->WriteAtomically("{}"),
          "empty and relative paths cannot create unexpected settings files");
    const auto verification = CreateApplicationStartupRegistration(true);
    StartupEntry startup;
    Check(verification->Read(startup) && !startup.exists &&
          !verification->Write(EnabledStartupEntry(L"\"X:\\LumeShell.exe\" --tray-only")),
          "verification startup adapter never accesses or mutates real Run");
    Cancellation cancelled;
    cancelled.cancelled = true;
    Check(CreateApplicationUpdateTransport(false)->Fetch(cancelled).error == "update-cancelled",
          "pre-cancelled request performs no network I/O");
    Cancellation fresh;
    Check(CreateApplicationUpdateTransport(true)->Fetch(fresh).error == "update-verification",
          "verification never contacts the official server");
    try {
        IsolatedCurrentUser isolatedRegistry;
        isolatedRegistry.VerifyRegistration();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "application settings Windows file adapter passed\n";
}
