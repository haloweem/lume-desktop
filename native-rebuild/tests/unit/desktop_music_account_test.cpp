#include "apps/shell/desktop_application.h"
#include <atomic>
#include <filesystem>
#include <iostream>

namespace {
using namespace lume::music::auth;
// Match the private desktop/WebView dispatch IDs without adding production test hooks.
constexpr UINT MusicWake = WM_APP + 43;
constexpr UINT ApplicationCommand = WM_APP + 26;
constexpr UINT LifecycleCleanup = WM_APP + 27;
struct Probe {
    lume::shell::DesktopApplication* app = nullptr;
    ULONGLONG started = 0;
    bool failed = false, opened = false, signedIn = false, loggedOut = false, fontsListed = false;
    bool holdMusic = false, replacementStarted = false, closed = false;
    bool blockPageCommands = false, minimized = false, minimizedState = false;
    std::atomic<bool> reopened{false}, pollEntered{false}, pollCancelled{false}, workerTimeout{false};
    std::atomic<int> begins{0}, saved{0}, cleared{0};

    void Check(bool value, const char* reason) {
        if (!value) { failed = true; std::cerr << "FAIL: " << reason << '\n'; }
    }
    void Stop() {
        holdMusic = false;
        app->Exit();
    }
    void Message(const std::wstring& text) {
        Check(text.find(L"fixture-native-secret") == std::wstring::npos,
              "native credentials never enter any public WebView message");
        const auto json = Json::parse(text);
        if (json.value("type", "") == "music-fonts-state" && json.at("request") == 77) {
            Check(json.size() == 5 && json.at("version") == 1 && json.at("error") == "",
                  "font bridge returns only the public v1 state fields without error");
            const auto& families = json.at("families");
            Check(families.is_array() && !families.empty(), "font enumeration returns installed family names");
            for (const auto& value : families) {
                Check(value.is_string(), "font bridge exposes family names, not file metadata");
                if (!value.is_string()) continue;
                const auto family = value.get<std::string>();
                Check(!family.empty() && family.find_first_of("/\\:") == std::string::npos,
                      "font family does not expose a filesystem path");
            }
            fontsListed = true;
        }
        if (json.value("type", "") == "music-state" && replacementStarted && !reopened) {
            if (!json.at("pageOpen").get<bool>()) closed = true;
            else if (closed) reopened = true;
        }
        if (json.value("type", "") == "music-provider-account-state") {
            Check(json.size() == 4 && json.at("version") == 1 && json.at("revision").is_number_unsigned() &&
                  json.at("accounts").is_array(), "exact public account state envelope");
            Check(json.at("accounts").size() == 3, "all three provider accounts stay present");
            for (const auto& account : json.at("accounts")) {
                Check(account.is_object() && account.size() == 11 && account.at("request").is_number_unsigned() &&
                      account.at("memberships").is_array(), "exact public account schema");
                for (const auto* key : {"provider", "status", "phase", "userId", "displayName",
                                       "qrImage", "qrText", "error", "avatarUrl"})
                    Check(account.at(key).is_string(), "account public field types");
                for (const auto& membership : account.at("memberships"))
                    Check(membership.is_object() && membership.size() == 6 &&
                          membership.at("kind").is_string() && membership.at("label").is_string() &&
                          membership.at("level").is_number_integer() && membership.at("active").is_boolean() &&
                          membership.at("iconUrl").is_string() && membership.at("expiresAt").is_number_integer(),
                          "exact public membership schema");
            }
            const auto& qq = json.at("accounts").at(1);
            Check(qq.at("provider") == "qq", "QQ state remains in its provider slot");
            const auto request = qq.at("request").get<unsigned>();
            const auto status = qq.at("status").get<std::string>();
            if (request == 39 && status == "authorizing") holdMusic = true;
            if (request == 41 && status == "loading" && !replacementStarted) {
                Check(holdMusic && !closed, "replacement begins before queued close/open drains");
                replacementStarted = true;
                holdMusic = false;
                Check(PostThreadMessageW(GetCurrentThreadId(), MusicWake, 0, 0) != FALSE,
                      "release the deliberately deferred music queue");
            }
            if (request == 41 && status == "signed-in") {
                Check(reopened && saved == 1, "replacement login survives the old queued close");
                Check(qq.at("avatarUrl") == "https://q1.qlogo.cn/fixture.jpg" &&
                      qq.at("memberships") == Json::array({{
                          {"kind", "qq-green-vip"}, {"label", "Green VIP"}, {"level", 5}, {"active", true},
                          {"iconUrl", "https://y.qq.com/vip5.png"}, {"expiresAt", 2'000'000'000'000LL}
                      }}), "avatar and all membership fields cross the WebView bridge");
                signedIn = true;
            }
            if (request == 41)
                Check(status == "loading" || status == "authorizing" || status == "signed-in",
                      "queued close must not cancel the replacement login");
            if (request == 42 && status == "signed-out") {
                Check(signedIn && cleared == 1, "logout completes after the login persistence");
                Check(qq.at("avatarUrl") == "" && qq.at("memberships").empty(), "logout clears public profile");
                loggedOut = true;
            }
            if (request == 43 && status == "signed-out") {
                Check(minimized && blockPageCommands, "minimize cancels without page commands");
                Check(qq.at("qrImage") == "" && qq.at("qrText") == "" &&
                      qq.at("userId") == "" && qq.at("displayName") == "" &&
                      qq.at("avatarUrl") == "" && qq.at("memberships").empty(),
                      "native cancellation removes QR and uncommitted identity");
                minimizedState = true;
            }
            if (request == 43)
                Check(status == "loading" || status == "authorizing" || status == "signed-out",
                      "minimized attempt never publishes its late signed-in reply");
        }
        if (failed) Stop();
    }
    void Tick() {
        if (GetTickCount64() - started > 20000 || workerTimeout) {
            Check(false, "bounded WebView/worker integration deadline");
            Stop();
        } else if (!opened) {
            opened = true;
            app->Open(lume::shell::ManagerPage::Music);
        } else if (pollEntered && !minimized) {
            Check(loggedOut && saved == 1 && cleared == 1, "roundtrip finishes before minimize test");
            blockPageCommands = true;
            minimized = true;
            ShowWindow(app->ManagerHandle(), SW_MINIMIZE);
            Check(IsIconic(app->ManagerHandle()) != FALSE, "real manager window is minimized");
        } else if (minimizedState && pollCancelled) {
            Check(saved == 1 && cleared == 1, "cancelled provider reply cannot replace stored account");
            Stop();
        }
        if (failed) Stop();
    }
};
Probe* active = nullptr;
void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) { if (active) active->Tick(); }
LRESULT CALLBACK GateMessages(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && wparam == PM_REMOVE && active) {
        auto& message = *reinterpret_cast<MSG*>(lparam);
        // Force close/open/begin to arrive before DrainMusic; do not rely on IPC timing.
        const bool music = active->holdMusic && !message.hwnd && message.message == MusicWake;
        // JS visibility/cancel and lifecycle ACK cannot rescue a missing native hide callback.
        const bool page = active->blockPageCommands && message.hwnd == active->app->ManagerHandle() &&
            (message.message == ApplicationCommand || message.message == LifecycleCleanup);
        if (music || page) message.message = WM_NULL;
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}
}
int main() {
    const auto root = std::filesystem::current_path() /
        ("desktop-music-account-fixture-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    if (!std::filesystem::create_directory(root)) return 1;
    lume::shell::DesktopApplicationOptions options;
    options.verification = true;
    options.startHidden = true;
    options.clockPreferencesPath = (root / "clock.json").u8string();
    options.calendarPreferencesPath = (root / "calendar.json").u8string();
    options.applicationSettings.preferencesPath = (root / "application.json").u8string();
    options.accountOptions.storageDirectory = (root / "account").u8string();
    options.wallpaperOptions.root = (root / "wallpapers").u8string();
    options.musicPreferencesPath = (root / "music.json").u8string();
    Probe probe;
    options.musicAccounts.pollInterval = std::chrono::milliseconds(10);
    options.musicAccounts.load = [](Provider) { return Json::object(); };
    options.musicAccounts.save = [&](Provider, const Json&) { ++probe.saved; };
    options.musicAccounts.clear = [&](Provider) { ++probe.cleared; };
    options.musicAccounts.execute = [&](Provider, Operation operation, Json& secrets, const Cancelled& cancelled) {
        secrets["token"] = "fixture-native-secret";
        Reply result;
        if (operation == Operation::Begin) {
            secrets["attempt"] = ++probe.begins;
            result.status = "authorizing";
            result.phase = "waiting";
            result.qrImage = "data:image/png;base64,YQ==";
        } else {
            const auto attempt = secrets.at("attempt").get<int>();
            if (attempt == 1) {
                result.status = "authorizing";
                result.phase = "waiting";
                return result;
            }
            if (attempt == 3) probe.pollEntered = true;
            const auto deadline = GetTickCount64() + 5000;
            while (!cancelled() && !(attempt == 2 && probe.reopened)) {
                if (GetTickCount64() >= deadline) {
                    probe.workerTimeout = true;
                    break;
                }
                Sleep(5);
            }
            if (attempt == 3) probe.pollCancelled = cancelled();
            // Deliberately return success even after cancellation: native must discard it.
            result.status = "signed-in";
            result.userId = "123";
            result.displayName = "Fixture";
            result.avatarUrl = "https://q1.qlogo.cn/fixture.jpg";
            result.memberships = {{"qq-green-vip", "Green VIP", 5, true, "https://y.qq.com/vip5.png", 2'000'000'000'000LL}};
        }
        return result;
    };
    options.readyScript = LR"JS(
        (() => {
            const webview=window.chrome.webview;
            let stage=0;
            const account=(action,request)=>webview.postMessage({
                version:1,scope:'music-provider-account',action,provider:'qq',request});
            webview.addEventListener('message',({data})=>{
                if(data.type==='music-fonts-state'&&data.request===77&&stage===0){
                    if(data.error===''&&Array.isArray(data.families)&&data.families.length)account('begin',39);
                    return;
                }
                if(data.type!=='music-provider-account-state')return;
                const qq=data.accounts.find(account=>account.provider==='qq');
                if(stage===0&&qq.request===39&&qq.status==='authorizing'){
                    stage=1;
                    webview.postMessage({version:1,scope:'music',action:'close'});
                    webview.postMessage({version:1,scope:'music',action:'open'});
                    account('begin',41);
                }else if(stage===1&&qq.request===41&&qq.status==='signed-in'){
                    stage=2;
                    account('logout',42);
                }else if(stage===2&&qq.request===42&&qq.status==='signed-out'){
                    stage=3;
                    account('begin',43);
                }
            });
            webview.postMessage({version:1,scope:'music-fonts',action:'list',request:77});
        })();
    )JS";
    options.onMessage = [&](const auto& text) { probe.Message(text); };
    {
        lume::shell::DesktopApplication application(GetModuleHandleW(nullptr), std::move(options));
        probe.app = &application;
        probe.started = GetTickCount64();
        active = &probe;
        const auto timer = SetTimer(nullptr, 0, 100, Tick);
        const auto hook = SetWindowsHookExW(WH_GETMESSAGE, GateMessages, nullptr, GetCurrentThreadId());
        probe.Check(timer != 0 && hook != nullptr, "bounded timer and deterministic dispatch gate installed");
        if (!probe.failed) probe.Check(application.Run() == 0, "isolated shell exits successfully");
        if (hook) UnhookWindowsHookEx(hook);
        KillTimer(nullptr, timer);
        active = nullptr;
    }
    probe.Check(probe.replacementStarted && probe.closed && probe.reopened && probe.signedIn,
                "quick close/open/begin ordering completes");
    probe.Check(probe.fontsListed, "real WebView font list request/reply completes before login");
    probe.Check(probe.loggedOut && probe.saved == 1 && probe.cleared == 1,
                "isolated WebView async QR confirmation/logout roundtrip");
    probe.Check(probe.minimized && probe.minimizedState && probe.pollCancelled && !probe.workerTimeout &&
                probe.begins == 3, "native minimize cancels a live worker without JS acknowledgement");
    const bool passed = !probe.failed;
    std::cout << (passed ? "PASS" : "FAIL")
              << ": WebView close/open/begin, login/logout, native minimize, fonts and secret guard\n";
    if (passed && root.parent_path() == std::filesystem::current_path() &&
        root.filename().u8string().find("desktop-music-account-fixture-") == 0) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    return passed ? 0 : 1;
}
