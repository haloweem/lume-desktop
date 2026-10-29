#include "apps/shell/desktop_icons_session.h"
#include "vendor/nlohmann/json.hpp"
#include <commctrl.h>
#include <objbase.h>
#include <cstdlib>
#include <iostream>
#include <map>
#include <atomic>
#include <thread>
#include <future>
#include <string_view>

using namespace lume;
using namespace platform::windows;
namespace {
void Check(bool ok, const char* why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
struct Files : settings::IFileStore {
    std::map<std::string, std::string> data;
    bool fail = false;
    bool Read(const std::string& path, std::string& out) const override {
        auto found = data.find(path); if (found == data.end()) return false;
        out = found->second; return true;
    }
    bool Exists(const std::string& path) const override { return data.count(path) != 0; }
    bool Write(const std::string& path, const std::string& value) override {
        if (fail) return false;
        data[path] = value; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (fail || !Exists(from)) return false;
        data[to] = data.at(from); data.erase(from); return true;
    }
    bool Remove(const std::string& path) override { data.erase(path); return true; }
};
nlohmann::json Json(const std::wstring& text) {
    return nlohmann::json::parse(text.begin(), text.end());
}
struct Fixture {
    std::promise<DesktopIconsTarget> ready;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread thread;
    DesktopIconsTarget target;
    std::atomic_bool rejectHide{false};
    static LRESULT CALLBACK ListProc(HWND hwnd, UINT message, WPARAM w, LPARAM l,
                                     UINT_PTR, DWORD_PTR context) {
        const auto& self = *reinterpret_cast<Fixture*>(context);
        if (message == WM_WINDOWPOSCHANGING && self.rejectHide) {
            // Refuse only this private list's visibility operation. The real
            // desktop and its input state remain untouched.
            reinterpret_cast<WINDOWPOS*>(l)->flags &= ~SWP_HIDEWINDOW;
        }
        return DefSubclassProc(hwnd, message, w, l);
    }
    Fixture() {
        auto future = ready.get_future();
        thread = std::thread([this] {
            INITCOMMONCONTROLSEX controls{}; controls.dwSize = sizeof(controls); controls.dwICC = ICC_LISTVIEW_CLASSES;
            InitCommonControlsEx(&controls);
            WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpfnWndProc = DefWindowProcW; wc.lpszClassName = L"SHELLDLL_DefView";
            RegisterClassExW(&wc);
            HWND shell = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Lume 图标会话验证",
                WS_POPUP | WS_VISIBLE, 70, 70, 220, 160, nullptr, nullptr, wc.hInstance, nullptr);
            HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 220, 160,
                shell, nullptr, wc.hInstance, nullptr);
            Check(SetWindowSubclass(list, ListProc, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE,
                  "private visibility failure fixture");
            ready.set_value({shell, list, GetCurrentProcessId(), GetCurrentThreadId()});
            for (;;) {
                const auto wake = MsgWaitForMultipleObjectsEx(1, &stop, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (wake == WAIT_OBJECT_0 || wake == WAIT_FAILED) break;
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message); DispatchMessageW(&message);
                }
            }
            DestroyWindow(shell);
        });
        target = future.get();
        Check(ValidDesktopIconsTarget(target), "private desktop fixture");
    }
    ~Fixture() { SetEvent(stop); thread.join(); CloseHandle(stop); }
};
template<class F> bool Until(F&& condition, DWORD timeout = 15000) {
    const auto deadline = GetTickCount64() + timeout;
    do {
        if (condition()) return true;
        Sleep(10);
    } while (GetTickCount64() < deadline);
    return condition();
}
}
int main(int argc, char** argv) {
    const bool persistenceOnly = argc == 2 && std::string_view(argv[1]) == "--persistence-only";
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    Fixture fixture;
    Files files;
    const auto discover = [&] { return std::vector<DesktopIconsTarget>{fixture.target}; };
    unsigned notices = 0;
    const auto owner = GetCurrentThreadId();
    const auto notify = [&](const std::wstring& message, bool) {
        Check(GetCurrentThreadId() == owner && Json(message)["type"] == "icons-state",
              "notifications return to owning session thread");
        ++notices;
    };
    {
        shell::DesktopIconsSession session(GetModuleHandleW(nullptr), "icons.json", notify, &files, discover);
        Check(session.Restore() && !session.Running() && files.data.empty(), "fresh session stays disabled and does not write");
        auto state = Json(session.State());
        Check(state["enabled"] == false && state["autoHideDelay"] == 5 &&
              state["enableAutoHide"] == false && state["showIconsOnAnyInput"] == true &&
              state["loadStatus"] == "fresh", "automatic hiding defaults to off in page state");
        Check(!session.HandlePage(ui::PageCommand::WeatherEnable), "unrelated feature command ignored");
        auto reply = session.HandlePage(ui::PageCommand::IconsAutoHideOff);
        Check(reply && Json(*reply)["saved"] == true && !session.Running(), "disabled feature stores setting without acquiring input");
        reply = session.HandlePage(ui::PageCommand::IconsEnable);
        Check(reply && Json(*reply)["saved"] == true && session.Running() &&
              Json(*reply)["desktops"] == 1 && DesktopIconsRuntime::ActiveWorkers() == 1, "actual runtime enabled and saved");
        MSG pending{};
        while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&pending); DispatchMessageW(&pending);
        }
        Check(notices > 0, "worker state change reaches live session on owning thread");
        const auto before = files.data.at("icons.json");
        files.fail = true;
        reply = session.HandlePage(ui::ParsePageCommand(L"v1|icons|delay|9"));
        Check(reply && Json(*reply)["error"] == "save" && Json(*reply)["autoHideDelay"] == 5 &&
              session.Running() && files.data.at("icons.json") == before, "running save failure preserves settings");
        reply = session.HandlePage(ui::PageCommand::IconsDisable);
        Check(reply && Json(*reply)["error"] == "save" && session.Running() &&
              files.data.at("icons.json") == before, "disable persistence failure restarts prior runtime");
        files.fail = false;
        reply = session.HandlePage(ui::ParsePageCommand(L"v1|icons|delay|9"));
        Check(reply && Json(*reply)["saved"] == true && Json(*reply)["autoHideDelay"] == 9,
              "source delay persisted");
        reply = session.HandlePage(ui::PageCommand::IconsRestoreOff);
        Check(reply && Json(*reply)["showIconsOnAnyInput"] == false, "restore option persisted");
        reply = session.HandlePage(ui::PageCommand::IconsAutoHideOn);
        Check(reply && Json(*reply)["enableAutoHide"] == true, "explicit automatic hiding is persisted");
        Check(session.Shutdown() && !session.Running() &&
              DesktopIconsRuntime::ActiveWorkers() == 0, "shutdown releases actual runtime");
        Check(!session.HandlePage(ui::PageCommand::IconsEnable), "shutdown session cannot be revived");
    }
    {
        shell::DesktopIconsSession session(GetModuleHandleW(nullptr), "icons.json", notify, &files, discover);
        Check(session.Restore() && session.Running() && Json(session.State())["autoHideDelay"] == 9 &&
              Json(session.State())["enableAutoHide"] == true,
              "explicit automatic hiding survives application shutdown");
        auto reply = session.HandlePage(ui::PageCommand::IconsReset);
        Check(reply && Json(*reply)["saved"] == true && Json(*reply)["autoHideDelay"] == 5 &&
              Json(*reply)["enableAutoHide"] == false && Json(*reply)["showIconsOnAnyInput"] == true &&
              Json(*reply)["enabled"] == true, "reset disables automatic hiding but preserves feature enable");
        reply = session.HandlePage(ui::PageCommand::IconsDisable);
        Check(reply && Json(*reply)["saved"] == true && !session.Running() &&
              !session.HasRecovery(), "disable persisted and no pending restore");
    }
    {
        shell::DesktopIconsSession session(GetModuleHandleW(nullptr), "icons.json", notify, &files, discover);
        Check(session.Restore() && !session.Running(), "unchecked module remains disabled next launch");
        session.HandlePage(ui::PageCommand::IconsRestoreOff);
        session.HandlePage(ui::PageCommand::IconsAutoHideOff);
        session.HandlePage(ui::ParsePageCommand(L"v1|icons|delay|11"));
        Check(session.Shutdown(), "save false icon preferences before exit");
    }
    {
        shell::DesktopIconsSession session(GetModuleHandleW(nullptr), "icons.json", notify, &files, discover);
        Check(session.Restore() && !session.Running() && Json(session.State())["enabled"] == false &&
              Json(session.State())["enableAutoHide"] == false && Json(session.State())["showIconsOnAnyInput"] == false &&
              Json(session.State())["autoHideDelay"] == 11, "all unchecked icon settings and custom delay survive restart");
        Check(session.Shutdown(), "false icon settings final shutdown");
    }
    if (persistenceOnly) {
        CoUninitialize();
        std::cout << "desktop icon settings restart and rollback passed (live idle policy excluded)\n";
        return 0;
    }
    files.data["icons.json"] = "corrupt";
    {
        shell::DesktopIconsSession session(GetModuleHandleW(nullptr), "icons.json", notify, &files, discover);
        Check(Json(session.State())["loadStatus"] == "recovered", "backup recovery disclosed");
        Check(session.Shutdown(), "not restored session owns no system state");
    }
    files.data["icons.json.bak"] = "corrupt too";
    {
        shell::DesktopIconsSession session(GetModuleHandleW(nullptr), "icons.json", notify, &files, discover);
        Check(session.Restore() && !session.Running() && Json(session.State())["loadStatus"] == "failed",
              "invalid state does not enable system effects");
        files.fail = true;
        auto reply = session.HandlePage(ui::PageCommand::IconsEnable);
        Check(reply && Json(*reply)["saved"] == false && !session.Running() &&
              DesktopIconsRuntime::ActiveWorkers() == 0, "enable failure unwinds actual runtime");
        files.fail = false;
    }
    {
        Files visibilityFiles;
        shell::DesktopIconsSession session(GetModuleHandleW(nullptr), "icons.json", notify,
                                            &visibilityFiles, discover);
        Check(session.Restore(), "visibility failure session starts from isolated memory settings");
        session.HandlePage(ui::PageCommand::IconsAutoHideOff);
        session.HandlePage(ui::ParsePageCommand(L"v1|icons|delay|3"));
        auto reply = session.HandlePage(ui::PageCommand::IconsEnable);
        Check(reply && Json(*reply)["saved"] == true && session.Running(),
              "start private runtime with no automatic visibility changes");
        Check(Until([&] {
            const auto o = ObserveDesktopIcons(fixture.target);
            return o.available && o.visible && o.inputKnown && !o.fullscreen &&
                   std::uint32_t(o.now - o.lastInput) >= 3000;
        }), "wait for observed idle before testing automatic hiding");
        fixture.rejectHide = true;
        reply = session.HandlePage(ui::PageCommand::IconsAutoHideOn);
        const auto failed = reply ? Json(*reply) : nlohmann::json{};
        std::cout << "saved_visibility_reply=" << failed.dump() << '\n';
        Check(reply && failed["result"] == "applied" && failed["saved"] == true &&
              failed["error"] == "visibility" && failed["enabled"] == true &&
              failed["running"] == true && failed["enableAutoHide"] == true &&
              failed["autoHideDelay"] == 3 && IsWindowVisible(fixture.target.list),
              "successful persistence retains the visibility error and current preferences");
        const auto stored = desktop_icons::PreferencesStore(visibilityFiles).Load("icons.json");
        Check(stored.state == desktop_icons::LoadState::Loaded && stored.value.enabled &&
              stored.value.enableAutoHide && stored.value.autoHideDelay == 3,
              "failed visibility acknowledgement does not roll back a saved preference");
        const auto snapshot = Json(session.State());
        Check(snapshot["result"] == "snapshot" && snapshot["saved"] == false &&
              snapshot["error"] == "visibility" && snapshot["enableAutoHide"] == true,
              "later error snapshot retains the saved preferences without another save acknowledgement");
        reply = session.HandlePage(ui::PageCommand::IconsDisable);
        Check(reply && Json(*reply)["saved"] == true && Json(*reply)["error"] == "" &&
              Json(*reply)["enabled"] == false && !session.Running() && !session.HasRecovery(),
              "visibility error still permits disabling and saving the actual disabled preference");
        fixture.rejectHide = false;
        reply = session.HandlePage(ui::PageCommand::IconsEnable);
        Check(reply && Json(*reply)["saved"] == true && Json(*reply)["error"] == "" &&
              Until([&] { return !IsWindowVisible(fixture.target.list); }, 5000),
              "retry applies the retained preferences after the private desktop responds");
        reply = session.HandlePage(ui::PageCommand::IconsDisable);
        Check(reply && Json(*reply)["saved"] == true && IsWindowVisible(fixture.target.list) &&
              DesktopIconsRuntime::ActiveWorkers() == 0,
              "retry shutdown restores the private target and releases the worker");
    }
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    CoUninitialize();
    std::cout << "desktop icon session persistence, rollback and native runtime passed\n";
}
