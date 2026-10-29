#include "platform/windows/webview_page_window.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "platform/windows/webview_com_support.h"
#include "platform/windows/lume_resource.h"
#include "core/ui/page_lifecycle_protocol.h"
#include "core/ui/original_window_layout.h"
#include "core/ui/webview_lifecycle.h"
#include "platform/windows/webview_process_memory.h"
#include <WebView2.h>
#include <shlobj.h>
#include <shellscalingapi.h>
#include "platform/windows/rounded_window_surface.h"
#include <algorithm>
#include <numeric>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <deque>
#include <cstdint>

namespace lume::platform::windows {
using namespace webview_detail;
namespace {
constexpr const wchar_t* kClassName = lume::window_ids::ManagerClass;
constexpr wchar_t kPageUrl[] = L"https://lume.local/index.html";
constexpr wchar_t kMusicFontPrefix[] = L"https://lume-fonts.local/";
constexpr wchar_t kMusicFontFilter[] = L"https://lume-fonts.local/*";
constexpr std::uint64_t kMaxMusicFontBytes = 50ULL * 1024 * 1024;
constexpr UINT kDragMessage = WM_APP + 21;
constexpr UINT kFullscreenMessage = WM_APP + 22;
constexpr UINT kPageCommandMessage = WM_APP + 23;
constexpr UINT kWallpaperCommandMessage = WM_APP + 24;
constexpr UINT kWallpaperDropMessage = WM_APP + 25;
constexpr UINT kApplicationCommandMessage = WM_APP + 26;
constexpr UINT kLifecycleCleanupMessage = WM_APP + 27;
constexpr UINT kMusicDropMessage = WM_APP + 28;
constexpr UINT_PTR kStartupTimeout = 1;
constexpr UINT kMaxDroppedFiles = 32;
constexpr auto kWebView3Id = L"{A0D6DF20-3B92-416D-AA0C-437A9C727857}";
constexpr auto kController2Id = L"{c979903e-d4ca-4228-92eb-47ee3fa96eab}";
constexpr auto kController3Id = L"{f9614724-5d2b-41dc-aef7-73d62b51543b}";
constexpr auto kSettings4Id = L"{cb56846c-4168-4d53-b04f-03b6d6796ff2}";
constexpr auto kMessageArgs2Id = L"{06fc7ab7-c90c-4297-9389-33ca01cf6d5e}";
constexpr auto kWebViewFileId = L"{f2c19559-6bc1-4583-a757-90021be9afec}";
constexpr auto kWebView19Id = L"{6921f954-79b0-437f-a997-c85811897c68}";
std::filesystem::path ExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}
std::filesystem::path ProfileDirectory(bool verification) {
    if (verification) return ExecutableDirectory() / L"test-webview-profile" / std::to_wstring(GetCurrentProcessId());
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    const auto path = std::filesystem::path(local) / L"Lume" / L"WebView2";
    CoTaskMemFree(local);
    return path;
}
bool IsPageUrl(const wchar_t* value) {
    if (!value) return false;
    const std::wstring_view uri(value), base(kPageUrl);
    return uri == base || (uri.size() > base.size() && uri.substr(0, base.size()) == base && uri[base.size()] == L'#');
}
struct FontReadHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit FontReadHandle(HANDLE handle) : value(handle) {}
    ~FontReadHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    FontReadHandle(const FontReadHandle&) = delete;
    FontReadHandle& operator=(const FontReadHandle&) = delete;
};
struct TaskString {
    LPWSTR value = nullptr;
    ~TaskString() { CoTaskMemFree(value); }
};
const wchar_t* MusicFontType(std::wstring_view uri, std::wstring_view& filename) {
    constexpr std::wstring_view prefix(kMusicFontPrefix);
    if (uri.substr(0, prefix.size()) != prefix) return nullptr;
    filename = uri.substr(prefix.size());
    if (filename.size() < 36 || filename.size() > 38 || filename[32] != L'.' ||
        filename.substr(0, 32).find_first_not_of(L"0123456789abcdef") != filename.npos) return nullptr;
    const auto extension = filename.substr(33);
    if (extension == L"ttf") return L"font/ttf";
    if (extension == L"otf") return L"font/otf";
    if (extension == L"woff") return L"font/woff";
    if (extension == L"woff2") return L"font/woff2";
    return nullptr;
}
bool FontFileMatches(HANDLE file, std::uint64_t expected) {
    BY_HANDLE_FILE_INFORMATION info{};
    return file != INVALID_HANDLE_VALUE && GetFileType(file) == FILE_TYPE_DISK &&
        GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        info.nNumberOfLinks == 1 &&
        ((static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) == expected;
}
ComPtr<IStream> ReadMusicFont(const std::wstring& directory, std::wstring_view filename,
    std::uint64_t expected) {
    if (!expected || expected > kMaxMusicFontBytes || directory.size() < 3 ||
        directory.size() > 32700 || directory.find(L'\0') != directory.npos ||
        directory[1] != L':' || (directory[2] != L'\\' && directory[2] != L'/') ||
        !((directory[0] >= L'A' && directory[0] <= L'Z') ||
          (directory[0] >= L'a' && directory[0] <= L'z')) ||
        directory.find(L':', 2) != directory.npos) return {};
    const std::filesystem::path root(directory);
    for (const auto& part : root.relative_path()) {
        const auto name = part.native();
        if (name.empty() || name == L"." || name == L".." ||
            name.back() == L'.' || name.back() == L' ') return {};
    }
    // Keep every checked ancestor pinned until the bytes have been copied.
    // Deny write/delete sharing so none can become a junction or be renamed.
    std::vector<std::unique_ptr<FontReadHandle>> ancestors;
    auto holdDirectory = [&](const std::filesystem::path& path) {
        auto handle = std::make_unique<FontReadHandle>(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (handle->value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle->value, &info) ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        ancestors.push_back(std::move(handle));
        return true;
    };
    auto path = root.root_path();
    if (!holdDirectory(path)) return {};
    for (const auto& part : root.relative_path()) {
        path /= part;
        if (!holdDirectory(path)) return {};
    }
    const FontReadHandle file(CreateFileW((root / std::wstring(filename)).c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!FontFileMatches(file.value, expected)) return {};
    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.Put()))) return {};
    std::array<char, 65536> buffer{};
    auto remaining = expected;
    while (remaining) {
        const auto wanted = static_cast<DWORD>(std::min<std::uint64_t>(remaining, buffer.size()));
        DWORD read = 0;
        ULONG written = 0;
        if (!ReadFile(file.value, buffer.data(), wanted, &read, nullptr) || read != wanted ||
            FAILED(stream->Write(buffer.data(), read, &written)) || written != read) return {};
        remaining -= read;
    }
    if (!FontFileMatches(file.value, expected) || FAILED(stream->Seek({}, STREAM_SEEK_SET, nullptr))) return {};
    // WebView receives an immutable snapshot, never a path it could reopen.
    return stream;
}
}

