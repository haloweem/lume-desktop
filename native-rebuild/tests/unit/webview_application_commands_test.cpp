#include "platform/windows/webview_page_window.h"
#include "vendor/nlohmann/json.hpp"
#include <iostream>

namespace {
lume::platform::windows::WebViewPageWindow* active = nullptr;
bool timedOut = false;
void CALLBACK Deadline(HWND, UINT, UINT_PTR timer, DWORD) {
    KillTimer(nullptr, timer);
    timedOut = true;
    if (active) active->RequestClose();
}
}

int main() {
    using namespace lume;
    platform::windows::WebViewPageWindow window;
    active = &window;
    int settings = 0, account = 0;
    bool valid = true;
    window.ConfigureApplicationCommands([&](std::wstring_view message) {
        const auto json = nlohmann::json::parse(message);
        if (json.value("scope", "") == "app-settings") {
            valid = valid && json.value("action", "") == "language" && json.value("value", "") == "en";
            ++settings;
        } else if (json.value("scope", "") == "account") {
            valid = valid && json.value("action", "") == "login";
            ++account;
        } else valid = false;
        if (settings == 1 && account == 1) window.RequestClose();
    });
    const auto timer = SetTimer(nullptr, 0, 12000, Deadline);
    const auto result = window.Run(GetModuleHandleW(nullptr), [](ui::PageCommand) {
        return std::wstring{};
    }, true, LR"JS(
        chrome.webview.postMessage({version:1,scope:"account",action:"login",value:"x".repeat(9000)});
        chrome.webview.postMessage({version:1,scope:"app-settings",action:"language",value:"en"});
        chrome.webview.postMessage({version:1,scope:"account",action:"login"});
    )JS");
    KillTimer(nullptr, timer);
    active = nullptr;
    if (result || timedOut || !valid || settings != 1 || account != 1) {
        std::cerr << "FAIL: trusted bounded application messages were not deferred exactly once\n";
        return 1;
    }
    std::cout << "PASS: real WebView2 dispatches bounded settings/account messages outside COM events\n";
    return 0;
}
