#pragma once
#include "core/application_settings/application_settings.h"
#include "core/ui/application_settings_page_protocol.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace lume::shell {
struct ApplicationSettingsTestPorts {
    std::shared_ptr<application_settings::IPreferencesFile> preferencesFile;
    std::shared_ptr<application_settings::IStartupRegistration> startup;
    std::shared_ptr<application_settings::IUpdateTransport> updates;
    std::wstring executablePath;
    std::shared_ptr<application_settings::IUpdateInstaller> installer;
};
struct ApplicationSettingsOptions {
    std::string preferencesPath;
    bool verification = false;
    ApplicationSettingsTestPorts testPorts;
    // Display-only data folder; the shell resolves it so this session carries no
    // platform dependency. Empty in tests.
    std::string dataDirectory;
    // Resolves the wallpaper root for a preferred directory (empty follows the
    // program). Called again after a folder is picked.
    std::function<std::string(const std::string&)> wallpaperDirectory;
    // Stores a chosen user-data folder for the next launch; the shell owns the
    // persistence so this session stays platform-free.
    std::function<bool(const std::string&)> setDataDirectory;
};
class ApplicationSettingsSession final {
public:
    using Notify = std::function<void(const std::wstring&)>;
    using UpdateObserver = std::function<void(bool manual, const application_settings::ManifestResult&)>;
    // Construct, issue commands, and destroy on the UI thread.
    // Construction only reads preferences; applyBalancedMode runs on our worker.
    ApplicationSettingsSession(HINSTANCE, ApplicationSettingsOptions, Notify,
                               std::function<bool(bool)> applyBalancedMode);
    ~ApplicationSettingsSession();
    ApplicationSettingsSession(const ApplicationSettingsSession&) = delete;
    ApplicationSettingsSession& operator=(const ApplicationSettingsSession&) = delete;
    // UI-thread commands; Restore reports queue acceptance, not runtime completion.
    bool Restore();
    // True only when the check was queued. Both entry points share the worker.
    bool RequestUpdateCheck(bool manual);
    void SetUpdateObserver(UpdateObserver);
    std::optional<std::wstring> HandlePage(const ui::ApplicationSettingsPageCommand&, HWND = nullptr);
    std::wstring State() const;
    // Restore 前返回配置意图（新用户默认开启），异步恢复后自启值来自系统读回。
    application_settings::Preferences Preferences() const;
    bool BalancedMode() const;
    // UI thread: cancel, stop/wake the runtime dependency, then JoinShutdown.
    // HasWork stays true until the cancelled worker has actually exited.
    void Shutdown();
    // After Shutdown, finish an accepted preference transaction before stopping
    // its runtime dependency. Startup/update cancellation remains nonblocking.
    void FlushPreferences();
    void JoinShutdown();
    bool HasWork() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
