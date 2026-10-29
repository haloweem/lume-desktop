#include "platform/windows/material/material_api.h"

#if defined(LUME_UPDATE_MATERIAL_FIXTURE)
#include <map>
namespace {
struct Lease { LumeMaterialParameters parameters; LumeMaterialStatus status; HWND surface; };
std::map<LumeMaterialHandle, Lease> leases;
LumeMaterialHandle nextLease = 1;
}
extern "C" __declspec(dllexport) LumeMaterialHandle __cdecl LumeMaterialAttach(const LumeMaterialParameters* p) {
    if (!p || p->size != sizeof(*p) || !IsWindow(p->owner)) return 0;
    const auto id = nextLease++;
    LumeMaterialStatus status{}; status.size = sizeof(status);
    const auto surface = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"STATIC", L"Isolated material ABI fixture", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!surface) return 0;
    SetPropW(surface, L"LumeGlassSurfaceOwner", p->owner);
    leases.emplace(id, Lease{*p, status, surface});
    return id;
}
extern "C" __declspec(dllexport) int __cdecl LumeMaterialUpdate(LumeMaterialHandle id, const LumeMaterialParameters* p) {
    const auto found = leases.find(id);
    if (found == leases.end() || !p || p->owner != found->second.parameters.owner) return 0;
    found->second.parameters = *p;
    return 1;
}
extern "C" __declspec(dllexport) void __cdecl LumeMaterialDetach(LumeMaterialHandle id) {
    const auto found = leases.find(id);
    if (found == leases.end()) return;
    const auto surface = found->second.surface; leases.erase(found); DestroyWindow(surface);
}
extern "C" __declspec(dllexport) int __cdecl LumeMaterialInspect(LumeMaterialHandle id, LumeMaterialStatus* status) {
    const auto found = leases.find(id);
    if (found == leases.end() || !status || status->size != sizeof(*status)) return 0;
    *status = found->second.status;
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl LumeMaterialStatistics(LumeMaterialStatisticsData* status) {
    if (!status || status->size != sizeof(*status)) return 0;
    status->users = static_cast<uint32_t>(leases.size());
    status->devices = status->captures = 0;
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl LumeUpdateTestStatus(HWND window, int state) {
    for (auto& item : leases) if (item.second.parameters.owner == window) {
        auto& status = item.second.status;
        status.initialized = 1;
        status.frameReady = state == 1;
        status.frames = state == 1 ? 3 : 0;
        status.failed = state == 2;
        ShowWindow(item.second.surface, state == 1 ? SW_SHOWNOACTIVATE : SW_HIDE);
        return PostMessageW(window, LumeMaterialStatusChangedMessage, 0, 0) != FALSE;
    }
    return 0;
}
extern "C" __declspec(dllexport) int __cdecl LumeUpdateTestFrosted(HWND window) {
    for (const auto& item : leases) if (item.second.parameters.owner == window)
        return item.second.parameters.frosted == 2 && item.second.parameters.opticalProfile == 1;
    return 0;
}
#else
#include <windowsx.h>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

#if !__has_include("platform/windows/update_notification/update_presenter.h")
int main() { Check(false, "UpdatePresenter must show a native update notification"); }
#else
#include "platform/windows/update_notification/update_presenter.h"
using namespace lume::platform::windows;
namespace {
void Pump(DWORD milliseconds = 0) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (milliseconds) Sleep(5);
    } while (GetTickCount64() < end);
}
UpdateNotice Notice(std::wstring version = L"1.0.8") {
    return {std::move(version), L"\u65b0\u7248\u672c\u5df2\u53ef\u7528\u3002\u6539\u5584\u7a33\u5b9a\u6027\u4e0e\u663e\u793a\u6548\u679c\u3002", true};
}
void Click(UpdatePresenter& presenter, unsigned action) {
    const auto window = presenter.Handle();
    const auto rect = UpdatePresenterTestAccess::ActionRect(presenter, action);
    Check(rect.right > rect.left && rect.bottom > rect.top, "action has a nonempty hit target");
    const auto point = MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
    SendMessageW(window, WM_LBUTTONDOWN, 0, point);
    SendMessageW(window, WM_LBUTTONUP, 0, point);
    Pump();
}
template<class T> T Resolve(HMODULE library, const char* name) {
    const auto address = GetProcAddress(library, name);
    T fn = nullptr; static_assert(sizeof(fn) == sizeof(address));
    std::memcpy(&fn, &address, sizeof(fn)); Check(fn != nullptr, name); return fn;
}
bool denySurface = false, closeOnShow = false;
LRESULT CALLBACK Observe(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0) {
        const auto& message = *reinterpret_cast<CWPRETSTRUCT*>(lp);
        wchar_t name[80]{}; GetClassNameW(message.hwnd, name, 80);
        if (lstrcmpW(name, L"LumeNativeUpdateNotice") == 0) {
            if (denySurface && message.message == WM_CREATE)
                SetLayeredWindowAttributes(message.hwnd, 0, 255, LWA_ALPHA);
            if (closeOnShow && message.message == WM_SHOWWINDOW && message.wParam) {
                closeOnShow = false; SendMessageW(message.hwnd, WM_CLOSE, 0, 0);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
void Lifecycle(HINSTANCE instance) {
    const auto hook = SetWindowsHookExW(WH_CALLWNDPROCRET, Observe, nullptr, GetCurrentThreadId());
    Check(hook != nullptr, "install isolated window probe");
    UpdatePresenter presenter(instance, {});
    const auto initialForeground = GetForegroundWindow();
    auto automatic = Notice(); automatic.manual = false;
    Check(!presenter.Show(automatic) && !presenter.Handle() && !presenter.HasVisible(),
          "automatic checks cannot create a native or topmost update window");
    Check(GetForegroundWindow() == initialForeground, "silent automatic checks preserve foreground");
    denySurface = true;
    Check(!presenter.Show(Notice()), "failed foreground upload must not report success");
    Check(!presenter.HasVisible() && !presenter.Handle(), "failed show cleans up");
    denySurface = false;
    closeOnShow = true;
    Check(!presenter.Show(Notice()), "synchronous close during show must not report visible");
    Check(presenter.Show(Notice()), "show returns true for a visible notification");
    const auto window = presenter.Handle();
    Check(presenter.HasVisible() && IsWindowVisible(window), "show is not merely asynchronous acceptance");
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    Check((style & WS_EX_TOOLWINDOW) && !(style & WS_EX_NOACTIVATE), "explicit manual notice supports keyboard input");
    Check(!(style & WS_EX_APPWINDOW), "notification does not add a taskbar button");
    RECT rect{}; GetWindowRect(window, &rect);
    MONITORINFO info{}; info.cbSize = sizeof(info);
    Check(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info), "query fixture monitor");
    Check(rect.right <= info.rcWork.right && rect.bottom <= info.rcWork.bottom &&
          rect.left >= info.rcWork.left && rect.top >= info.rcWork.top, "notice stays inside work area");
    Check(presenter.Show(Notice(L"1.0.8")) && presenter.Handle() == window, "repeated show updates the same HWND");
    wchar_t title[256]{}; GetWindowTextW(window, title, 256);
    Check(std::wstring(title).find(L"1.0.8") != std::wstring::npos, "updated version reaches native window text");
    Click(presenter, 2);
    Check(!presenter.HasVisible() && !IsWindow(window), "later dismisses without shutdown");
    Check(presenter.Show(Notice()), "can show after later");
    SendMessageW(presenter.Handle(), WM_CLOSE, 0, 0);
    Check(!presenter.HasVisible(), "close dismisses immediately");
    Check(presenter.Show(Notice()), "can show after close");
    const auto doomed = presenter.Handle();
    PostMessageW(doomed, LumeMaterialStatusChangedMessage, 0, 0);
    PostMessageW(doomed, WM_TIMER, 1, 0);
    presenter.Shutdown(); presenter.Shutdown(); Pump();
    Check(!presenter.Handle() && !IsWindow(doomed) && !presenter.Show(Notice()), "shutdown is terminal and ignores late messages");
    UnhookWindowsHookEx(hook);
}
void Actions(HINSTANCE instance) {
    int opens = 0;
    std::unique_ptr<UpdatePresenter> dying;
    dying = std::make_unique<UpdatePresenter>(instance, [&] { ++opens; dying.reset(); });
    Check(dying->Show(Notice()), "callback fixture shows");
    const auto doomed = dying->Handle();
    Click(*dying, 1);
    Check(!dying && opens == 1 && !IsWindow(doomed), "website callback can destroy presenter synchronously");
    UpdatePresenter throwing(instance, [] { throw std::runtime_error("fixture"); });
    Check(throwing.Show(Notice()), "throwing callback fixture shows");
    Click(throwing, 1);
    Check(!throwing.HasVisible(), "callback exception never escapes window procedure");
    UpdatePresenter replacement(instance, [&] { ++opens; });
    auto manual = Notice(); manual.manual = true;
    Check(replacement.Show(manual), "manual notice shows");
    auto automatic = Notice(L"9.0"); automatic.manual = false;
    Check(!replacement.Show(automatic), "automatic notices cannot replace an explicit manual result");
    Check((GetWindowLongPtrW(replacement.Handle(), GWL_EXSTYLE) & WS_EX_NOACTIVATE) == 0,
          "ignored automatic completion does not change the manual window");
    Check(replacement.Show(manual), "manual keyboard mode can be restored");
    SendMessageW(replacement.Handle(), WM_KEYDOWN, VK_RETURN, 0);
    Pump();
    Check(opens == 2 && !replacement.HasVisible(), "manual Enter invokes the website once");
    Check(replacement.Show(manual), "manual escape fixture shows");
    SendMessageW(replacement.Handle(), WM_KEYDOWN, VK_ESCAPE, 0);
    Check(!replacement.HasVisible(), "escape dismisses");
    UpdatePresenter* self = nullptr;
    UpdatePresenter reentrant(instance, [&] { Check(self->Show(Notice(L"2.0")), "callback may show a new notice"); });
    self = &reentrant;
    Check(reentrant.Show(Notice()), "reentrant fixture shows");
    Click(reentrant, 1);
    Check(reentrant.HasVisible(), "old action does not close the new notification");
    UpdatePresenter queued(instance, [&] { ++opens; });
    Check(queued.Show(manual), "queued action fixture shows");
    SendMessageW(queued.Handle(), WM_KEYDOWN, VK_RETURN, 0);
    Check(queued.Show(Notice(L"3.0")), "new content supersedes a queued action");
    Pump();
    Check(opens == 2 && queued.HasVisible(), "queued old action cannot open or dismiss the new version");
    queued.SetLanguage(true);
    wchar_t title[256]{}; GetWindowTextW(queued.Handle(), title, 256);
    Check(std::wstring(title).find(L"New version") != std::wstring::npos, "language refresh updates accessible native title");
    auto result = Notice(L""); result.manual = true;
    result.detail = L"Already up to date.";
    Check(queued.Show(result), "manual check result does not require a new version");
    GetWindowTextW(queued.Handle(), title, 256);
    Check(std::wstring(title).find(L"Check for updates") != std::wstring::npos,
          "current or failed checks are not labelled as a new version");
    Check(!queued.Show(automatic), "automatic notices are rejected even with a valid version");
    const auto hit = UpdatePresenterTestAccess::ActionRect(queued, 1);
    const auto point = MAKELPARAM((hit.left + hit.right) / 2, (hit.top + hit.bottom) / 2);
    SendMessageW(queued.Handle(), WM_LBUTTONDOWN, 0, point);
    Check(queued.Show(Notice(L"4.0")), "new version can arrive while a pointer is pressed");
    SendMessageW(queued.Handle(), WM_LBUTTONUP, 0, point); Pump();
    Check(opens == 2 && queued.HasVisible(), "a press on old content cannot activate new content");
}
void LayoutAndTimeout(HINSTANCE instance) {
    for (const auto dpi : {96u, 144u, 192u}) {
        UpdatePresenter presenter(instance, {});
        const RECT work{100, 100, 420, 800};
        Check(UpdatePresenterTestAccess::Environment(presenter, work, dpi, 10000, 3000), "isolated work area override");
        auto notice = Notice(L"2026.12345678901234567890");
        notice.detail = std::wstring(600, L'\x4e2d') + L" long_unbroken_release_description_012345678901234567890123456789";
        Check(presenter.Show(notice), "narrow high-DPI notification fits");
        RECT outer{}; GetWindowRect(presenter.Handle(), &outer);
        Check(outer.left >= work.left && outer.top >= work.top && outer.right <= work.right && outer.bottom <= work.bottom,
              "DPI layout never exceeds work area");
        RECT client{}; GetClientRect(presenter.Handle(), &client);
        for (unsigned action : {1u, 2u, 3u}) {
            const auto rect = UpdatePresenterTestAccess::ActionRect(presenter, action);
            Check(rect.left >= 0 && rect.top >= 0 && rect.right <= client.right && rect.bottom <= client.bottom,
                  "actions are not clipped on narrow screens");
        }
        Check(UpdatePresenterTestAccess::CanScroll(presenter), "long detail remains available via scrolling");
        Click(presenter, 4);
        Check(UpdatePresenterTestAccess::ScrollOffset(presenter) > 0,
              "inactive toast detail can be scrolled by pointer without stealing focus");
        SendMessageW(presenter.Handle(), WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(presenter.Handle(), WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        Check(UpdatePresenterTestAccess::ScrollOffset(presenter) > 0, "mouse wheel exposes the rest of detail");
        SendMessageW(presenter.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&outer));
        Check(presenter.HasVisible(), "DPI event keeps a usable notification");
    }
    UpdatePresenter tiny(instance, {});
    Check(UpdatePresenterTestAccess::Environment(tiny, {0, 0, 80, 80}, 192, 10000, 3000), "tiny work area");
    Check(!tiny.Show(Notice()) && !tiny.Handle(), "impossible layout is rejected, not clipped");
    UpdatePresenter timed(instance, {});
    Check(UpdatePresenterTestAccess::Environment(timed, {100, 100, 640, 640}, 96, 100, 3000), "short isolated lifetime");
    Check(timed.Show(Notice()), "timed notification shows");
    Pump(60);
    const auto oldTimer = UpdatePresenterTestAccess::DismissTimer(timed);
    Check(timed.Show(Notice(L"1.0.8")), "repeat refreshes lifetime");
    SendMessageW(timed.Handle(), WM_TIMER, oldTimer, 0);
    Check(timed.HasVisible(), "stale timeout cannot dismiss a refreshed notice");
    Pump(50); Check(timed.HasVisible(), "new notice gets a full lifetime");
    Pump(100); Check(!timed.HasVisible() && !timed.Handle(), "timeout destroys window and lease");
}
void Material(HINSTANCE instance, bool fixture) {
    HMODULE dll = fixture ? LoadLibraryW(L"LumeGlass.dll") : nullptr;
    using Change = int (__cdecl*)(HWND, int);
    using Frosted = int (__cdecl*)(HWND);
    const auto change = fixture ? Resolve<Change>(dll, "LumeUpdateTestStatus") : nullptr;
    const auto frosted = fixture ? Resolve<Frosted>(dll, "LumeUpdateTestFrosted") : nullptr;
    const auto stats = fixture ? Resolve<LumeMaterialStatisticsFn>(dll, "LumeMaterialStatistics") : nullptr;
    UpdatePresenter presenter(instance, {});
    Check(presenter.Show(Notice()), "material fixture initially shows readable content");
    Check(!GetPropW(presenter.Handle(), L"LumeUpdateGlassActive"), "attach alone is not real glass readiness");
    Check(UpdatePresenterTestAccess::PixelAlpha(presenter, 10, 50) == 255, "pending or missing glass has opaque readable backing");
    if (fixture) {
        Check(frosted(presenter.Handle()) != 0, "shared runtime uses the frosted panel optical profile");
        Check(change(presenter.Handle(), 1) != 0, "publish real-frame fixture status"); Pump(80);
        Check(GetPropW(presenter.Handle(), L"LumeUpdateGlassActive") != nullptr, "only presented frames enable glass");
        Check(UpdatePresenterTestAccess::PixelAlpha(presenter, 10, 50) < 8, "ready foreground is transparent, not an opaque imitation");
        Check(change(presenter.Handle(), 2) != 0, "publish failed status"); Pump();
        Check(!GetPropW(presenter.Handle(), L"LumeUpdateGlassActive"), "failed material leaves glass mode");
        Check(UpdatePresenterTestAccess::PixelAlpha(presenter, 10, 50) == 255, "failed glass restores an opaque readable body");
    }
    presenter.Shutdown();
    if (fixture) {
        LumeMaterialStatisticsData s{}; s.size = sizeof(s);
        Check(stats(&s) && s.users == 0, "shutdown releases all material leases");
        UpdatePresenter timeout(instance, {});
        Check(UpdatePresenterTestAccess::Environment(timeout, {100, 100, 640, 640}, 96, 10000, 40), "bounded first-frame wait");
        Check(timeout.Show(Notice()), "pending material still has a visible fallback");
        Pump(70);
        Check(timeout.HasVisible() && !GetPropW(timeout.Handle(), L"LumeUpdateGlassActive"), "material timeout preserves readable notification");
        Check(stats(&s) && s.users == 0, "first-frame timeout retires the stalled material");
        timeout.Shutdown(); FreeLibrary(dll);
    }
}
bool alternate = false;
LRESULT CALLBACK Background(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{}; const auto dc = BeginPaint(window, &ps);
        RECT rect{}; GetClientRect(window, &rect);
        for (int y = 0; y < rect.bottom; y += 48) for (int x = 0; x < rect.right; x += 48) {
            const bool light = ((x / 48 + y / 48) % 2 == 0) ^ alternate;
            const auto brush = CreateSolidBrush(light ? RGB(220, 175, 55) : RGB(28, 95, 165));
            RECT cell{x, y, x + 48, y + 48}; FillRect(dc, &cell, brush); DeleteObject(brush);
        }
        EndPaint(window, &ps); return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
std::vector<std::uint32_t> Capture(HWND window, const std::string& path) {
    RECT rect{}; Check(GetWindowRect(window, &rect), "own window screenshot bounds");
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    const auto screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr; const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    Check(bitmap && bits, "allocate screenshot");
    const auto old = SelectObject(dc, bitmap);
    Check(BitBlt(dc, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT), "capture own fixture region");
    GdiFlush();
    std::vector<std::uint32_t> pixels(static_cast<std::uint32_t*>(bits), static_cast<std::uint32_t*>(bits) + width * height);
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42; header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + width * height * 4;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(static_cast<const char*>(bits), width * height * 4);
    Check(file.good(), "write isolated screenshot");
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr, screen);
    return pixels;
}
void Visual(HINSTANCE instance, const std::string& output) {
    const auto library = LoadLibraryW(L"LumeGlass.dll");
    Check(library != nullptr, "real material DLL is staged next to the isolated exe");
    const auto stats = Resolve<LumeMaterialStatisticsFn>(library, "LumeMaterialStatistics");
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    Check(GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor), "visual fixture work area");
    RECT area{std::max(monitor.rcWork.left, monitor.rcWork.right - 560),
              std::max(monitor.rcWork.top, monitor.rcWork.bottom - 540), monitor.rcWork.right, monitor.rcWork.bottom};
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = Background; wc.lpszClassName = L"LumeUpdateIsolatedBackdrop";
    RegisterClassW(&wc);
    const auto background = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"Isolated update material verification", WS_POPUP,
        area.left, area.top, area.right - area.left, area.bottom - area.top, nullptr, nullptr, instance, nullptr);
    Check(background != nullptr, "create only a test-owned patterned backdrop");
    ShowWindow(background, SW_SHOWNOACTIVATE); UpdateWindow(background); Pump(150);
    UpdatePresenter presenter(instance, [] {});
    Check(UpdatePresenterTestAccess::Environment(presenter, area, GetDpiForWindow(background), 20000, 8000), "real material test bounds");
    Check(presenter.Show(Notice()), "real material notification shows");
    Capture(background, output + "/initial-readable.bmp");
    const auto deadline = GetTickCount64() + 9000;
    while (!GetPropW(presenter.Handle(), L"LumeUpdateGlassActive") && GetTickCount64() < deadline) Pump(20);
    Check(GetPropW(presenter.Handle(), L"LumeUpdateGlassActive") != nullptr, "real DLL produces a presented glass frame");
    struct SurfaceSearch { HWND owner; HWND surface = nullptr; } search{presenter.Handle()};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<SurfaceSearch*>(data);
        const auto owner = reinterpret_cast<HWND>(GetPropW(window, L"LumeGlassSurfaceOwner"));
        if (owner == search.owner) { search.surface = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    Check(search.surface != nullptr, "real DLL supplies a separate glass back surface");
    Pump(250);
    const auto below = GetWindow(presenter.Handle(), GW_HWNDNEXT);
    wchar_t belowName[128]{}; if (below) GetClassNameW(below, belowName, 128);
    std::wcout << L"owner=" << presenter.Handle() << L" glass=" << search.surface << L" below=" << below
        << L" (" << belowName << L") styles=" << std::hex << GetWindowLongPtrW(presenter.Handle(), GWL_EXSTYLE)
        << L"/" << GetWindowLongPtrW(search.surface, GWL_EXSTYLE) << std::dec << L'\n';
    Capture(presenter.Handle(), output + "/layer-order.bmp");
    Check(IsWindowVisible(search.surface), "glass back surface is actually visible");
    auto nextVisible = GetWindow(presenter.Handle(), GW_HWNDNEXT);
    while (nextVisible && !IsWindowVisible(nextVisible)) nextVisible = GetWindow(nextVisible, GW_HWNDNEXT);
    Check(nextVisible == search.surface,
          "glass back surface is immediately beneath transparent foreground");
    const auto before = Capture(presenter.Handle(), output + "/glass-before.bmp");
    RECT bounds{}; GetClientRect(presenter.Handle(), &bounds);
    std::size_t opticalPixels = 0, sampled = 0;
    for (int y = bounds.bottom * 65 / 100; y < bounds.bottom * 73 / 100; ++y)
        for (int x = 16; x < bounds.right - 16; ++x) {
            const auto p = before[static_cast<std::size_t>(y) * bounds.right + x];
            const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            const int yellow = std::abs(r - 220) + std::abs(g - 175) + std::abs(b - 55);
            const int blue = std::abs(r - 28) + std::abs(g - 95) + std::abs(b - 165);
            ++sampled;
            if (std::min(yellow, blue) > 24) ++opticalPixels;
        }
    Check(sampled && opticalPixels > sampled / 5,
          "empty foreground region contains real optical blur, not the unprocessed backdrop");
    alternate = true; InvalidateRect(background, nullptr, FALSE); UpdateWindow(background); Pump(700);
    const auto after = Capture(presenter.Handle(), output + "/glass-after.bmp");
    Check(before.size() == after.size(), "captured surface dimensions are stable");
    std::size_t changed = 0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        const int a = static_cast<int>(before[i] & 255), b = static_cast<int>(after[i] & 255);
        if (std::abs(a - b) > 12) ++changed;
    }
    Check(changed > before.size() / 8, "real glass pixels respond to the test backdrop, not a static fill");
    LumeMaterialStatisticsData status{}; status.size = sizeof(status);
    Check(stats(&status) && status.users == 1 && status.devices > 0 && status.captures > 0, "real material owns capture/device resources");
    std::cout << "real glass changed pixels: " << changed << '/' << before.size() << '\n';
    std::cout << "real optical pixels: " << opticalPixels << '/' << sampled << '\n';
    presenter.SetLanguage(true); Pump(80); Capture(presenter.Handle(), output + "/glass-english.bmp");
    presenter.Shutdown(); Pump(100);
    Check(stats(&status) && !status.users && !status.devices && !status.captures, "real material fully releases on shutdown");
    DestroyWindow(background); FreeLibrary(library);
    std::cout << "real material verification passed\n";
}
}
int main(int argc, char** argv) {
    try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto instance = GetModuleHandleW(nullptr);
    const bool fixture = argc > 1 && std::string(argv[1]) == "--fixture";
    if (argc > 2 && std::string(argv[1]) == "--real-material") {
        Visual(instance, argv[2]); return 0;
    }
    Lifecycle(instance); Actions(instance); LayoutAndTimeout(instance); Material(instance, fixture);
    std::cout << "update_presenter_test passed (" << (fixture ? "material ABI fixture" : "missing material fallback") << ")\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
#endif
#endif
