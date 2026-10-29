#include "platform/windows/taskbar/taskbar_payload.h"
#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

using namespace lume::platform::windows;
namespace {
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); } }
void CheckRuntimeLoader(const std::filesystem::path& directory) {
    std::vector<const wchar_t*> names{L"vcruntime140.dll"};
#if !defined(_M_ARM64) && !defined(__aarch64__)
    names.push_back(L"vcruntime140_1.dll");
#endif
    names.push_back(L"msvcp140.dll");
    names.push_back(L"msvcp140_atomic_wait.dll");
    std::vector<HMODULE> loaded;
    for (const auto* name : names) {
        const auto expected = directory / name;
        // Only Microsoft CRT initialization runs. Never load or execute the
        // taskbar core or its Explorer/XAML/ProgramLog modules in this test.
        const auto module = LoadLibraryExW(expected.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            std::wcerr << L"CRT loader failed: " << expected << L" error=" << GetLastError() << L'\n';
        }
        Check(module != nullptr, "verified app-local Microsoft runtime loads with restricted search");
        wchar_t actual[32768]{};
        Check(GetModuleFileNameW(module, actual, 32768) != 0 &&
              std::filesystem::equivalent(expected, actual),
              "runtime loader resolves the staged file, not a global developer runtime");
        loaded.push_back(module);
    }
    for (auto iterator = loaded.rbegin(); iterator != loaded.rend(); ++iterator)
        Check(FreeLibrary(*iterator) != FALSE, "release only the test's runtime module references");
}
}
int main(int count, char** args) {
    namespace fs = std::filesystem;
    std::atomic_bool cancelled{true}; fs::path executable;
    Check(StageTaskbarPayload({}, false, cancelled, executable) == lume::taskbar::Error::Cancelled,
          "cancelled staging allocates nothing");
    Check(VerifyTaskbarPayload({}, false) == lume::taskbar::Error::MissingPayload, "missing explicit payload is rejected");
    if (count != 2) {
        std::cout << "taskbar payload guards passed; full pinned payload requires explicit read-only path\n";
        return 0;
    }
    const auto source = fs::absolute(fs::u8path(args[1])).lexically_normal();
    Check(VerifyTaskbarPayload(source, false) == lume::taskbar::Error::None, "pinned x64 source includes its complete fixed runtime dependencies");
    Check(VerifyTaskbarPayload(source, true) == lume::taskbar::Error::None, "pinned arm64 source includes its complete fixed runtime dependencies");
    const auto root = fs::temp_directory_path() /
        (L"lume-taskbar-payload-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Check(root.is_absolute() && fs::equivalent(root.parent_path(), fs::temp_directory_path()) && fs::create_directory(root),
          "test owns a new explicit temporary directory");
    TaskbarPaths paths{"", source, root}; cancelled = false;
    Check(StageTaskbarPayload(paths, false, cancelled, executable) == lume::taskbar::Error::None &&
          fs::is_regular_file(executable), "verified core staged without executing it");
    const auto stamp = fs::last_write_time(executable);
    Check(StageTaskbarPayload(paths, false, cancelled, executable) == lume::taskbar::Error::None &&
          fs::last_write_time(executable) == stamp, "unchanged verified core is reused");
    const auto dll = executable.parent_path() / L"ExplorerHooks.dll";
    { std::ofstream corrupt(dll, std::ios::binary | std::ios::app); corrupt << "corrupt"; }
    Check(StageTaskbarPayload(paths, false, cancelled, executable) == lume::taskbar::Error::None &&
          fs::file_size(dll) == fs::file_size(source / L"x64/ExplorerHooks.dll"), "damaged staged dependency repaired from verified source");
    for (const auto* name : {L"msvcp140.dll", L"msvcp140_atomic_wait.dll", L"vcruntime140.dll", L"vcruntime140_1.dll"})
        Check(fs::is_regular_file(executable.parent_path() / name), "x64 staging carries each required Microsoft runtime");
    const auto runtime = executable.parent_path() / L"msvcp140.dll";
    { std::ofstream corrupt(runtime, std::ios::binary | std::ios::app); corrupt << "corrupt-runtime"; }
    Check(StageTaskbarPayload(paths, false, cancelled, executable) == lume::taskbar::Error::None &&
          fs::file_size(runtime) == fs::file_size(source / L"x64/msvcp140.dll"),
          "tampered runtime is repaired using its compiled hash");
    fs::path arm64Executable;
    Check(StageTaskbarPayload(paths, true, cancelled, arm64Executable) == lume::taskbar::Error::None &&
          fs::is_regular_file(arm64Executable), "ARM64 dependencies stage without executing ARM64 code");
    for (const auto* name : {L"msvcp140.dll", L"msvcp140_atomic_wait.dll", L"vcruntime140.dll"})
        Check(fs::is_regular_file(arm64Executable.parent_path() / name), "ARM64 staging carries each required Microsoft runtime");
    Check(!fs::exists(arm64Executable.parent_path() / L"vcruntime140_1.dll"),
          "ARM64 dependency closure does not include an unused runtime");
#if defined(_M_ARM64) || defined(__aarch64__)
    CheckRuntimeLoader(arm64Executable.parent_path());
#else
    CheckRuntimeLoader(executable.parent_path());
#endif
    const auto broken = root / L"broken"; fs::create_directory(broken);
    { std::ofstream manifest(broken / L"SHA256SUMS.txt"); manifest << "untrusted"; }
    Check(VerifyTaskbarPayload(broken, false) == lume::taskbar::Error::PayloadIntegrity, "modified manifest cannot authorize replacement binaries");
    Check(fs::equivalent(root.parent_path(), fs::temp_directory_path()), "cleanup remains inside owned temporary directory");
    fs::remove_all(root);
    std::cout << "taskbar pinned payload verification and staging passed; no core was launched\n";
}
