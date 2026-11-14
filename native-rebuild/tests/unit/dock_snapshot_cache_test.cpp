// Real HWND churn through the running-program preview and minimize snapshot
// protocol. Only fixture windows/mappings are touched; the physical pointer
// and application preferences are left alone.
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/dock_slots.h"
#include <objbase.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace lume::platform::windows;
using lume::dock::Item;
using lume::dock::Kind;
namespace {
HWND syntheticHover = nullptr;
void Check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
void Pump(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            // Fixture-only input: the unmoved physical cursor must not cancel
            // the synthetic hover that opens this test's preview.
            if (message.hwnd == syntheticHover && message.message == WM_MOUSELEAVE) continue;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(2);
    } while (GetTickCount64() < end);
}
template<class Ready> bool Until(Ready ready, DWORD timeout = 1500) {
    const auto end = GetTickCount64() + timeout;
    do { Pump(5); if (ready()) return true; } while (GetTickCount64() < end);
    return ready();
}
class FixtureWindow final {
public:
    explicit FixtureWindow(bool minimized, std::uint32_t color) {
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Dock snapshot cache fixture",
            WS_OVERLAPPEDWINDOW, 80, 80, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(hwnd_ != nullptr, "fixture HWND created");
        const auto name = dock_slots::SnapshotName(Handle());
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, dock_slots::SnapshotBytes, name.c_str());
        Check(mapping_ != nullptr && GetLastError() != ERROR_ALREADY_EXISTS, "fixture owns its snapshot mapping");
        ShowWindow(hwnd_, minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
        auto* bytes = static_cast<unsigned char*>(MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, dock_slots::SnapshotBytes));
        Check(bytes != nullptr, "fixture snapshot mapping writable");
        auto& header = *reinterpret_cast<dock_slots::SnapshotHeader*>(bytes);
        header.serial = 1;
        header.width = header.height = 16;
        auto* pixels = reinterpret_cast<std::uint32_t*>(bytes + sizeof(header));
        std::fill_n(pixels, 16 * 16, color);
        InterlockedExchange(&header.serial, 2);
        UnmapViewOfFile(bytes);
    }
    ~FixtureWindow() { Close(); if (mapping_) CloseHandle(mapping_); }
    FixtureWindow(const FixtureWindow&) = delete;
    FixtureWindow& operator=(const FixtureWindow&) = delete;
    std::uint64_t Handle() const { return reinterpret_cast<std::uint64_t>(hwnd_); }
    void Close() {
        const auto window = std::exchange(hwnd_, nullptr);
        if (IsWindow(window)) DestroyWindow(window);
    }
private:
    HWND hwnd_ = nullptr;
    HANDLE mapping_ = nullptr;
};
void Preview(DockWindow& dock, std::size_t index, unsigned& requests) {
    const auto before = requests;
    SendMessageW(dock.Handle(), WM_CANCELMODE, 0, 0);
    const RECT icon = dock.IconBounds(index);
    RECT layer{}; GetWindowRect(dock.Handle(), &layer);
    syntheticHover = dock.Handle();
    SendMessageW(dock.Handle(), WM_MOUSEMOVE, 0,
        MAKELPARAM((icon.left + icon.right) / 2 - layer.left, (icon.top + icon.bottom) / 2 - layer.top));
    Check(Until([&] { return requests > before; }), "running-program hover invokes the real preview path");
    syntheticHover = nullptr;
}
void Scenario() {
    const std::string programA = "C:\\LumeSnapshotCacheFixture\\a.exe";
    const std::string programB = "C:\\LumeSnapshotCacheFixture\\b.exe";
    FixtureWindow survivor(true, 0xff337799), removedProgram(false, 0xff995533);
    Check(IsIconic(reinterpret_cast<HWND>(survivor.Handle())) != FALSE, "surviving fixture is actually minimized");
    std::vector<lume::dock::ProgramWindow> groupA{{programA, "Minimized survivor", survivor.Handle(), true}};
    std::vector<lume::dock::ProgramWindow> groupB{{programB, "Removed program", removedProgram.Handle(), false}};
    unsigned previewRequests = 0;
    const auto icon = std::make_shared<WidgetBitmap>(WidgetBitmap{32, 32, std::vector<std::uint32_t>(32 * 32, 0xff5588aa)});
    DockWindowPorts ports;
    ports.slotsName = L"Local\\LumeDockSnapshotCacheTest-" + std::to_wstring(GetCurrentProcessId());
    ports.icon = [icon](const Item&) { return icon; };
    ports.windowsFor = [&](const Item& item) { ++previewRequests; return item.path == programA ? groupA : groupB; };
    DockWindow dock(GetModuleHandleW(nullptr), std::move(ports));
    DockAppearance look; look.transparent = true;
    const std::vector<Item> both{{Kind::Running, programA, "A", true}, {Kind::Running, programB, "B", true}};
    const std::vector<Item> onlyA{{Kind::Running, programA, "A", true}};
    Check(dock.Show(both, look), "fixture Dock shows without a material dependency");
    Pump(350);
    Preview(dock, 0, previewRequests); Preview(dock, 1, previewRequests);
    Check(dock.CachedSnapshotCount() == 2 && dock.HasCachedSnapshot(survivor.Handle()) &&
          dock.HasCachedSnapshot(removedProgram.Handle()), "both actual windows load shared snapshot pixels through previews");
    Check(dock.Show(onlyA, look), "program removal refreshes the Dock");
    Check(IsWindow(reinterpret_cast<HWND>(removedProgram.Handle())) && !dock.HasCachedSnapshot(removedProgram.Handle()) &&
          dock.CachedSnapshotCount() == 1, "a still-live window loses its cache when its program leaves the Dock");

    for (unsigned batch = 0; batch < 6; ++batch) {
        std::vector<std::unique_ptr<FixtureWindow>> temporary;
        for (unsigned n = 0; n < 3; ++n) {
            auto window = std::make_unique<FixtureWindow>(false, 0xff446688 + batch * 16 + n);
            groupA.push_back({programA, "Temporary document", window->Handle(), false});
            temporary.push_back(std::move(window));
        }
        Preview(dock, 0, previewRequests);
        Check(dock.CachedSnapshotCount() == 4 && dock.HasCachedSnapshot(survivor.Handle()),
              "current program windows each retain their own snapshot");
        for (const auto& window : temporary) window->Close();
        groupA.resize(1);
        // The mappings intentionally stay alive here. Reclamation must follow
        // the actual HWND lifetime, not depend on a failed mapping lookup.
        if (batch % 2 == 0) Check(dock.Show(onlyA, look), "same-program item refresh prunes closed windows");
        else Preview(dock, 0, previewRequests);
        Check(dock.CachedSnapshotCount() == 1 && dock.HasCachedSnapshot(survivor.Handle()),
              "closed HWND history returns to one without evicting the live minimized snapshot");
    }
    Item minimized{Kind::Window, programA, "Minimized survivor", true}; minimized.window = survivor.Handle();
    Check(dock.Show({minimized}, look), "minimized window can retain its snapshot across a Dock representation change");
    Check(dock.CachedSnapshotCount() == 1 && dock.HasCachedSnapshot(survivor.Handle()) && dock.HasThumbnail(0),
          "the surviving minimized window still has usable thumbnail pixels");
    dock.Stop();
    Check(dock.CachedSnapshotCount() == 0, "stopping the Dock releases every snapshot cache entry");
}
} // namespace
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    try {
        Scenario(); CoUninitialize();
        std::cout << "PASS Dock snapshot HWND churn, removed programs and minimized-image retention\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; CoUninitialize(); return 1;
    }
}
