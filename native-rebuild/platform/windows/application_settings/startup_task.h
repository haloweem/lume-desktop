#pragma once
#include <string>

namespace lume::platform::windows {
// A Task Scheduler task that starts Lume when this user signs in. Windows
// holds back programs in the Run key for several seconds after sign-in and
// starts them one after another; a logon task starts at once, as Wallpaper
// Engine's high-priority startup does. The Run key entry stays as the
// fallback and as the setting's source of truth; a second launch from it
// exits quietly. Every call is best effort and never fails the setting.
std::wstring StartupTaskName();
// Creates or updates the task to run `command` (executable path plus
// arguments) at this user's sign-in, at normal priority, without a time limit.
bool EnsureStartupTask(const std::wstring& executable, const std::wstring& arguments);
bool RemoveStartupTask();
// Task Manager's Startup tab turned the Run key entry off.
bool RunEntryDisabledByUser();
}
