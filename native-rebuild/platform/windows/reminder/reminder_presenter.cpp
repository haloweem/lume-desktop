#include "platform/windows/reminder/reminder_presenter.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace lume::platform::windows {
namespace {
constexpr wchar_t kClass[] = L"LumeNativeReminder";
constexpr UINT kAction = WM_APP + 0x391;
int Px(float value, UINT dpi) { return static_cast<int>(std::lround(value * static_cast<float>(dpi) / 96.f)); }
Gdiplus::Color Color(COLORREF c, BYTE alpha = 255) { return {alpha, GetRValue(c), GetGValue(c), GetBValue(c)}; }
bool Same(const ReminderNotice& a, const ReminderNotice& b) { return a.taskId == b.taskId && a.reminderAt == b.reminderAt; }
UINT MonitorDpi(HMONITOR monitor) {
    // Dynamically resolve to avoid adding a shcore import dependency.
    const auto library = LoadLibraryW(L"shcore.dll");
    UINT x = 96, y = 96;
    if (library) {
        using Query = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        const auto address = GetProcAddress(library, "GetDpiForMonitor");
        Query query = nullptr;
        static_assert(sizeof(query)==sizeof(address));
        std::memcpy(&query,&address,sizeof(query));
        if (!query || FAILED(query(monitor, 0, &x, &y))) x = 96;
        FreeLibrary(library);
    }
    return x ? x : 96;
}
void Round(Gdiplus::Graphics& graphics, Gdiplus::RectF rect, float radius,
           Gdiplus::Color fill, Gdiplus::Color border = Gdiplus::Color(0,0,0,0)) {
    const float d = std::min({radius * 2.f, rect.Width, rect.Height});
    if (d <= 0) return;
    Gdiplus::GraphicsPath path;
    path.AddArc(rect.X, rect.Y, d, d, 180, 90);
    path.AddArc(rect.GetRight()-d, rect.Y, d, d, 270, 90);
    path.AddArc(rect.GetRight()-d, rect.GetBottom()-d, d, d, 0, 90);
    path.AddArc(rect.X, rect.GetBottom()-d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(fill); graphics.FillPath(&brush, &path);
    if (border.GetA()) { Gdiplus::Pen pen(border, 1.f); graphics.DrawPath(&pen, &path); }
}
}

struct ReminderPresenter::Impl : std::enable_shared_from_this<Impl> {
    struct Entry : std::enable_shared_from_this<Entry> {
        std::weak_ptr<Impl> owner;
        ReminderNotice notice;
        HWND window = nullptr;
        UINT dpi = 96;
        bool ready = false, actionPending = false;
        int hover = 0, pressed = 0;
        Gdiplus::RectF close{}, open{};
    };
    HINSTANCE instance;
    Open open;
    Changed changed;
    std::function<bool()> allowed;
    ReminderAppearance appearance;
    std::vector<std::shared_ptr<Entry>> entries;
    ULONG_PTR graphicsToken = 0;
    bool stopped = false, layingOut = false, graphicsReady = false;

