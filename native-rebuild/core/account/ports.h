#pragma once
#include "core/account/security.h"
#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
namespace lume::account {
class Cancellation final {
public:
    void Cancel(bool forgetCredentials = true) noexcept {
        if (forgetCredentials) forgetCredentials_.store(true);
        cancelled_.store(true);
    }
    bool Cancelled() const noexcept { return cancelled_.load(); }
    bool ForgetCredentials() const noexcept { return forgetCredentials_.load(); }
    void Check() const { if (Cancelled()) throw Failure(Error::Cancelled); }
private:
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> forgetCredentials_{false};
};
struct HttpRequest {
    Target target = Target::Discovery;
    std::string method = "GET", url, bearer, body;
    std::map<std::string, std::string> headers;
    std::size_t maxResponse = 256 * 1024;
};
// date: the server's Date header in Unix seconds, 0 when absent.
struct HttpResponse { unsigned status = 0; std::string body, contentType; std::int64_t date = 0; };
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual HttpResponse Send(const HttpRequest&, const Cancellation&) = 0;
};
struct Credentials { std::string refreshToken, subject, nonce; };
// A guest gets ten downloads in total on this device, not per day; signing
// in then gives the account ten a day (enforced by the server).
inline constexpr unsigned GuestWallpaperTotalLimit = 10;
inline constexpr unsigned MemberWallpaperDailyLimit = 10;
// The server is authoritative for signed-in tiers because the allowlist can
// grant an account unlimited downloads while ordinary members remain capped.
inline constexpr unsigned SignedInWallpaperReservationLimit =
    std::numeric_limits<unsigned>::max();
struct WallpaperQuota {
    std::int64_t day = 0;
    unsigned total = 0;
    std::map<std::string, unsigned> accounts;
    // Guest downloads ever made on this device; never reset by a new day.
    unsigned guest = 0;
};
class ICredentials {
public:
    virtual ~ICredentials() = default;
    virtual std::optional<Credentials> Load() = 0;
    virtual void Save(const Credentials&) = 0;
    virtual void Clear() = 0;
    virtual std::string DeviceId() = 0;
    // Atomic daily reservation. Logout never clears this device ledger.
    // Implementations must fail closed when storage cannot be committed.
    virtual void ReserveWallpaperDownload(std::int64_t day, std::string_view subject, unsigned limit) = 0;
    virtual void ReleaseWallpaperDownload(std::int64_t day, std::string_view subject) = 0;
};
struct Avatar { Bytes bytes; std::string mime; };
// Windows implements byte validation/decoding; the core never trusts a filename.
Avatar ValidateAvatar(Bytes);
std::string AvatarDataUrl(const Avatar&);
using Clock = std::function<std::int64_t()>;
}
