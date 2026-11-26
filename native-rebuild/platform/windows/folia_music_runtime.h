#pragma once
#include "core/music/music_provider_account.h"
#include <filesystem>
#include <memory>

namespace lume::platform::windows {
// One lazily started, job-contained provider runtime. Only native code can send
// its bounded requests; no server address or credential is exposed to the UI.
class FoliaMusicRuntime final {
public:
    explicit FoliaMusicRuntime(std::filesystem::path root);
    ~FoliaMusicRuntime();
    music::auth::Json Request(std::string_view action, music::auth::Provider,
        music::auth::Json arguments, const music::auth::Cancelled& = {});
    void CancelLogin(music::auth::Provider, std::uint64_t attempt);
    bool Available() const;
    void Prepare(const music::auth::Cancelled& = {},
        const std::function<void(std::string_view)>& phase = {});
    void Shutdown();
    bool SourceAllowed(std::string_view) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::shared_ptr<FoliaMusicRuntime> SharedFoliaMusicRuntime(const std::filesystem::path& root);
}
