#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <string_view>

// Lume's own runtime log, kept so a user's diagnostic report carries what
// happened before a problem instead of a description of it. One line per
// event, local time and thread id first; never credentials, tokens or URLs
// with query strings. Thread safe; every call is best effort and silent.
namespace lume::platform::windows::diag {
// Opens <directory>\lume.log (rotated to lume.1.log at 2 MiB) and keeps crash
// dumps in <directory>\crashes. Before this call Log() only drops lines.
void Initialize(const std::filesystem::path& directory);
std::filesystem::path Directory();
void Log(std::string_view module, std::string_view message);
// Writes a minidump of LumeShell on an unhandled exception, std::terminate,
// a pure virtual call or an invalid CRT parameter (newest three kept) and logs
// the exception code and address.
void InstallCrashHandler();
// Probes the UI thread through `window` every five seconds; after fifteen
// seconds without an answer it logs the hang and writes one dump of where
// every thread is (at most two per run). Once per process.
void StartHangWatch(HWND window);
// "0xC0000005 (access violation)" style text for a process exit code.
std::string ExitCodeText(unsigned long code);
}  // namespace lume::platform::windows::diag
