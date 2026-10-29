#include "platform/windows/application_settings/startup_task.h"
#include <windows.h>
#include <comdef.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <vector>

namespace lume::platform::windows {
namespace {
using Microsoft::WRL::ComPtr;
struct ComScope {
    HRESULT result;
    ComScope() : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
    // RPC_E_CHANGED_MODE: the thread already has COM in another apartment.
    bool Usable() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};
std::wstring UserName() {
    wchar_t name[257]{};
    DWORD size = 257;
    return GetUserNameW(name, &size) ? std::wstring(name) : std::wstring{};
}
std::wstring QualifiedUser() {
    wchar_t domain[256]{};
    const auto length = GetEnvironmentVariableW(L"USERDOMAIN", domain, 256);
    const auto user = UserName();
    if (user.empty()) return {};
    return length && length < 256 ? std::wstring(domain) + L"\\" + user : user;
}
bool Connect(ComPtr<ITaskService>& service, ComPtr<ITaskFolder>& root) {
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service))))
        return false;
    if (FAILED(service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))) return false;
    return SUCCEEDED(service->GetFolder(_bstr_t(L"\\"), &root));
}
}

std::wstring StartupTaskName() {
    // One task per Windows user: the task namespace is shared by the machine.
    const auto user = UserName();
    return user.empty() ? std::wstring{} : L"Lume Startup (" + user + L")";
}

bool EnsureStartupTask(const std::wstring& executable, const std::wstring& arguments) {
    try {
        const auto name = StartupTaskName(), user = QualifiedUser();
        if (name.empty() || user.empty() || executable.empty()) return false;
        ComScope com;
        if (!com.Usable()) return false;
        ComPtr<ITaskService> service;
        ComPtr<ITaskFolder> root;
        if (!Connect(service, root)) return false;
        ComPtr<ITaskDefinition> task;
        if (FAILED(service->NewTask(0, &task))) return false;

        ComPtr<IRegistrationInfo> info;
        if (SUCCEEDED(task->get_RegistrationInfo(&info))) {
            info->put_Author(_bstr_t(L"Lume"));
            info->put_Description(_bstr_t(L"Starts Lume and its wallpaper as soon as you sign in."));
        }
        ComPtr<IPrincipal> principal;
        if (FAILED(task->get_Principal(&principal)) ||
            FAILED(principal->put_UserId(_bstr_t(user.c_str()))) ||
            FAILED(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)) ||
            FAILED(principal->put_RunLevel(TASK_RUNLEVEL_LUA))) return false;

        ComPtr<ITaskSettings> settings;
        if (FAILED(task->get_Settings(&settings))) return false;
        settings->put_StartWhenAvailable(VARIANT_TRUE);
        settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        // The default limit (72 hours) would end Lume; PT0S means none.
        settings->put_ExecutionTimeLimit(_bstr_t(L"PT0S"));
        // Task Scheduler starts programs below normal priority by default (7).
        settings->put_Priority(4);
        settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
        settings->put_AllowHardTerminate(VARIANT_TRUE);
        ComPtr<IIdleSettings> idle;
        if (SUCCEEDED(settings->get_IdleSettings(&idle))) idle->put_StopOnIdleEnd(VARIANT_FALSE);

        ComPtr<ITriggerCollection> triggers;
        ComPtr<ITrigger> trigger;
        ComPtr<ILogonTrigger> logon;
        if (FAILED(task->get_Triggers(&triggers)) || FAILED(triggers->Create(TASK_TRIGGER_LOGON, &trigger)) ||
            FAILED(trigger.As(&logon)) || FAILED(logon->put_UserId(_bstr_t(user.c_str())))) return false;

        ComPtr<IActionCollection> actions;
        ComPtr<IAction> action;
        ComPtr<IExecAction> exec;
        if (FAILED(task->get_Actions(&actions)) || FAILED(actions->Create(TASK_ACTION_EXEC, &action)) ||
            FAILED(action.As(&exec)) || FAILED(exec->put_Path(_bstr_t(executable.c_str()))) ||
            FAILED(exec->put_Arguments(_bstr_t(arguments.c_str())))) return false;
        const auto slash = executable.find_last_of(L"\\/");
        if (slash != std::wstring::npos) exec->put_WorkingDirectory(_bstr_t(executable.substr(0, slash).c_str()));

        ComPtr<IRegisteredTask> registered;
        return SUCCEEDED(root->RegisterTaskDefinition(_bstr_t(name.c_str()), task.Get(), TASK_CREATE_OR_UPDATE,
            _variant_t(user.c_str()), _variant_t(), TASK_LOGON_INTERACTIVE_TOKEN, _variant_t(L""), &registered));
    } catch (...) { return false; }
}

bool RemoveStartupTask() {
    try {
        const auto name = StartupTaskName();
        if (name.empty()) return false;
        ComScope com;
        if (!com.Usable()) return false;
        ComPtr<ITaskService> service;
        ComPtr<ITaskFolder> root;
        if (!Connect(service, root)) return false;
        const auto deleted = root->DeleteTask(_bstr_t(name.c_str()), 0);
        return SUCCEEDED(deleted) || deleted == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    } catch (...) { return false; }
}

bool RunEntryDisabledByUser() {
    // Explorer records the Startup tab's switch here: the first byte is even
    // (02) when enabled and odd (03) when turned off.
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
            0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    BYTE data[16]{};
    DWORD size = sizeof(data), type = 0;
    const bool read = RegQueryValueExW(key, L"Lume", nullptr, &type, data, &size) == ERROR_SUCCESS &&
                      type == REG_BINARY && size >= 1;
    RegCloseKey(key);
    return read && (data[0] & 1) != 0;
}
}
