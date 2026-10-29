#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lume::shell {

struct MusicFontAssetSessionOptions {
    // Dedicated owned directory. Metadata lives here, font files in root/fonts.
    std::filesystem::path root;
    bool verification = false;
    // Empty path means cancellation. Verification never opens a real dialog.
    std::function<std::filesystem::path(HWND)> chooseFont;
    // Must include active, persisted AND backup preferences. Missing/throwing
    // hook denies drop; only files imported in this session are ever droppable.
    std::function<std::vector<std::string>()> selectedFamilies;
};

// UI-thread owner: choose may pump messages. Cancel/Shutdown close its modal
// picker and invalidate reentrant chooser results. No font installation/cache.
class MusicFontAssetSession final {
public:
    explicit MusicFontAssetSession(MusicFontAssetSessionOptions);
    ~MusicFontAssetSession();
    MusicFontAssetSession(const MusicFontAssetSession&) = delete;
    MusicFontAssetSession& operator=(const MusicFontAssetSession&) = delete;

    // action:"cancel",request:<choose ID> cancels only that active chooser.
    // A late cancel drops that request's candidate only if the reference hook
    // permits it. Choose IDs must be unique while their candidates remain.
    // Its acknowledgement and the interrupted choose both report "cancelled";
    // failed/protected cleanup reports the normal drop error.
    std::optional<std::wstring> HandleMessage(std::wstring_view, HWND owner = nullptr);
    // Native-only mapping directory; never included in a public response.
    std::filesystem::path AssetDirectory() const;
    // Exact 32hex.ext lookup in loaded metadata only; no IO. Zero denies access,
    // including before loading and after Shutdown. UI-thread only.
    std::uint64_t AssetSize(std::wstring_view filename) const;
    void Cancel() noexcept;
    void Shutdown() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lume::shell
