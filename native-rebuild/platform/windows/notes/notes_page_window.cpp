#include "platform/windows/notes/notes_page_window.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/lume_resource.h"
#include "platform/windows/webview_com_support.h"
#include "platform/windows/material/widget_material_runtime.h"
#include "platform/windows/rounded_window_surface.h"
#include "platform/windows/widget_ui/widget_snap_controller.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include <gdiplus.h>
#include "vendor/nlohmann/json.hpp"
#include <WebView2.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <filesystem>
#include <windowsx.h>

namespace lume::platform::windows {
using namespace webview_detail;
namespace {
constexpr wchar_t kClass[] = L"LumeNotesWindow";
constexpr wchar_t kStickyClass[] = L"LumeStickyNoteWindow";
constexpr int kContextMenu = -20;
constexpr wchar_t kPageUrl[] = L"https://lume.local/notes.html";
constexpr UINT kDeliver = WM_APP + 0x6e1;
constexpr UINT_PTR kCloseTimer = 1;
constexpr float kCornerRadius = 22.f;
constexpr auto kWebView3Id = L"{A0D6DF20-3B92-416D-AA0C-437A9C727857}";
constexpr auto kController2Id = L"{c979903e-d4ca-4228-92eb-47ee3fa96eab}";
void DesktopLevel(HWND window) { desktop_level::Place(window, false); }
std::filesystem::path ExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}
// The manager's profile and environment options, so both windows share one browser process.
std::filesystem::path ProfileDirectory() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
        return {};
    const auto path = std::filesystem::path(local) / L"Lume" / L"WebView2";
    CoTaskMemFree(local);
    return path;
}
bool IsPageUrl(const wchar_t *value) {
    if (!value)
        return false;
    const std::wstring_view uri(value), base(kPageUrl);
    return uri == base || (uri.size() > base.size() && uri.substr(0, base.size()) == base && uri[base.size()] == L'#');
}
bool IsCloseReady(const std::wstring &json) {
    if (json.size() > 256)
        return false;
    try {
        std::string ascii;
        for (const auto c : json) {
            if (c > 127)
                return false;
            ascii.push_back(static_cast<char>(c));
        }
        const auto value = nlohmann::json::parse(ascii);
        return value.is_object() && value.size() == 3 && value.value("version", 0) == 1 &&
               value.value("scope", std::string{}) == "notes" && value.value("action", std::string{}) == "closeReady";
    } catch (...) {
        return false;
    }
}
int WindowAction(const std::wstring &json) {
    if (json.size() > 256)
        return 0;
    try {
        std::string ascii;
        for (const auto c : json) {
            if (c > 127)
                return 0;
            ascii.push_back(static_cast<char>(c));
        }
        const auto value = nlohmann::json::parse(ascii);
        if (!value.is_object() || value.value("version", 0) != 1 || value.value("type", std::string{}) != "notes-window")
            return 0;
        const auto action = value.value("action", std::string{});
        if (value.size() == 3)
            return action == "close" ? HTCLOSE : action == "drag" ? HTCAPTION : action == "menu" ? kContextMenu : 0;
        if (value.size() != 4 || action != "resize")
            return 0;
        const auto edge = value.value("edge", std::string{});
        if (edge == "n") return HTTOP;
        if (edge == "ne") return HTTOPRIGHT;
        if (edge == "e") return HTRIGHT;
        if (edge == "se") return HTBOTTOMRIGHT;
        if (edge == "s") return HTBOTTOM;
        if (edge == "sw") return HTBOTTOMLEFT;
        if (edge == "w") return HTLEFT;
        if (edge == "nw") return HTTOPLEFT;
    } catch (...) {}
    return 0;
}
} // namespace

