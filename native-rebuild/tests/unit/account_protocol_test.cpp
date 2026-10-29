#include <iostream>
#include "platform/windows/account/protocol_registration.h"
#include "apps/shell/account_session.h"
#include "tests/unit/support/account_backend_fixture.h"
#include <map>
#include <cstdlib>
#include <windows.h>
#include <stdexcept>
using namespace lume::platform::windows::account;
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
namespace {
void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
class IsolatedCurrentUser {
public:
    IsolatedCurrentUser() {
        FILETIME time{};
        GetSystemTimeAsFileTime(&time);
        path_ = L"Software\\LumeLoginProtocolTest-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(time.dwHighDateTime) + L"-" + std::to_wstring(time.dwLowDateTime);
        Require(RegOpenCurrentUser(KEY_ALL_ACCESS, &user_) == ERROR_SUCCESS, "open own user hive for isolated test");
        DWORD disposition = 0;
        const auto created = RegCreateKeyExW(user_, path_.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, nullptr, &isolated_, &disposition);
        if (created != ERROR_SUCCESS || disposition != REG_CREATED_NEW_KEY) {
            if (isolated_) RegCloseKey(isolated_);
            RegCloseKey(user_);
            throw std::runtime_error("reserve fresh isolated registry root");
        }
        const auto redirected = RegOverridePredefKey(HKEY_CURRENT_USER, isolated_);
        if (redirected != ERROR_SUCCESS) {
            RegCloseKey(isolated_);
            RegDeleteTreeW(user_, path_.c_str());
            RegCloseKey(user_);
            throw std::runtime_error("redirect this test process only");
        }
    }
    ~IsolatedCurrentUser() {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegCloseKey(isolated_);
        RegDeleteTreeW(user_, path_.c_str());
        RegCloseKey(user_);
    }
    void CheckProduction() {
        Require(EnsureLoginProtocol(), "production registry adapter registers the executable");
        wchar_t marker[8]{};
        DWORD bytes = sizeof(marker);
        Require(RegGetValueW(isolated_, L"Software\\Classes\\lume", L"URL Protocol", RRF_RT_REG_SZ,
            nullptr, marker, &bytes) == ERROR_SUCCESS && marker[0] == L'\0',
            "real empty URL Protocol marker survives readback");
        Require(EnsureLoginProtocol(), "production registry adapter is idempotent");
        const wchar_t* commandKey = L"Software\\Classes\\lume\\shell\\open\\command";
        HKEY key = nullptr;
        Require(RegOpenKeyExW(isolated_, commandKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS,
            "open isolated legacy command fixture");
        const wchar_t oldCommand[] = L"\"D:\\old\\LumeUI.exe\" \"%1\"";
        const wchar_t delegate[] = L"old-handler";
        const auto written = RegSetValueExW(key, L"", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(oldCommand), sizeof(oldCommand));
        const auto delegated = RegSetValueExW(key, L"DelegateExecute", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(delegate), sizeof(delegate));
        RegCloseKey(key);
        Require(written == ERROR_SUCCESS && delegated == ERROR_SUCCESS, "seed isolated legacy handler");
        Require(EnsureLoginProtocol(), "production registry adapter repairs a real old handler");
        wchar_t module[32768]{}, actual[32768]{};
        const auto length = GetModuleFileNameW(nullptr, module, 32768);
        Require(length > 0 && length < 32768, "get fixture executable");
        bytes = sizeof(actual);
        Require(RegGetValueW(isolated_, commandKey, L"", RRF_RT_REG_SZ, nullptr, actual, &bytes) == ERROR_SUCCESS &&
            std::wstring(actual) == L"\"" + std::wstring(module, length) + L"\" --login-callback \"%1\"",
            "real repaired command uses the current executable and fixed callback argument");
        bytes = 0;
        Require(RegGetValueW(isolated_, commandKey, L"DelegateExecute", RRF_RT_ANY, nullptr, nullptr, &bytes) == ERROR_FILE_NOT_FOUND,
            "real stale delegate was removed");

        lume::shell::AccountSessionOptions options;
        options.transport = std::make_shared<lume::account::testing::FixtureTransport>();
        options.credentials = std::make_shared<lume::account::testing::MemoryCredentials>();
        unsigned browserCalls = 0;
        options.openBrowser = [&](std::wstring_view url) {
            ++browserCalls;
            return lume::account::ValidAuthorizationUrl(lume::account::Utf8(url));
        };
        // Keep the production protocol callback, but replace credentials, network and browser.
        lume::shell::AccountSession session(GetModuleHandleW(nullptr), std::move(options), {});
        session.HandlePage({lume::ui::AccountAction::Login, {}});
        const auto state = lume::account::StrictJson(lume::account::Utf8(session.State()));
        Require(browserCalls == 1 && state["status"] == "authorizing" && state["error"] == "",
            "real registration lets the production login session open authorization");
        session.Shutdown();
    }
private:
    HKEY user_ = nullptr, isolated_ = nullptr;
    std::wstring path_;
};
}
class Registry final : public ILoginProtocolRegistry {
public:
    bool Write(std::wstring_view key, std::wstring_view name, std::wstring_view value) override {
        ++writes;
        Expect(key == L"Software\\Classes\\lume\\shell\\open\\command" || key == L"Software\\Classes\\lume" ||
            key == L"Software\\Classes\\lume\\shell", "only fixed lume scheme keys");
        if (failWrite) return false;
        values[{std::wstring(key), std::wstring(name)}] = value; return true;
    }
    std::optional<std::wstring> Read(std::wstring_view key, std::wstring_view name) override {
        if (failRead) return {};
        const auto it = values.find({std::wstring(key), std::wstring(name)});
        return it == values.end() ? std::optional<std::wstring>{} : it->second;
    }
    bool RemoveValue(std::wstring_view key, std::wstring_view name) override {
        if (failWrite) return false;
        values.erase({std::wstring(key), std::wstring(name)}); return true;
    }
    std::map<std::pair<std::wstring, std::wstring>, std::wstring> values;
    bool failWrite = false, failRead = false;
    unsigned writes = 0;
};
int main() {
    Registry registry;
    const std::wstring commandKey = L"Software\\Classes\\lume\\shell\\open\\command";
    registry.values[{commandKey, L""}] = L"\"D:\\old\\Electron.exe\" \"%1\"";
    registry.values[{commandKey, L"DelegateExecute"}] = L"old-handler";
    const std::wstring exe = L"C:\\Program Files\\Lume\\LumeShell.exe";
    Expect(EnsureLoginProtocol(registry, exe), "explicit user login repairs old per-user override");
    Expect(registry.values[{commandKey, L""}] == L"\"C:\\Program Files\\Lume\\LumeShell.exe\" --login-callback \"%1\"",
        "fixed executable and callback arguments");
    Expect(registry.values[{L"Software\\Classes\\lume", L"URL Protocol"}].empty(), "URL Protocol marker");
    Expect(!registry.values.count({commandKey, L"DelegateExecute"}), "stale delegate cannot override fixed command");
    Expect(EnsureLoginProtocol(registry, exe), "idempotent registration");
    Registry failure; failure.failWrite = true;
    Expect(!EnsureLoginProtocol(failure, exe), "write failure not success");
    failure.failWrite = false; failure.failRead = true;
    Expect(!EnsureLoginProtocol(failure, exe), "readback failure not success");
    for (const auto* bad : {L"relative.exe", L"https://evil.example/a.exe", L"C:\\x.exe\" --evil",
        L"C:\\x.exe\r\n", L"C:\\x.bat", L""}) {
        Registry isolated;
        Expect(!EnsureLoginProtocol(isolated, bad) && isolated.writes == 0, "invalid executable rejected without writes");
    }
    try {
        IsolatedCurrentUser isolated;
        isolated.CheckProduction();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "protocol ports and production registry passed with an isolated redirected hive\n";
}
