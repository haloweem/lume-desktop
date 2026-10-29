#pragma once
#include "core/account/jwt.h"
#include "core/account/ports.h"
namespace lume::account {
struct Profile {
    std::string id, displayName, avatarUrl;
    bool vip = false;
    std::string vipExpiresAt;
};
struct WallpaperEngagement {
    std::uint64_t likes = 0, downloads = 0;
    bool liked = false;
};
std::int64_t UnixNow();
std::optional<std::int64_t> ParseIsoTime(std::string_view);
std::string IsoTime(std::int64_t milliseconds);

// Owned exclusively by the session worker; no HWND or page/DOM dependency.
class Backend final {
public:
    Backend(std::shared_ptr<ITransport>, std::shared_ptr<ICredentials>, Clock);
    Profile Login(const Grant&, const Cancellation&);
    std::optional<Profile> Restore(const Cancellation&);
    Profile Refresh(const Cancellation&);
    Profile UpdateName(std::string_view, const Cancellation&);
    Profile UploadAvatar(Avatar, const Cancellation&);
    void CheckDevice(const Cancellation&);
    WallpaperEngagement WallpaperStats(std::string_view id, const Cancellation&,
        const Cancellation* accountCancellation = nullptr);
    WallpaperEngagement SetWallpaperLike(std::string_view id, bool liked, const Cancellation&,
        const Cancellation* accountCancellation = nullptr);
    void RecordWallpaperDownload(std::string_view id, std::string_view sourceUrl, const Cancellation&);
    void AuthorizeWallpaperDownload(std::string_view id, std::string_view sourceUrl, const Cancellation&);
    void Logout();
    bool SignedIn() const { return !access_.empty() && credentials_.has_value() && profile_.has_value(); }
    const std::optional<Profile>& Current() const { return profile_; }
private:
    struct Record { Profile profile; std::string owner, name, avatar; };
    HttpResponse Send(HttpRequest, const Cancellation&, const Cancellation* accountCancellation = nullptr);
    Json SendJson(HttpRequest, const Cancellation&, const Cancellation* accountCancellation = nullptr);
    void Discover(const Cancellation&);
    Profile Exchange(std::string body, const Grant*, const Credentials*, const Cancellation&);
    void EnsureAccess(const Cancellation&);
    Record GetAccount(std::string_view access, std::string_view subject, const Cancellation&);
    void Device(std::string_view access, bool registration, const Cancellation&);
    Profile PublicProfile(Record, std::string_view access, const Cancellation&);
    void UpdateColumn(const Record&, const char* column, const std::string& value, const Cancellation&);
    void Save(const Credentials&, const Cancellation&);
    void FailureCleanup(Error);
    void Reset();
    std::shared_ptr<ITransport> http_;
    std::shared_ptr<ICredentials> store_;
    Clock clock_;
    // Server time minus this PC's clock, learned from response Date headers.
    // Token times are the server's; a PC clock that is off by more than the
    // validation leeway would otherwise reject every sign-in.
    std::int64_t skew_ = 0;
    std::int64_t Now() const { return clock_() + skew_; }
    bool discovered_ = false;
    bool exchanging_ = false;
    std::string access_;
    std::int64_t expires_ = 0;
    std::optional<Credentials> credentials_;
    std::optional<Profile> profile_;
};
}
