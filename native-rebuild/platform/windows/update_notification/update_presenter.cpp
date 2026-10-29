#include "platform/windows/update_notification/update_presenter.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/material/widget_material_runtime.h"
#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace lume::platform::windows {
namespace {
constexpr wchar_t kClass[] = L"LumeNativeUpdateNotice";
constexpr wchar_t kGlass[] = L"LumeUpdateGlassActive";
constexpr UINT kAction = WM_APP + 0x3a1;
int Px(float value, UINT dpi) { return static_cast<int>(std::lround(value * dpi / 96.f)); }
UINT MonitorDpi(HMONITOR monitor) {
    UINT x = 96, y = 96;
    const auto library = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (library) {
        using Query = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        const auto address = GetProcAddress(library, "GetDpiForMonitor");
        Query query = nullptr; static_assert(sizeof(query) == sizeof(address));
        std::memcpy(&query, &address, sizeof(query));
        if (!query || FAILED(query(monitor, 0, &x, &y))) x = 96;
        FreeLibrary(library);
    }
    return x ? x : 96;
}
void Rounded(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius, Gdiplus::Color color) {
    const float d = std::min({radius * 2, r.Width, r.Height});
    if (d <= 0) return;
    Gdiplus::GraphicsPath path;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(color); g.FillPath(&brush, &path);
}
struct Dib {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    std::uint32_t* pixels = nullptr;
    int width = 0, height = 0;
    ~Dib() { Clear(); }
    void Clear() {
        if (old) SelectObject(dc, old);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        dc = nullptr; bitmap = nullptr; old = nullptr; pixels = nullptr; width = height = 0;
    }
    bool Resize(int w, int h) {
        if (pixels && width == w && height == h) return true;
        Clear();
        if (w <= 0 || h <= 0) return false;
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        void* bits = nullptr; bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits) { Clear(); return false; }
        old = SelectObject(dc, bitmap); pixels = static_cast<std::uint32_t*>(bits);
        width = w; height = h; return true;
    }
};
}

struct UpdatePresenter::Impl : std::enable_shared_from_this<Impl> {
    HINSTANCE instance;
    std::function<void()> openWebsite;
    HWND window = nullptr;
    WidgetMaterialRuntime material;
    Dib dib;
    ULONG_PTR graphicsToken = 0;
    DWORD thread = GetCurrentThreadId();
    bool stopped = false, closing = false, layingOut = false, graphicsReady = false;
    bool glass = false, attached = false, english = false, actionPending = false;
    bool testEnvironment = false;
    RECT testWork{};
    UINT dpi = 96, testDpi = 96, lifetimeMs = 10000, materialTimeoutMs = 3000;
    UINT_PTR nextTimer = 0x4000, dismissTimer = 0, materialTimer = 0, orderTimer = 0;
    ULONGLONG dismissAt = 0;
    unsigned hover = 0, pressed = 0, focused = 1;
    std::uint64_t generation = 0;
    UpdateNotice notice;
    Gdiplus::RectF titleRect{}, detailRect{}, websiteRect{}, laterRect{}, closeRect{};
    float detailExtent = 0, scroll = 0;
    int width = 0, height = 0;

