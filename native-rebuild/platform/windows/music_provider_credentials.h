#pragma once
#include "core/music/music_provider_account.h"
#include <filesystem>

namespace lume::music::auth {
// Each provider uses a separate, path-bound current-user DPAPI file.
Json LoadCredentials(const std::filesystem::path& root, Provider);
void SaveCredentials(const std::filesystem::path& root, Provider, const Json&);
void ClearCredentials(const std::filesystem::path& root, Provider);
}  // namespace lume::music::auth
