// Opt-in live-desktop diagnostic; not part of CTest or the installer.
// No arguments: DockWindow only. --session: full DockSession with hidden
// launchpad prewarm. --no-preload: the same session without its prepare timer.
// --session-policy also enables the production display-policy watcher.
// Preferences are memory-only; the probe exits after three minutes.
#include "platform/windows/dock/dock_window.h"
#include "apps/shell/dock_session.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <objbase.h>
#include <cstdio>
#include <cstring>
#include <map>

using namespace lume::platform::windows;
namespace {
struct Files final : lume::settings::IFileStore {
    std::map<std::string,std::string> values;
    bool Read(const std::string& p,std::string& out) const override {auto i=values.find(p);if(i==values.end())return false;out=i->second;return true;}
    bool Exists(const std::string& p) const override {return values.count(p)!=0;}
    bool Write(const std::string& p,const std::string& value) override {values[p]=value;return true;}
    bool Rename(const std::string& a,const std::string& b) override {auto i=values.find(a);if(i==values.end())return false;values[b]=i->second;values.erase(i);return true;}
    bool Remove(const std::string& p) override {values.erase(p);return true;}
};
}
int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    const bool policyMode = argc > 1 && std::strcmp(argv[1], "--session-policy") == 0;
    const bool sessionMode = policyMode || (argc > 1 && (std::strcmp(argv[1], "--session") == 0 || std::strcmp(argv[1], "--no-preload") == 0));
    const bool noPreload = argc > 1 && std::strcmp(argv[1], "--no-preload") == 0;
    if (argc > 1 && !sessionMode) { CoUninitialize(); return 1; }
    {
        Files files;
        desktop_level::Watcher watcher;
        if(policyMode&&!watcher.Start(GetModuleHandleW(nullptr)))return 5;
        std::unique_ptr<lume::shell::DockSession> session;
        auto bitmap = std::make_shared<WidgetBitmap>();
        bitmap->width = bitmap->height = 64;
        bitmap->pixels.assign(64 * 64, 0xff579bd1u);
        DockWindowPorts ports;
        ports.icon = [bitmap](const lume::dock::Item&) { return bitmap; };
        DockWindow dock(GetModuleHandleW(nullptr), ports);
        DockAppearance look; look.transparent = true;
        if (sessionMode) {
            session=std::make_unique<lume::shell::DockSession>(GetModuleHandleW(nullptr),lume::shell::DockSessionOptions{"probe.json",true},
                lume::shell::DockSession::Notify{},std::function<void()>{},std::function<void()>{},[]{return false;},&files);
            session->HandlePage(lume::ui::ParsePageCommand(L"v1|dock|material|none"));
            session->HandlePage(lume::ui::ParsePageCommand(L"v1|dock|enabled|true"));
            if(!session->Running())return 4;
            if(noPreload) for(HWND h=FindWindowExW(HWND_MESSAGE,nullptr,L"LumeDockSession",nullptr);h;h=FindWindowExW(HWND_MESSAGE,h,L"LumeDockSession",nullptr)) {
                DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId())KillTimer(h,0x7b4);
            }
        } else if (!dock.Show({{lume::dock::Kind::Pinned,"C:/fixture-a.exe","Diagnostic A"},
                       {lume::dock::Kind::Pinned,"C:/fixture-b.exe","Diagnostic B"}}, look)) return 3;
        const HWND icons=session?session->Handle():dock.Handle();
        std::printf("READY pid=%lu icons=%p mode=%s\n", GetCurrentProcessId(), icons, argc>1?argv[1]:"normal");
        std::fflush(stdout);
        const auto until = GetTickCount64() + 180000;
        while (GetTickCount64() < until) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) return 0;
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            Sleep(2);
        }
        if(session)session->Shutdown();
        dock.Stop();
    }
    CoUninitialize();
    return 0;
}