    Impl(HINSTANCE value, std::function<void()> callback)
        : instance(value ? value : GetModuleHandleW(nullptr)), openWebsite(std::move(callback)) {
        Gdiplus::GdiplusStartupInput input;
        graphicsReady = Gdiplus::GdiplusStartup(&graphicsToken, &input, nullptr) == Gdiplus::Ok;
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = instance;
        wc.lpfnWndProc = Proc; wc.lpszClassName = kClass; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) graphicsReady = false;
    }
    ~Impl() {
        Stop();
        dib.Clear();
        if (graphicsToken) Gdiplus::GdiplusShutdown(graphicsToken);
    }
    bool OnThread() const { return thread == GetCurrentThreadId(); }
    std::wstring Title() const {
        if (notice.version.empty()) return english ? L"Check for updates" : L"\u68c0\u67e5\u66f4\u65b0";
        return (english ? L"New version " : L"\u65b0\u7248\u672c ") + notice.version;
    }
    std::wstring Detail() const {
        if (notice.detail.empty() && notice.version.empty())
            return english ? L"Visit the official website for update information." :
                L"\u8bf7\u524d\u5f80\u5b98\u7f51\u67e5\u770b\u66f4\u65b0\u4fe1\u606f\u3002";
        return notice.detail.empty() ? (english ? L"A new version of Lume is available." :
               L"Lume \u65b0\u7248\u672c\u5df2\u53ef\u7528\u3002") : notice.detail;
    }
    RECT WorkArea(UINT& selectedDpi) const {
        if (testEnvironment) { selectedDpi = testDpi; return testWork; }
        const auto monitor = MonitorFromWindow(window ? window : GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{}; info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &info.rcWork, 0);
        selectedDpi = MonitorDpi(monitor);
        return info.rcWork;
    }
    static float Measure(const std::wstring& value, float size, int weight, float available) {
        direct_write::Style style;
        style.size = size;
        style.weight = weight;
        style.flags = DT_WORDBREAK | DT_NOPREFIX;
        const auto bounds = direct_write::Measure(value, available, style);
        return bounds.Height > 0 ? std::ceil(bounds.Height) + 2.f : 0;
    }
    bool Layout(bool moveWindow = true) {
        if (stopped || closing || layingOut) return false;
        const auto area = WorkArea(dpi);
        const float scale = dpi / 96.f, margin = 12.f, pad = 14.f;
        const float w = std::min(400.f, (area.right - area.left) / scale - 2 * margin);
        const float availableHeight = (area.bottom - area.top) / scale - 2 * margin;
        if (w < 136.f || availableHeight < 180.f) return false;
        const float textWidth = w - 2 * pad;
        const float titleHeight = Measure(Title(), 18.f, FW_BOLD, textWidth);
        detailExtent = Measure(Detail(), 13.f, FW_NORMAL, textWidth - 10.f);
        if (!titleHeight || !detailExtent) return false;
        const bool stacked = textWidth < 252.f;
        const float footerHeight = stacked ? 74.f : 34.f;
        titleRect = {pad, 52.f, textWidth, titleHeight};
        const float detailTop = titleRect.GetBottom() + 8.f;
        const float minimum = detailTop + 40.f + 16.f + footerHeight + pad;
        if (minimum > availableHeight) return false;
        const float h = std::min(availableHeight, detailTop + std::clamp(detailExtent, 46.f, 190.f) + 16.f + footerHeight + pad);
        detailRect = {pad, detailTop, textWidth - 10.f, h - pad - footerHeight - 16.f - detailTop};
        scroll = std::clamp(scroll, 0.f, std::max(0.f, detailExtent - detailRect.Height));
        const float footerY = h - pad - footerHeight;
        websiteRect = {pad, footerY, stacked ? textWidth : 142.f, 34.f};
        laterRect = {stacked ? pad : websiteRect.GetRight() + 10.f,
                     stacked ? footerY + 40.f : footerY, stacked ? textWidth : textWidth - 152.f, 34.f};
        closeRect = {w - pad - 28.f, 12.f, 28.f, 28.f};
        width = Px(w, dpi); height = Px(h, dpi);
        if (window && moveWindow) {
            layingOut = true;
            const bool result = SetWindowPos(window, HWND_TOPMOST, area.right - Px(margin, dpi) - width,
                area.bottom - Px(margin, dpi) - height, width, height, SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
            layingOut = false;
            return result && window && !stopped && !closing;
        }
        return true;
    }
    bool Render() {
        if (!window || closing || !dib.Resize(width, height)) return false;
        Gdiplus::Bitmap image(width, height, width * 4, PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(dib.pixels));
        {
            Gdiplus::Graphics g(&image);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));
            g.ScaleTransform(dpi / 96.f, dpi / 96.f);
            const float w = width * 96.f / dpi, h = height * 96.f / dpi;
            if (!glass) Rounded(g, {0, 0, w, h}, 8.f, Gdiplus::Color(255, 32, 35, 41));
            auto text = [&](const std::wstring& value, bool heading,
                            const Gdiplus::RectF& rect, BYTE alpha, bool centered = false) {
                direct_write::Style style;
                style.size = heading ? 18.f : 13.f;
                style.weight = heading ? FW_BOLD : FW_NORMAL;
                style.flags = DT_NOPREFIX | (centered ? DT_SINGLELINE | DT_CENTER | DT_VCENTER : DT_WORDBREAK);
                direct_write::Draw(g, value, rect, Gdiplus::Color(alpha, 255, 255, 255), style);
            };
            text(L"Lume", false, {14.f, 17.f, closeRect.X - 22.f, 24.f}, 240);
            text(Title(), true, titleRect, 255);
            const auto clip = g.Save();
            g.SetClip(detailRect);
            text(Detail(), false, {detailRect.X, detailRect.Y - scroll, detailRect.Width, detailExtent}, 228);
            g.Restore(clip);
            if (detailExtent > detailRect.Height) {
                const float thumb = std::max(12.f, detailRect.Height * detailRect.Height / detailExtent);
                const float offset = scroll / (detailExtent - detailRect.Height) * (detailRect.Height - thumb);
                Rounded(g, {detailRect.GetRight() + 5.f, detailRect.Y, 3.f, detailRect.Height}, 1.5f, Gdiplus::Color(36, 255, 255, 255));
                Rounded(g, {detailRect.GetRight() + 5.f, detailRect.Y + offset, 3.f, thumb}, 1.5f, Gdiplus::Color(160, 255, 255, 255));
            }
            auto button = [&](unsigned action, const Gdiplus::RectF& r, const std::wstring& label) {
                const BYTE alpha = pressed == action ? 105 : hover == action ? 76 : 48;
                Rounded(g, r, 5.f, action == 1 ? Gdiplus::Color(255, 35, 108, 194) : Gdiplus::Color(alpha, 255, 255, 255));
                text(label, false, r, 255, true);
                if (GetFocus() == window && focused == action) {
                    Gdiplus::Pen pen(Gdiplus::Color(240, 255, 255, 255), 1.f);
                    g.DrawRectangle(&pen, r.X + 3, r.Y + 3, r.Width - 6, r.Height - 6);
                }
            };
            button(1, websiteRect, english ? L"Visit website" : L"\u67e5\u770b\u5b98\u7f51");
            button(2, laterRect, english ? L"Later" : L"\u7a0d\u540e");
            if (hover == 3 || pressed == 3 || (GetFocus() == window && focused == 3))
                Rounded(g, closeRect, 5.f, Gdiplus::Color(60, 255, 255, 255));
            Gdiplus::Pen cross(Gdiplus::Color(255, 255, 255, 255), 1.5f);
            const float cx = closeRect.X + 14, cy = closeRect.Y + 14;
            g.DrawLine(&cross, cx - 4, cy - 4, cx + 4, cy + 4);
            g.DrawLine(&cross, cx + 4, cy - 4, cx - 4, cy + 4);
        }
        // 仅一位 alpha 用于空白区域命中测试；文字维持不透明，不使用整窗降透明度。
        if (glass) for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            auto& pixel = dib.pixels[static_cast<std::size_t>(y) * width + x];
            if (!pixel && x >= Px(8, dpi) && x < width - Px(8, dpi) &&
                y >= Px(8, dpi) && y < height - Px(8, dpi)) pixel = 0x01000000u;
        }
        POINT source{}; SIZE size{width, height}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        return UpdateLayeredWindow(window, nullptr, nullptr, &size, dib.dc, &source, 0, &blend, ULW_ALPHA) != FALSE;
    }
    void RetireMaterial() {
        if (window) {
            if (materialTimer) KillTimer(window, materialTimer);
            if (orderTimer) KillTimer(window, orderTimer);
            RemovePropW(window, kGlass);
        }
        materialTimer = orderTimer = 0; glass = false; attached = false;
        material.Detach();
    }
    void Close() {
        if (closing) return;
        closing = true; ++generation;
        const auto target = std::exchange(window, nullptr);
        if (target) {
            KillTimer(target, dismissTimer); KillTimer(target, materialTimer); KillTimer(target, orderTimer);
            RemovePropW(target, kGlass);
            SetWindowLongPtrW(target, GWLP_USERDATA, 0);
        }
        dismissTimer = materialTimer = orderTimer = 0; actionPending = false; hover = pressed = 0;
        if (GetCapture() == target && target) ReleaseCapture();
        RetireMaterial();
        if (target && IsWindow(target)) DestroyWindow(target);
        dib.Clear(); closing = false;
    }
    void Stop() {
        if (stopped) return;
        stopped = true; openWebsite = {}; Close();
    }
    bool OrderMaterial() {
        struct Search { HWND owner, surface = nullptr; } search{window};
        EnumWindows([](HWND candidate, LPARAM data) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(data);
            DWORD process = 0; GetWindowThreadProcessId(candidate, &process);
            if (process == GetCurrentProcessId() &&
                reinterpret_cast<HWND>(GetPropW(candidate, lume::window_ids::GlassSurfaceOwnerProp)) == search.owner) {
                search.surface = candidate; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        HWND below = GetWindow(window, GW_HWNDNEXT);
        while (below && !IsWindowVisible(below)) below = GetWindow(below, GW_HWNDNEXT);
        if (search.surface && below == search.surface && IsWindowVisible(search.surface) &&
            (GetWindowLongPtrW(search.surface, GWL_EXSTYLE) & WS_EX_TOPMOST)) {
            if (orderTimer) KillTimer(window, orderTimer);
            orderTimer = 0; return true;
        }
        if (!orderTimer) {
            orderTimer = SetTimer(window, ++nextTimer, 30, nullptr);
            if (!orderTimer) return false;
        }
        if (search.surface) {
            // 材质工作线程拥有后层。显式进入 topmost 组再排到前景之后，
            // 异步执行避免两个 UI 线程在窗口排序消息中互等。
            constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS;
            if (!(GetWindowLongPtrW(search.surface, GWL_EXSTYLE) & WS_EX_TOPMOST))
                SetWindowPos(search.surface, HWND_TOPMOST, 0, 0, 0, 0, flags);
            SetWindowPos(search.surface, window, 0, 0, 0, 0, flags);
        }
        return false;
    }
    void RefreshMaterial() {
        if (!attached || !window || closing || stopped) return;
        const auto status = material.Status();
        if (status.failed) {
            RetireMaterial();
            if (window && !Render()) Close();
        } else if (status.frameReady && status.frames) {
            if (!OrderMaterial()) {
                if (glass) {
                    glass = false; RemovePropW(window, kGlass);
                    if (!Render()) { Close(); return; }
                }
                if (!materialTimer) materialTimer = SetTimer(window, ++nextTimer, materialTimeoutMs, nullptr);
                if (!materialTimer || !orderTimer) RetireMaterial();
                return;
            }
            glass = true;
            if (Render()) {
                SetPropW(window, kGlass, reinterpret_cast<HANDLE>(1));
                if (materialTimer) KillTimer(window, materialTimer);
                materialTimer = 0;
            } else {
                RetireMaterial();
                if (window && !Render()) Close();
            }
        } else if (glass) {
            glass = false; RemovePropW(window, kGlass);
            if (!Render()) { Close(); return; }
            if (!materialTimer) materialTimer = SetTimer(window, ++nextTimer, materialTimeoutMs, nullptr);
            if (!materialTimer) RetireMaterial();
        }
    }
    void AttachMaterial() {
        if (!window || stopped || closing || attached) return;
        LumeMaterialParameters p{}; p.size = sizeof(p); p.owner = window;
        p.frosted = 2; p.opticalProfile = 1; p.lightForeground = 1; p.cornerRadius = 8.f;
        BOOL animation = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0);
        p.reducedMotion = !animation;
        attached = material.Attach(p);
        if (!window || stopped || closing) { material.Detach(); return; }
        if (attached) {
            materialTimer = SetTimer(window, ++nextTimer, materialTimeoutMs, nullptr);
            if (!materialTimer) RetireMaterial();
            else RefreshMaterial();
        }
    }
    bool Show(const UpdateNotice& value) {
        if (!OnThread() || stopped || closing || !graphicsReady || !value.manual ||
            value.version.size() > 128 || value.detail.size() > 16384 ||
            value.version.find(L'\0') != std::wstring::npos || value.detail.find(L'\0') != std::wstring::npos) return false;
        notice = value; scroll = 0; actionPending = false; focused = 1; ++generation;
        pressed = hover = 0;
        if (window && GetCapture() == window) ReleaseCapture();
        if (!Layout(false)) { Close(); return false; }
        if (!window) {
            if (!CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                kClass, Title().c_str(), WS_POPUP, 0, 0, width, height, nullptr, nullptr, instance, this)) {
                Close(); return false;
            }
        }
        if (!window || stopped || !Layout() || !Render()) { Close(); return false; }
        SetWindowTextW(window, (L"Lume - " + Title()).c_str());
        if (dismissTimer) KillTimer(window, dismissTimer);
        dismissAt = GetTickCount64() + lifetimeMs;
        dismissTimer = SetTimer(window, ++nextTimer, lifetimeMs, nullptr);
        if (!dismissTimer) { Close(); return false; }
        const auto target = window;
        const auto current = generation;
        // 激活及窗口消息允许重入销毁；每个边界之后重新检查窗口身份。
        const auto style = GetWindowLongPtrW(target, GWL_EXSTYLE);
        SetWindowLongPtrW(target, GWL_EXSTYLE, value.manual ? style & ~WS_EX_NOACTIVATE : style | WS_EX_NOACTIVATE);
        ShowWindow(target, value.manual ? SW_SHOW : SW_SHOWNOACTIVATE);
        if (window != target || stopped || generation != current || !IsWindowVisible(target)) {
            if (generation == current) Close();
            return false;
        }
        if (value.manual) SetFocus(target);
        if (window != target || stopped || generation != current) return false;
        AttachMaterial();
        return window == target && !stopped && generation == current && IsWindowVisible(target);
    }
    Gdiplus::RectF ActionRect(unsigned action) const {
        if (action == 4 && detailExtent > detailRect.Height)
            return {detailRect.GetRight(), detailRect.Y, 10.f, detailRect.Height};
        return action == 1 ? websiteRect : action == 2 ? laterRect : action == 3 ? closeRect : Gdiplus::RectF{};
    }
    unsigned Hit(LPARAM lp) const {
        const Gdiplus::PointF point(GET_X_LPARAM(lp) * 96.f / dpi, GET_Y_LPARAM(lp) * 96.f / dpi);
        for (unsigned action : {1u, 2u, 3u, 4u}) if (ActionRect(action).Contains(point)) return action;
        return 0;
    }
    void Act(unsigned action) {
        if (stopped || closing || !window || !IsWindowVisible(window) || action < 1 || action > 3) return;
        const auto callback = action == 1 ? openWebsite : std::function<void()>{};
        // 先退休旧窗口；回调可以删除 presenter，或同步 Show 新版本。
        Close();
        if (callback) { try { callback(); } catch (...) { /* 宿主错误不能穿越 Win32 窗口过程。 */ } }
    }
    void QueueAction(unsigned action) {
        if (!actionPending && action && action <= 3 && window)
            actionPending = PostMessageW(window, kAction, action, static_cast<LPARAM>(generation)) != FALSE;
    }
    void Scroll(float amount) {
        scroll = std::clamp(scroll + amount, 0.f, std::max(0.f, detailExtent - detailRect.Height));
        if (!Render()) Close();
    }
    void DragScroll(LPARAM lp) {
        const float thumb = std::min(detailRect.Height, std::max(12.f, detailRect.Height * detailRect.Height / detailExtent));
        const float range = detailRect.Height - thumb;
        if (range <= 0) return;
        const float fraction = (GET_Y_LPARAM(lp) * 96.f / dpi - detailRect.Y - thumb / 2) / range;
        const float target = std::clamp(fraction, 0.f, 1.f) * std::max(0.f, detailExtent - detailRect.Height);
        Scroll(target - scroll);
    }
    static LRESULT CALLBACK Proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) {
            const auto raw = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            raw->window = w; SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(raw));
        }
        const auto raw = reinterpret_cast<Impl*>(GetWindowLongPtrW(w, GWLP_USERDATA));
        if (!raw) return DefWindowProcW(w, message, wp, lp);
        const auto self = raw->shared_from_this();
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(w, GWLP_USERDATA, 0);
            if (self->window == w) { self->window = nullptr; self->RetireMaterial(); self->dib.Clear(); }
            return DefWindowProcW(w, message, wp, lp);
        }
        if (self->stopped || self->closing) return DefWindowProcW(w, message, wp, lp);
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; BeginPaint(w, &ps); EndPaint(w, &ps);
            if (!self->layingOut && self->width && !self->Render()) self->Close();
            return 0;
        }
        case WM_MOUSEACTIVATE: return self->notice.manual ? MA_ACTIVATE : MA_NOACTIVATE;
        case WM_CLOSE: self->Close(); return 0;
        case WM_TIMER:
            if (wp && wp == self->dismissTimer && GetTickCount64() >= self->dismissAt) self->Close();
            else if (wp && wp == self->orderTimer) self->RefreshMaterial();
            else if (wp && wp == self->materialTimer) {
                self->RetireMaterial();
                if (self->window && !self->Render()) self->Close();
            }
            return 0;
        case LumeMaterialStatusChangedMessage: self->RefreshMaterial(); return 0;
        case kAction:
            if (self->actionPending && static_cast<std::uint64_t>(lp) == self->generation) {
                self->actionPending = false; self->Act(static_cast<unsigned>(wp));
            }
            return 0;
        case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: case WM_DPICHANGED:
            if (!self->layingOut && (!self->Layout() || !self->Render())) self->Close();
            return 0;
        case WM_SETFOCUS: case WM_KILLFOCUS:
            InvalidateRect(w, nullptr, FALSE); return 0;
        case WM_MOUSEMOVE: {
            if (self->pressed == 4) { self->DragScroll(lp); return 0; }
            const auto hover = self->Hit(lp);
            if (hover != self->hover) { self->hover = hover; InvalidateRect(w, nullptr, FALSE); }
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, w, 0}; TrackMouseEvent(&track);
            SetCursor(LoadCursorW(nullptr, hover ? IDC_HAND : IDC_ARROW)); return 0;
        }
        case WM_MOUSELEAVE: self->hover = 0; InvalidateRect(w, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN:
            self->pressed = self->Hit(lp);
            if (self->pressed) {
                if (self->pressed <= 3) self->focused = self->pressed;
                SetCapture(w);
                if (self->pressed == 4) self->DragScroll(lp);
            }
            InvalidateRect(w, nullptr, FALSE); return 0;
        case WM_LBUTTONUP: {
            const auto action = self->pressed; self->pressed = 0;
            if (GetCapture() == w) ReleaseCapture();
            if (self->window == w && action == self->Hit(lp)) self->QueueAction(action);
            InvalidateRect(w, nullptr, FALSE); return 0;
        }
        case WM_CAPTURECHANGED: self->pressed = 0; InvalidateRect(w, nullptr, FALSE); return 0;
        case WM_MOUSEWHEEL: self->Scroll(-GET_WHEEL_DELTA_WPARAM(wp) * 36.f / WHEEL_DELTA); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) self->Close();
            else if (wp == VK_TAB) {
                self->focused = (self->focused - 1 + (GetKeyState(VK_SHIFT) < 0 ? 2 : 1)) % 3 + 1;
                InvalidateRect(w, nullptr, FALSE);
            } else if ((wp == VK_RETURN || wp == VK_SPACE) && !(lp & (1LL << 30))) self->QueueAction(self->focused);
            else if (wp == VK_DOWN || wp == VK_NEXT) self->Scroll(wp == VK_NEXT ? self->detailRect.Height : 30.f);
            else if (wp == VK_UP || wp == VK_PRIOR) self->Scroll(wp == VK_PRIOR ? -self->detailRect.Height : -30.f);
            else if (wp == VK_HOME) self->Scroll(-self->detailExtent);
            else if (wp == VK_END) self->Scroll(self->detailExtent);
            return 0;
        }
        return DefWindowProcW(w, message, wp, lp);
    }
};