struct WebViewPageWindow::State : std::enable_shared_from_this<State> {
    HWND hwnd = nullptr;
    HMODULE loader = nullptr;
    bool closing = false, ready = false, fullscreen = false, verification = false, ending = false;
    int result = 0;
    RECT windowed{};
    RECT windowedWorkArea{};
    int windowedDpi = 96;
    unsigned verificationRasterDpi = 0;
    std::wstring error;
    std::wstring verificationReadyScript;
    CommandHandler handler;
    WallpaperHandler wallpaperHandler;
    WallpaperDropHandler wallpaperDropHandler;
    MusicDropHandler musicDropHandler;
    ApplicationCommandHandler applicationHandler;
    VisibilityHandler visibilityHandler;
    std::wstring wallpaperMediaDirectory;
    std::wstring musicFontDirectory;
    MusicFontAssetLookup musicFontAssetLookup;
    std::deque<ui::WallpaperPageCommand> wallpaperCommands;
    std::deque<std::vector<std::wstring>> wallpaperDrops;
    std::deque<std::vector<std::wstring>> musicDrops;
    std::deque<std::wstring> applicationCommands;
    bool wallpaperDropZoneLocal = false;
    bool musicDropZoneLocal = false;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> view;
    ComPtr<ICoreWebView2_3> lifecycleView;
    ComPtr<ICoreWebView2_19> memoryView;
    ui::WebViewLifecycle lifecycle;
    bool suspendInFlight = false;
    EventRegistrationToken messageToken{}, navigationToken{}, popupToken{}, completedToken{};
    bool messageRegistered = false, navigationRegistered = false, popupRegistered = false, completedRegistered = false;
    EventRegistrationToken fontResourceToken{};
    bool fontResourceRegistered = false, fontFilterRegistered = false;

