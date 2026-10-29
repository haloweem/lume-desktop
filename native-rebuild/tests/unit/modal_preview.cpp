#include "platform/windows/webview_page_window.h"
#include <string>

int main(int argc, char** argv) {
    const std::string mode = argc == 2 ? argv[1] : "guest";
    if (argc > 2 || (mode != "guest" && mode != "member" && mode != "update")) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    lume::platform::windows::WebViewPageWindow window;
    return window.Run(GetModuleHandleW(nullptr), [&](lume::ui::PageCommand command) {
        if (command != lume::ui::PageCommand::Ready) return std::wstring{};
        if (mode == "member") window.Post(LR"({"version":1,"type":"account-state","revision":1,"status":"signed-in","busy":false,"error":"","profile":{"id":"preview","displayName":"Lume","avatarUrl":"","vip":false,"vipExpiresAt":""}})");
        if (mode == "update") return std::wstring(LR"({"version":1,"type":"app-update-notice","kind":"incompatible","updateVersion":"1.0.8","error":"update-incompatible","manual":false})");
        return std::wstring(LR"({"version":1,"type":"wallpaper-engagement-notice","id":"preview","error":"WALLPAPER_DOWNLOAD_STATS_QUOTA"})");
    }, true);
}
