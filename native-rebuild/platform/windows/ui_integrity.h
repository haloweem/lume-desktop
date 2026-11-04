#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace lume::platform::windows {
struct UiIntegrityEntry {
    std::wstring relative;
    std::uint64_t bytes;
    std::string sha256;
};
struct UiIntegrityResult {
    std::filesystem::path directory;
    std::wstring failedFile;
    unsigned damaged=0;
    bool recovered=false;
};
// Expected digests are compiled into this executable, independent of loose files.
const std::vector<UiIntegrityEntry>& BuiltUiManifest();
const char* BuiltUiIdentity();
// A damaged installation uses its verified backup, or a verified per-user mirror.
// No user settings,
// running processes or administrator-owned installation files are changed.
UiIntegrityResult PrepareUiDirectory(const std::filesystem::path& ui,
    const std::filesystem::path& backup,const std::filesystem::path& cache,
    const std::vector<UiIntegrityEntry>& entries);
}
