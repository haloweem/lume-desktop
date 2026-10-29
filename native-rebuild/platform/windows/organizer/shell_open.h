#pragma once
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

namespace lume::platform::windows {
// Called in the disposable helper, or on a dedicated Lume thread when the
// helper cannot start; never on the manager's UI thread. Explorer
// and Shell extension verbs can require an STA even for ordinary directories.
inline bool OpenShellTarget(const wchar_t* path,
    BOOL (WINAPI* execute)(SHELLEXECUTEINFOW*) = ShellExecuteExW) {
    if (!path || !*path) return false;
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(apartment)) return false;
    struct Apartment { ~Apartment() { CoUninitialize(); } } lease;
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOASYNC;
    request.lpVerb = L"open";
    request.lpFile = path;
    request.nShow = SW_SHOWNORMAL;
    const bool opened = execute(&request) != FALSE;
    if (request.hProcess) CloseHandle(request.hProcess);
    return opened;
}
}
