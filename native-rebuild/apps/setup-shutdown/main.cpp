#include "platform/windows/installer_shutdown.h"
#include <shellapi.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int count = 0;
    const auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 2;
    const bool worker = count == 3 && std::wstring(arguments[1]) == L"--worker";
    const bool valid = worker || (count == 3 && std::wstring(arguments[1]) == L"--directory");
    const std::filesystem::path directory = valid ? arguments[2] : L"";
    LocalFree(arguments);
    if (!valid) return 2;
    try {
        if (worker) return static_cast<int>(lume::platform::windows::StopInstalledLume(directory));
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) return 3;
        executable.resize(length);
        return static_cast<int>(lume::platform::windows::RunInstallShutdownWorker(executable, directory));
    } catch (...) {
        return 3;
    }
}
