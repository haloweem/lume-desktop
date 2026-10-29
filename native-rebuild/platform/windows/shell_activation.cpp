#include "platform/windows/shell_activation.h"
#include <shellapi.h>
#include <cstring>

namespace lume::platform::windows {
namespace {
constexpr std::size_t MaxCallbackCharacters = 8192;
bool SameExecutable(HWND target) {
    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    if (!pid) return false;
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    std::wstring other(32768, L'\0'), current(32768, L'\0');
    DWORD size = static_cast<DWORD>(other.size());
    const bool read = QueryFullProcessImageNameW(process, 0, other.data(), &size) != FALSE;
    CloseHandle(process);
    const auto length = GetModuleFileNameW(nullptr, current.data(), static_cast<DWORD>(current.size()));
    return read && length && length < current.size() &&
        CompareStringOrdinal(current.data(), static_cast<int>(length),
            other.data(), static_cast<int>(size), TRUE) == CSTR_EQUAL;
}
}

bool IsLoginActivationUri(std::wstring_view value) noexcept {
    if (value.size() > MaxCallbackCharacters) return false;
    constexpr std::wstring_view prefix = L"lume://login?";
    constexpr std::wstring_view slashPrefix = L"lume://login/?";
    if (value.substr(0, prefix.size()) != prefix && value.substr(0, slashPrefix.size()) != slashPrefix)
        return false;
    for (const auto ch : value) {
        if (ch < 0x21 || ch > 0x7e || ch == L'\\' || ch == L'"' || ch == L'#') return false;
    }
    return value.back() != L'?';
}

ShellLaunch ParseShellLaunch(const std::vector<std::wstring>& arguments) {
    if (arguments.empty()) return {ShellLaunchKind::Interactive, {}};
    if (arguments.size() == 1 && (arguments[0] == L"--tray-only" || arguments[0] == L"-tray-only"))
        return {ShellLaunchKind::TrayOnly, {}};
    if (arguments.size() == 2 && arguments[0] == L"--login-callback" && IsLoginActivationUri(arguments[1]))
        return {ShellLaunchKind::LoginCallback, arguments[1]};
    return {};
}

ShellLaunch CurrentShellLaunch() {
    int count = 0;
    const auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv) return {};
    std::vector<std::wstring> arguments;
    for (int i = 1; i < count; ++i) arguments.emplace_back(argv[i]);
    LocalFree(argv);
    return ParseShellLaunch(arguments);
}

std::optional<std::wstring> DecodeLoginActivation(const COPYDATASTRUCT& data) {
    if (data.dwData != LoginActivationTag || !data.lpData ||
        data.cbData < 2 * sizeof(wchar_t) ||
        data.cbData > (MaxCallbackCharacters + 1) * sizeof(wchar_t) ||
        data.cbData % sizeof(wchar_t)) return {};
    std::wstring value(data.cbData / sizeof(wchar_t), L'\0');
    std::memcpy(value.data(), data.lpData, data.cbData);
    if (value.back() != L'\0') return {};
    value.pop_back();
    return IsLoginActivationUri(value) ? std::optional<std::wstring>{std::move(value)} : std::nullopt;
}

bool ForwardLoginActivation(HWND target, std::wstring_view uri) {
    if (!target || !IsLoginActivationUri(uri) || !SameExecutable(target)) return false;
    const std::wstring payload(uri);
    COPYDATASTRUCT data{};
    data.dwData = LoginActivationTag;
    data.cbData = static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(payload.c_str());
    DWORD_PTR accepted = 0;
    return SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &accepted) && accepted == 1;
}
}
