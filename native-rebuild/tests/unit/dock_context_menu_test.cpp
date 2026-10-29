// Exercise the real DockSession -> DockWindow -> modal menu path on a desktop
// that is never switched to. Preferences stay in memory; no app is launched.
#include "apps/shell/dock_session.h"
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/launchpad_window.h"
#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/organizer/organizer_text.h"
#include <windowsx.h>
#include <objbase.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <filesystem>
#include <fstream>

namespace {
using lume::shell::DockSession;
using lume::platform::windows::DockWindow;
using lume::ui::PageCommand;
void Check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
struct Files final : lume::settings::IFileStore {
    std::map<std::string, std::string> values;
    bool failWrites = false;
    bool Read(const std::string& path, std::string& out) const override {
        const auto found = values.find(path);
        if (found == values.end()) return false;
        out = found->second; return true;
    }
    bool Exists(const std::string& path) const override { return values.count(path) != 0; }
    bool Write(const std::string& path, const std::string& value) override {
        if (failWrites) return false;
        values[path] = value; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        const auto found = values.find(from);
        if (found == values.end()) return false;
        values[to] = found->second; values.erase(found); return true;
    }
    bool Remove(const std::string& path) override { values.erase(path); return true; }
};
HWND Find(const wchar_t* name, HWND owner = nullptr) {
    struct Search { const wchar_t* name; HWND owner; HWND result = nullptr; } search{name, owner};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM data) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(data);
        wchar_t name[64]{}; GetClassNameW(hwnd, name, 64);
        if (_wcsicmp(name, state.name) != 0) return TRUE;
        if (state.owner && GetWindow(hwnd, GW_OWNER) != state.owner) return TRUE;
        state.result = hwnd; return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
enum class Action { Cancel, Settings, UnpinAfterRefresh, DisableWhileOpen, NewCollection, Rename, Delete };
struct Drive { DockSession* session; Action action; unsigned opened = 0; };
Drive* drive = nullptr;
void CALLBACK MenuTimer(HWND, UINT, UINT_PTR timer, DWORD) {
    const HWND menu = Find(L"LumeWidgetMenuWindow");
    if (!menu) return;
    KillTimer(nullptr, timer);
    ++drive->opened;
    switch (drive->action) {
    case Action::NewCollection:
    case Action::Rename:
    case Action::Delete:
        PostMessageW(menu, WM_KEYDOWN, VK_HOME, 0);
        if (drive->action == Action::Delete) PostMessageW(menu, WM_KEYDOWN, VK_DOWN, 0);
        PostMessageW(menu, WM_KEYDOWN, VK_RETURN, 0);
        break;
    case Action::UnpinAfterRefresh:
        // A settings/heartbeat update dispatches inside the modal message loop
        // and replaces the session's items vector before the command returns.
        Check(drive->session->HandlePage(PageCommand::DockReflectionOn).has_value(), "refresh during menu");
        PostMessageW(menu, WM_KEYDOWN, VK_HOME, 0);
        PostMessageW(menu, WM_KEYDOWN, VK_RETURN, 0);
        break;
    case Action::Settings:
        PostMessageW(menu, WM_KEYDOWN, VK_HOME, 0);
        PostMessageW(menu, WM_KEYDOWN, VK_DOWN, 0);  // past 添加系统图标
        PostMessageW(menu, WM_KEYDOWN, VK_DOWN, 0);
        PostMessageW(menu, WM_KEYDOWN, VK_DOWN, 0);
        PostMessageW(menu, WM_KEYDOWN, VK_RETURN, 0);
        break;
    case Action::DisableWhileOpen:
        Check(drive->session->HandlePage(PageCommand::DockDisable).has_value(), "disable while menu open");
        break;
    default: PostMessageW(menu, WM_KEYDOWN, VK_ESCAPE, 0); break;
    }
}
LPARAM At(DockSession& session, float x, float y) {
    RECT bar{}, icons{};
    Check(GetWindowRect(Find(L"LumeNativeDesktopDockBar"), &bar) &&
          GetWindowRect(session.Handle(), &icons), "find Dock layers");
    const double scale = GetDpiForWindow(session.Handle()) / 96.0;
    return MAKELPARAM(bar.left - icons.left + static_cast<int>(std::lround(x * scale)),
                     bar.top - icons.top + static_cast<int>(std::lround(y * scale)));
}
void RightClick(DockSession& session, float x, float y, Action action = Action::Cancel) {
    Drive current{&session, action}; drive = &current;
    const auto timer = SetTimer(nullptr, 0, 20, MenuTimer);
    Check(timer != 0, "start modal menu driver");
    SendMessageW(session.Handle(), WM_RBUTTONUP, 0, At(session, x, y));
    KillTimer(nullptr, timer); drive = nullptr;
    Check(current.opened == 1 && !Find(L"LumeWidgetMenuWindow"), "one real menu opens and closes");
}
void Pump(DWORD ms) {
    const auto end = GetTickCount64() + ms;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(2);
    } while (GetTickCount64() < end);
}
void Drag(HWND window, LPARAM from, LPARAM to) {
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, from);
    for (int i = 1; i <= 24; ++i) {
        const int x = GET_X_LPARAM(from) + (GET_X_LPARAM(to) - GET_X_LPARAM(from)) * i / 24;
        const int y = GET_Y_LPARAM(from) + (GET_Y_LPARAM(to) - GET_Y_LPARAM(from)) * i / 24;
        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, y)); Pump(4);
    }
    SendMessageW(window, WM_LBUTTONUP, 0, to);
    SendMessageW(window, WM_MOUSELEAVE, 0, 0); Pump(650);
}
LPARAM PanelAt(HWND window, float x, float y) {
    const auto dpi = GetDpiForWindow(window);
    return MAKELPARAM(static_cast<int>(std::lround(x * dpi / 96)), static_cast<int>(std::lround(y * dpi / 96)));
}
void Scenario() {
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize COM");
    {
        Files files;
        lume::dock::Preferences prefs;
        prefs.enabled = prefs.transparent = true;
        prefs.pinned = {"C:\\LumeContextFixture\\first-application.exe", "C:\\LumeContextFixture\\second-application.exe",
                        "C:\\LumeContextFixture\\third-application.exe"};
        Check(lume::dock::Encode(prefs, files.values["dock.json"]), "encode in-memory preferences");
        unsigned settings = 0;
        DockSession session(GetModuleHandleW(nullptr), {"dock.json", true}, {}, {}, [&] { ++settings; }, [] { return false; }, &files);
        Check(session.Restore() && session.Running(), "restore real Dock session");
        const auto items = lume::dock::DockItems(prefs.pinned, prefs.pinned, {},
            lume::platform::windows::dock_sources::PathKey);
        for (int i = 0; i < 10; ++i) {
            RightClick(session, 2, 50);  // padding: no Item exists
            RightClick(session, DockWindow::Width(items) - 2, 50);
            RightClick(session, 30, 3);  // above the icon hit area
        }
        RightClick(session, 2, 50, Action::Settings);
        Check(settings == 1 && session.Running(), "background menu routes to settings without exiting");
        for(const auto& value:{std::pair{L"v1|dock|launchpadScale|83",83},std::pair{L"v1|dock|launchpadIconScale|50",50}}){
            const auto command=lume::ui::ParsePageCommand(value.first);
            Check(command!=PageCommand::Invalid&&session.HandlePage(command).has_value(),"launchpad size applies through session");
        }
        Check(session.Snapshot().launchpadScale==83&&session.Snapshot().launchpadIconScale==50,"session owns independent launchpad preferences");
        const auto stored=files.values["dock.json"];files.failWrites=true;
        session.HandlePage(lume::ui::ParsePageCommand(L"v1|dock|launchpadIconScale|120"));
        Check(session.Snapshot().launchpadIconScale==50&&files.values["dock.json"]==stored,"failed scale save restores previous runtime and file");
        files.failWrites=false;
        session.HandlePage(lume::ui::ParsePageCommand(L"v1|dock|launchpadScale|100"));
        session.HandlePage(lume::ui::ParsePageCommand(L"v1|dock|launchpadIconScale|100"));
        for (const std::size_t index : {0u, 1u, 2u, 3u}) {
            const auto slot = DockWindow::Slot(items, index);
            RightClick(session, slot.x + slot.width / 2, 50);
        }
        const auto pinned = DockWindow::Slot(items, 2);
        RightClick(session, pinned.x + pinned.width / 2, 50, Action::UnpinAfterRefresh);
        Check(session.Snapshot().pinned == std::vector<std::string>{prefs.pinned[1], prefs.pinned[2]},
              "menu command keeps the original item after list replacement");
        RightClick(session, 2, 50, Action::NewCollection); Pump(600);
        Check(session.Snapshot().dockCollections.size() == 1, "background right click creates Dock collection");
        const auto id = session.Snapshot().dockCollections[0];
        const auto currentItems = lume::dock::DockItemsFor(session.Snapshot(), session.Snapshot().pinned, {},
            lume::platform::windows::dock_sources::PathKey);
        Check(currentItems[1].kind == lume::dock::Kind::Collection, "collection initially next to launchpad");
        const auto groupSlot = DockWindow::Slot(currentItems, 1), source = DockWindow::Slot(currentItems, 3);
        files.failWrites = true;
        Drag(session.Handle(), At(session, source.x + source.width / 2, 50), At(session, groupSlot.x + groupSlot.width / 2, 50));
        Check(session.Snapshot().pinned.size() == 2 && lume::dock::FindCollection(session.Snapshot(), id)->paths.empty(),
              "save failure retains source icon and empty collection");
        files.failWrites = false;
        for (int i = 0; i < 2; ++i) {
            Drag(session.Handle(), At(session, source.x + source.width / 2, 50), At(session, groupSlot.x + groupSlot.width / 2, 50));
        }
        Check(session.Snapshot().pinned.empty() && lume::dock::FindCollection(session.Snapshot(), id)->paths ==
              std::vector<std::string>{prefs.pinned[1], prefs.pinned[2]}, "real Dock drags move icons into collection");
        const auto groupAt = At(session, groupSlot.x + groupSlot.width / 2, 50);
        SendMessageW(session.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, groupAt);
        SendMessageW(session.Handle(), WM_LBUTTONUP, 0, groupAt); Pump(600);
        const HWND popup = Find(L"LumeDockCollectionPopup");
        RECT client{};
        Check(popup && IsWindowVisible(popup) && GetClientRect(popup, &client), "collection opens a separate popup");
        const float dpi = GetDpiForWindow(popup) / 96.f;
        Check(client.right / dpi < 430 && client.bottom / dpi < 450 && !IsWindowVisible(Find(L"LumeDockLaunchpad")),
              "small popup does not open or depend on the large launchpad");
        RightClick(session, groupSlot.x + groupSlot.width / 2, 50, Action::Rename);
        const HWND edit = Find(L"EDIT", popup);
        Check(edit && IsWindowVisible(edit), "Dock collection rename opens native edit");
        SetWindowTextW(edit, L"我的 Dock 收纳"); SendMessageW(edit, WM_KEYDOWN, VK_RETURN, 0); Pump(60);
        Check(lume::dock::FindCollection(session.Snapshot(), id)->title == "我的 Dock 收纳" && !IsWindowVisible(edit),
              "rename saves and restores popup title without a search state");
        const auto layout = lume::platform::windows::LaunchpadWindow::PopupLayoutFor(client.right / dpi, client.bottom / dpi);
        const auto cell0 = lume::platform::windows::LaunchpadWindow::Cell(layout, 0);
        const auto cell1 = lume::platform::windows::LaunchpadWindow::Cell(layout, 1);
        Drag(popup, PanelAt(popup, cell0.x + cell0.width / 2, cell0.y + cell0.height / 2),
             PanelAt(popup, cell1.x + cell1.width / 2, cell1.y + cell1.height / 2));
        Check(lume::dock::FindCollection(session.Snapshot(), id)->paths[0] == prefs.pinned[2], "popup native drag saves member ordering");
        POINT outside{GET_X_LPARAM(At(session, groupSlot.x + groupSlot.width + 100, 50)), GET_Y_LPARAM(At(session, groupSlot.x + groupSlot.width + 100, 50))};
        ClientToScreen(session.Handle(), &outside); ScreenToClient(popup, &outside);
        Drag(popup, PanelAt(popup, cell0.x + cell0.width / 2, cell0.y + cell0.height / 2), MAKELPARAM(outside.x, outside.y));
        Check(session.Snapshot().pinned == std::vector<std::string>{prefs.pinned[2]} &&
              lume::dock::FindCollection(session.Snapshot(), id)->paths == std::vector<std::string>{prefs.pinned[1]},
              "dragging member from popup back to Dock is atomic");
        session.HandlePage(lume::ui::ParsePageCommand(L"v1|dock|launchpadScale|83"));
        session.HandlePage(lume::ui::ParsePageCommand(L"v1|dock|launchpadIconScale|57"));
        session.HandlePage(lume::ui::ParsePageCommand(L"v1|dock|iconScale|73"));
        session.HandlePage(PageCommand::DockReflectionOff);
        session.Shutdown();
        DockSession restarted(GetModuleHandleW(nullptr), {"dock.json", true}, {}, {}, {}, [] { return false; }, &files);
        Check(restarted.Snapshot().launchpadScale == 83 && restarted.Snapshot().launchpadIconScale == 57 &&
              restarted.Snapshot().iconScale == 73 && !restarted.Snapshot().reflection,
              "nondefault Dock and launchpad scales and unchecked reflection survive restart");
        Check(restarted.Restore() && restarted.Snapshot().dockCollections == std::vector<std::string>{id} &&
              lume::dock::FindCollection(restarted.Snapshot(), id)->title == "我的 Dock 收纳" && restarted.Snapshot().pinned.size() == 1,
              "collection binding title members and Dock items persist after restart");
        restarted.HandlePage(lume::ui::ParsePageCommand(L"v1|dock|iconScale|100"));
        Pump(400);
        RightClick(restarted, groupSlot.x + groupSlot.width / 2, 50, Action::Delete); Pump(300);
        Check(restarted.Snapshot().dockCollections.empty() && restarted.Snapshot().collections.empty() &&
              restarted.Snapshot().pinned.size() == 2, "delete collection returns members to Dock without deleting files");
        RightClick(restarted, 2, 50, Action::DisableWhileOpen);
        Check(!restarted.Running() && !restarted.Snapshot().enabled, "closing owner unwinds modal menu safely");
        restarted.Shutdown();
    }
    CoUninitialize();
}
LRESULT CALLBACK PreviewBackground(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; const HDC dc = BeginPaint(window, &paint);
        RECT r{}; GetClientRect(window, &r);
        for (int y = 0; y < r.bottom; y += 8) {
            const auto brush = CreateSolidBrush(RGB(25 + y / 18, 48 + y / 10, 78 + y / 7));
            const RECT stripe{0, y, r.right, y + 8}; FillRect(dc, &stripe, brush); DeleteObject(brush);
        }
        const auto brush = CreateSolidBrush(RGB(170, 194, 188)); const auto old = SelectObject(dc, brush);
        Ellipse(dc, 240, 60, 520, 340); SelectObject(dc, old); DeleteObject(brush);
        EndPaint(window, &paint); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
void SavePopup(HWND window, const std::filesystem::path& path) {
    RECT r{}; GetWindowRect(window, &r); const int width = r.right - r.left, height = r.bottom - r.top;
    const HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void* pixels = nullptr;
    const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    Check(bitmap && memory, "create preview pixels"); const auto old = SelectObject(memory, bitmap);
    Check(BitBlt(memory, 0, 0, width, height, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != FALSE, "capture own popup");
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + width * height * 4;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    out.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(width) * height * 4);
    Check(out.good(), "save preview bitmap");
    SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
}
void PopupProbe(bool preview) {
    using namespace lume::platform::windows;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize popup COM");
    const HWND previous = GetForegroundWindow();
    HWND background = nullptr;
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    Check(GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) != FALSE, "read popup monitor");
    const int middle = (monitor.rcWork.left + monitor.rcWork.right) / 2;
    const RECT anchor{middle - 30, monitor.rcWork.bottom - 110, middle + 30, monitor.rcWork.bottom - 50};
    if (preview) {
        WNDCLASSW type{}; type.hInstance = GetModuleHandleW(nullptr); type.lpfnWndProc = PreviewBackground;
        type.lpszClassName = L"LumeCollectionPreviewBackdrop"; RegisterClassW(&type);
        background = CreateWindowExW(WS_EX_TOOLWINDOW, type.lpszClassName, L"Lume collection preview", WS_POPUP | WS_VISIBLE,
            middle - 400, anchor.top - 620, 800, 640, nullptr, nullptr, type.hInstance, nullptr);
        Check(background != nullptr, "create own preview backdrop");
    }
    {
        std::vector<lume::dock::Entry> entries;
        std::map<std::string, std::shared_ptr<const WidgetBitmap>> icons;
        const wchar_t* names[]{L"浏览器", L"文件夹", L"编辑器", L"音乐", L"图片", L"工具", L"文档", L"日历", L"游戏", L"更多", L"终端", L"项目"};
        for (int i = 0; i < 12; ++i) {
            const auto path = "C:\\PopupFixture\\app" + std::to_string(i) + ".exe";
            entries.push_back({path, lume::platform::windows::OrganizerUtf8(names[i]), true});
            WidgetCanvas icon; WidgetBitmap pixels;
            Check(icon.Begin(64, 64, 96), "begin fixture icon");
            icon.Rounded({1, 1, 62, 62}, 14, 0x279bccu + static_cast<unsigned>(i) * 0x071501u, 1);
            icon.Text(std::to_wstring(i + 1), {0, 10, 64, 44}, 28, 0xffffff, 1, true, WidgetTextAlign::Center);
            Check(icon.Finish(pixels), "finish fixture icon");
            icons[path] = std::make_shared<const WidgetBitmap>(std::move(pixels));
        }
        unsigned opened = 0, contexts = 0;
        LaunchpadWindow* pointer = nullptr;
        LaunchpadPorts ports;
        ports.entries = [&] { return entries; };
        ports.icon = [&](const std::string& path) { return icons.at(path); };
        ports.open = [&](const lume::dock::Entry&) { ++opened; };
        ports.backgroundContext = [&](POINT) { ++contexts; };
        ports.back = [&] { pointer->Hide(); };
        LaunchpadWindow popup(GetModuleHandleW(nullptr), std::move(ports), LaunchpadWindow::Presentation::CollectionPopup);
        pointer = &popup;
        popup.SetCollection("fixture", "工作与工具");
        Check(popup.Show(anchor, false), "show standalone compact collection"); Pump(180); popup.Settle();
        Check(popup.Pages() == 2 && popup.PageEntries().size() == 9, "compact collection pages by nine");
        popup.Turn(1); popup.Settle();
        Check(popup.PageEntries().size() == 3, "second compact page contains remaining members");
        popup.Turn(-1); popup.Settle();
        SendMessageW(popup.Handle(), WM_RBUTTONUP, 0, PanelAt(popup.Handle(), 195, 25));
        Check(contexts == 1 && popup.Visible(), "popup header supports collection context without closing");
        if (preview) {
            wchar_t directory[32768]{};
            Check(GetEnvironmentVariableW(L"LUME_DOCK_COLLECTION_PREVIEW", directory, 32768) != 0, "preview output supplied");
            std::filesystem::create_directories(directory);
            Pump(450); SavePopup(popup.Handle(), std::filesystem::path(directory) / "popup-liquid.bmp");
            popup.SetFrosted(true); Pump(300); SavePopup(popup.Handle(), std::filesystem::path(directory) / "popup-frosted.bmp");
            std::printf("preview material state=%d\n", static_cast<int>(popup.MaterialState()));
        }
        SendMessageW(popup.Handle(), WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(background));
        Check(!popup.Visible(), "clicking outside compact popup dismisses it without a deferred reopen");
        popup.SetCollection("fixture", "工作与工具");
        Check(popup.Show(anchor, false), "reopen after outside activation"); popup.Settle();
        RECT frame{}; GetClientRect(popup.Handle(), &frame);
        const float dpi = GetDpiForWindow(popup.Handle()) / 96.f;
        const auto layout = LaunchpadWindow::PopupLayoutFor(frame.right / dpi, frame.bottom / dpi);
        const auto cell = LaunchpadWindow::Cell(layout, 0);
        const auto at = PanelAt(popup.Handle(), cell.x + cell.width / 2, cell.y + cell.height / 2);
        SendMessageW(popup.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, at);
        SendMessageW(popup.Handle(), WM_LBUTTONUP, 0, at);
        Check(opened == 1 && !popup.Visible(), "member click hides popup and invokes launch port once");
        popup.SetCollection("fixture", "空收纳"); entries.clear();
        Check(popup.Show(anchor, true), "reopen empty compact collection"); popup.Settle();
        Check(popup.PageEntries().empty() && popup.Pages() == 1, "empty collection keeps a usable panel");
        SendMessageW(popup.Handle(), WM_KEYDOWN, VK_ESCAPE, 0); Check(!popup.Visible(), "Escape closes compact collection");
        popup.Stop();
    }
    if (background) DestroyWindow(background);
    if (preview && previous) SetForegroundWindow(previous);
    CoUninitialize();
}
// A dragged launchpad item leaves the panel in its own topmost window: the old
// in-panel copy was clipped at the client edge and covered by the Dock.
void DragGhostProbe() {
    using namespace lume::platform::windows;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize drag ghost COM");
    const RECT anchor{0, 1200, 900, 1300};
    std::vector<lume::dock::Entry> entries;
    std::map<std::string, std::shared_ptr<const WidgetBitmap>> icons;
    for (int i = 0; i < 8; ++i) {
        const auto path = "C:\\DragGhostFixture\\item" + std::to_string(i) + ".exe";
        entries.push_back({path, lume::platform::windows::OrganizerUtf8(L"Item" + std::to_wstring(i + 1)), true});
        WidgetCanvas icon;
        WidgetBitmap pixels;
        Check(icon.Begin(64, 64, 96), "begin drag fixture icon");
        icon.Rounded({1, 1, 62, 62}, 14, 0x3a7bd5u + static_cast<unsigned>(i) * 0x101010u, 1);
        Check(icon.Finish(pixels), "finish drag fixture icon");
        icons[path] = std::make_shared<const WidgetBitmap>(std::move(pixels));
    }
    unsigned drops = 0, leaves = 0;
    std::string dropped;
    LaunchpadPorts ports;
    ports.entries = [&] { return entries; };
    ports.icon = [&](const std::string& path) { return icons.at(path); };
    ports.dragOutside = [](POINT) { return true; };
    ports.dropOutside = [&](const lume::dock::Entry& entry, POINT) { dropped = entry.path; ++drops; return true; };
    ports.leaveOutside = [&] { ++leaves; };
    LaunchpadWindow pad(GetModuleHandleW(nullptr), std::move(ports));
    Check(pad.Show(anchor, false), "show the launchpad with fixture entries");
    Pump(200);
    pad.Settle();
    RECT client{};
    GetClientRect(pad.Handle(), &client);
    const auto dpi = GetDpiForWindow(pad.Handle()) / 96.f;
    const auto layout = LaunchpadWindow::LayoutFor(client.right / dpi, client.bottom / dpi);
    const auto cell = LaunchpadWindow::Cell(layout, 0);
    const auto press = PanelAt(pad.Handle(), cell.x + cell.width / 2, cell.y + cell.height / 2);
    SendMessageW(pad.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, press);
    SendMessageW(pad.Handle(), WM_MOUSEMOVE, MK_LBUTTON,
                 MAKELPARAM(GET_X_LPARAM(press) + 20, GET_Y_LPARAM(press) + 20));
    const HWND ghost = Find(L"LumeLaunchpadDragIcon");
    Check(ghost != nullptr, "a drag creates the pointer window");
    Check(IsWindowVisible(ghost) != FALSE, "the pointer window is visible during the drag");
    Check((GetWindowLongW(ghost, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0, "the pointer window stays above the Dock");
    RECT startGhost{};
    GetWindowRect(ghost, &startGhost);
    // Far outside the client area: the icon must follow the pointer there.
    const int outsideX = client.right + 40, outsideY = 40;
    SendMessageW(pad.Handle(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(outsideX, outsideY));
    RECT frame{};
    GetWindowRect(pad.Handle(), &frame);
    RECT ghostRect{};
    GetWindowRect(ghost, &ghostRect);
    Check(IsWindowVisible(ghost) != FALSE, "the pointer window survives leaving the panel");
    Check(ghostRect.right > frame.right, "the dragged icon is drawn outside the panel");
    // The icon keeps its grab offset, so compare the window's own movement with
    // the pointer's movement instead of centring it under the cursor.
    Check(std::abs((ghostRect.left - startGhost.left) - (outsideX - (GET_X_LPARAM(press) + 20))) <= 2 &&
          std::abs((ghostRect.top - startGhost.top) - (outsideY - (GET_Y_LPARAM(press) + 20))) <= 2,
          "the dragged icon follows the pointer");
    Check(leaves == 0, "the panel keeps the drop while the pointer is outside");
    SendMessageW(pad.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(outsideX, outsideY));
    Check(drops == 1 && dropped == entries[0].path, "releasing over the Dock hands the item to it");
    Check(IsWindowVisible(ghost) == FALSE, "the pointer window disappears after the drop");
    pad.Stop();
    Check(Find(L"LumeLaunchpadDragIcon") == nullptr, "stopping the panel destroys the pointer window");
    CoUninitialize();
}
}  // namespace
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc > 1 && wcscmp(argv[1], L"--preview") == 0) { PopupProbe(true); return 0; }
    if (argc > 1 && wcscmp(argv[1], L"--scenario") == 0) {
        Scenario(); PopupProbe(false); DragGhostProbe();
        std::puts("PASS Dock collection lifecycle, transactional dragging, popup paging and launch, menu crash regressions and the launchpad drag pointer window");
        return 0;
    }
    const auto name = L"LumeDockContextTest-" + std::to_wstring(GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    Check(desktop != nullptr, "create isolated desktop");
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
    auto command = std::wstring(L"\"") + executable + L"\" --scenario";
    auto location = std::wstring(L"WinSta0\\") + name;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.lpDesktop = location.data();
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE); startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    Check(job != nullptr, "create bounded fixture job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    Check(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE, "set fixture lifetime");
    Check(CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
          nullptr, nullptr, &startup, &child) != FALSE, "start isolated scenario");
    Check(AssignProcessToJobObject(job, child.hProcess) != FALSE, "bound scenario process"); ResumeThread(child.hThread);
    Check(WaitForSingleObject(child.hProcess, 25000) == WAIT_OBJECT_0, "scenario completes in time");
    DWORD code = 1; Check(GetExitCodeProcess(child.hProcess, &code) != FALSE, "read scenario result");
    CloseHandle(child.hThread); CloseHandle(child.hProcess); CloseHandle(job);
    Check(CloseDesktop(desktop) != FALSE, "release isolated desktop");
    return static_cast<int>(code);
}
