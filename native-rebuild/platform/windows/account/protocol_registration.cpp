#include "platform/windows/account/protocol_registration.h"
#include <windows.h>
#include <vector>
namespace lume::platform::windows::account {
namespace {
constexpr wchar_t Root[] = L"Software\\Classes\\lume";
constexpr wchar_t Shell[] = L"Software\\Classes\\lume\\shell";
constexpr wchar_t Command[] = L"Software\\Classes\\lume\\shell\\open\\command";
struct Key {
    HKEY value = nullptr;
    ~Key() { if (value) RegCloseKey(value); }
};
class CurrentUserRegistry final : public ILoginProtocolRegistry {
public:
    bool Write(std::wstring_view path, std::wstring_view name, std::wstring_view value) override {
        Key key;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, std::wstring(path).c_str(), 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key.value, nullptr) != ERROR_SUCCESS) return false;
        const std::wstring data(value);
        return RegSetValueExW(key.value, std::wstring(name).c_str(), 0, REG_SZ,
            reinterpret_cast<const BYTE*>(data.c_str()), static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }
    std::optional<std::wstring> Read(std::wstring_view path, std::wstring_view name) override {
        DWORD size = 0;
        const std::wstring subkey(path), valueName(name);
        if (RegGetValueW(HKEY_CURRENT_USER, subkey.c_str(), valueName.c_str(), RRF_RT_REG_SZ,
            nullptr, nullptr, &size) != ERROR_SUCCESS || size < sizeof(wchar_t) || size > 128 * 1024 || size % sizeof(wchar_t)) return {};
        std::vector<wchar_t> buffer(size / sizeof(wchar_t));
        if (RegGetValueW(HKEY_CURRENT_USER, subkey.c_str(), valueName.c_str(), RRF_RT_REG_SZ,
            nullptr, buffer.data(), &size) != ERROR_SUCCESS || size < sizeof(wchar_t) ||
            size > buffer.size() * sizeof(wchar_t) || size % sizeof(wchar_t)) return {};
        // RegGetValue's size-only query can reserve more bytes than the actual read.
        const auto characters = size / sizeof(wchar_t);
        if (buffer[characters - 1] != L'\0') return {};
        std::wstring value(buffer.data(), characters - 1);
        if (value.find(L'\0') != value.npos) return {};
        return value;
    }
    bool RemoveValue(std::wstring_view path, std::wstring_view name) override {
        Key key;
        auto status = RegOpenKeyExW(HKEY_CURRENT_USER, std::wstring(path).c_str(), 0, KEY_SET_VALUE, &key.value);
        if (status == ERROR_FILE_NOT_FOUND) return true;
        if (status != ERROR_SUCCESS) return false;
        status = RegDeleteValueW(key.value, std::wstring(name).c_str());
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }
};
bool ValidExecutable(std::wstring_view path) {
    if (path.size() < 7 || path.size() > 32760 || path.find_first_of(L"\"\r\n\t") != path.npos ||
        path.find(L'\0') != path.npos || path.back() == L' ') return false;
    for (auto c : path) if (c < 32) return false;
    const bool drive = ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':' && path[2] == L'\\';
    const bool unc = path.substr(0, 2) == L"\\\\" && path.size() > 5 && path.find(L'\\', 2) != path.npos;
    return (drive || unc) && CompareStringOrdinal(path.data() + path.size() - 4, 4, L".exe", 4, TRUE) == CSTR_EQUAL;
}
}
bool EnsureLoginProtocol(ILoginProtocolRegistry& registry, std::wstring_view executable) {
    if (!ValidExecutable(executable)) return false;
    try {
        const auto command = L"\"" + std::wstring(executable) + L"\" --login-callback \"%1\"";
        if (!registry.Write(Root, L"", L"URL:Lume Login Protocol") ||
            !registry.Write(Root, L"URL Protocol", L"") ||
            !registry.Write(Shell, L"", L"open") ||
            !registry.Write(Command, L"", command) ||
            !registry.RemoveValue(Command, L"DelegateExecute")) return false;
        return registry.Read(Root, L"URL Protocol") == std::optional<std::wstring>(L"") &&
            registry.Read(Shell, L"") == std::optional<std::wstring>(L"open") &&
            registry.Read(Command, L"") == std::optional<std::wstring>(command) &&
            !registry.Read(Command, L"DelegateExecute");
    } catch (...) { return false; }
}
bool EnsureLoginProtocol() {
    std::vector<wchar_t> buffer(32768);
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) return false;
    CurrentUserRegistry registry;
    return EnsureLoginProtocol(registry, std::wstring_view(buffer.data(), size));
}
}