struct NotesPageWindow::State : std::enable_shared_from_this<State> {
    HWND hwnd = nullptr;
    HMODULE loader = nullptr;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    MessageHandler onMessage;
    std::function<void()> closed;
    NotesPageOptions options;
    bool Compact() const { return !options.noteId.empty(); }
    widget::Preferences Bounds() const {
        auto result = options.preferences;
        RECT rect{};
        if (hwnd && GetWindowRect(hwnd, &rect)) {
            const auto dpi = static_cast<int>(GetDpiForWindow(hwnd));
            result.x = rect.left; result.y = rect.top; result.placed = true;
            result.width = MulDiv(rect.right - rect.left, 96, dpi ? dpi : 96);
            result.height = MulDiv(rect.bottom - rect.top, 96, dpi ? dpi : 96);
        }
        return result;
    }
    void Menu() {
        if (!Compact() || !hwnd || closing) return;
        POINT point{}; GetCursorPos(&point);
        Gdiplus::GdiplusStartupInput input; ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return;
        const auto command = LumeWidgetMenu::ShowNotes(hwnd, point, options.preferences.frosted, options.preferences.locked);
        Gdiplus::GdiplusShutdown(token);
        if (hwnd && !closing && options.menu) options.menu(command);
    }
    std::deque<std::wstring> inbox;
    WidgetMaterialRuntime material;
    WidgetSnapController snap;
    POINT moveCursor{};
    RECT moveOrigin{};
    bool moveAnchored = false;
    bool ready = false, closing = false, closeRequested = false;
    bool transparentHost = false, transparentView = false;