UpdatePresenter::UpdatePresenter(HINSTANCE instance, std::function<void()> callback)
    : impl_(std::make_shared<Impl>(instance, std::move(callback))) {}
UpdatePresenter::~UpdatePresenter() { Shutdown(); }
bool UpdatePresenter::Show(const UpdateNotice& notice) { const auto self = impl_; return self->Show(notice); }
void UpdatePresenter::Shutdown() { const auto self = impl_; if (self->OnThread()) self->Stop(); }
bool UpdatePresenter::HasVisible() const {
    const auto self = impl_;
    return !self->stopped && self->window && IsWindowVisible(self->window);
}
HWND UpdatePresenter::Handle() const { return impl_->window; }
void UpdatePresenter::SetLanguage(bool english) {
    const auto self = impl_;
    if (!self->OnThread() || self->stopped || self->english == english) return;
    self->english = english;
    if (self->window && (!self->Layout() || !self->Render())) self->Close();
    if (self->window) SetWindowTextW(self->window, (L"Lume - " + self->Title()).c_str());
}
bool UpdatePresenterTestAccess::Environment(UpdatePresenter& presenter, RECT work, UINT dpi, UINT lifetime, UINT materialTimeout) {
    const auto self = presenter.impl_;
    if (!self->OnThread() || self->stopped || self->window || dpi < 96 || dpi > 480 || !lifetime || !materialTimeout) return false;
    self->testEnvironment = true; self->testWork = work; self->testDpi = dpi;
    self->lifetimeMs = lifetime; self->materialTimeoutMs = materialTimeout;
    return true;
}
RECT UpdatePresenterTestAccess::ActionRect(const UpdatePresenter& presenter, unsigned action) {
    const auto self = presenter.impl_; const auto r = self->ActionRect(action);
    return {Px(r.X, self->dpi), Px(r.Y, self->dpi), Px(r.GetRight(), self->dpi), Px(r.GetBottom(), self->dpi)};
}
bool UpdatePresenterTestAccess::CanScroll(const UpdatePresenter& presenter) {
    const auto self = presenter.impl_; return self->detailExtent > self->detailRect.Height;
}
float UpdatePresenterTestAccess::ScrollOffset(const UpdatePresenter& presenter) { return presenter.impl_->scroll; }
UINT_PTR UpdatePresenterTestAccess::DismissTimer(const UpdatePresenter& presenter) { return presenter.impl_->dismissTimer; }
BYTE UpdatePresenterTestAccess::PixelAlpha(const UpdatePresenter& presenter, int x, int y) {
    const auto& dib = presenter.impl_->dib;
    if (!dib.pixels || x < 0 || y < 0 || x >= dib.width || y >= dib.height) return 0;
    return static_cast<BYTE>(dib.pixels[static_cast<std::size_t>(y) * dib.width + x] >> 24);
}
}
