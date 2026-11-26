#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/organizer_text.h"
#include "platform/windows/shell_location.h"
#include <algorithm>
#include <cstdio>
#include <objbase.h>

using namespace lume::platform::windows;

namespace {
bool CheckSettingsMinimizeRestore(const std::string& path) {
    const auto windows = dock_sources::ProgramWindowsFor(path);
    if (windows.empty()) return false;
    const auto window = windows.front();
    const HWND handle = reinterpret_cast<HWND>(window.handle);
    struct Restore {
        HWND handle;
        bool minimized;
        ~Restore() {
            if (IsWindow(handle) && minimized != (IsIconic(handle) != FALSE))
                ShowWindowAsync(handle, minimized ? SW_MINIMIZE : SW_RESTORE);
        }
    } restore{handle, window.minimized};
    ShowWindowAsync(handle, SW_MINIMIZE);
    Sleep(1500);
    const auto running = dock_sources::RunningApps();
    const auto minimized = dock_sources::ProgramWindowsFor(path);
    if (std::count_if(running.begin(), running.end(), [&](const auto& app) { return app.executable == path; }) != 1 ||
        std::none_of(minimized.begin(), minimized.end(), [&](const auto& item) {
            return item.handle == window.handle && item.minimized;
        })) return false;
    if (!dock_sources::ActivateProgram(path)) return false;
    const auto restored = dock_sources::ProgramWindowsFor(path);
    return std::any_of(restored.begin(), restored.end(), [&](const auto& item) {
        return item.handle == window.handle && !item.minimized;
    });
}
}

int wmain(int argc, wchar_t** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    const auto running = dock_sources::RunningApps();
    std::vector<std::wstring> locations;
    OrganizerIconCache icons;
    const auto generic = std::count_if(running.begin(), running.end(), [](const auto& app) {
        const auto path = dock_sources::PathKey(app.executable);
        constexpr std::string_view suffix = "\\explorer.exe";
        return path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
    });
    std::printf("generic Explorer entries: %zu\n", static_cast<std::size_t>(generic));
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view location = argv[index];
        if (location == L"--settings" || location == L"--settings-cycle") {
            const auto settings = std::find_if(running.begin(), running.end(), [](const auto& app) {
                return dock_sources::IsWindowsSettings(app.executable);
            });
            if (settings == running.end() || settings->name.empty() ||
                dock_sources::ProgramWindowsFor(settings->executable).empty()) return 7;
            locations.push_back(OrganizerWide(settings->executable));
            std::printf("Windows Settings: running entry and window found\n");
            if (location == L"--settings-cycle") {
                if (!CheckSettingsMinimizeRestore(settings->executable)) return 8;
                std::puts("Windows Settings: one minimized running item, same window restored via Dock");
            }
            continue;
        }
        if (location == L"explorer") {
            if (generic != 1) return 2;
            continue;
        }
        if (location == L"--shortcut") {
            if (index + 2 >= argc) return 3;
            const auto shortcut = OrganizerUtf8(argv[++index]);
            const auto target = OrganizerUtf8(argv[++index]);
            const bool matches = dock_sources::ShortcutTarget(shortcut) == target;
            std::printf("pinned shortcut for %s: %s\n", target.c_str(), matches ? "ok" : "wrong target");
            if (!matches) return 5;
            continue;
        }
        if (!IsDockShellTarget(location)) return 3;
        const auto target = OrganizerUtf8(location);
        const auto count = std::count_if(running.begin(), running.end(), [&](const auto& app) {
            return app.executable == target && !app.name.empty();
        });
        const auto windows = dock_sources::ProgramWindowsFor(target);
        std::printf("%s: entries=%zu windows=%zu\n", target.c_str(), static_cast<std::size_t>(count), windows.size());
        if (count != 1 || windows.empty()) return 4;
        locations.emplace_back(location);
    }
    if (!icons.SetPaths(locations, {}, 96)) return 6;
    const auto deadline = GetTickCount64() + 8000;
    while (GetTickCount64() < deadline && icons.CachedPaths() < locations.size()) Sleep(20);
    for (const auto& path : locations) {
        const auto bitmap = icons.Find(path);
        if (!bitmap) return 6;
        if (dock_sources::IsWindowsSettings(OrganizerUtf8(path))) {
            std::printf("Settings icon: %ux%u, corner ARGB %08x, center ARGB %08x\n",
                bitmap->width, bitmap->height, bitmap->pixels.front(),
                bitmap->pixels[(bitmap->height / 2) * bitmap->width + bitmap->width / 2]);
        }
    }
    std::printf("icons loaded through Dock cache: %zu/%zu\n", locations.size(), locations.size());
    return 0;
}