    ~State() { Close(); if (loader) FreeLibrary(loader); }
    void Close() {
        if (closing) return;
        lifecycle.Close();
        wallpaperCommands.clear();
        wallpaperDrops.clear();
        musicDrops.clear();
        applicationCommands.clear();
        closing = true;
        if (hwnd) KillTimer(hwnd, kStartupTimeout);
        if (view) {
            if (messageRegistered) view->remove_WebMessageReceived(messageToken);
            if (navigationRegistered) view->remove_NavigationStarting(navigationToken);
            if (popupRegistered) view->remove_NewWindowRequested(popupToken);
            if (completedRegistered) view->remove_NavigationCompleted(completedToken);
            if (fontResourceRegistered) view->remove_WebResourceRequested(fontResourceToken);
            if (fontFilterRegistered) view->RemoveWebResourceRequestedFilter(kMusicFontFilter, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        }
        if (controller) controller->Close();
        suspendInFlight = false;
        memoryView.Reset(); lifecycleView.Reset();
        view.Reset(); controller.Reset(); environment.Reset();
        handler = {};
        wallpaperHandler = {};
        wallpaperDropHandler = {};
        musicDropHandler = {};
        applicationHandler = {};
        visibilityHandler = {};
        musicFontAssetLookup = {};
    }
    void Fail(const wchar_t* description, HRESULT code) {
        if (closing) return;
        result = 1;
        wchar_t hex[24]{};
        swprintf(hex, 24, L" (0x%08lX)", static_cast<unsigned long>(code));
        error = std::wstring(description) + hex + L"\n请检查 Microsoft Edge WebView2 Runtime 和安装包内的 ui 目录。\n按 Alt+F4 关闭窗口。";
        if (controller) controller->put_IsVisible(FALSE);
        if (hwnd) {
            KillTimer(hwnd, kStartupTimeout);
            InvalidateRect(hwnd, nullptr, TRUE);
            if (verification) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            else { ShowWindow(hwnd, SW_SHOWNORMAL); UpdateWindow(hwnd); }
        }
    }
    void Post(const std::wstring& json) {
        if (!closing && view && !json.empty()) view->PostWebMessageAsJson(json.c_str());
    }
    void NotifyWindow() {
        Post(fullscreen ? L"{\"version\":1,\"type\":\"window\",\"fullscreen\":true}" : L"{\"version\":1,\"type\":\"window\",\"fullscreen\":false}");
    }
    void Layout() {
        if (!hwnd || closing || IsIconic(hwnd)) return;
        RECT client{}; GetClientRect(hwnd, &client);
        const double dpi = static_cast<double>(verificationRasterDpi?verificationRasterDpi:GetDpiForWindow(hwnd))/96.0;
        const double zoom = ui::CalculateOriginalZoom(static_cast<int>(std::lround(client.right / dpi)), static_cast<int>(std::lround(client.bottom / dpi)));
        if (controller) { controller->put_Bounds(client); controller->put_ZoomFactor(zoom); }
        // CSS owns the original 34px radius. Preserve its coverage alpha all
        // the way to DWM instead of clipping it again with a binary HRGN.
        ConfigureRoundedWindow(hwnd,fullscreen?0.f:static_cast<float>(34.0*zoom*dpi),true);
    }
    void StartSuspend(std::uint32_t generation) {
        if (closing || suspendInFlight || !lifecycleView) return;
        suspendInFlight = true;
        if (memoryView) memoryView->put_MemoryUsageTargetLevel(
            COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW);
        const auto weak = weak_from_this();
        auto completion = Handler<ICoreWebView2TrySuspendCompletedHandler, HRESULT, BOOL>(
            L"{00f206a7-9d17-4605-91f6-4e8e4de192e3}",
            [weak, generation](HRESULT code, BOOL suspended) {
                const auto self = weak.lock();
                if (!self || self->closing) return S_OK;
                self->suspendInFlight = false;
                self->ApplyLifecycleActions(self->lifecycle.OnSuspendCompleted(
                    generation, SUCCEEDED(code) && suspended));
                return S_OK;
            });
        const auto suspendResult = lifecycleView->TrySuspend(completion.Get());
        if (FAILED(suspendResult)) {
            suspendInFlight = false;
            ApplyLifecycleActions(lifecycle.OnSuspendCompleted(generation, false));
        }
    }
    void ApplyLifecycleActions(const std::vector<ui::WebViewLifecycle::Action>& actions) {
        for (const auto& action : actions) {
            if (closing) return;
            using Type = ui::WebViewLifecycle::Action::Type;
            switch (action.type) {
            case Type::NotifyVisibility:
                if (!action.visible && visibilityHandler) visibilityHandler(false);
                Post(std::wstring(L"{\"version\":1,\"type\":\"visibility\",\"visible\":")+
                    (action.visible ? L"true" : L"false")+L",\"generation\":"+
                    std::to_wstring(action.generation)+L"}");
                break;
            case Type::TrySuspend:
                StartSuspend(action.generation);
                break;
            case Type::TrimWorkingSet:
                TrimWebViewProcessWorkingSets(GetCurrentProcessId());
                break;
            case Type::Resume:
                if (memoryView) memoryView->put_MemoryUsageTargetLevel(
                    COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL);
                if (lifecycleView) lifecycleView->Resume();
                if (controller) controller->put_IsVisible(TRUE);
                break;
            case Type::RequestStateSync:
                if (visibilityHandler) visibilityHandler(true);
                break;
            }
        }
    }
    void ToggleFullscreen() {
        if (closing || !hwnd) return;
        if (!fullscreen) {
            GetWindowRect(hwnd, &windowed);
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            windowedWorkArea = monitor.rcWork;
            windowedDpi = static_cast<int>(GetDpiForWindow(hwnd));
            fullscreen = true;
            SetWindowPos(hwnd, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_NOOWNERZORDER);
        } else {
            fullscreen = false;
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            const auto asBounds = [](const RECT& r) { return ui::PageBounds{r.left, r.top, r.right-r.left, r.bottom-r.top}; };
            const auto restored = ui::RestoreOriginalWindowBounds(asBounds(windowed), asBounds(windowedWorkArea),
                asBounds(monitor.rcWork), windowedDpi, static_cast<int>(GetDpiForWindow(hwnd)));
            SetWindowPos(hwnd, nullptr, restored.x, restored.y, restored.width, restored.height, SWP_NOZORDER | SWP_NOOWNERZORDER);
        }
        Layout(); NotifyWindow();
    }
    void FitFullscreenMonitor() {
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            SetWindowPos(hwnd, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right-monitor.rcMonitor.left, monitor.rcMonitor.bottom-monitor.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void SetDropZone(ui::PageCommand command) {
        wallpaperDropZoneLocal = command == ui::PageCommand::WallpaperDropZoneLocal;
        musicDropZoneLocal = command == ui::PageCommand::MusicDropZone;
    }
    void QueueDrop(ICoreWebView2WebMessageReceivedEventArgs* args, bool music) {
        const bool enabled = music ? musicDropZoneLocal : wallpaperDropZoneLocal;
        const bool available = music ? static_cast<bool>(musicDropHandler) : static_cast<bool>(wallpaperDropHandler);
        auto& drops = music ? musicDrops : wallpaperDrops;
        if (!enabled || !available || drops.size() >= 4) return;
        ComPtr<ICoreWebView2WebMessageReceivedEventArgs2> extended;
        if (FAILED(args->QueryInterface(InterfaceId(kMessageArgs2Id),reinterpret_cast<void**>(extended.Put())))) return;
        ComPtr<ICoreWebView2ObjectCollectionView> objects;
        UINT count=0;
        if (FAILED(extended->get_AdditionalObjects(objects.Put())) || !objects ||
            FAILED(objects->get_Count(&count)) || !count || count>kMaxDroppedFiles) return;
        std::vector<std::wstring> paths;
        paths.reserve(count);
        for (UINT index=0;index<count;++index) {
            ComPtr<IUnknown> object;
            ComPtr<ICoreWebView2File> file;
            if (FAILED(objects->GetValueAtIndex(index,object.Put())) || !object ||
                FAILED(object->QueryInterface(InterfaceId(kWebViewFileId),reinterpret_cast<void**>(file.Put())))) return;
            LPWSTR path=nullptr;
            const auto pathResult=file->get_Path(&path);
            const auto length=path?wcsnlen(path,32768):0;
            if (FAILED(pathResult) || !length || length>32767) {CoTaskMemFree(path);return;}
            paths.emplace_back(path,length);
            CoTaskMemFree(path);
            if (!std::filesystem::path(paths.back()).is_absolute()) return;
        }
        drops.push_back(std::move(paths));
        const auto message = music ? kMusicDropMessage : kWallpaperDropMessage;
        if (!PostMessageW(hwnd,message,0,0)) drops.pop_back();
    }
    void Command(ui::PageCommand command) {
        if (closing || command == ui::PageCommand::Invalid) return;
        using C = ui::PageCommand;
        switch (command) {
        case C::Minimize: ShowWindow(hwnd, SW_MINIMIZE); return;
        case C::ToggleFullscreen: PostMessageW(hwnd, kFullscreenMessage, 0, 0); return;
        case C::Close: PostMessageW(hwnd, WM_CLOSE, 0, 0); return;
        case C::Drag: if (!fullscreen) PostMessageW(hwnd, kDragMessage, 0, 0); return;
        case C::WallpaperDropZoneLocal:
        case C::MusicDropZone:
        case C::WallpaperDropZoneNone: SetDropZone(command); return;
        case C::WallpaperDropFiles: return;
        case C::MusicDropFiles: return;
        case C::Ready:
            if(ready)return;
            ready = true; lifecycle.OnReady(); KillTimer(hwnd, kStartupTimeout); InvalidateRect(hwnd,nullptr,TRUE); NotifyWindow(); break;
        default: break;
        }
        // Close can run inside a native modal editor and clear handler. Keep both
        // the active callback and State alive until the invocation has unwound.
        const auto keepAlive=shared_from_this();
        const auto callback=handler;
        if (callback) Post(callback(command));
        // The page sends ready only after its first-view assets and layout are
        // prepared. Keep the native loading surface offscreen until then.
        if(command==C::Ready&&!closing&&hwnd) {
            ShowWindow(hwnd,SW_SHOWNORMAL);UpdateWindow(hwnd);
        }
        if(command==C::Ready && verification && !closing && view && !verificationReadyScript.empty()) {
            const auto script=std::move(verificationReadyScript);
            view->ExecuteScript(script.c_str(),nullptr);
        }
    }
    HRESULT FontResponse(ICoreWebView2WebResourceRequestedEventArgs* args, IStream* stream,
        const wchar_t* type = L"application/octet-stream") {
        ComPtr<ICoreWebView2WebResourceResponse> response;
        const auto headers = std::wstring(L"Access-Control-Allow-Origin: https://lume.local\r\n"
            L"Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Type: ") + type;
        const auto hr = environment->CreateWebResourceResponse(stream, stream ? 200 : 404,
            stream ? L"OK" : L"Not Found", headers.c_str(), response.Put());
        return FAILED(hr) ? hr : args->put_Response(response.Get());
    }
    HRESULT ReadFontResource(ICoreWebView2WebResourceRequestedEventArgs* args) {
        // Install a denial before callbacks/allocations: failure must not fall through.
        auto hr = FontResponse(args, nullptr);
        if (FAILED(hr)) return hr;
        try {
            ComPtr<ICoreWebView2WebResourceRequest> request;
            TaskString uri, method;
            if (FAILED(args->get_Request(request.Put())) || !request ||
                FAILED(request->get_Uri(&uri.value)) || !uri.value ||
                FAILED(request->get_Method(&method.value)) || !method.value ||
                std::wstring_view(method.value) != L"GET") return S_OK;
            std::wstring_view filename;
            const auto type = MusicFontType(uri.value, filename);
            if (!type || !musicFontAssetLookup || musicFontDirectory.empty()) return S_OK;
            const auto expected = musicFontAssetLookup(filename);
            if (closing) return S_OK;
            auto stream = ReadMusicFont(musicFontDirectory, filename, expected);
            if (stream) hr = FontResponse(args, stream.Get(), type);
        } catch (...) { return S_OK; }
        return hr;
    }
    void AttachController(ICoreWebView2Controller* created) {
        if (closing) { if (created) created->Close(); return; }
        controller.CopyFrom(created);
        auto hr = controller->get_CoreWebView2(view.Put());
        if (FAILED(hr)) { Fail(L"无法获取页面控制器", hr); return; }
        ComPtr<ICoreWebView2_3> mapped;
        hr = view->QueryInterface(InterfaceId(kWebView3Id), reinterpret_cast<void**>(mapped.Put()));
        if (FAILED(hr)) { Fail(L"WebView2 Runtime 版本过旧", hr); return; }
        lifecycleView.CopyFrom(mapped.Get());
        view->QueryInterface(InterfaceId(kWebView19Id), reinterpret_cast<void**>(memoryView.Put()));
        const auto root = ExecutableDirectory() / L"ui";
        hr = mapped->SetVirtualHostNameToFolderMapping(L"lume.local", root.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        if(SUCCEEDED(hr)&&!wallpaperMediaDirectory.empty()) {
            hr=mapped->SetVirtualHostNameToFolderMapping(L"lume-wallpaper.local",
                wallpaperMediaDirectory.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
        if (FAILED(hr)) { Fail(L"无法加载本地界面资源", hr); return; }
        const auto fontWeak = weak_from_this();
        auto fontResource = Handler<ICoreWebView2WebResourceRequestedEventHandler,
            ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>(
            L"{ab00b74c-15f1-4646-80e8-e76341d25d71}",
            [fontWeak](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) {
                const auto self = fontWeak.lock();
                return self && !self->closing ? self->ReadFontResource(args) : E_ABORT;
            });
        hr = view->add_WebResourceRequested(fontResource.Get(), &fontResourceToken);
        if (SUCCEEDED(hr)) {
            fontResourceRegistered = true;
            hr = view->AddWebResourceRequestedFilter(kMusicFontFilter, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
            fontFilterRegistered = SUCCEEDED(hr);
        }
        if (FAILED(hr)) { Fail(L"Unable to initialize font resource boundary", hr); return; }
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(view->get_Settings(settings.Put()))) {
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            settings->put_AreDefaultScriptDialogsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            // No browser autofill: it offers saved entries under search boxes and
            // repaints autofilled fields in its own colours.
            ComPtr<ICoreWebView2Settings4> autofill;
            if (SUCCEEDED(settings->QueryInterface(InterfaceId(kSettings4Id), reinterpret_cast<void**>(autofill.Put())))) {
                autofill->put_IsGeneralAutofillEnabled(FALSE);
                autofill->put_IsPasswordAutosaveEnabled(FALSE);
            }
        }
        ComPtr<ICoreWebView2Controller2> colored;
        if (SUCCEEDED(controller->QueryInterface(InterfaceId(kController2Id), reinterpret_cast<void**>(colored.Put()))))
            colored->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0, 0, 0, 0});
        if(verificationRasterDpi) {
            ComPtr<ICoreWebView2Controller3> scaled;
            hr=controller->QueryInterface(InterfaceId(kController3Id),reinterpret_cast<void**>(scaled.Put()));
            if(SUCCEEDED(hr))hr=scaled->put_ShouldDetectMonitorScaleChanges(FALSE);
            if(SUCCEEDED(hr))hr=scaled->put_RasterizationScale(verificationRasterDpi/96.0);
            double actual=0;
            if(SUCCEEDED(hr))hr=scaled->get_RasterizationScale(&actual);
            if(FAILED(hr)||std::abs(actual-verificationRasterDpi/96.0)>.0001) {
                Fail(L"无法验证测试页面的栅格比例",FAILED(hr)?hr:E_FAIL);return;
            }
        }
        const auto weak = weak_from_this();
        auto message = Handler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
            L"{57213f19-00e6-49fa-8e07-898ea01ecbd2}", [weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
                const auto self = weak.lock(); if (!self || self->closing) return S_OK;
                LPWSTR source = nullptr, data = nullptr;
                const bool trusted = SUCCEEDED(args->get_Source(&source)) && IsPageUrl(source);
                if (trusted && SUCCEEDED(args->get_WebMessageAsJson(&data)) && data) {
                    const auto command=ui::ParsePageJson(data);
                    // WebView2 forbids modal loops inside an event callback.
                    // Finish the COM event before dispatching any native command.
                    if(command==ui::PageCommand::WallpaperDropZoneLocal ||
                        command==ui::PageCommand::MusicDropZone ||
                        command==ui::PageCommand::WallpaperDropZoneNone)
                        self->SetDropZone(command);
                    else if(command==ui::PageCommand::WallpaperDropFiles)self->QueueDrop(args,false);
                    else if(command==ui::PageCommand::MusicDropFiles)self->QueueDrop(args,true);
                    else if(command!=ui::PageCommand::Invalid)PostMessageW(self->hwnd,kPageCommandMessage,static_cast<WPARAM>(command),0);
                    else {
                        bool handled=false;
                        if(const auto lifecycleCommand=ui::ParseLifecyclePageJson(data)) {
                            handled=true;
                            if(lifecycleCommand->action==ui::LifecycleAction::Cleanup &&
                                !PostMessageW(self->hwnd,kLifecycleCleanupMessage,
                                    lifecycleCommand->generation,0)) handled=false;
                        }
                        if(self->wallpaperHandler&&self->wallpaperCommands.size()<32) {
                            if(!handled) if(const auto wallpaper=ui::ParseWallpaperPageJson(data)) {
                                handled=true;
                                self->wallpaperCommands.push_back(*wallpaper);
                                if(!PostMessageW(self->hwnd,kWallpaperCommandMessage,0,0))self->wallpaperCommands.pop_back();
                            }
                        }
                        const auto length=wcsnlen(data,8193);
                        if(!handled&&self->applicationHandler&&self->applicationCommands.size()<16&&length<=8192) {
                            self->applicationCommands.emplace_back(data,length);
                            if(!PostMessageW(self->hwnd,kApplicationCommandMessage,0,0))self->applicationCommands.pop_back();
                        }
                    }
                }
                CoTaskMemFree(source); CoTaskMemFree(data); return S_OK;
            });
        hr = view->add_WebMessageReceived(message.Get(), &messageToken);
        if (FAILED(hr)) { Fail(L"无法初始化页面消息", hr); return; } messageRegistered = true;
        auto navigation = Handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
            L"{9adbe429-f36d-432b-9ddc-f8881fbd76e3}", [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
                LPWSTR uri = nullptr; args->get_Uri(&uri);
                if (!IsPageUrl(uri)) args->put_Cancel(TRUE);
                CoTaskMemFree(uri); return S_OK;
            });
        hr = view->add_NavigationStarting(navigation.Get(), &navigationToken);
        if (FAILED(hr)) { Fail(L"无法初始化页面导航边界", hr); return; } navigationRegistered = true;
        auto popup = Handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
            L"{d4c185fe-c81c-4989-97af-2d3fa7ab5651}", [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
                args->put_Handled(TRUE); return S_OK;
            });
        hr = view->add_NewWindowRequested(popup.Get(), &popupToken);
        if (FAILED(hr)) { Fail(L"无法初始化窗口边界", hr); return; } popupRegistered = true;
        auto completed = Handler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
            L"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}", [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) {
                BOOL success = FALSE; args->get_IsSuccess(&success);
                if (!success) if (const auto self = weak.lock()) self->Fail(L"界面资源加载失败", E_FAIL);
                return S_OK;
            });
        hr = view->add_NavigationCompleted(completed.Get(), &completedToken);
        if (FAILED(hr)) { Fail(L"无法监听页面加载", hr); return; } completedRegistered = true;
        Layout(); controller->put_IsVisible(TRUE);
        hr = view->Navigate(kPageUrl);
        if (FAILED(hr)) Fail(L"无法打开 Lume 页面", hr);
    }
    void Initialize() {
        const auto directory = ExecutableDirectory();
        std::error_code ec;
        if (!std::filesystem::is_regular_file(directory / L"ui" / L"index.html", ec)) { Fail(L"缺少 ui/index.html", HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)); return; }
        loader = LoadLibraryExW((directory / L"WebView2Loader.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!loader) { Fail(L"无法加载 WebView2Loader.dll", HRESULT_FROM_WIN32(GetLastError())); return; }
        using CreateEnvironment = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
        auto address = GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions");
        CreateEnvironment create = nullptr; static_assert(sizeof(create) == sizeof(address));
        std::memcpy(&create, &address, sizeof(create));
        if (!create) { Fail(L"WebView2Loader 导出不完整", E_NOINTERFACE); return; }
        const auto profile = ProfileDirectory(verification);
        if (profile.empty()) { Fail(L"无法定位页面数据目录", E_FAIL); return; }
        // Strong ownership during asynchronous creation; event subscriptions below use weak ownership.
        auto environmentReady = Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
            L"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}", [self = shared_from_this()](HRESULT result, ICoreWebView2Environment* created) {
                if (self->closing) return S_OK;
                if (FAILED(result) || !created) { self->Fail(L"WebView2 Runtime 启动失败", result); return S_OK; }
                self->environment.CopyFrom(created);
                auto controllerReady = Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                    L"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}", [self](HRESULT hr, ICoreWebView2Controller* controller) {
                        if (self->closing) { if (controller) controller->Close(); return S_OK; }
                        if (FAILED(hr) || !controller) self->Fail(L"页面窗口创建失败", hr);
                        else self->AttachController(controller);
                        return S_OK;
                    });
                const auto hr = created->CreateCoreWebView2Controller(self->hwnd, controllerReady.Get());
                if (FAILED(hr)) self->Fail(L"无法创建页面窗口", hr);
                return S_OK;
            });
        const auto hr = create(nullptr, profile.c_str(), nullptr, environmentReady.Get());
        if (FAILED(hr)) Fail(L"无法创建 WebView2 Environment", hr);
    }
};

