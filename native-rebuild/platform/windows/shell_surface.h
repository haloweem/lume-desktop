#pragma once
#include <windows.h>
#include <cwchar>
#include <initializer_list>

namespace lume::platform::windows::shell_surface {
// Shell overlays may cover a monitor without obscuring the desktop behind
// them. Share their identity between widget protection and wallpaper pausing.
inline bool ShellSurfaceImage(const wchar_t* path) {
    if (!path) return false;
    const wchar_t* name = path;
    for (const wchar_t* at = path; *at; ++at) if (*at == L'\\' || *at == L'/') name = at + 1;
    for (const wchar_t* shell : {L"ShellExperienceHost.exe", L"StartMenuExperienceHost.exe", L"SearchHost.exe",
                                 L"SearchApp.exe", L"SearchUI.exe", L"TextInputHost.exe", L"ShellHost.exe"})
        if (_wcsicmp(name, shell) == 0) return true;
    return false;
}
inline bool ExplorerSurfaceClass(const wchar_t* name) {
    return name && *name && wcscmp(name, L"CabinetWClass") != 0 && wcscmp(name, L"ExploreWClass") != 0;
}
inline bool ShellSurface(HWND window) {
    const HWND owner = GetAncestor(window, GA_ROOTOWNER);
    const HWND root = owner ? owner : window;
    DWORD pid = 0, shell = 0;
    GetWindowThreadProcessId(root, &pid); GetWindowThreadProcessId(GetShellWindow(), &shell);
    if (!pid) return false;
    // File Explorer remains an ordinary application, including fullscreen.
    if (shell && pid == shell) {
        wchar_t name[64]{}; GetClassNameW(root, name, 64);
        return ExplorerSurfaceClass(name);
    }
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[MAX_PATH]{}; DWORD size = MAX_PATH;
    const bool known = QueryFullProcessImageNameW(process, 0, path, &size) && ShellSurfaceImage(path);
    CloseHandle(process);
    return known;
}
}  // namespace lume::platform::windows::shell_surface