    void UpdateMaterial() {
        if (!hwnd || closing || !transparentHost || !transparentView)
            return;
        LumeMaterialParameters p{};
        p.size = sizeof(p);
        p.owner = hwnd;
        p.frosted = !Compact() || options.preferences.frosted ? 1u : 0u;
        p.opticalProfile = 1;
        p.lightForeground = 1;
        p.cornerRadius = kCornerRadius;
        BOOL animation = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0);
        p.reducedMotion = animation ? 0u : 1u;
        if (material.State() == WidgetMaterialState::Detached)
            material.Attach(p);
        else
            material.Update(p);
        PostMaterialStatus();
    }

    void PostMaterialStatus() {
        if (!ready || !view || closing)
            return;
        const auto state = transparentHost && transparentView ? material.State() : WidgetMaterialState::TransparentFallback;
        const wchar_t *status = state == WidgetMaterialState::Ready ? L"ready" :
                                state == WidgetMaterialState::Starting ? L"starting" : L"fallback";
        const std::wstring json = std::wstring(L"{\"version\":1,\"type\":\"notes-material\",\"status\":\"") + status + L"\"}";
        view->PostWebMessageAsJson(json.c_str());
    }

    void Layout() {
        if (!controller || !hwnd)
            return;
        RECT client{};
        GetClientRect(hwnd, &client);
        controller->put_Bounds(client);
    }
    void Release() {
        closing = true;
        ready = false;
        snap.Clear();
        material.Detach();
        if (controller)
            controller->Close();
        controller.Reset();
        view.Reset();
        environment.Reset();
        if (loader)
            FreeLibrary(loader);
        loader = nullptr;
        inbox.clear();
    }
    void Attach(ICoreWebView2Controller *created) {
        controller.CopyFrom(created);
        if (FAILED(controller->get_CoreWebView2(view.Put())) || !view) {
            Fail();
            return;
        }
        ComPtr<ICoreWebView2_3> mapped;
        if (FAILED(view->QueryInterface(InterfaceId(kWebView3Id), reinterpret_cast<void **>(mapped.Put()))) ||
            FAILED(mapped->SetVirtualHostNameToFolderMapping(L"lume.local", (ExecutableDirectory() / L"ui").c_str(),
                                                             COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
            Fail();
            return;
        }
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(view->get_Settings(settings.Put()))) {
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            settings->put_AreDefaultScriptDialogsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
        }
        // The page foreground reveals the shared native LumeGlass surface.
        ComPtr<ICoreWebView2Controller2> colored;
        transparentView = SUCCEEDED(controller->QueryInterface(InterfaceId(kController2Id), reinterpret_cast<void **>(colored.Put()))) &&
                          SUCCEEDED(colored->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0, 0, 0, 0}));
        UpdateMaterial();
        const auto weak = weak_from_this();
        EventRegistrationToken token{};
        auto message = Handler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2 *,
                               ICoreWebView2WebMessageReceivedEventArgs *>(
            L"{57213f19-00e6-49fa-8e07-898ea01ecbd2}", [weak](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) {
                const auto self = weak.lock();
                if (!self || self->closing)
                    return S_OK;
                LPWSTR source = nullptr, data = nullptr;
                if (SUCCEEDED(args->get_Source(&source)) && IsPageUrl(source) &&
                    SUCCEEDED(args->get_WebMessageAsJson(&data)) && data) {
                    // A message proves the page script is live; it can receive replies now.
                    self->ready = true;
                    self->PostMaterialStatus();
                    const auto length = wcsnlen(data, 8193);
                    // Deliver after this COM event returns, bounded like the manager.
                    if (length <= 8192 && self->inbox.size() < 64) {
                        self->inbox.emplace_back(data, length);
                        if (!PostMessageW(self->hwnd, kDeliver, 0, 0))
                            self->inbox.pop_back();
                    }
                }
                CoTaskMemFree(source);
                CoTaskMemFree(data);
                return S_OK;
            });
        view->add_WebMessageReceived(message.Get(), &token);
        auto navigated = Handler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2 *,
                                 ICoreWebView2NavigationCompletedEventArgs *>(
            L"{30d68b7d-20d9-4752-a9ca-ec8448fbb5c1}", [weak](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) {
                const auto self = weak.lock();
                BOOL ok = FALSE;
                if (self && !self->closing && SUCCEEDED(args->get_IsSuccess(&ok)))
                    self->ready = ok != FALSE;
                if (self && self->ready)
                    self->PostMaterialStatus();
                return S_OK;
            });
        view->add_NavigationCompleted(navigated.Get(), &token);
        // Pages and local files only: no navigation away from the notes page.
        auto starting = Handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2 *,
                                ICoreWebView2NavigationStartingEventArgs *>(
            L"{9adbe429-f36d-432b-9ddc-f8881fbd76e3}", [](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                if (!IsPageUrl(uri))
                    args->put_Cancel(TRUE);
                CoTaskMemFree(uri);
                return S_OK;
            });
        view->add_NavigationStarting(starting.Get(), &token);
        auto windows = Handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2 *,
                               ICoreWebView2NewWindowRequestedEventArgs *>(
            L"{d4c185fe-c81c-4989-97af-2d3fa7ab5651}", [](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) {
                args->put_Handled(TRUE);
                return S_OK;
            });
        view->add_NewWindowRequested(windows.Get(), &token);
        Layout();
        controller->put_IsVisible(TRUE);
        const auto url = std::wstring(kPageUrl) + (Compact() ? L"#sticky=" + std::wstring(options.noteId.begin(), options.noteId.end()) + (options.edit ? L"&edit=1" : L"") : L"");
        if (FAILED(view->Navigate(url.c_str())))
            Fail();
    }
    void Fail() {
        if (hwnd && !closing)
            MessageBoxW(hwnd, L"便签页面无法加载，请检查 WebView2 Runtime 与安装包内的界面资源。", L"Lume 便签",
                        MB_OK | MB_ICONWARNING);
        if (hwnd)
            DestroyWindow(hwnd);
    }
    std::filesystem::path profile;
    bool Initialize() {
        const auto directory = ExecutableDirectory();
        std::error_code ec;
        if (!std::filesystem::is_regular_file(directory / L"ui" / L"notes.html", ec))
            return false;
        loader = LoadLibraryExW((directory / L"WebView2Loader.dll").c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!loader)
            return false;
        using Create = HRESULT(STDAPICALLTYPE *)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
                                                 ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
        auto address = GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions");
        Create create = nullptr;
        static_assert(sizeof(create) == sizeof(address));
        std::memcpy(&create, &address, sizeof(create));
        if (profile.empty())
            profile = ProfileDirectory();
        if (!create || profile.empty())
            return false;
        auto created = Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment *>(
            L"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}", [self = shared_from_this()](HRESULT result, ICoreWebView2Environment *created) {
                if (self->closing)
                    return S_OK;
                if (FAILED(result) || !created) {
                    self->Fail();
                    return S_OK;
                }
                self->environment.CopyFrom(created);
                auto controllerReady = Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                                               ICoreWebView2Controller *>(
                    L"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}", [self](HRESULT hr, ICoreWebView2Controller *made) {
                        if (self->closing) {
                            if (made)
                                made->Close();
                            return S_OK;
                        }
                        if (FAILED(hr) || !made)
                            self->Fail();
                        else
                            self->Attach(made);
                        return S_OK;
                    });
                if (FAILED(created->CreateCoreWebView2Controller(self->hwnd, controllerReady.Get())))
                    self->Fail();
                return S_OK;
            });
        return SUCCEEDED(create(nullptr, profile.c_str(), nullptr, created.Get()));
    }
    void Deliver() {
        while (!inbox.empty() && !closing) {
            auto message = std::move(inbox.front());
            inbox.pop_front();
            const int action = WindowAction(message);
            if (action) {
                if (action == kContextMenu) Menu();
                else if (action == HTCLOSE)
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                else if (!Compact() || !options.preferences.locked) {
                    POINT point{};
                    // WebView messages are asynchronous. A released pointer
                    // must not start a sticky native move/resize operation.
                    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && GetCursorPos(&point)) {
                        ReleaseCapture();
                        SendMessageW(hwnd, WM_NCLBUTTONDOWN, static_cast<WPARAM>(action), MAKELPARAM(point.x, point.y));
                    }
                }
                continue;
            }
            if (IsCloseReady(message)) {
                if (closeRequested && hwnd)
                    DestroyWindow(hwnd);
                continue;
            }
            const auto handler = onMessage;
            if (handler)
                handler(message);
        }
    }
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto *raw = reinterpret_cast<State *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            raw = static_cast<State *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
            raw->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(raw));
        }
        if (!raw)
            return DefWindowProcW(hwnd, message, wparam, lparam);
        const auto self = raw->shared_from_this();
        if (message == desktop_level::Message()) {
            // Only resting sticky notes live at desktop level; never push down
            // the full page or a note being edited.
            if (self->Compact() && !self->options.edit && GetForegroundWindow() != hwnd && IsWindowVisible(hwnd))
                DesktopLevel(hwnd);
            return 0;
        }
        switch (message) {
        case WM_NCHITTEST: {
            if (self->Compact() && self->options.preferences.locked) return HTCLIENT;
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &point);
            RECT client{};
            GetClientRect(hwnd, &client);
            const int edge = MulDiv(8, static_cast<int>(GetDpiForWindow(hwnd)), 96);
            const bool left = point.x < edge, right = point.x >= client.right - edge;
            const bool top = point.y < edge, bottom = point.y >= client.bottom - edge;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            return HTCLIENT;
        }
        case WM_ENTERSIZEMOVE:
            widget_snap::BeginInteraction(hwnd);
            self->moveAnchored = GetWindowRect(hwnd, &self->moveOrigin) && GetCursorPos(&self->moveCursor);
            return 0;
        case WM_MOVING: {
            auto &bounds = *reinterpret_cast<RECT *>(lparam);
            POINT cursor{};
            if (self->moveAnchored && GetCursorPos(&cursor)) {
                const auto width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
                bounds.left = self->moveOrigin.left + cursor.x - self->moveCursor.x;
                bounds.top = self->moveOrigin.top + cursor.y - self->moveCursor.y;
                bounds.right = bounds.left + width;
                bounds.bottom = bounds.top + height;
            }
            self->snap.Move(bounds);
            return TRUE;
        }
        case WM_SIZING:
            self->moveAnchored = false;
            self->snap.Resize(*reinterpret_cast<RECT *>(lparam), wparam);
            return TRUE;
        case WM_EXITSIZEMOVE:
            if (self->Compact() && self->options.boundsChanged) self->options.boundsChanged(self->Bounds());
            self->moveAnchored = false;
            self->snap.Clear();
            return 0;
        case WM_CANCELMODE:
        case WM_CAPTURECHANGED:
        case widget_snap::CancelMessage:
            self->moveAnchored = false;
            self->snap.Clear();
            return 0;
        case WM_SIZE:
            if (self->controller)
                self->controller->put_IsVisible(wparam != SIZE_MINIMIZED);
            self->Layout();
            self->UpdateMaterial();
            return 0;
        case WM_MOVE:
            if (self->controller)
                self->controller->NotifyParentWindowPositionChanged();
            self->UpdateMaterial();
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            self->UpdateMaterial();
            return 0;
        case WM_GETMINMAXINFO: {
            const auto dpi = GetDpiForWindow(hwnd);
            auto *info = reinterpret_cast<MINMAXINFO *>(lparam);
            info->ptMinTrackSize = {MulDiv(self->Compact() ? 220 : 720, static_cast<int>(dpi ? dpi : 96), 96),
                                    MulDiv(self->Compact() ? 160 : 480, static_cast<int>(dpi ? dpi : 96), 96)};
            if (self->Compact()) info->ptMaxTrackSize = {MulDiv(720, static_cast<int>(dpi ? dpi : 96), 96), MulDiv(720, static_cast<int>(dpi ? dpi : 96), 96)};
            return 0;
        }
        case WM_DPICHANGED: {
            const auto *rect = reinterpret_cast<RECT *>(lparam);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ConfigureRoundedWindow(hwnd, kCornerRadius * static_cast<float>(HIWORD(wparam)) / 96.f, true);
            if (self->moveAnchored) {
                self->moveAnchored = GetWindowRect(hwnd, &self->moveOrigin) && GetCursorPos(&self->moveCursor);
                self->snap.Clear();
            }
            return 0;
        }
        case WM_ERASEBKGND: {
            // Alpha-zero black lets the transparent WebView reveal LumeGlass.
            RECT client{};
            GetClientRect(hwnd, &client);
            FillRect(reinterpret_cast<HDC>(wparam), &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return 1;
        }
        case kDeliver:
            self->Deliver();
            return 0;
        case LumeMaterialStatusChangedMessage:
            self->PostMaterialStatus();
            return 0;
        case WM_CLOSE:
            if (self->ready) {
                self->closeRequested = true;
                self->Post(L"{\"version\":1,\"type\":\"notes-close\"}");
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_TIMER:
            if (wparam == kCloseTimer)
                DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            KillTimer(hwnd, kCloseTimer);
            self->Release();
            RemovePropW(hwnd, widget_snap::Capability);
            RemovePropW(hwnd, lume::window_ids::DesktopWidgetProp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            self->hwnd = nullptr;
            auto closed = std::move(self->closed);
            self->onMessage = {};
            if (closed)
                closed();
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }
    void Post(const std::wstring &json) {
        if (closeRequested) {
            try {
                const auto message = nlohmann::json::parse(json);
                if (message.value("type", std::string{}) == "notes-state" &&
                    !message.value("savedId", std::string{}).empty() && !message.value("saved", true)) closeRequested = false;
            } catch (...) {}
        }
        if (ready && view && !closing)
            view->PostWebMessageAsJson(json.c_str());
    }
};

NotesPageWindow::NotesPageWindow() = default;
NotesPageWindow::~NotesPageWindow() { Close(); }
bool NotesPageWindow::Open(HINSTANCE instance, MessageHandler onMessage, std::function<void()> closed,
                           const std::wstring &profile, NotesPageOptions options) {
    if (IsOpen()) {
        const auto hwnd = state_->hwnd;
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd);
        return true;
    }
    auto state = std::make_shared<State>();
    state->onMessage = std::move(onMessage);
    state->closed = std::move(closed);
    state->profile = profile;
    state->options = std::move(options);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = State::Proc;
    wc.lpszClassName = state->Compact() ? kStickyClass : kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_LUME_APP));
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    // Opens as the full page: 1100x720 DIP centred on the work area under the cursor.
    POINT cursor{};
    GetCursorPos(&cursor);
    if (state->Compact() && state->options.preferences.placed)
        cursor = {state->options.preferences.x, state->options.preferences.y};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const auto area = monitor.rcWork;
    UINT dpiX = 96, dpiY = 96;
    using MonitorDpi = HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
    if (const auto shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        auto address = GetProcAddress(shcore, "GetDpiForMonitor");
        MonitorDpi query = nullptr;
        std::memcpy(&query, &address, sizeof(query));
        if (query)
            query(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), 0, &dpiX, &dpiY);
        FreeLibrary(shcore);
    }
    const auto &preferences = state->options.preferences;
    const int width = std::min(MulDiv(state->Compact() ? preferences.width : 1100, static_cast<int>(dpiX), 96), static_cast<int>(area.right - area.left));
    const int height = std::min(MulDiv(state->Compact() ? preferences.height : 720, static_cast<int>(dpiX), 96), static_cast<int>(area.bottom - area.top));
    const bool placed = state->Compact() && preferences.placed;
    const HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Lume 便签", WS_POPUP,
                                      placed ? std::clamp(preferences.x, static_cast<int>(area.left), static_cast<int>(area.right) - width) : area.left + (area.right - area.left - width) / 2,
                                      placed ? std::clamp(preferences.y, static_cast<int>(area.top), static_cast<int>(area.bottom) - height) : area.top + (area.bottom - area.top - height) / 2, width, height, nullptr, nullptr,
                                      instance, state.get());
    if (!hwnd)
        return false;
    SetPropW(hwnd, lume::window_ids::DesktopWidgetProp, reinterpret_cast<HANDLE>(1));
    SetPropW(hwnd, widget_snap::Capability, reinterpret_cast<HANDLE>(1));
    state->snap.Bind(hwnd);
    ConfigureRoundedWindow(hwnd, kCornerRadius * static_cast<float>(GetDpiForWindow(hwnd)) / 96.f, true);
    const int noBackdrop = 1;
    DwmSetWindowAttribute(hwnd, 38, &noBackdrop, sizeof(noBackdrop));
    const MARGINS margins{-1, -1, -1, -1};
    const bool frameReady = SUCCEEDED(DwmExtendFrameIntoClientArea(hwnd, &margins));
    // An empty blur region opts the redirected client into alpha composition,
    // even when Windows' transparency effects are disabled. The empty region
    // adds no DWM blur: LumeGlass provides the same frosted pixels as widgets.
    const HRGN alphaRegion = CreateRectRgn(0, 0, -1, -1);
    DWM_BLURBEHIND alpha{};
    alpha.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    alpha.fEnable = TRUE;
    alpha.hRgnBlur = alphaRegion;
    state->transparentHost = alphaRegion && frameReady && SUCCEEDED(DwmEnableBlurBehindWindow(hwnd, &alpha));
    if (alphaRegion)
        DeleteObject(alphaRegion);
    state_ = state;
    if (!state->Initialize()) {
        state->Fail();
        return false;
    }
    ShowWindow(hwnd, state->Compact() && !state->options.edit ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!state->Compact() || state->options.edit) SetForegroundWindow(hwnd);
    else DesktopLevel(hwnd);
    return true;
}
void NotesPageWindow::RequestClose() {
    if (IsOpen())
        PostMessageW(state_->hwnd, WM_CLOSE, 0, 0);
}
void NotesPageWindow::CancelClose() {
    if (!IsOpen()) return;
    state_->closeRequested = false;
    MSG message{};
    while (PeekMessageW(&message, state_->hwnd, WM_CLOSE, WM_CLOSE, PM_REMOVE)) {}
}
void NotesPageWindow::Close() {
    if (!state_)
        return;
    const auto state = std::move(state_);
    state->closed = {};
    state->onMessage = {};
    if (state->hwnd)
        DestroyWindow(state->hwnd);
    else
        state->Release();
}
void NotesPageWindow::Post(const std::wstring &json) {
    if (state_)
        state_->Post(json);
}
bool NotesPageWindow::IsOpen() const { return state_ && state_->hwnd; }
HWND NotesPageWindow::Handle() const { return state_ ? state_->hwnd : nullptr; }
widget::Preferences NotesPageWindow::Bounds() const { return state_ ? state_->Bounds() : widget::Preferences{}; }
void NotesPageWindow::Configure(const widget::Preferences &value) {
    if (!state_ || !state_->Compact()) return;
    state_->options.preferences = value;
    state_->UpdateMaterial();
    if (state_->hwnd) {
        const auto dpi = static_cast<int>(GetDpiForWindow(state_->hwnd));
        SetWindowPos(state_->hwnd, nullptr, value.x, value.y, MulDiv(value.width, dpi, 96), MulDiv(value.height, dpi, 96),
                     SWP_NOZORDER | SWP_NOACTIVATE | (value.placed ? 0 : SWP_NOMOVE));
    }
}
} // namespace lume::platform::windows
