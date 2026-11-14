#include "platform/windows/diagnostics/runtime_log.h"
#include "platform/windows/material/material_diagnostics.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void Check(bool ok, const char* text) { if (!ok) { ++failures; std::cerr << "FAIL: " << text << '\n'; } }
void Size(const fs::path& path, std::uint64_t size) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(handle != INVALID_HANDLE_VALUE, "create owned size fixture");
    if (handle == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(size);
    Check(SetFilePointerEx(handle, position, nullptr, FILE_BEGIN) && SetEndOfFile(handle), "size bounded fixture");
    CloseHandle(handle);
}
}
int main() {
    const auto root = fs::temp_directory_path() / (L"LumeDiagnosticBounds-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root / "crashes");
    for (int i = 0; i < 4; ++i) Size(root / "crashes" / (L"LumeShell-test-" + std::to_wstring(i) + L".dmp"), 10);
    Size(root / "crashes" / "LumeShell-test-large.dmp", 17ull << 20);
    Size(root / "crashes" / "unrelated.dmp", 10);
    lume::platform::windows::diag::Initialize(root);
    std::size_t dumps = 0;
    for (const auto& item : fs::directory_iterator(root / "crashes"))
        if (item.path().filename().wstring().rfind(L"LumeShell-", 0) == 0) {
            ++dumps; Check(item.file_size() <= (16ull << 20), "retained crash dumps have an individual limit");
        }
    Check(dumps == 3 && fs::exists(root / "crashes" / "unrelated.dmp"), "dump retention preserves unrelated files and only three owned dumps");
    Size(root / "lume.log", 2ull << 20); Size(root / "lume.1.log", 10);
    const auto locked = CreateFileW((root / "lume.1.log").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "lock only the owned log backup");
    lume::platform::windows::diag::Log("test", "rotation failure must not grow the current log");
    Check(fs::file_size(root / "lume.log") == (2ull << 20), "runtime log stops appending when rotation is refused");
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    lume::platform::windows::diag::Log("test", "rotation succeeds after backup unlock");
    Check(fs::file_size(root / "lume.log") < 4096 && fs::file_size(root / "lume.1.log") == (2ull << 20), "runtime log retains one bounded backup");
    Size(root / "material.log", (1ull << 20) + 1); Size(root / "material.1.log", 10);
    SetEnvironmentVariableW(L"LUME_MATERIAL_DIAGNOSTIC_LOG", (root / "material.log").c_str());
    const auto materialLock = CreateFileW((root / "material.1.log").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    lume::platform::windows::material_diagnostics::Write("test", E_FAIL);
    Check(fs::file_size(root / "material.log") == (1ull << 20) + 1, "material log also stops if backup rotation fails");
    if (materialLock != INVALID_HANDLE_VALUE) CloseHandle(materialLock);
    lume::platform::windows::diag::Initialize({});
    SetEnvironmentVariableW(L"LUME_MATERIAL_DIAGNOSTIC_LOG", nullptr);
    std::error_code error; fs::remove_all(root, error);
    if (!failures) std::cout << "diagnostic log rotation failures and crash-dump storage bounds passed\n";
    return failures ? 1 : 0;
}
