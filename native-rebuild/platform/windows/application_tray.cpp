#include "platform/windows/application_tray.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/lume_resource.h"
#include "platform/windows/shell_activation.h"
#include <shellapi.h>
#include <utility>
namespace lume::platform::windows {
namespace {
constexpr UINT kTrayMessage = WM_APP + 41;
constexpr UINT kLoginMessage = WM_APP + 44;
constexpr UINT_PTR kHeartbeatTimer = 0x7b5;
constexpr const wchar_t* kTrayClass = lume::window_ids::TrayClass;
}
std::optional<TrayAction> MapTrayCommand(UINT command) {
    if (command == 1) return TrayAction::Open;
    if (command == 2) return TrayAction::Exit;
    if (command == 3) return TrayAction::ExportDiagnostics;
    return std::nullopt;
}
bool ApplicationTray::AddIcon() {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon); icon.hWnd = window_; icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; icon.uCallbackMessage = kTrayMessage;
    icon.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_LUME_APP));
    lstrcpynW(icon.szTip, L"Lume", static_cast<int>(sizeof(icon.szTip)/sizeof(wchar_t)));
    return Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}
bool ApplicationTray::ShowDiagnosticNotice(bool english) {
    if(!window_)return false;
    NOTIFYICONDATAW notice{};
    notice.cbSize=sizeof(notice);notice.hWnd=window_;notice.uID=1;
    notice.uFlags=NIF_INFO;notice.dwInfoFlags=NIIF_WARNING;
    lstrcpynW(notice.szInfoTitle,english?L"Lume diagnostics":L"Lume 诊断提醒",
        static_cast<int>(sizeof(notice.szInfoTitle)/sizeof(wchar_t)));
    lstrcpynW(notice.szInfo,english
        ?L"A desktop effect may not be working. Open Diagnostics & maintenance for details."
        :L"桌面效果可能未按设置生效，点击此处查看诊断与维护。",
        static_cast<int>(sizeof(notice.szInfo)/sizeof(wchar_t)));
    return Shell_NotifyIconW(NIM_MODIFY,&notice)!=FALSE;
}
bool ApplicationTray::ShowNotice(const std::wstring& title, const std::wstring& text) {
    if(!window_)return false;
    NOTIFYICONDATAW notice{};
    notice.cbSize=sizeof(notice);notice.hWnd=window_;notice.uID=1;
    notice.uFlags=NIF_INFO;notice.dwInfoFlags=NIIF_WARNING;
    lstrcpynW(notice.szInfoTitle,title.c_str(),static_cast<int>(sizeof(notice.szInfoTitle)/sizeof(wchar_t)));
    lstrcpynW(notice.szInfo,text.c_str(),static_cast<int>(sizeof(notice.szInfo)/sizeof(wchar_t)));
    return Shell_NotifyIconW(NIM_MODIFY,&notice)!=FALSE;
}
bool ApplicationTray::Create(HINSTANCE instance, std::function<void(TrayAction)> handler,
    std::function<void(const std::wstring&)> loginCallback,std::function<void()> heartbeat) {
    handler_ = std::move(handler);
    loginCallback_ = std::move(loginCallback);
    heartbeat_ = std::move(heartbeat);
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=WindowProc; wc.lpszClassName=kTrayClass;
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
    window_=CreateWindowExW(WS_EX_TOOLWINDOW,kTrayClass,L"Lume background",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,this);
    if (!window_) return false;
    if(heartbeat_)SetTimer(window_,kHeartbeatTimer,5000,nullptr);
    return AddIcon();
}
ApplicationTray::~ApplicationTray() {
    if (!window_) return;
    NOTIFYICONDATAW icon{}; icon.cbSize=sizeof(icon); icon.hWnd=window_; icon.uID=1;
    Shell_NotifyIconW(NIM_DELETE,&icon); DestroyWindow(window_);
}
LRESULT CALLBACK ApplicationTray::WindowProc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_NCCREATE) SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
    auto* self=reinterpret_cast<ApplicationTray*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd,message,wparam,lparam);
    if (message == RegisterWindowMessageW(L"TaskbarCreated")) { self->AddIcon(); return 0; }
    if (message == WM_COPYDATA) {
        if (!self->loginCallback_ || !lparam || self->pendingLogins_.size() >= 4) return 0;
        const auto value = DecodeLoginActivation(*reinterpret_cast<const COPYDATASTRUCT*>(lparam));
        if (!value) return 0;
        self->pendingLogins_.push_back(*value);
        if (!PostMessageW(hwnd, kLoginMessage, 0, 0)) { self->pendingLogins_.pop_back(); return 0; }
        return 1;
    }
    if (message == kLoginMessage) {
        if (!self->pendingLogins_.empty()) {
            const auto value = std::move(self->pendingLogins_.front());
            self->pendingLogins_.pop_front();
            const auto callback = self->loginCallback_;
            if (callback) callback(value);
        }
        return 0;
    }
    if(message==WM_TIMER&&wparam==kHeartbeatTimer) {
        if(self->heartbeat_)self->heartbeat_();
        return 0;
    }
    if (message == lume::window_ids::TrayOpenMessage) { if (self->handler_) self->handler_(TrayAction::Open); return 0; }
    if (message == kTrayMessage) {
        if (lparam == WM_LBUTTONDBLCLK) { if(self->handler_) self->handler_(TrayAction::Open); }
        else if(lparam == NIN_BALLOONUSERCLICK) { if(self->handler_) self->handler_(TrayAction::OpenDiagnostics); }
        else if(lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
            POINT point{}; GetCursorPos(&point); SetForegroundWindow(hwnd);
            const UINT result=LumeWidgetMenu::ShowItems(hwnd,point,{{L"打开 Lume",1},{L"导出诊断报告",3},{L"",0},{L"退出 Lume",2}},true);
            PostMessageW(hwnd,WM_NULL,0,0);
            if (self->handler_) {
                if (const auto action = MapTrayCommand(result)) self->handler_(*action);
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd,message,wparam,lparam);
}
}
