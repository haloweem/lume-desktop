#pragma once
#include <windows.h>
#include <functional>
#include <deque>
#include <optional>
#include <string>
namespace lume::platform::windows {
enum class TrayAction { Open, OpenDiagnostics, ExportDiagnostics, Exit };
std::optional<TrayAction> MapTrayCommand(UINT command);
class ApplicationTray final {
public:
    bool Create(HINSTANCE instance, std::function<void(TrayAction)> handler,
        std::function<void(const std::wstring&)> loginCallback = {},
        std::function<void()> heartbeat = {});
    bool ShowDiagnosticNotice(bool english);
    // A warning balloon; clicking it opens Settings > Report a problem.
    bool ShowNotice(const std::wstring& title, const std::wstring& text);
    HWND Window() const { return window_; }
    ~ApplicationTray();
private:
    HWND window_ = nullptr;
    std::function<void(TrayAction)> handler_;
    std::function<void(const std::wstring&)> loginCallback_;
    std::function<void()> heartbeat_;
    std::deque<std::wstring> pendingLogins_;
    bool AddIcon();
    static LRESULT CALLBACK WindowProc(HWND,UINT,WPARAM,LPARAM);
};
}