    Impl(HINSTANCE i, Open o, Changed c, std::function<bool()> a)
        : instance(i), open(std::move(o)), changed(std::move(c)), allowed(std::move(a)) {
        Gdiplus::GdiplusStartupInput input;
        graphicsReady = Gdiplus::GdiplusStartup(&graphicsToken, &input, nullptr) == Gdiplus::Ok;
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = instance;
        wc.lpfnWndProc = Proc; wc.lpszClassName = kClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
    }
    ~Impl() { Stop(); if (graphicsReady) Gdiplus::GdiplusShutdown(graphicsToken); }
    struct Geometry { RECT area; UINT dpi; int width, height, margin, gap; std::size_t capacity; };
    Geometry Bounds() const {
        RECT anchor = appearance.anchor;
        const auto monitor = IsRectEmpty(&anchor) ? MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY)
                                                  : MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{}; info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &info.rcWork, 0);
        const UINT dpi = MonitorDpi(monitor);
        const int margin = Px(16,dpi), gap = Px(12,dpi);
        const int width = Px(360,dpi), height = Px(204,dpi);
        const int availableWidth = std::max(0, static_cast<int>(info.rcWork.right-info.rcWork.left)-2*margin);
        const int availableHeight = std::max(0, static_cast<int>(info.rcWork.bottom-info.rcWork.top)-2*margin);
        const auto capacity = availableWidth < width ? 0 : std::clamp((availableHeight+gap)/(height+gap),0,3);
        // Keep the complete title and actions usable. Work areas too small for
        // one full notice leave delivery in the host queue until a display change.
        return {info.rcWork, dpi, width, height, margin, gap, static_cast<std::size_t>(capacity)};
    }
    void Layout() {
        if (stopped || layingOut) return;
        layingOut = true;
        const bool permitted = !allowed || allowed();
        const auto g = Bounds();
        for (std::size_t i=0; i<entries.size(); ++i) {
            const auto& entry = entries[i];
            if (!entry->window) continue;
            if (!permitted) { ShowWindow(entry->window, SW_HIDE); continue; }
            entry->dpi = g.dpi;
            const int y = g.area.bottom-g.margin-g.height-static_cast<int>(i)*(g.height+g.gap);
            SetWindowPos(entry->window, HWND_NOTOPMOST, g.area.right-g.margin-g.width, y,
                         g.width, g.height, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            ShowWindow(entry->window, entry->ready && i<g.capacity ? SW_SHOWNOACTIVATE : SW_HIDE);
            InvalidateRect(entry->window, nullptr, FALSE);
        }
        layingOut = false;
    }
    void Remove(const std::shared_ptr<Entry>& entry) {
        const auto found = std::find(entries.begin(),entries.end(),entry);
        if (found == entries.end()) return;
        entries.erase(found);
        if (entry->window) DestroyWindow(entry->window);
        Layout();
    }
    void Act(const std::shared_ptr<Entry>& entry, bool shouldOpen) {
        if (stopped || !entry->ready) return;
        // User callbacks may synchronously destroy the presenter or call Keep.
        // Proc holds both the entry and Impl alive until this dispatch returns.
        const auto callback = open;
        const auto taskId = entry->notice.taskId;
        if (shouldOpen && (!callback || !callback(taskId))) {
            // Opening may fail transiently (for example, while another editor
            // owns the calendar). Keep the notice and let the user try again.
            entry->actionPending = false;
            return;
        }
        if (stopped) return;
        Remove(entry);
        const auto notify = changed;
        if (notify) notify();
    }
    void Stop() {
        if (stopped) return;
        stopped = true;
        auto old = std::move(entries);
        for (const auto& entry : old) if (entry->window) DestroyWindow(entry->window);
        open = {}; changed = {};
    }
    static int Hit(const Entry& entry, LPARAM lp) {
        const float scale = static_cast<float>(entry.dpi)/96.f;
        const Gdiplus::PointF p(static_cast<float>(GET_X_LPARAM(lp))/scale,static_cast<float>(GET_Y_LPARAM(lp))/scale);
        return entry.close.Contains(p) ? 1 : entry.open.Contains(p) ? 2 : 0;
    }
    bool Render(Entry& entry) {
        const auto dc = GetDC(entry.window);
        if (!dc) return false;
        RECT r{}; GetClientRect(entry.window,&r);
        if (r.right <= 0 || r.bottom <= 0) { ReleaseDC(entry.window,dc); return false; }
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=r.right; info.bmiHeader.biHeight=-r.bottom;
        info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        void* pixels=nullptr;
        const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        const auto memory=CreateCompatibleDC(dc);
        if (!bitmap || !memory) {
            if (bitmap) DeleteObject(bitmap);
            if (memory) DeleteDC(memory);
            ReleaseDC(entry.window,dc); return false;
        }
        const auto old=SelectObject(memory,bitmap);
        {
            // A premultiplied layered surface has no DWM frame or shadow. Only
            // the 22 DIP antialiased outline is transparent; the body is solid.
            Gdiplus::Bitmap image(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
            Gdiplus::Graphics graphics(&image);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const float scale = static_cast<float>(entry.dpi)/96.f;
            graphics.ScaleTransform(scale,scale);
            const float width = static_cast<float>(r.right)/scale, height = static_cast<float>(r.bottom)/scale;
            const bool compact = height<190.f;
            const float x = compact ? 14.f : 18.f, top = compact ? 10.f : 16.f;
            const COLORREF fg = appearance.foreground;
            const bool light = GetRValue(fg)*299+GetGValue(fg)*587+GetBValue(fg)*114 >= 128000;
            const auto bg = light ? Gdiplus::Color(255,35,39,46) : Gdiplus::Color(255,239,241,245);
            graphics.Clear(Gdiplus::Color(0,0,0,0));
            Round(graphics,{0,0,width,height},22.f,bg);
            const auto color = Color(fg);
            auto text = [&](const std::wstring& value, Gdiplus::RectF rect,
                            BYTE alpha, bool multiline = false, bool heading = false) {
                direct_write::Style style;
                style.size = heading ? (compact ? 17.f : 20.f) : 12.f;
                style.weight = heading ? FW_SEMIBOLD : FW_NORMAL;
                style.flags = DT_NOPREFIX | DT_END_ELLIPSIS |
                    (multiline ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER);
                direct_write::Draw(graphics, value, rect, Color(fg,alpha), style);
            };
            // Bell icon, matching the original 18 DIP outline in its 24 DIP slot.
            Gdiplus::Pen bell(Color(appearance.accent),1.7f); bell.SetLineJoin(Gdiplus::LineJoinRound);
            const float bx=x+12.f, by=top+14.f;
            Gdiplus::GraphicsPath bellPath;
            bellPath.AddBezier(bx-6,by+4,bx-4,by+1,bx-6,by-6,bx,by-6);
            bellPath.AddBezier(bx,by-6,bx+6,by-6,bx+4,by+1,bx+6,by+4);
            bellPath.AddLine(bx+6,by+4,bx-6,by+4);
            graphics.DrawPath(&bell,&bellPath); graphics.DrawArc(&bell,bx-2,by+4,4.f,4.f,0.f,180.f);
            text(L"Lume · 日程提醒",{x+32,top,width-2*x-68,28},255);
            entry.close = {width-x-28,top,28,28};
            if (entry.hover==1 || entry.pressed==1) Round(graphics,entry.close,6,Color(fg,entry.pressed==1?38:20));
            Gdiplus::Pen cross(color,1.5f);
            const float cx=entry.close.X+14, cy=entry.close.Y+14;
            graphics.DrawLine(&cross,cx-4.5f,cy-4.5f,cx+4.5f,cy+4.5f);
            graphics.DrawLine(&cross,cx+4.5f,cy-4.5f,cx-4.5f,cy+4.5f);
            const float titleY=top+28+(compact?6.f:10.f);
            const float titleHeight=compact?25.f:56.f;
            text(entry.notice.title.empty()?L"日程提醒":entry.notice.title,{x,titleY,width-2*x,titleHeight},255,!compact,true);
            text(entry.notice.time,{x,titleY+titleHeight+(compact?6.f:10.f),width-2*x,18},199);
            const float footerY=std::max(top+28.f,height-top-30.f);
            entry.open={width-x-74,footerY,74,30};
            Round(graphics,entry.open,6,Color(fg,entry.pressed==2?55:entry.hover==2?42:30),Color(fg,56));
            text(L"查看日程",{entry.open.X+10,footerY,58,30},255);
            text(entry.notice.status,{x,footerY,std::max(0.f,entry.open.X-x-10),30},217);
        }
        POINT origin{}; SIZE size{r.right,r.bottom};
        BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        const bool rendered = UpdateLayeredWindow(entry.window,dc,nullptr,&size,memory,&origin,0,&blend,ULW_ALPHA) != FALSE;
        SelectObject(memory,old); DeleteDC(memory); DeleteObject(bitmap);
        ReleaseDC(entry.window,dc);
        return rendered;
    }
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        if (message==WM_NCCREATE) {
            const auto entry=static_cast<Entry*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            entry->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(entry));
        }
        const auto raw=reinterpret_cast<Entry*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (!raw) return DefWindowProcW(window,message,wp,lp);
        const auto entry=raw->shared_from_this(); const auto self=entry->owner.lock();
        if (message==WM_NCDESTROY) {
            SetWindowLongPtrW(window,GWLP_USERDATA,0); entry->window=nullptr;
            return DefWindowProcW(window,message,wp,lp);
        }
        if (!self) return DefWindowProcW(window,message,wp,lp);
        switch (message) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; BeginPaint(window,&paint);
            self->Render(*entry); EndPaint(window,&paint); return 0;
        }
        case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: case WM_DPICHANGED: {
            if (self->layingOut) return 0;
            self->Layout();
            const auto notify=self->changed;
            if (notify) notify();
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int hover=Hit(*entry,lp);
            if (entry->hover!=hover) { entry->hover=hover; InvalidateRect(window,nullptr,FALSE); }
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0}; TrackMouseEvent(&track);
            SetCursor(LoadCursorW(nullptr,hover?IDC_HAND:IDC_ARROW)); return 0;
        }
        case WM_MOUSELEAVE: entry->hover=0; InvalidateRect(window,nullptr,FALSE); return 0;
        case WM_LBUTTONDOWN:
            entry->pressed=Hit(*entry,lp);
            if (entry->pressed) SetCapture(window);
            InvalidateRect(window,nullptr,FALSE); return 0;
        case WM_LBUTTONUP: {
            const int action=entry->pressed; entry->pressed=0;
            if (GetCapture()==window) ReleaseCapture();
            InvalidateRect(window,nullptr,FALSE);
            if (action && action==Hit(*entry,lp) && !entry->actionPending) {
                entry->actionPending=PostMessageW(window,kAction,static_cast<WPARAM>(action),0)!=FALSE;
            }
            return 0;
        }
        case WM_CAPTURECHANGED: entry->pressed=0; InvalidateRect(window,nullptr,FALSE); return 0;
        case WM_CLOSE:
            if (!entry->actionPending) entry->actionPending=PostMessageW(window,kAction,1,0)!=FALSE;
            return 0;
        case kAction: self->Act(entry,wp==2); return 0;
        }
        return DefWindowProcW(window,message,wp,lp);
    }
};

