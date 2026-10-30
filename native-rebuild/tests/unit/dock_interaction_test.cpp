// Regress the user-visible running Dock path, on a private desktop by default.
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include <windowsx.h>
#include <objbase.h>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace lume::platform::windows;
using lume::dock::Item;
using lume::dock::Kind;
namespace {
void Check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::fflush(stderr); std::exit(1); }
}
void Pump(DWORD ms) {
    const auto end = GetTickCount64() + ms;
    do {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            // Synthetic mouse input, independent of the user's real cursor.
            if (msg.message == WM_MOUSELEAVE) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        Sleep(2);
    } while (GetTickCount64() < end);
}
HWND Find(const wchar_t* name) {
    struct Search { const wchar_t* name; HWND found = nullptr; } search{name};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM data) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(data);
        wchar_t cls[128]{}; GetClassNameW(hwnd, cls, 128);
        if (wcscmp(cls, s.name) == 0) { s.found = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
std::wstring Title(HWND window) {
    wchar_t title[512]{}; GetWindowTextW(window, title, 512); return title;
}
LPARAM At(const DockWindow& dock, std::size_t i) {
    RECT r = dock.IconBounds(i), layer{}; GetWindowRect(dock.Handle(), &layer);
    return MAKELPARAM((r.left + r.right) / 2 - layer.left, (r.top + r.bottom) / 2 - layer.top);
}
LPARAM Move(LPARAM p, int dx, int dy = 0) { return MAKELPARAM(GET_X_LPARAM(p) + dx, GET_Y_LPARAM(p) + dy); }
void Hover(DockWindow& dock, std::size_t i) {
    SendMessageW(dock.Handle(), WM_MOUSEMOVE, 0, At(dock, i)); Pump(360);
}
LPARAM Drag(DockWindow& dock, std::size_t i, int dx, int dy = 0) {
    Hover(dock, i);
    const auto p = At(dock, i);
    SendMessageW(dock.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, p);
    for (int n = 1; n <= 24; ++n) {
        SendMessageW(dock.Handle(), WM_MOUSEMOVE, MK_LBUTTON, Move(p, dx * n / 24, dy * n / 24));
        Pump(16);
    }
    Pump(180);
    return Move(p, dx, dy);
}
void Release(DockWindow& dock, LPARAM p) {
    SendMessageW(dock.Handle(), WM_LBUTTONUP, 0, p);
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0); Pump(650);
}
void CALLBACK ChoosePenultimate(HWND, UINT, UINT_PTR timer, DWORD) {
    const auto menu = Find(L"LumeWidgetMenuWindow");
    if (!menu) return;
    KillTimer(nullptr, timer);
    PostMessageW(menu, WM_KEYDOWN, VK_END, 0);
    PostMessageW(menu, WM_KEYDOWN, VK_UP, 0);
    PostMessageW(menu, WM_KEYDOWN, VK_RETURN, 0);
}
LRESULT CALLBACK FixtureProcedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(hwnd, &paint);
        RECT rect{}; GetClientRect(hwnd, &rect);
        const auto brush = CreateSolidBrush(RGB(223, 238, 250)); FillRect(dc, &rect, brush); DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(30, 70, 120));
        const auto text = Title(hwnd); DrawTextW(dc, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &paint); return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
