#pragma once
#include <windows.h>
#include <filesystem>

namespace lume::platform::windows {
enum class InstallShutdownResult { Stopped = 0, InvalidDirectory = 2, Failed = 3, Forced = 10 };
struct InstallShutdownTiming {
    DWORD gracefulMs = 8000;
    DWORD terminateMs = 2000;
};
InstallShutdownResult StopInstalledLume(const std::filesystem::path& directory,
    InstallShutdownTiming timing = {});
InstallShutdownResult RunInstallShutdownWorker(const std::filesystem::path& executable,
    const std::filesystem::path& directory, DWORD timeoutMs = 12000);
}