LRESULT CALLBACK WebViewPageWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        state->hwnd = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) {
            if (state->controller) state->controller->put_IsVisible(FALSE);
            state->ApplyLifecycleActions(state->lifecycle.OnHidden());
        } else {
            if (state->controller) state->controller->put_IsVisible(TRUE);
            const auto actions = state->lifecycle.OnVisible();
            state->ApplyLifecycleActions(actions);
            if (actions.empty() && state->ready)
                state->Post(L"{\"version\":1,\"type\":\"visibility\",\"visible\":true}");
        }
        state->Layout(); return 0;
    case WM_DPICHANGED: {
        auto* rect = reinterpret_cast<RECT*>(lparam);
        if (state->fullscreen) state->FitFullscreenMonitor();
        else SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        state->Layout(); return 0;
    }
    case WM_MOUSEACTIVATE:
        desktop_level::ManagerSelected();
        break;
    case WM_ACTIVATE:
        if (LOWORD(wparam) != WA_INACTIVE) desktop_level::ManagerSelected();
        break;
    case WM_WINDOWPOSCHANGED: {
        const auto* position = reinterpret_cast<const WINDOWPOS*>(lparam);
        if (!(position->flags & SWP_NOZORDER) || (position->flags & SWP_SHOWWINDOW))
            desktop_level::RequestAll();
        break;
    }
    case WM_DISPLAYCHANGE: if (state->fullscreen) state->FitFullscreenMonitor(); return 0;
    case WM_MOVE: if (state->controller) state->controller->NotifyParentWindowPositionChanged(); return 0;
    case kFullscreenMessage: state->ToggleFullscreen(); return 0;
    case kPageCommandMessage: state->Command(static_cast<ui::PageCommand>(wparam)); return 0;
    case kWallpaperCommandMessage:
        if(!state->closing&&state->wallpaperHandler&&!state->wallpaperCommands.empty()) {
            auto command=std::move(state->wallpaperCommands.front());state->wallpaperCommands.pop_front();
            state->wallpaperHandler(command);
        }
        return 0;
    case kWallpaperDropMessage: {
        if (state->closing || state->wallpaperDrops.empty()) return 0;
        auto paths=std::move(state->wallpaperDrops.front());state->wallpaperDrops.pop_front();
        const auto callback=state->wallpaperDropHandler;
        if (callback) callback(std::move(paths));
        return 0;
    }
    case kMusicDropMessage: {
        if (state->closing || state->musicDrops.empty()) return 0;
        auto paths=std::move(state->musicDrops.front());state->musicDrops.pop_front();
        const auto callback=state->musicDropHandler;
        if (callback) callback(std::move(paths));
        return 0;
    }
    case kApplicationCommandMessage: {
        if(state->closing||state->applicationCommands.empty())return 0;
        const auto keepAlive=state->shared_from_this();
        auto command=std::move(state->applicationCommands.front());
        state->applicationCommands.pop_front();
        const auto callback=state->applicationHandler;
        if(callback)callback(command);
        return 0;
    }
    case kLifecycleCleanupMessage:
        state->ApplyLifecycleActions(state->lifecycle.OnCleanupAcknowledged(
            static_cast<std::uint32_t>(wparam)));
        return 0;
    case kDragMessage:
        if (!state->fullscreen && (GetAsyncKeyState(VK_LBUTTON)&0x8000)) {
            ReleaseCapture(); SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    case WM_TIMER: if (wparam == kStartupTimeout && !state->ready) state->Fail(L"界面初始化超时", HRESULT_FROM_WIN32(ERROR_TIMEOUT)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; auto target = BeginPaint(hwnd, &paint); RECT rect{}; GetClientRect(hwnd, &rect);
        // The WebView owns every normal pixel; the hidden startup window must
        // never retain a painted loading page for its first visible frame.
        if(state->error.empty()) {
            FillRect(target,&rect,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hwnd,&paint);return 0;
        }
        RoundedWindowPaint buffer(target,rect.right,rect.bottom);auto dc=buffer.Dc();
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        InflateRect(&rect, -40, -50);
        direct_write::Style style;
        style.flags = DT_LEFT | DT_WORDBREAK | DT_NOPREFIX;
        direct_write::Draw(dc, state->error, rect, RGB(225, 225, 225), style);
        RECT client{};GetClientRect(hwnd,&client);
        const double dpi=static_cast<double>(GetDpiForWindow(hwnd))/96.0;
        const auto zoom=ui::CalculateOriginalZoom(static_cast<int>(std::lround(client.right/dpi)),static_cast<int>(std::lround(client.bottom/dpi)));
        buffer.Present(state->fullscreen?0.f:static_cast<float>(34*zoom*dpi));
        EndPaint(hwnd, &paint); return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: state->Close(); if(!state->ending)PostQuitMessage(state->result); return 0;
    case WM_NCDESTROY: state->hwnd = nullptr; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WebViewPageWindow::Run(HINSTANCE instance,CommandHandler handler,bool verification,
    const std::wstring& verificationReadyScript,unsigned verificationRasterDpi) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    state_ = std::make_shared<State>(); state_->handler = std::move(handler); state_->verification = verification;
    state_->wallpaperHandler=wallpaperHandler_;
    state_->wallpaperDropHandler=wallpaperDropHandler_;
    state_->musicDropHandler=musicDropHandler_;
    state_->wallpaperMediaDirectory=wallpaperMediaDirectory_;
    state_->musicFontDirectory=musicFontDirectory_;
    state_->musicFontAssetLookup=musicFontAssetLookup_;
    state_->applicationHandler=applicationHandler_;
    state_->visibilityHandler=visibilityHandler_;
    if(verification) {
        state_->verificationReadyScript=verificationReadyScript;
        if(verificationRasterDpi) {
            if(verificationRasterDpi!=96&&verificationRasterDpi!=120&&verificationRasterDpi!=144&&verificationRasterDpi!=192) {
                state_.reset();CoUninitialize();return 1;
            }
            state_->verificationRasterDpi=verificationRasterDpi;
        }
    }
    WNDCLASSEXW windowClass{}; windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance; windowClass.lpfnWndProc = WindowProc; windowClass.lpszClassName = kClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_LUME_APP)); windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { state_.reset(); CoUninitialize(); return 1; }
    const HMONITOR primary = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor); GetMonitorInfoW(primary, &monitor);
    // The window opens on the primary monitor, at that monitor's DPI; the
    // system DPI differs when scaling changed since sign-in or on mixed-DPI setups.
    UINT monitorDpi = 96, unusedDpi = 96;
    if (FAILED(GetDpiForMonitor(primary, MDT_EFFECTIVE_DPI, &monitorDpi, &unusedDpi)) || !monitorDpi) monitorDpi = GetDpiForSystem();
    const unsigned openDpi = state_->verificationRasterDpi ? state_->verificationRasterDpi : monitorDpi;
    const double scale = static_cast<double>(openDpi)/96.0;
    const auto& area = monitor.rcWork;
    auto bounds = ui::CalculateManagerWindowBounds({0,0,static_cast<int>(std::lround((area.right-area.left)/scale)),static_cast<int>(std::lround((area.bottom-area.top)/scale))});
    if(state_->verificationRasterDpi) {
        bounds.width=960;bounds.height=540;
        bounds.x=static_cast<int>(std::lround(((area.right-area.left)/scale-bounds.width)/2));
        bounds.y=static_cast<int>(std::lround(((area.bottom-area.top)/scale-bounds.height)/2));
    }
    {
        // Keep the size whole in physical pixels: a fractional width (for
        // example 1493 DIP at 125%) leaves the page and the rounded frame a
        // quarter pixel apart at the right corners.
        const int dpi=static_cast<int>(openDpi);
        const int step=dpi>0?96/std::gcd(96,dpi):1;
        const int width=bounds.width-bounds.width%step,height=bounds.height-bounds.height%step;
        bounds.x+=(bounds.width-width)/2;bounds.y+=(bounds.height-height)/2;
        bounds.width=width;bounds.height=height;
    }
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClassName, L"Lume", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
        area.left + static_cast<int>(std::lround(bounds.x*scale)), area.top + static_cast<int>(std::lround(bounds.y*scale)),
        static_cast<int>(std::lround(bounds.width*scale)), static_cast<int>(std::lround(bounds.height*scale)), nullptr, nullptr, instance, state_.get());
    if (!hwnd) { state_.reset(); CoUninitialize(); return 1; }
    state_->Layout();
    if(SetTimer(hwnd,kStartupTimeout,15000,nullptr))state_->Initialize();
    else state_->Fail(L"无法初始化界面启动期限",HRESULT_FROM_WIN32(GetLastError()));
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!message.hwnd && wakeMessage_ && message.message == wakeMessage_ && wakeHandler_) {
            wakeHandler_();
            continue;
        }
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    const int result = state_->result;
    // External quit can end Run while its HWND still exists. Destroy it without
    // posting another low-priority quit into the caller's background loop.
    state_->ending=true;
    if(state_->hwnd)DestroyWindow(state_->hwnd);
    state_->Close(); state_.reset(); CoUninitialize();
    return result;
}
void WebViewPageWindow::ConfigureThreadWake(UINT message,std::function<void()> handler) {
    wakeMessage_=message;
    wakeHandler_=std::move(handler);
}
void WebViewPageWindow::ConfigureWallpaper(WallpaperHandler handler,std::wstring mediaDirectory,
    WallpaperDropHandler dropHandler) {
    wallpaperHandler_=std::move(handler);
    wallpaperMediaDirectory_=std::move(mediaDirectory);
    wallpaperDropHandler_=std::move(dropHandler);
}
void WebViewPageWindow::ConfigureMusicDrop(MusicDropHandler handler) {
    musicDropHandler_=std::move(handler);
}
void WebViewPageWindow::ConfigureMusicBackground(std::wstring mediaDirectory) {
    musicBackgroundMediaDirectory_=std::move(mediaDirectory);
}
void WebViewPageWindow::ConfigureMusicFonts(std::wstring fontDirectory, MusicFontAssetLookup lookup) {
    musicFontDirectory_=std::move(fontDirectory);
    musicFontAssetLookup_=std::move(lookup);
}
void WebViewPageWindow::ConfigureApplicationCommands(ApplicationCommandHandler handler) {
    applicationHandler_=std::move(handler);
}
void WebViewPageWindow::ConfigureVisibility(VisibilityHandler handler) {
    visibilityHandler_=std::move(handler);
}
void WebViewPageWindow::RequestClose() { if (state_ && state_->hwnd) PostMessageW(state_->hwnd, WM_CLOSE, 0, 0); }
HWND WebViewPageWindow::Handle() const { return state_?state_->hwnd:nullptr; }
void WebViewPageWindow::Post(const std::wstring& message) { if (state_) state_->Post(message); }
void WebViewPageWindow::Present() {
    if (!state_ || !state_->hwnd || (!state_->ready&&state_->error.empty())) return;
    ShowWindow(state_->hwnd, IsIconic(state_->hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(state_->hwnd);
    desktop_level::ManagerSelected();
    state_->Post(L"{\"version\":1,\"type\":\"visibility\",\"visible\":true}");
}
}
