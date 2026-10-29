// Dock and launchpad windows with stand-in data: layout, hit testing, desktop
// level, magnification, drag reordering, paging, search filtering, open/close
// animation and ports. No program is started.
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/launchpad_window.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/dock/dock_slots.h"
#include "platform/windows/organizer/organizer_drop_target.h"
#include <algorithm>
#include <objbase.h>
#include <windowsx.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <tuple>

using namespace lume::platform::windows;
using lume::dock::Item;
using lume::dock::Kind;
namespace {
int failures = 0;
void Check(bool ok, const char* text) { if (!ok) { std::cerr << "FAIL: " << text << '\n'; ++failures; } }
void Pump(DWORD milliseconds = 0) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG m{};
        // Synthetic pointer input: drop the leave notice the real cursor triggers.
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_MOUSELEAVE) continue;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        if (milliseconds) Sleep(5);
    } while (GetTickCount64() < end);
}
float Scale(HWND window) { return static_cast<float>(GetDpiForWindow(window)) / 96; }
LPARAM At(HWND window, float x, float y) {
    const float scale = Scale(window);
    return MAKELPARAM(static_cast<int>(std::lround(x * scale)), static_cast<int>(std::lround(y * scale)));
}
// A point in the Dock's icon layer, from coordinates relative to the bar at rest.
// While hovering, the point under the pointer stays on the same icon.
RECT restBar{};
LPARAM DockAt(const DockWindow& dock, float x, float y) {
    RECT layer{};
    GetWindowRect(dock.Handle(), &layer);
    const RECT& bar = restBar;
    const float scale = Scale(dock.Handle());
    return MAKELPARAM(static_cast<int>(std::lround(bar.left - layer.left + x * scale)),
                      static_cast<int>(std::lround(bar.top - layer.top + y * scale)));
}
LPARAM IconAt(const DockWindow& dock, std::size_t index, int dx = 0) {
    RECT icon = dock.IconBounds(index), layer{};
    GetWindowRect(dock.Handle(), &layer);
    return MAKELPARAM((icon.left + icon.right) / 2 - layer.left + dx, (icon.top + icon.bottom) / 2 - layer.top);
}
// Drags items[index] horizontally by `pixels` in small steps.
void DragBy(const DockWindow& dock, std::size_t index, int pixels) {
    SendMessageW(dock.Handle(), WM_MOUSEMOVE, 0, IconAt(dock, index));
    Pump(400);
    const LPARAM start = IconAt(dock, index);
    SendMessageW(dock.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, start);
    const int step = pixels > 0 ? 8 : -8;
    for (int moved = 0; std::abs(moved) < std::abs(pixels); moved += step) {
        SendMessageW(dock.Handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(GET_X_LPARAM(start) + moved, GET_Y_LPARAM(start)));
        Pump(16);
    }
    SendMessageW(dock.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(GET_X_LPARAM(start) + pixels, GET_Y_LPARAM(start)));
}
LPARAM BeginDragBy(const DockWindow& dock, std::size_t index, int pixels) {
    SendMessageW(dock.Handle(), WM_MOUSEMOVE, 0, IconAt(dock, index));
    Pump(400);
    const LPARAM start = IconAt(dock, index);
    SendMessageW(dock.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, start);
    const int step = pixels > 0 ? 8 : -8;
    for (int moved = 0; std::abs(moved) < std::abs(pixels); moved += step) {
        SendMessageW(dock.Handle(), WM_MOUSEMOVE, MK_LBUTTON,
                     MAKELPARAM(GET_X_LPARAM(start) + moved, GET_Y_LPARAM(start)));
        Pump(16);
    }
    SendMessageW(dock.Handle(), WM_MOUSEMOVE, MK_LBUTTON,
                 MAKELPARAM(GET_X_LPARAM(start) + pixels, GET_Y_LPARAM(start)));
    Pump(120);
    return MAKELPARAM(GET_X_LPARAM(start) + pixels, GET_Y_LPARAM(start));
}
}  // namespace
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {   // The Recycle Bin shortcut accepts dropped files; other shortcuts do not.
        using namespace lume::platform::windows;
        const auto bin=ShellLocationShortcut(L"::{645FF040-5081-101B-9F08-00AA002F954E}");
        const auto pc=ShellLocationShortcut(L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
        Check(bin&&pc&&dock_sources::IsRecycleBin(*bin)&&!dock_sources::IsRecycleBin(*pc)&&!dock_sources::IsRecycleBin("C:\\Windows\\notepad.exe"),"recycle bin shortcut recognised");
        Check(!dock_sources::Recycle(nullptr,{*bin}),"the recycle bin never recycles itself");
    }
    const std::vector<Item> items{{Kind::Launchpad, {}, "Launchpad", false}, {Kind::Lume, {}, "Lume", true, 0},
                                  {Kind::Pinned, "C:\\Apps\\a.exe", "A", true, 0}, {Kind::Pinned, "C:\\Apps\\b.exe", "B", false, 1},
                                  {Kind::Pinned, "C:\\Apps\\c.exe", "C", false, 2}, {Kind::Separator, {}, {}, false},
                                  {Kind::Running, "C:\\Apps\\d.exe", "D", true}};
    // Pure layout.
    Check(std::abs(DockWindow::Width(items) - (2 * DockWindow::Padding + 6 * DockWindow::IconSize + 1 + 6 * DockWindow::Gap)) < .01f,
          "width covers icons, separator and gaps");
    const auto third = DockWindow::Slot(items, 2);
    Check(DockWindow::Hit(items, third.x + 5, 30) == 2, "icon hit");
    Check(DockWindow::Hit(items, DockWindow::Slot(items, 5).x, 30) != 5, "separator is not clickable");
    Check(DockWindow::Hit(items, 2, 30) == DockWindow::None, "padding is background");
    Check(std::abs(DockWindow::Magnification(0) - DockWindow::MaxScale) < .001f &&
          DockWindow::Magnification(DockWindow::IconSize + DockWindow::Gap) > 1.25f &&
          DockWindow::Magnification(2 * (DockWindow::IconSize + DockWindow::Gap)) > 1.05f &&
          DockWindow::Magnification(3 * (DockWindow::IconSize + DockWindow::Gap)) == 1.f,
          "magnification reaches two neighbours on each side and none beyond");
    const auto lastPinned = DockWindow::Slot(items, 4);
    Check(DockWindow::DropSlot(items, third.x) == 0 && DockWindow::DropSlot(items, lastPinned.x + lastPinned.width) == 3 &&
          DockWindow::DropSlot(items, lastPinned.x + lastPinned.width, 2) == 2 && DockWindow::DropSlot(items, 0) == 0,
          "drop slot counts the other pinned items left of the point");

    std::size_t activated = DockWindow::None, context = 99;
    std::tuple<std::size_t, std::size_t, std::size_t> moved{99, 99, 99};
    DockWindow dock(GetModuleHandleW(nullptr), {{}, [&](std::size_t i) { activated = i; },
                                                [&](std::size_t i, POINT) { context = i; },
                                                [&](std::size_t index, std::size_t pinnedSlot, std::size_t lumePosition) {
                                                    moved = {index, pinnedSlot, lumePosition};
                                                }, {}});
    Check(dock.Show(items, false), "dock shows");
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT bounds = dock.ScreenBounds();
    restBar = bounds;
    const int margin = MulDiv(static_cast<int>(DockWindow::BottomMargin), static_cast<int>(GetDpiForWindow(dock.Handle())), 96);
    Check(std::abs(bounds.bottom - (monitor.rcWork.bottom - margin)) <= 1, "bar sits just above the work area bottom");
    Check(std::abs((bounds.left + bounds.right) / 2 - (monitor.rcWork.left + monitor.rcWork.right) / 2) <= 1, "bar is centred");
    Check(!(GetWindowLongPtrW(dock.Handle(), GWL_EXSTYLE) & WS_EX_TOPMOST), "dock is not topmost");
    Check(desktop_level::IsDesktopWidget(dock.Handle()) && desktop_level::IsDesktopWidget(dock.BarHandle()),
          "both dock layers join the desktop widget layer");
    // This test runs on the live desktop, where a maximized window hides the Dock.
    const auto pairOrdered = [&] {
        if (!IsWindowVisible(dock.Handle())) return !IsWindowVisible(dock.BarHandle());
        HWND below = GetWindow(dock.Handle(), GW_HWNDNEXT);
        while (below && !IsWindowVisible(below)) below = GetWindow(below, GW_HWNDNEXT);
        return below == dock.BarHandle();
    };
    Check(pairOrdered(), "icon layer sits directly above the glass bar");
    // Explorer can temporarily hide a layered window during a desktop-host
    // restart. A desktop-level restack must recover both Dock layers without
    // requiring DockSession or the process to be restarted.
    const bool covered = !IsWindowVisible(dock.Handle());  // by a maximized window
    ShowWindow(dock.Handle(), SW_HIDE);
    ShowWindow(dock.BarHandle(), SW_HIDE);
    Check(covered || !dock.LayersVisible(), "hidden native Dock layers are detectable");
    SendMessageW(dock.Handle(), desktop_level::Message(), 0, 0);
    Check(dock.LayersVisible(), "desktop restack re-shows hidden Dock layers");
    Check(pairOrdered(), "restack keeps the glass immediately below icons");
    WINDOWPOS implicit{};
    implicit.hwnd = dock.BarHandle();
    SendMessageW(dock.BarHandle(), WM_WINDOWPOSCHANGING, 0, reinterpret_cast<LPARAM>(&implicit));
    Check((implicit.flags & SWP_NOZORDER) != 0, "glass bar rejects implicit z-order changes");
    // Hover magnifies the icon under the pointer more than its neighbours.
    SendMessageW(dock.Handle(), WM_MOUSEMOVE, 0, DockAt(dock, third.x + third.width / 2, 40));
    Pump(500);
    Check(dock.ScaleOf(2) > 1.3f && dock.ScaleOf(2) > dock.ScaleOf(3) && dock.ScaleOf(3) > dock.ScaleOf(5) - .01f,
          "hover magnifies the pointed icon most");
    RECT grown{};
    GetWindowRect(dock.BarHandle(), &grown);
    Check(grown.right - grown.left > bounds.right - bounds.left, "the bar widens with magnified icons");
    // Far icons keep their size: only two neighbours on each side grow.
    Check(std::abs(dock.ScaleOf(6) - 1) < .01f, "the third icon away does not magnify");
    // A click without movement activates.
    SendMessageW(dock.Handle(), WM_MOUSEMOVE, 0, IconAt(dock, 2));
    Pump(400);
    SendMessageW(dock.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, IconAt(dock, 2));
    SendMessageW(dock.Handle(), WM_LBUTTONUP, 0, IconAt(dock, 2));
    Check(activated == 2, "left click activates the item under the pointer");
    // Rest centres (screen pixels) for computing drag distances.
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0);
    Pump(800);
    std::vector<int> centre;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const RECT r = dock.IconBounds(i);
        centre.push_back((r.left + r.right) / 2);
    }
    const int pitch = centre[3] - centre[2];
    const auto drag = [&](std::size_t index, int pixels) {
        moved = {99, 99, 99};
        DragBy(dock, index, pixels);
        SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0);
        Pump(700);
        return moved;
    };
    // Pinned icons swap in both directions.
    Check(drag(2, pitch * 2 + pitch / 4) == std::tuple<std::size_t, std::size_t, std::size_t>{2, 2, 0},
          "first pinned icon moves right past two others");
    const auto movedLeftAcrossLume = drag(4, -(pitch * 3 + pitch / 4));
    Check(movedLeftAcrossLume == std::tuple<std::size_t, std::size_t, std::size_t>{4, 0, 1},
          "last pinned icon moves left across Lume");
    Check(drag(3, pitch + pitch / 4) == std::tuple<std::size_t, std::size_t, std::size_t>{3, 2, 0},
          "middle pinned icon moves one place right");
    const auto movedMiddleLeft = drag(3, -(pitch * 2 + pitch / 4));
    Check(movedMiddleLeft == std::tuple<std::size_t, std::size_t, std::size_t>{3, 0, 1},
          "middle pinned icon moves one place left across Lume");
    // A running (unpinned) program dragged between A and B pins it there.
    Check(drag(6, (centre[2] + centre[3]) / 2 - centre[6]) ==
              std::tuple<std::size_t, std::size_t, std::size_t>{6, 1, 0},
          "dragging a running program into the pinned area pins it at that slot");
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0);
    Pump(800);
    Check(std::abs(dock.ScaleOf(2) - 1) < .01f, "leaving the dock returns icons to rest");
    // A launchpad item dragged over the Dock opens a gap; leaving closes it.
    RECT barNow{};
    GetWindowRect(dock.BarHandle(), &barNow);
    const POINT over{(barNow.left + barNow.right) / 2, (barNow.top + barNow.bottom) / 2};
    Check(dock.DragHover(over) && dock.DragSlot() != DockWindow::None, "a drag over the Dock opens a slot");
    Pump(500);
    Check(dock.GapOpen() > .9f, "the gap springs open");
    Check(!dock.DragHover({over.x, barNow.top - 400}) && dock.DragSlot() == DockWindow::None, "leaving the Dock closes the slot");
    Pump(500);
    Check(dock.GapOpen() < .05f, "the gap springs closed");
    SendMessageW(dock.Handle(), WM_RBUTTONUP, 0, DockAt(dock, 3, 40));
    Check(context == DockWindow::None, "right click on padding opens the background menu");
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0);Pump(600);
    DockAppearance appearance;
    appearance.transparent = true;
    appearance.position = 0; appearance.bottomOffset = 80; appearance.iconSpacing = 24;
    Check(dock.Show(items, appearance), "custom position and spacing apply");Pump(800);
    const RECT custom = dock.ScreenBounds(), customIcon = dock.IconBounds(2), nextCustomIcon = dock.IconBounds(3);
    const int effectiveDpi = static_cast<int>(GetDpiForWindow(dock.Handle()));
    Check(custom.left < bounds.left && std::abs(custom.bottom - (bounds.bottom - MulDiv(80, effectiveDpi, 96))) <= 2,
          "the Dock moves up and stays centred");
    Check(std::abs((nextCustomIcon.left - customIcon.left) - MulDiv(82, effectiveDpi, 96)) <= 2,
          "actual resting icon pitch follows the selected spacing");
    SendMessageW(dock.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, IconAt(dock, 2));
    SendMessageW(dock.Handle(), WM_LBUTTONUP, 0, IconAt(dock, 2));
    Check(activated == 2, "custom-position icon remains clickable");
    Check(drag(2, MulDiv(190, effectiveDpi, 96)) == std::tuple<std::size_t, std::size_t, std::size_t>{2, 2, 0},
          "drag reordering uses the custom icon spacing");
    appearance.position = 100;
    Check(dock.Show(items, appearance), "an old right position still shows");Pump(800);
    const RECT right = dock.ScreenBounds();
    Check(std::abs(right.left - custom.left) <= 2 && right.right <= monitor.rcWork.right && custom.left >= monitor.rcWork.left,
          "an old horizontal position is ignored: the Dock stays centred");
    appearance = {};
    Check(dock.Show(items, appearance), "restore default Dock appearance");Pump(800);
    const RECT restored = dock.ScreenBounds();
    Check(std::abs(restored.left - bounds.left) <= 2 && std::abs(restored.bottom - bounds.bottom) <= 2,
          "default layout restores centre and original height");
    // Lume may be hidden or moved between pinned entries; its drag and
    // context hit must remain independent of the visible array index.
    const std::vector<Item> movedItems{{Kind::Launchpad, {}, "Launchpad", false},
                                       {Kind::Pinned, "C:\\Apps\\a.exe", "A", false, 0},
                                       {Kind::Lume, {}, "Lume", true, 1},
                                       {Kind::Pinned, "C:\\Apps\\b.exe", "B", false, 1}};
    Check(dock.Show(movedItems, appearance), "dock shows Lume in a non-fixed slot");Pump(800);
    SendMessageW(dock.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, IconAt(dock, 1));
    SendMessageW(dock.Handle(), WM_LBUTTONUP, 0, IconAt(dock, 1));
    Check(activated == 1, "non-fixed pinned item remains clickable");
    context = 99;
    SendMessageW(dock.Handle(), WM_RBUTTONUP, 0, IconAt(dock, 2));
    Check(context == 2, "moved Lume opens its item context menu");
    moved = {99, 99, 99};
    const int movedPitch = dock.IconBounds(3).left - dock.IconBounds(2).left;
    const auto aBeforeLumeDrag = dock.IconBounds(1);
    const LPARAM lumeDragEnd = BeginDragBy(dock, 2, -std::max(20, movedPitch * 2));
    const auto aDuringLumeDrag = dock.IconBounds(1);
    Check(aDuringLumeDrag.left > aBeforeLumeDrag.left + 8,
          "Lume drag makes the neighboring icon yield space");
    SendMessageW(dock.Handle(), WM_LBUTTONUP, 0, lumeDragEnd);
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0);Pump(700);
    Check(moved == std::tuple<std::size_t, std::size_t, std::size_t>{2, DockWindow::None, 0},
          "Lume drag reports its primary insertion slot");
    // A pinned icon crossing Lume must report the same side that the
    // animated preview uses. In particular, dropping past the last pinned
    // icon must not leave Lume in front of that icon after commit.
    const std::vector<Item> crossItems{{Kind::Launchpad, {}, "Launchpad", false},
                                       {Kind::Pinned, "C:\\Apps\\a.exe", "A", false, 0},
                                       {Kind::Lume, {}, "Lume", true, 1},
                                       {Kind::Pinned, "C:\\Apps\\b.exe", "B", false, 1}};
    const auto centreOf = [&](std::size_t index) {
        const auto r = dock.IconBounds(index);
        return (r.left + r.right) / 2;
    };
    Check(dock.Show(crossItems, appearance), "dock resets cross-Lume drag fixture");Pump(800);
    const int crossPitch = dock.IconBounds(3).left - dock.IconBounds(2).left;
    moved = {99, 99, 99};
    const int betweenLumeAndB = (centreOf(2) + centreOf(3)) / 2;
    DragBy(dock, 1, betweenLumeAndB - centreOf(1));
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0);Pump(700);
    Check(moved == std::tuple<std::size_t, std::size_t, std::size_t>{1, 0, 0},
          "pinned drag between Lume and B preserves the primary order");
    Check(dock.Show(crossItems, appearance), "dock resets cross-Lume drag fixture for trailing drop");Pump(800);
    moved = {99, 99, 99};
    const int afterB = centreOf(3) + std::max(20, crossPitch / 2);
    DragBy(dock, 1, afterB - centreOf(1));
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0);Pump(700);
    Check(moved == std::tuple<std::size_t, std::size_t, std::size_t>{1, 1, 0},
          "pinned drag past B keeps the primary preview order");
    dock.Stop();
    Check(!dock.Handle() && !dock.BarHandle(), "stop destroys both windows");
    {   // The Recycle Bin: files dropped on it go to dropOnto; a Dock icon dragged onto it is discarded.
        const std::string binPath = "C:/Lume/bin.lnk";
        const std::vector<Item> binItems{{Kind::Launchpad, {}, "Launchpad", false}, {Kind::Lume, {}, "Lume", true, 0},
                                         {Kind::Pinned, "C:/Apps/a.exe", "A", false, 0}, {Kind::Pinned, binPath, "Bin", false, 1}};
        std::vector<std::string> recycled;std::string discarded;std::size_t placed=99;
        DockWindowPorts binPorts;
        binPorts.place=[&](std::size_t i,std::size_t,std::size_t){placed=i;};
        binPorts.drop=[](const std::vector<std::string>&,std::size_t){return true;};
        binPorts.acceptsDrop=[&](const Item& item){return item.path==binPath;};
        binPorts.dropOnto=[&](const std::vector<std::string>& paths,const Item&){recycled=paths;return true;};
        binPorts.discard=[&](const Item& item){discarded=item.path;};
        DockWindow bin(GetModuleHandleW(nullptr), binPorts);
        Check(bin.Show(binItems, false), "recycle bin dock shows");Pump(600);
        RECT icon = bin.IconBounds(3);
        const POINT binCentre{(icon.left + icon.right) / 2, (icon.top + icon.bottom) / 2};
        Check(bin.DragHover(binCentre) && bin.DragSlot() == DockWindow::None && bin.CollectionTarget() == binPath,
              "a file over the bin highlights it instead of opening a gap");
        Check(bin.DropPathsAt({"C:/tmp/x.txt"}, binCentre) && recycled.size() == 1, "files dropped on the bin go to the bin");
        Pump(400);
        const RECT a = bin.IconBounds(2);
        DragBy(bin, 2, binCentre.x - (a.left + a.right) / 2);Pump(400);
        Check(discarded == "C:/Apps/a.exe" && placed == 99, "a Dock icon dragged onto the bin leaves the Dock");
        bin.Stop();
    }
    {   // Dragging an icon up out of the Dock shows 移除 and removes it on release;
        // dragging it back in before release keeps it. Running icons never show 移除.
        const std::vector<Item> outItems{{Kind::Launchpad, {}, "Launchpad", false}, {Kind::Pinned, "C:/Apps/a.exe", "A", false, 0},
                                         {Kind::Pinned, "C:/Apps/b.exe", "B", false, 1}, {Kind::Separator, {}, {}, false},
                                         {Kind::Running, "C:/Apps/c.exe", "C", true}};
        std::string discarded;std::size_t placed=99;
        DockWindowPorts outPorts;
        outPorts.place=[&](std::size_t i,std::size_t,std::size_t){placed=i;};
        outPorts.discard=[&](const Item& item){discarded=item.path;};
        DockWindow out(GetModuleHandleW(nullptr), outPorts);
        Check(out.Show(outItems, false), "drag-out dock shows");Pump(600);
        const auto dragUp = [&](std::size_t index, int up, bool release) {
            SendMessageW(out.Handle(), WM_MOUSEMOVE, 0, IconAt(out, index));Pump(300);
            const LPARAM start = IconAt(out, index);
            SendMessageW(out.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, start);
            for (int moved = 0; moved <= up; moved += 10) {
                SendMessageW(out.Handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(GET_X_LPARAM(start), GET_Y_LPARAM(start) - moved));
                Pump(16);
            }
            Pump(300);
            const bool removing = out.Removing();
            if (release) SendMessageW(out.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(GET_X_LPARAM(start), GET_Y_LPARAM(start) - up));
            return removing;
        };
        const int away = static_cast<int>(160 * Scale(out.Handle()));
        Check(dragUp(1, away, false), "a pinned icon dragged above the Dock is marked for removal");
        HWND ghost = FindWindowW(L"LumeNativeDockDragIcon", nullptr);
        Check(ghost && IsWindowVisible(ghost), "the dragged icon follows the pointer outside the Dock");
        RECT ghostRect{};GetWindowRect(ghost, &ghostRect);RECT layer{};GetWindowRect(out.Handle(), &layer);
        Check(ghostRect.top < layer.top, "the dragged icon is drawn above the Dock's own window");
        {   // Back into the row before letting go: nothing is removed.
            const LPARAM back = IconAt(out, 2);
            SendMessageW(out.Handle(), WM_MOUSEMOVE, MK_LBUTTON, back);Pump(300);
            Check(!out.Removing(), "moving back over the Dock cancels the removal");
            SendMessageW(out.Handle(), WM_LBUTTONUP, 0, back);Pump(400);
            Check(discarded.empty(), "releasing inside the Dock does not remove");
        }
        Check(!IsWindowVisible(ghost), "the drag icon hides on release");
        dragUp(1, away, true);Pump(400);
        Check(discarded == "C:/Apps/a.exe", "releasing above the Dock removes the icon");
        discarded.clear();
        Check(!dragUp(4, away, true) && discarded.empty(), "running icons are not removed by dragging out");
        Pump(400);
        out.Stop();
    }
    {   // A minimized window sits at the end of the Dock as a live thumbnail.
        WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"LumeDockThumbnailFixture";wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        RegisterClassW(&wc);
        HWND source = CreateWindowExW(0, wc.lpszClassName, L"Fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 640, 400,
                                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Pump(300);ShowWindow(source, SW_MINIMIZE);
        // A running minimize animation (a real Lume on this desktop) defers the minimize.
        for (int i = 0; i < 60 && !IsIconic(source); ++i) Pump(50);
        Pump(300);
        std::vector<Item> withWindow{{Kind::Launchpad, {}, "Launchpad", false}, {Kind::Pinned, "C:/Apps/a.exe", "A", false, 0}};
        lume::dock::AppendWindows(withWindow, {{"C:/Apps/a.exe", "Fixture", reinterpret_cast<std::uint64_t>(source)}});
        Check(withWindow.size() == 4 && withWindow[2].kind == Kind::Separator && withWindow[3].kind == Kind::Window,
              "minimized windows follow a separator");
        DockWindowPorts thumbPorts;
        thumbPorts.slotsName = L"Local\\Lume.Dock.Slots.test." + std::to_wstring(GetCurrentProcessId());
        DockWindow thumbs(GetModuleHandleW(nullptr), thumbPorts);
        DockAppearance thumbLook;thumbLook.frosted = true;
        if (std::getenv("LUME_DOCK_HOLD_MS")) thumbLook.bottomOffset = 160;  // above another dock, for screenshots
        Check(thumbs.Show(withWindow, thumbLook), "dock with a window shows");Pump(700);
        Check(thumbs.HasThumbnail(3), "the minimized window is drawn as a live DWM thumbnail");
        {   // The minimize animation finds the thumbnail's place, and the program icon's.
            RECT slot{}, icon = thumbs.IconBounds(3);
            Check(lume::platform::windows::dock_slots::Find(source, 0, slot, thumbPorts.slotsName.c_str()) && slot.left >= icon.left - 2 &&
                      slot.right <= icon.right + 2 && slot.bottom <= icon.bottom + 2,
                  "the Dock publishes where a minimized window sits");
            RECT program{}, pinnedIcon = thumbs.IconBounds(1);
            Check(lume::platform::windows::dock_slots::Find(nullptr, lume::platform::windows::dock_slots::ProgramKey(L"C:\\Apps\\A.EXE"), program, thumbPorts.slotsName.c_str()) &&
                      std::abs(program.left - pinnedIcon.left) <= 2, "and where each program's icon sits");
        }
        // Optional visual check: keep it on screen for a screenshot.
        if (const char* hold = std::getenv("LUME_DOCK_HOLD_MS")) Pump(static_cast<DWORD>(std::atoi(hold)));
        {   // Resting the pointer on it shows a larger live preview above the Dock.
            SendMessageW(thumbs.Handle(), WM_MOUSEMOVE, 0, IconAt(thumbs, 3));Pump(700);
            HWND preview = FindWindowW(L"LumeNativeDockPreview", nullptr);
            RECT previewRect{}, barRect{};
            if (preview) GetWindowRect(preview, &previewRect);
            GetWindowRect(thumbs.BarHandle(), &barRect);
            Check(preview && IsWindowVisible(preview) && previewRect.bottom <= barRect.top, "hovering a minimized window previews it above the Dock");
            SendMessageW(thumbs.Handle(), WM_MOUSEMOVE, 0, IconAt(thumbs, 1));Pump(300);
            Check(!preview || !IsWindowVisible(preview), "the preview hides when the pointer moves on");
            // Passing over it quickly and leaving the Dock never leaves a preview behind.
            SendMessageW(thumbs.Handle(), WM_MOUSEMOVE, 0, IconAt(thumbs, 3));Pump(60);
            SendMessageW(thumbs.Handle(), WM_MOUSELEAVE, 0, 0);Pump(700);
            preview = FindWindowW(L"LumeNativeDockPreview", nullptr);
            Check(!preview || !IsWindowVisible(preview), "a preview never appears after the pointer has left");
        }
        withWindow.pop_back();withWindow.pop_back();
        Check(thumbs.Show(withWindow, false), "restored window leaves");Pump(300);
        Check(!thumbs.HasThumbnail(3), "the thumbnail goes with its window");
        {   RECT kept{};
            Check(lume::platform::windows::dock_slots::Find(source, 0, kept, thumbPorts.slotsName.c_str()), "a restored window keeps its place briefly for the restore animation");
            Pump(1700);thumbs.Paint();
            Check(!lume::platform::windows::dock_slots::Find(source, 0, kept, thumbPorts.slotsName.c_str()), "and then forgets it");
        }
        thumbs.Stop();
        DestroyWindow(source);
    }

    // Launchpad layout and behaviour.
    const auto layout = LaunchpadWindow::LayoutFor(1100, 620);
    Check(layout.search.x + layout.search.width < layout.add.x &&
          std::abs((layout.add.y + layout.add.height / 2) - (layout.search.y + layout.search.height / 2)) < .01f &&
          layout.add.width == layout.add.height, "a round add button sits right of the search field");
    const auto first = LaunchpadWindow::Cell(layout, 0), last = LaunchpadWindow::Cell(layout, 20);
    Check(first.y >= layout.grid.y && last.y + last.height <= layout.grid.y + layout.grid.height + .01f, "3 rows fit the grid");
    Check(LaunchpadWindow::Hit(layout, 21, 0, 2, layout.add.x + 4, layout.add.y + 20).kind == LaunchpadWindow::HitKind::Add, "add hit");
    Check(LaunchpadWindow::Hit(layout, 21, 0, 2, layout.previous.x + 4, layout.previous.y + 4).kind == LaunchpadWindow::HitKind::None,
          "previous disabled on the first page");
    Check(LaunchpadWindow::Hit(layout, 21, 0, 2, layout.next.x + 4, layout.next.y + 4).kind == LaunchpadWindow::HitKind::Next, "next hit");
    const auto cellHit = LaunchpadWindow::Hit(layout, 21, 0, 2, last.x + last.width / 2, last.y + last.height / 2);
    Check(cellHit.kind == LaunchpadWindow::HitKind::Item && cellHit.index == 20, "cell hit");
    Check(LaunchpadWindow::Hit(layout, 3, 0, 1, last.x + last.width / 2, last.y + last.height / 2).kind == LaunchpadWindow::HitKind::None,
          "empty cells are not items");

    std::vector<lume::dock::Entry> entries;
    for (int i = 0; i < 30; ++i) entries.push_back({"C:\\Items\\" + std::to_string(i) + ".txt", "item " + std::to_string(i), false});
    entries.push_back({"C:\\Items\\special.txt", "special", true});
    entries.push_back({"collection:c1", "项目", true, true, "c1"});
    std::string opened, renamed;
    std::vector<std::string> arranged;
    int visibility = 0, backs = 0;
    LaunchpadWindow launchpad(GetModuleHandleW(nullptr), {
        [&] { return entries; },
        [](const std::vector<lume::dock::Entry>& all, const std::string& query) {
            std::vector<lume::dock::Entry> result;
            for (const auto& e : all) if (e.name.find(query) != std::string::npos) result.push_back(e);
            return result;
        },
        {}, [&](const lume::dock::Entry& e) { opened = e.path; }, {}, {}, [&] { ++visibility; },
        [&] { ++backs; }, [&](const std::string& id, const std::string& title) { renamed = id + "|" + title; }, {}, {}, {}, {},
        [&](std::vector<std::string> order) { arranged = std::move(order); }});
    Check(launchpad.Show(RECT{}, true), "launchpad shows");
    BOOL animate = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
    const HWND preparedSearch = launchpad.SearchHandle();
    // While the glass opens the EDIT is already shown and focused but blank:
    // the panel draws its raster until the hand-off at rest.
    Check(preparedSearch && (!animate || (IsWindowVisible(preparedSearch) && GetFocus() == preparedSearch)),
          "search EDIT is prepared and focused, still blank, while the glass opens");
    RECT opening{};
    GetWindowRect(launchpad.Handle(), &opening);
    Pump(900);
    RECT open{};
    GetWindowRect(launchpad.Handle(), &open);
    Check(!animate || open.right - open.left > opening.right - opening.left, "the launchpad springs open from a smaller frame");
    const auto dpi = static_cast<int>(GetDpiForWindow(launchpad.Handle()));
    // The opening droplet scales the panel by its smaller axis instead of
    // cropping it, so the search panel sits inside the frame from the start.
    const double openW = open.right - open.left, openH = open.bottom - open.top;
    const double grow = std::min((opening.right - opening.left) / openW, (opening.bottom - opening.top) / openH);
    const double searchTop = LaunchpadWindow::LayoutFor(900, 540).search.y;
    const double searchAt = ((opening.bottom - opening.top) - openH * grow) / 2 + searchTop * dpi / 96.0 * grow;
    Check(!animate || (searchAt >= 0 && searchAt < opening.bottom - opening.top),
          "the glass crop never crosses the search panel");
    {
        // While the glass presents the panel, the window itself must still take
        // the pointer (wheel paging, clicks, drags), not pass it to the desktop.
        // A real wheel turn through the OS input path over the panel's middle.
        const POINT middle{(open.left + open.right) / 2, (open.top + open.bottom) / 2};
        POINT restore{};
        GetCursorPos(&restore);
        SetCursorPos(open.left + MulDiv(70, dpi, 96), open.bottom - MulDiv(30, dpi, 96));
        Pump(60);
        INPUT wheel{};
        wheel.type = INPUT_MOUSE;
        wheel.mi.dwFlags = MOUSEEVENTF_WHEEL;
        wheel.mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA);
        const int before = launchpad.Page();
        SendInput(1, &wheel, sizeof(wheel));
        Pump(150);
        const int after = launchpad.Page();
        // Which window the OS routes pointer messages to over the panel: a
        // thread mouse hook sees only messages for this thread's windows, so a
        // pointer passing through to the desktop leaves it empty.
        static HWND routed = nullptr;
        routed = nullptr;
        const HHOOK hook = SetWindowsHookExW(WH_MOUSE, [](int code, WPARAM w, LPARAM l) -> LRESULT {
            if (code == HC_ACTION && w == WM_MOUSEMOVE) routed = reinterpret_cast<MOUSEHOOKSTRUCT*>(l)->hwnd;
            return CallNextHookEx(nullptr, code, w, l);
        }, nullptr, GetCurrentThreadId());
        for (int step = 0; step < 4; ++step) {
            // Blank glass beside the page dots: no icon or text drawn there.
            SetCursorPos(open.left + MulDiv(70, dpi, 96) + step * 7, open.bottom - MulDiv(30, dpi, 96) - step * 3);
            INPUT move{};
            move.type = INPUT_MOUSE;
            move.mi.dwFlags = MOUSEEVENTF_MOVE;
            move.mi.dx = 1;
            SendInput(1, &move, sizeof(move));
            Pump(40);
        }
        if (hook) UnhookWindowsHookEx(hook);
        SetCursorPos(restore.x, restore.y);
        std::cout << "pointer over the opened launchpad goes to " << routed << " (launchpad " << launchpad.Handle() << ")"
                  << std::endl;
        Check(!hook || routed == launchpad.Handle(), "the opened launchpad takes the pointer over its panel");
        std::cout << "wheel over the opened launchpad (material " << static_cast<int>(launchpad.MaterialState())
                  << "): page " << before << " -> " << after << std::endl;
        Check(after == before + 1, "the opened launchpad takes the wheel over its panel");
        launchpad.Turn(before - after);
        launchpad.Settle();
    }
    Check(launchpad.Visible() && visibility == 1 && launchpad.SearchHandle() == preparedSearch &&
          IsWindowVisible(preparedSearch), "prepared search is revealed without creating another EDIT");
    Check(launchpad.Pages() == 2 && launchpad.PageEntries().size() == 21, "32 items over two pages");
    launchpad.Turn(1);
    Check(launchpad.Page() == 1 && launchpad.PageEntries().size() == 11, "second page");
    launchpad.Turn(5);
    Check(launchpad.Page() == 1, "turning clamps");
    launchpad.Turn(-1);
    launchpad.Settle();
    // Drag the first item onto the third cell.
    RECT client{};
    GetClientRect(launchpad.Handle(), &client);
    const float width = static_cast<float>(client.right) / Scale(launchpad.Handle());
    const float height = static_cast<float>(client.bottom) / Scale(launchpad.Handle());
    const auto live = LaunchpadWindow::LayoutFor(width, height);
    const auto from = LaunchpadWindow::Cell(live, 0), to = LaunchpadWindow::Cell(live, 2);
    const auto start = At(launchpad.Handle(), from.x + from.width / 2, from.y + from.height / 2);
    SendMessageW(launchpad.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, start);
    for (float x = from.x + from.width / 2; x <= to.x + to.width / 2; x += 10)
        SendMessageW(launchpad.Handle(), WM_MOUSEMOVE, MK_LBUTTON, At(launchpad.Handle(), x, from.y + from.height / 2));
    SendMessageW(launchpad.Handle(), WM_LBUTTONUP, 0, At(launchpad.Handle(), to.x + to.width / 2, to.y + to.height / 2));
    Check(arranged.size() == entries.size() && arranged[2] == entries[0].path && arranged[0] == entries[1].path,
          "dragging an item rearranges the launchpad");
    Check(launchpad.PageEntries()[2].path == entries[0].path, "the new arrangement is shown at once");
    launchpad.Settle();
    SetWindowTextW(launchpad.SearchHandle(), L"special");
    Pump();
    Check(launchpad.Page() == 0 && launchpad.PageEntries().size() == 1 && launchpad.Pages() == 1, "search filters and resets the page");
    const auto cell = LaunchpadWindow::Cell(live, 0);
    const auto click = At(launchpad.Handle(), cell.x + cell.width / 2, cell.y + cell.height / 2);
    SendMessageW(launchpad.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, click);
    SendMessageW(launchpad.Handle(), WM_LBUTTONUP, 0, click);
    Check(opened == "C:\\Items\\special.txt" && !launchpad.Visible() && visibility == 2, "click opens the item and closes");
    Pump(600);
    Check(!IsWindowVisible(launchpad.Handle()), "the close animation ends hidden");
    Check(launchpad.Show(RECT{}, false), "reopens");
    if (animate) SendMessageW(launchpad.Handle(), WM_CHAR, L's', 0);
    launchpad.Settle();
    if (animate) {
        wchar_t text[8]{};
        GetWindowTextW(launchpad.SearchHandle(), text, 8);
        Check(std::wstring(text) == L"s" && launchpad.PageEntries().size() == 1,
              "typing during opening transfers to the prepared native search");
        SetWindowTextW(launchpad.SearchHandle(), L"");
        Pump();
    }
    Check(launchpad.PageEntries().size() == 21, "reopening clears the search");
    {
        RECT normal{};GetClientRect(launchpad.Handle(),&normal);
        Check(launchpad.SetAppearance(60,120),"set small launchpad with large icons");launchpad.Settle();
        RECT reducedBounds{};GetClientRect(launchpad.Handle(),&reducedBounds);
        Check(reducedBounds.right<normal.right&&reducedBounds.bottom<normal.bottom,"launchpad panel actually resizes");
        const auto scaledLayout=LaunchpadWindow::LayoutFor(reducedBounds.right/Scale(launchpad.Handle()),reducedBounds.bottom/Scale(launchpad.Handle()),false,120);
        const int capacity=scaledLayout.columns*scaledLayout.rows;
        Check(capacity<21&&launchpad.PageEntries().size()==static_cast<std::size_t>(capacity)&&launchpad.Pages()>2,"small panel adapts paging to fit larger icons");
        const auto scaledCell=LaunchpadWindow::Cell(scaledLayout,0);
        Check(LaunchpadWindow::Hit(scaledLayout,launchpad.PageEntries().size(),0,launchpad.Pages(),scaledCell.x+scaledCell.width/2,scaledCell.y+scaledCell.height/2).kind==LaunchpadWindow::HitKind::Item,"scaled layout and input hit regions agree");
        Check(!launchpad.SetAppearance(59,100)&&!launchpad.SetAppearance(100,121),"invalid panel and icon settings rejected");
        Check(launchpad.SetAppearance(100,100),"restore launchpad dimensions");launchpad.Settle();
        Check(launchpad.PageEntries().size()==21,"default restores original page capacity");
    }
    // Collection tile and sub-page: back hit, inline rename commits on Enter.
    launchpad.SetCollection("c1", "项目");
    Check(LaunchpadWindow::Hit(LaunchpadWindow::LayoutFor(900, 540, true), 0, 0, 1,
                               LaunchpadWindow::LayoutFor(900, 540, true).back.x + 8,
                               LaunchpadWindow::LayoutFor(900, 540, true).back.y + 8, true).kind ==
              LaunchpadWindow::HitKind::Back,
          "collection sub-page exposes a back target");
    launchpad.SetCollection("", "");
    launchpad.Turn(1);
    launchpad.BeginRename("c1", "项目");
    SetWindowTextW(launchpad.SearchHandle(), L"改名");
    SendMessageW(launchpad.SearchHandle(), WM_KEYDOWN, VK_RETURN, 0);
    Check(renamed == "c1|改名", "Enter commits the collection rename");
    launchpad.SetCollection("", "");
    SendMessageW(launchpad.Handle(), WM_ACTIVATEAPP, FALSE, 0);
    Check(!launchpad.Visible(), "another program taking the foreground closes it");
    launchpad.Stop();

    // Dragging a launchpad item onto a collection tile moves it into the
    // collection instead of reordering it in the flat grid.
    std::vector<lume::dock::Entry> dropEntries{{"C:\\Items\\a.txt", "a", false},
                                               {"collection:c1", "项目", true, true, "c1"}};
    std::string movedInto;
    LaunchpadWindow dropPad(GetModuleHandleW(nullptr), {
        [&] { return dropEntries; }, [](const auto& all, const std::string&) { return all; },
        {}, {}, {}, {}, {}, {}, {}, {}, {}, [&](const lume::dock::Entry& entry, const std::string& id) {
            movedInto = entry.path + "|" + id;
            return true;
        }, {}, {}, {}, {}, {}});
    Check(dropPad.Show(RECT{}, false), "collection drop target shows");
    dropPad.Settle();
    RECT dropClient{};
    GetClientRect(dropPad.Handle(), &dropClient);
    const auto dropLayout = LaunchpadWindow::LayoutFor(
        static_cast<float>(dropClient.right) / Scale(dropPad.Handle()),
        static_cast<float>(dropClient.bottom) / Scale(dropPad.Handle()));
    const auto source = LaunchpadWindow::Cell(dropLayout, 0);
    const auto target = LaunchpadWindow::Cell(dropLayout, 1);
    SendMessageW(dropPad.Handle(), WM_LBUTTONDOWN, MK_LBUTTON,
                 At(dropPad.Handle(), source.x + source.width / 2, source.y + source.height / 2));
    SendMessageW(dropPad.Handle(), WM_MOUSEMOVE, MK_LBUTTON,
                 At(dropPad.Handle(), target.x + target.width / 2, target.y + target.height / 2));
    SendMessageW(dropPad.Handle(), WM_LBUTTONUP, 0,
                 At(dropPad.Handle(), target.x + target.width / 2, target.y + target.height / 2));
    Check(movedInto == "C:\\Items\\a.txt|c1", "dropping an item on a collection moves it into the collection");
    dropPad.Stop();

    CoUninitialize();
    if (failures) return 1;
    std::cout << "dock window passed\n";
    return 0;
}