ReminderPresenter::ReminderPresenter(HINSTANCE i, Open o, Changed c, std::function<bool()> a)
    : impl_(std::make_shared<Impl>(i,std::move(o),std::move(c),std::move(a))) {}
ReminderPresenter::~ReminderPresenter() { Shutdown(); }
std::size_t ReminderPresenter::Capacity() const {
    const auto self=impl_; return self->stopped ? 0 : self->Bounds().capacity;
}
bool ReminderPresenter::CanShow() const {
    const auto self=impl_; return !self->stopped && self->graphicsReady && self->entries.size()<self->Bounds().capacity;
}
bool ReminderPresenter::Show(const ReminderNotice& notice, const ReminderAppearance& appearance, std::function<bool()> beforeShow) {
    const auto self=impl_;
    if (self->stopped || !self->graphicsReady || notice.taskId.empty()) return false;
    self->appearance=appearance; self->Layout();
    if (self->entries.size()>=self->Bounds().capacity || std::any_of(self->entries.begin(),self->entries.end(),
        [&](const auto& entry){return Same(entry->notice,notice);})) return false;
    const auto entry=std::make_shared<Impl::Entry>(); entry->owner=self; entry->notice=notice;
    const auto g=self->Bounds(); entry->dpi=g.dpi;
    if (!CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,kClass,L"Lume 日程提醒",WS_POPUP,
        g.area.right-g.margin-g.width,g.area.bottom-g.margin-g.height,g.width,g.height,nullptr,nullptr,self->instance,entry.get())) return false;
    self->entries.push_back(entry);
    // Allocate and upload the layered surface while hidden. Creation alone does
    // not guarantee a paintable window; failed drawing must not consume a key.
    if (!self->Render(*entry)) { self->Remove(entry); return false; }
    bool accepted=false;
    try { accepted=beforeShow && beforeShow(); }
    catch (...) { self->Remove(entry); throw; }
    if (!accepted || self->stopped || !entry->window) { self->Remove(entry); return false; }
    // Attention can change inside the commit callback or rendering. A delivered
    // key may be consumed silently, but its hidden window must never reappear.
    if (self->allowed && !self->allowed()) { self->Remove(entry); return true; }
    entry->ready=true; self->Layout();
    return true;
}
void ReminderPresenter::UpdateAppearance(const ReminderAppearance& appearance) {
    const auto self=impl_; if (self->stopped) return; self->appearance=appearance; self->Layout();
}
void ReminderPresenter::Keep(const std::vector<ReminderNotice>& active) {
    const auto self=impl_; if (self->stopped) return;
    const auto snapshot=self->entries; bool removed=false;
    for (const auto& entry:snapshot) {
        const auto found=std::find_if(active.begin(),active.end(),[&](const auto& notice){return Same(entry->notice,notice);});
        if (found==active.end()) { self->Remove(entry); removed=true; }
        else { entry->notice=*found; if (entry->window) InvalidateRect(entry->window,nullptr,FALSE); }
    }
    const auto notify=self->changed; if (removed && notify) notify();
}
bool ReminderPresenter::HasVisible() const {
    const auto self=impl_; return std::any_of(self->entries.begin(),self->entries.end(),[](const auto& entry){return entry->window && IsWindowVisible(entry->window);});
}
std::size_t ReminderPresenter::Count() const { return impl_->entries.size(); }
HWND ReminderPresenter::Handle(std::size_t index) const { return index<impl_->entries.size()?impl_->entries[index]->window:nullptr; }
void ReminderPresenter::Shutdown() { const auto self=impl_; self->Stop(); }
}
