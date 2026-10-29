#pragma once
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace lume::shell {
// Startup self-check for the problems users ask about most: missing runtime
// libraries (the "msvcp140.dll" error), the wallpaper player and its GPU
// decoding, and what keeps the transparent taskbar from starting. A check
// that cannot tell reports Unknown, never Ok. Read-only; safe off the UI thread.
enum class CheckStatus { Ok, Problem, Unknown };
struct SelfCheck {
    std::string id;       // runtime | wallpaper | helpers | gpu-decode | taskbar
    CheckStatus status = CheckStatus::Unknown;
    std::string title;    // Chinese, shown to the user
    std::string detail;   // what was found
    std::string fix;      // what the user can do; empty when Ok
};
std::vector<SelfCheck> RunSelfChecks(const std::filesystem::path& installDirectory);
const char* CheckStatusName(CheckStatus);
nlohmann::json SelfChecksJson(const std::vector<SelfCheck>&);
// Stable text of the problems only, to notify once per distinct set.
std::string SelfCheckSignature(const std::vector<SelfCheck>&);
}  // namespace lume::shell
