#pragma once
#include <cwchar>
#include <initializer_list>
#include <string_view>

namespace lume::platform::windows {
inline constexpr const wchar_t* ThisPcLocation = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
inline constexpr const wchar_t* RecycleBinLocation = L"::{645FF040-5081-101B-9F08-00AA002F954E}";
inline constexpr const wchar_t* ControlPanelLocation = L"::{26EE0668-A00A-44D7-9371-BEB064C98683}";
inline constexpr const wchar_t* NetworkLocation = L"::{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}";

inline const wchar_t* DockShellLocation(std::wstring_view path) {
    for (const auto* location : {ThisPcLocation, RecycleBinLocation, ControlPanelLocation, NetworkLocation}) {
        const std::wstring_view key(location);
        if (path.size() == key.size() && _wcsnicmp(path.data(), key.data(), key.size()) == 0) return location;
    }
    constexpr std::wstring_view classicControlPanel = L"::{21EC2020-3AEA-1069-A2DD-08002B30309D}";
    const std::wstring_view panel(ControlPanelLocation);
    if (path.size() == classicControlPanel.size() && _wcsnicmp(path.data(), classicControlPanel.data(), path.size()) == 0)
        return ControlPanelLocation;
    if (path.size() > panel.size() && path[panel.size()] == L'\\' &&
        _wcsnicmp(path.data(), panel.data(), panel.size()) == 0) return ControlPanelLocation;
    return nullptr;
}

inline bool IsDockShellTarget(std::wstring_view path) {
    const auto* location = DockShellLocation(path);
    return location && path == location;
}
}
