#pragma once
#include "core/music/music_provider_account.h"
#include <filesystem>
#include <functional>

namespace lume::platform::windows {
bool FoliaRuntimeAvailable(const std::filesystem::path& executableDirectory,
    const std::filesystem::path& accountsRoot) noexcept;
// Blocking worker operation. Public errors are cancelled, network, timeout,
// invalid-response, storage or unavailable. Never opens a window or retries.
std::filesystem::path EnsureFoliaRuntime(const std::filesystem::path& executableDirectory,
    const std::filesystem::path& accountsRoot, const music::auth::Cancelled& cancelled,
    const std::function<void(std::string_view)>& phase = {});

#ifdef LUME_FOLIA_DOWNLOAD_TESTING
using FoliaRuntimeTestSink = std::function<void(const void*, std::size_t)>;
using FoliaRuntimeTestTransport = std::function<void(const FoliaRuntimeTestSink&, const music::auth::Cancelled&)>;
std::filesystem::path EnsureFoliaRuntimeForTests(const std::filesystem::path& executableDirectory,
    const std::filesystem::path& accountsRoot, const music::auth::Cancelled& cancelled,
    const FoliaRuntimeTestTransport&, const std::function<void(std::string_view)>& phase = {});
#endif
}
