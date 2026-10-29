#pragma once
#include "core/account/ports.h"
#include <filesystem>
#include <mutex>
namespace lume::platform::windows::account {
class DpapiCredentials final : public lume::account::ICredentials {
public:
    explicit DpapiCredentials(std::string directory);
    std::optional<lume::account::Credentials> Load() override;
    void Save(const lume::account::Credentials&) override;
    void Clear() override;
    std::string DeviceId() override;
    void ReserveWallpaperDownload(std::int64_t day, std::string_view subject, unsigned limit) override;
    void ReleaseWallpaperDownload(std::int64_t day, std::string_view subject) override;
private:
    void ChangeWallpaperQuota(std::int64_t day, std::string_view subject, unsigned limit, bool release);
    lume::account::Bytes Protect(std::string_view, bool decrypt);
    std::optional<std::string> Read(const std::filesystem::path&);
    void AtomicWrite(const std::filesystem::path&, std::string_view);
    std::filesystem::path root_;
    lume::account::Bytes entropy_;
    std::mutex mutex_;
};
}
