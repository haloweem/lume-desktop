#pragma once
#include <windows.h>
#include <string>

namespace lume::platform::windows {
inline constexpr wchar_t WindowsSettingsAppId[] =
    L"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel";

inline const std::wstring& WindowsSettingsImage() {
    static const auto path = [] {
        wchar_t directory[MAX_PATH]{};
        const auto count = GetWindowsDirectoryW(directory, ARRAYSIZE(directory));
        return count && count < ARRAYSIZE(directory) ?
            std::wstring(directory) + L"\\ImmersiveControlPanel\\SystemSettings.exe" : std::wstring{};
    }();
    return path;
}
}
