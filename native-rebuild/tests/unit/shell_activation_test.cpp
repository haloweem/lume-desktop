#include "platform/windows/shell_activation.h"
#include "platform/windows/application_tray.h"
#include <iostream>
#include <string>

namespace {
bool passed = true;
void Check(bool value, const char* name) {
    if (!value) { passed = false; std::cerr << "FAIL: " << name << '\n'; }
}
}

int main() {
    using namespace lume::platform::windows;
    Check(MapTrayCommand(1).value_or(TrayAction::Exit) == TrayAction::Open,
        "tray open command");
    Check(MapTrayCommand(2).value_or(TrayAction::Open) == TrayAction::Exit,
        "tray exit command");
    Check(!MapTrayCommand(3).has_value(), "removed tray commands are ignored");
    Check(ParseShellLaunch({}).kind == ShellLaunchKind::Interactive, "empty launch opens manager");
    Check(ParseShellLaunch({L"--tray-only"}).kind == ShellLaunchKind::TrayOnly, "native startup remains hidden");
    Check(ParseShellLaunch({L"-tray-only"}).kind == ShellLaunchKind::TrayOnly, "legacy startup spelling");
    const std::wstring uri = L"lume://login?code=test-code&state=test-state";
    const auto login = ParseShellLaunch({L"--login-callback", uri});
    Check(login.kind == ShellLaunchKind::LoginCallback && login.callback == uri, "explicit protocol activation");
    Check(ParseShellLaunch({L"--login-callback"}).kind == ShellLaunchKind::Invalid, "missing callback rejected");
    Check(ParseShellLaunch({L"--login-callback", uri, L"extra"}).kind == ShellLaunchKind::Invalid, "extra arguments rejected");
    Check(ParseShellLaunch({uri}).kind == ShellLaunchKind::Invalid, "unqualified URI is not an implicit command");
    Check(ParseShellLaunch({L"--tray-only", L"--login-callback", uri}).kind == ShellLaunchKind::Invalid, "ambiguous modes rejected");
    for (const auto& invalid : {L"https://login?code=a", L"lume://login.evil?code=a",
        L"lume://login:443?code=a", L"lume://login?code=a#fragment",
        L"lume://login?code=a\" --anything", L"lume://login?code=a\\b",
        L"lume://login?code=a\n", L"lume://other?code=a", L"lume://login"}) {
        Check(!IsLoginActivationUri(invalid), "non-login or unsafe activation rejected");
    }
    Check(!IsLoginActivationUri(std::wstring(9000, L'a')), "oversized activation rejected");
    Check(!IsLoginActivationUri(std::wstring(L"lume://login?code=a\0b", 21)), "embedded NUL rejected");

    COPYDATASTRUCT copy{};
    copy.dwData = LoginActivationTag;
    copy.cbData = static_cast<DWORD>((uri.size() + 1) * sizeof(wchar_t));
    copy.lpData = const_cast<wchar_t*>(uri.c_str());
    Check(DecodeLoginActivation(copy).value_or(L"") == uri, "bounded copydata round trip");
    copy.dwData = 0;
    Check(!DecodeLoginActivation(copy), "unknown copydata tag rejected");
    copy.dwData = LoginActivationTag;
    --copy.cbData;
    Check(!DecodeLoginActivation(copy), "odd or truncated bytes rejected");
    copy.cbData = 2;
    Check(!DecodeLoginActivation(copy), "missing envelope rejected");
    copy.cbData = 1000000;
    Check(!DecodeLoginActivation(copy), "oversized byte count rejected before reading payload");
    copy.cbData = static_cast<DWORD>((uri.size() + 1) * sizeof(wchar_t));
    copy.lpData = nullptr;
    Check(!DecodeLoginActivation(copy), "null payload rejected");

    int callbacks = 0;
    ApplicationTray tray;
    Check(tray.Create(GetModuleHandleW(nullptr), [](TrayAction) {},
        [&](const std::wstring& value) { Check(value == uri, "queued callback preserved"); ++callbacks; }),
        "create isolated tray for callback dispatch");
    HWND target = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM result) -> BOOL {
        wchar_t name[80]{};
        GetClassNameW(hwnd, name, 80);
        if (std::wstring(name) != L"LumeNativeApplicationTray") return TRUE;
        *reinterpret_cast<HWND*>(result) = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&target));
    // Do not send authorization data to an installed process during tests.
    DWORD owner = 0;
    if (target) GetWindowThreadProcessId(target, &owner);
    if (owner == GetCurrentProcessId()) {
        Check(ForwardLoginActivation(target, uri), "same-executable callback forwarded");
        Check(callbacks == 0, "WM_COPYDATA does not reenter application callback");
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Check(callbacks == 1, "callback delivered after sender returns");
    } else {
        Check(false, "test must locate its own tray window");
    }
    Check(!ForwardLoginActivation(nullptr, uri), "missing window rejected");
    return passed ? 0 : 1;
}
