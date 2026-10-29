#pragma once
#include "core/application_settings/application_settings.h"
#include <functional>
#include <memory>
#include <string>

namespace lume::platform::windows {
std::shared_ptr<application_settings::IPreferencesFile> CreateApplicationSettingsFile(std::string path);
// The logon task kept beside the Run entry (startup_task.h). Empty hooks
// leave Task Scheduler alone, as tests that use the real registry need.
struct StartupTaskHooks {
    std::function<bool(const std::wstring& executable, const std::wstring& arguments)> ensure;
    std::function<bool()> remove;
    std::function<bool()> disabledByUser;
};
StartupTaskHooks SystemStartupTaskHooks();
std::shared_ptr<application_settings::IStartupRegistration> CreateApplicationStartupRegistration(
    bool verification, StartupTaskHooks task = {});
std::shared_ptr<application_settings::IUpdateTransport> CreateApplicationUpdateTransport(bool verification);
std::shared_ptr<application_settings::IUpdateInstaller> CreateApplicationUpdateInstaller(bool verification);
bool VerifyApplicationUpdateFile(const std::wstring& path, const application_settings::ManifestResult& release);
std::wstring ApplicationSettingsExecutablePath();
}
