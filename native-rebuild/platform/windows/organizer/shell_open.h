#pragma once
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <shobjidl.h>
#include <string>

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
    std::wstring target = path;
    const auto length = target.size();
    if (length >= 4 && _wcsicmp(target.c_str() + length - 4, L".lnk") == 0) {
        IShellLinkW* shortcut = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&shortcut)))) {
            IPersistFile* file = nullptr;
            if (SUCCEEDED(shortcut->QueryInterface(IID_PPV_ARGS(&file)))) {
                if (SUCCEEDED(file->Load(target.c_str(), STGM_READ))) {
                    wchar_t resolved[MAX_PATH * 4]{};
                    if (SUCCEEDED(shortcut->GetPath(resolved, ARRAYSIZE(resolved), nullptr, 0)) && resolved[0]) {
                        const auto attributes = GetFileAttributesW(resolved);
                        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
                            target = resolved;
                    }
                }
                file->Release();
            }
            shortcut->Release();
        }
    }
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOASYNC;
    request.lpVerb = L"open";
    request.lpFile = target.c_str();
    request.nShow = SW_SHOWNORMAL;
    const bool opened = execute(&request) != FALSE;
    if (request.hProcess) CloseHandle(request.hProcess);
    return opened;
}
}
