#pragma once
#include "core/update_notification/update_notification.h"
#include "platform/windows/update_notification/update_presenter.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>

namespace lume::shell {
struct UpdateNotificationTestPorts {
    std::shared_ptr<application_settings::IPreferencesFile> file;
    std::function<update_notification::ClockSample()> now;
    std::function<bool(bool manual)> requestCheck;
    std::function<bool(const platform::windows::UpdateNotice&)> show;
    std::function<void()> shutdownPresenter;
    std::function<bool()> hasVisible;
};
struct UpdateNotificationOptions {
    std::string statePath;
    std::string currentVersion = application_settings::CurrentVersion;
    bool verification = false;
    UpdateNotificationTestPorts testPorts;
    // Manual results only. Return true to consume silently when the user has
    // left the page; returning false requests a presentation retry.
    std::function<bool(const update_notification::Notice&)> showInApp;
};
class UpdateNotificationSession final {
public:
    using RequestCheck = std::function<bool(bool manual)>;
    using Changed = std::function<void()>;
    UpdateNotificationSession(HINSTANCE, UpdateNotificationOptions, RequestCheck, Changed = {});
    ~UpdateNotificationSession();
    UpdateNotificationSession(const UpdateNotificationSession&) = delete;
    UpdateNotificationSession& operator=(const UpdateNotificationSession&) = delete;
    bool Start();
    bool RequestManual();
    void OnCheckCompleted(bool manual, const application_settings::ManifestResult&);
    void Wake();
    void RetryPresentation();
    void Tick();
    void SetLanguage(bool english);
    void Shutdown();
    bool HasWork() const;
    bool HasVisible() const;
    HWND Handle() const;
    std::string Error() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