void Scenario(bool visual) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = FixtureProcedure; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LumeDockInteractionFixture"; wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    RegisterClassW(&wc);
    HWND first = CreateWindowExW(0, wc.lpszClassName, L"文档一 · Lume 预览验证", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, 620, 360, nullptr, nullptr, wc.hInstance, nullptr);
    HWND second = CreateWindowExW(0, wc.lpszClassName, L"文档二 · Lume 预览验证", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        180, 140, 620, 360, nullptr, nullptr, wc.hInstance, nullptr);
    Check(first && second, "fixture windows created");
    ShowWindow(first, SW_SHOWNOACTIVATE); ShowWindow(second, SW_SHOWNOACTIVATE);
    UpdateWindow(first); UpdateWindow(second);
    const auto h1 = reinterpret_cast<std::uint64_t>(first), h2 = reinterpret_cast<std::uint64_t>(second);
    std::vector<lume::dock::ProgramWindow> windows{{"C:/A.exe", "文档一", h1, false}};
    std::string activated, contextual, discarded;
    std::uint64_t selected = 0;
    unsigned places = 0, collections = 0;
    DockWindow* view = nullptr;
    DockWindowPorts ports;
    ports.slotsName = L"Local\\LumeDockInteractionSlots-" + std::to_wstring(GetCurrentProcessId());
    ports.icon = [](const Item&) {
        auto image = std::make_shared<WidgetBitmap>(); image->width = image->height = 48;
        image->pixels.assign(48 * 48, 0xff3388cc); return image;
    };
    ports.windowsFor = [&](const Item&) { return windows; };
    ports.focusWindow = [&](std::uint64_t handle) { selected = handle; };
    ports.activate = [&](std::size_t i) { activated = view->ItemAt(i)->path; };
    ports.context = [&](std::size_t i, POINT) { contextual = view->ItemAt(i)->path; };
    ports.place = [&](std::size_t, std::size_t, std::size_t) { ++places; };
    ports.collect = [&](const std::string&, const std::string&) { ++collections; return true; };
    ports.discard = [&](const Item& item) { discarded = item.path; };
    DockWindow dock(wc.hInstance, ports); view = &dock;
    std::vector<Item> items{{Kind::Launchpad}, {Kind::Collection, "collection:fixture", "收纳"},
        {Kind::Pinned, "C:/pin1.exe", "左一", false, 0}, {Kind::Pinned, "C:/pin2.exe", "左二", false, 1},
        {Kind::Separator}, {Kind::Running, "C:/A.exe", "应用 A", true},
        {Kind::Running, "C:/B.exe", "应用 B", true}, {Kind::Running, "C:/C.exe", "应用 C", true}};
    Check(dock.Show(items, false), "running Dock shows"); Pump(650);
    Hover(dock, 5);
    auto popup = Find(L"LumeNativeDockPreview");
    Check(popup && IsWindowVisible(popup) && Title(popup).find(L"1 个窗口") != std::wstring::npos,
        "single running window has a visible preview and count");
    Check(!(GetWindowLongPtrW(popup, GWL_EXSTYLE) & WS_EX_TRANSPARENT), "preview receives clicks");
    const int unit = static_cast<int>(GetDpiForWindow(popup));
    const auto card = [&](int column) { return MAKELPARAM(MulDiv(110 + column * 224, unit, 96), MulDiv(105, unit, 96)); };
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, card(0));
    SendMessageW(popup, WM_LBUTTONUP, 0, card(0));
    Check(selected == h1 && !IsWindowVisible(popup), "single preview selects its HWND and dismisses");
    windows.push_back({"C:/A.exe", "文档二", h2, false});
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0); Hover(dock, 5);
    Check(IsWindowVisible(popup) && Title(popup).find(L"2 个窗口") != std::wstring::npos, "all grouped windows appear");
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0); Pump(150);
    Check(IsWindowVisible(popup), "preview stays visible while crossing the gap");
    SendMessageW(popup, WM_MOUSEMOVE, 0, card(1)); Pump(400);
    Check(IsWindowVisible(popup), "entering the preview cancels its dismissal");
    if (visual) { Pump(12000); }
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, card(1));
    SendMessageW(popup, WM_LBUTTONUP, 0, card(1));
    Check(selected == h2, "second card selects the second HWND");
    Hover(dock, 5);
    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0); Pump(450);
    Check(!IsWindowVisible(popup), "leaving the Dock and gallery dismisses it");

    SendMessageW(dock.Handle(), WM_MOUSELEAVE, 0, 0); Pump(600);
    const RECT left1 = dock.IconBounds(2), left2 = dock.IconBounds(3);
    const int pitch = dock.IconBounds(6).left - dock.IconBounds(5).left;
    Release(dock, Drag(dock, 5, pitch * 2 + pitch / 3));
    Check(dock.IconBounds(6).left < dock.IconBounds(7).left && dock.IconBounds(7).left < dock.IconBounds(5).left,
        "right icons actually reorder past two neighbours");
    Check(std::abs(dock.IconBounds(2).left - left1.left) <= 2 && std::abs(dock.IconBounds(3).left - left2.left) <= 2,
        "right reorder returns left icons to unchanged positions");
    Check(places == 0 && collections == 0, "right reorder never writes pinned order");
    dock.Show(items, false); Pump(650);
    Check(dock.IconBounds(7).left < dock.IconBounds(5).left, "refresh retains running order");
    SendMessageW(dock.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, At(dock, 5));
    SendMessageW(dock.Handle(), WM_LBUTTONUP, 0, At(dock, 5));
    SendMessageW(dock.Handle(), WM_RBUTTONUP, 0, At(dock, 5));
    Check(activated == "C:/A.exe" && contextual == "C:/A.exe", "reordered click and context keep application identity");

    const auto cancelled = Drag(dock, 5, -pitch * 2);
    (void)cancelled;
    SendMessageW(dock.Handle(), WM_KEYDOWN, VK_ESCAPE, 0); Pump(650);
    Check(dock.IconBounds(7).left < dock.IconBounds(5).left, "Escape restores prior running order");
    auto away = Drag(dock, 5, 0, -MulDiv(200, unit, 96));
    const auto ghost = Find(L"LumeNativeDockDragIcon");
    Check(dock.Removing() && ghost && IsWindowVisible(ghost) && Title(ghost) == L"关闭窗口",
        "dragging running icon outside displays close-window feedback");
    if (visual) { Pump(12000); }
    // Returning over the left collection cannot pin, collect, or move left items.
    auto back = At(dock, 1);
    SendMessageW(dock.Handle(), WM_MOUSEMOVE, MK_LBUTTON, back); Pump(250);
    Check(!dock.Removing(), "returning to Dock cancels close feedback");
    Release(dock, back);
    Check(discarded.empty() && places == 0 && collections == 0, "drag back does not close, pin, or collect");
    Check(dock.IconBounds(2).left < dock.IconBounds(3).left && dock.IconBounds(3).left < dock.IconBounds(5).left,
        "left pinned icons remain before all running icons");
    away = Drag(dock, 5, 0, -MulDiv(200, unit, 96));
    auto refreshed = items; refreshed.erase(refreshed.begin() + 6);
    dock.Show(refreshed, false); // refresh while capture is held
    Release(dock, away);
    Check(discarded == "C:/A.exe", "drag-out closes the captured application despite pending refresh");
    Check(dock.IconBounds(2).left < dock.IconBounds(3).left, "drag-out preserves left order");
    SetTimer(nullptr, 0, visual ? 12000 : 30, ChoosePenultimate);
    Check(LumeWidgetMenu::ShowDockItem(dock.Handle(), {300, 300}, true, false, true, false, true) == 21,
        "left remove action is directly above Close Dock");
    SetTimer(nullptr, 0, visual ? 12000 : 30, ChoosePenultimate);
    Check(LumeWidgetMenu::ShowDockItem(dock.Handle(), {300, 300}, false, false, true, false, true, false, false, true) == 24,
        "right close-window action is directly above Close Dock");
    dock.Stop(); DestroyWindow(first); DestroyWindow(second); CoUninitialize();
    std::puts("PASS single/multiple previews, preview selection, gap transition, running reorder/refresh/cancel/identity, drag-out/back, section isolation, bottom menu actions");
}
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc > 1) { Scenario(wcscmp(argv[1], L"--visual") == 0); return 0; }
    const auto name = L"LumeDockInteraction-" + std::to_wstring(GetCurrentProcessId());
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
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    Check(job && SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "bound fixture job");
    Check(CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child), "start isolated scenario");
    Check(AssignProcessToJobObject(job, child.hProcess), "assign fixture job"); ResumeThread(child.hThread);
    Check(WaitForSingleObject(child.hProcess, 50000) == WAIT_OBJECT_0, "scenario completes");
    DWORD code = 1; GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hThread); CloseHandle(child.hProcess); CloseHandle(job); CloseDesktop(desktop);
    return static_cast<int>(code);
}
