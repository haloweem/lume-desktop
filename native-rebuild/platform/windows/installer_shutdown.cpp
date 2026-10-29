#include "platform/windows/installer_shutdown.h"
#include "platform/windows/lume_window_ids.h"
#include <tlhelp32.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace lume::platform::windows {
namespace {
namespace fs = std::filesystem;
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE value = nullptr) : value(value) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
};
struct Target {
    std::wstring image;
    bool shell = false;
};
struct Process {
    Handle handle;
    DWORD id;
    bool shell;
    bool terminated = false;
    bool Exited() const { return WaitForSingleObject(handle.value, 0) == WAIT_OBJECT_0; }
};
bool Same(std::wstring_view left, std::wstring_view right) {
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
        right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}
std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (const auto character : value) {
        if (character == L'\\') {
            ++slashes;
        } else {
            quoted.append(slashes * (character == L'"' ? 2 : 1), L'\\');
            slashes = 0;
            if (character == L'"') quoted += L'\\';
            quoted += character;
        }
    }
    quoted.append(slashes * 2, L'\\');
    return quoted + L'"';
}
std::wstring Canonical(const fs::path& path) {
    Handle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (file.value != INVALID_HANDLE_VALUE) {
        std::wstring resolved(32768, L'\0');
        const auto length = GetFinalPathNameByHandleW(file.value, resolved.data(),
            static_cast<DWORD>(resolved.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!length || length >= resolved.size()) return {};
        resolved.resize(length);
        if (resolved.substr(0, 8) == L"\\\\?\\UNC\\") return L"\\\\" + resolved.substr(8);
        if (resolved.substr(0, 4) == L"\\\\?\\") resolved.erase(0, 4);
        return resolved;
    }
    if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return {};
    std::error_code error;
    const auto normalized = fs::weakly_canonical(path, error);
    return error ? std::wstring{} : normalized.lexically_normal().wstring();
}
bool Observe(const std::vector<Target>& targets, std::vector<Process>& processes) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.value, &entry)) return GetLastError() == ERROR_NO_MORE_FILES;
    do {
        if (entry.th32ProcessID == GetCurrentProcessId()) continue;
        if (std::none_of(targets.begin(), targets.end(), [&](const auto& target) {
            return Same(fs::path(target.image).filename().wstring(), entry.szExeFile);
        })) continue;
        if (std::any_of(processes.begin(), processes.end(), [&](const auto& process) {
            return process.id == entry.th32ProcessID && !process.Exited();
        })) continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE,
            FALSE, entry.th32ProcessID));
        if (!process.value) {
            process.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (!process.value) {
                if (GetLastError() == ERROR_INVALID_PARAMETER) continue;
                return false;
            }
        }
        std::wstring image(32768, L'\0');
        DWORD length = static_cast<DWORD>(image.size());
        if (!QueryFullProcessImageNameW(process.value, 0, image.data(), &length)) {
            if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) continue;
            return false;
        }
        image.resize(length);
        image = Canonical(image);
        if (image.empty()) return false;
        const auto target = std::find_if(targets.begin(), targets.end(), [&](const auto& candidate) {
            return Same(candidate.image, image);
        });
        if (target != targets.end())
            processes.push_back({std::move(process), entry.th32ProcessID, target->shell, false});
    } while (Process32NextW(snapshot.value, &entry));
    return GetLastError() == ERROR_NO_MORE_FILES;
}
void RequestExit(const Process& process) {
    if (!process.shell || process.Exited()) return;
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        const auto& process = *reinterpret_cast<const Process*>(parameter);
        DWORD owner = 0;
        const DWORD thread = GetWindowThreadProcessId(window, &owner);
        if (owner != process.id) return TRUE;
        wchar_t name[80]{};
        GetClassNameW(window, name, 80);
        if (!Same(name, lume::window_ids::TrayClass) && !Same(name, lume::window_ids::ManagerClass)) return TRUE;
        // Old shells consume a quit in WebView before entering their retained
        // native loop. Repeat on subsequent polls until the process really exits.
        PostThreadMessageW(thread, WM_QUIT, 0, 0);
        return FALSE;
    }, reinterpret_cast<LPARAM>(&process));
}
bool AllExited(const std::vector<Process>& processes) {
    return std::all_of(processes.begin(), processes.end(), [](const auto& process) { return process.Exited(); });
}
}
InstallShutdownResult StopInstalledLume(const fs::path& directory, InstallShutdownTiming timing) {
    if (directory.empty() || !directory.is_absolute()) return InstallShutdownResult::InvalidDirectory;
    fs::path root(Canonical(directory));
    // Missing directories canonicalize "Lume\\." to "Lume\\"; existing ones do not.
    // Remove only the empty terminal component before building the boundary prefix.
    while (root.has_relative_path() && !root.has_filename()) root = root.parent_path();
    if (root.empty() || root == root.root_path()) return InstallShutdownResult::InvalidDirectory;
    std::vector<Target> targets;
    for (const auto* relative : {L"LumeShell.exe", L"LumeLauncher.exe", L"LumeMusicHost.exe",
        L"LumeIconHost.exe", L"LumeMinimizeHost32.exe", L"LumeMinimizeHost64.exe",
        L"wallpaper-runtime\\mpv.exe"}) {
        const auto path = root / relative;
        const auto image = Canonical(path);
        const auto prefix = root.wstring() + L"\\";
        const auto attributes = GetFileAttributesW(path.parent_path().c_str());
        if (image.size() <= prefix.size() || !Same(image.substr(0, prefix.size()), prefix) ||
            (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)))
            return InstallShutdownResult::InvalidDirectory;
        targets.push_back({image, Same(relative, L"LumeShell.exe")});
    }
    std::vector<Process> processes;
    const auto gracefulDeadline = GetTickCount64() + std::min<DWORD>(timing.gracefulMs, 8000);
    for (;;) {
        if (!Observe(targets, processes)) return InstallShutdownResult::Failed;
        if (AllExited(processes)) return InstallShutdownResult::Stopped;
        for (const auto& process : processes) RequestExit(process);
        if (GetTickCount64() >= gracefulDeadline) break;
        Sleep(50);
    }
    const auto terminateDeadline = GetTickCount64() + std::min<DWORD>(timing.terminateMs, 2000);
    // Retain verified process handles throughout, never reopen a recycled PID.
    for (;;) {
        if (!Observe(targets, processes)) return InstallShutdownResult::Failed;
        for (auto& process : processes) {
            if (process.Exited() || process.terminated) continue;
            if (!TerminateProcess(process.handle.value, 0) && !process.Exited())
                return InstallShutdownResult::Failed;
            process.terminated = true;
        }
        if (AllExited(processes)) return InstallShutdownResult::Forced;
        if (GetTickCount64() >= terminateDeadline) return InstallShutdownResult::Failed;
        Sleep(25);
    }
}
InstallShutdownResult RunInstallShutdownWorker(const fs::path& executable,
    const fs::path& directory, DWORD timeoutMs) {
    const auto started = GetTickCount64();
    const auto limit = std::min<DWORD>(timeoutMs, 12000);
    if (!executable.is_absolute()) return InstallShutdownResult::Failed;
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.value) return InstallShutdownResult::Failed;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return InstallShutdownResult::Failed;
    auto command = QuoteArgument(executable.wstring()) + L" --worker " + QuoteArgument(directory.wstring());
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    if (!attributeBytes) return InstallShutdownResult::Failed;
    std::vector<unsigned char> storage(attributeBytes);
    const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes))
        return InstallShutdownResult::Failed;
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
        &job.value, sizeof(job.value), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attributes);
        return InstallShutdownResult::Failed;
    }
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION info{};
    // Only this new worker belongs to the job, never an installed Lume process.
    // Assign it during creation so a supervisor crash cannot orphan it.
    const bool created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
        &startup.StartupInfo, &info) != FALSE;
    DeleteProcThreadAttributeList(attributes);
    if (!created) return InstallShutdownResult::Failed;
    Handle process(info.hProcess), thread(info.hThread);
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
        TerminateProcess(process.value, static_cast<UINT>(InstallShutdownResult::Failed));
        WaitForSingleObject(process.value, 500);
        return InstallShutdownResult::Failed;
    }
    const auto elapsed = GetTickCount64() - started;
    const auto remaining = elapsed >= limit ? 0 : limit - static_cast<DWORD>(elapsed);
    if (WaitForSingleObject(process.value, remaining) != WAIT_OBJECT_0) {
        TerminateJobObject(job.value, static_cast<UINT>(InstallShutdownResult::Failed));
        WaitForSingleObject(process.value, 500);
        return InstallShutdownResult::Failed;
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(process.value, &code)) return InstallShutdownResult::Failed;
    if (code == static_cast<DWORD>(InstallShutdownResult::Stopped)) return InstallShutdownResult::Stopped;
    if (code == static_cast<DWORD>(InstallShutdownResult::Forced)) return InstallShutdownResult::Forced;
    if (code == static_cast<DWORD>(InstallShutdownResult::InvalidDirectory))
        return InstallShutdownResult::InvalidDirectory;
    return InstallShutdownResult::Failed;
}
}
