#pragma once
#include "vendor/nlohmann/json.hpp"
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace lume::account {
inline constexpr char Issuer[] = "https://auth.lumedesktop.com";
inline constexpr char ClientId[] = "0530ff0ec79045ccac38";
inline constexpr char CallbackUri[] = "lume://login";
// How long a started sign-in waits for the browser to come back. Creating an
// account on the way (email, verification code, password) easily takes more
// than a few minutes; PKCE and state still bind the callback to this request.
inline constexpr std::int64_t LoginTransactionSeconds = 30 * 60;
inline constexpr std::size_t MaxAvatarBytes = 2 * 1024 * 1024;
using Json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
enum class Error {
    None, InvalidCommand, InvalidName, InvalidAvatar, AvatarTooLarge,
    CallbackInvalid, CallbackInactive, CallbackState, CallbackExpired,
    AuthorizationDenied, InvalidToken, TokenExpired, Network, Service,
    InvalidResponse, Storage, CredentialCorrupt, Cancelled, AuthRequired,
    DeviceInactive, Browser, LocalLogout, InvalidTarget, ProtocolRegistration, AvatarUploadFailed, WallpaperQuota
};
std::string ErrorCode(Error);
struct Failure final : std::exception {
    explicit Failure(Error value) : code(value) {}
    const char* what() const noexcept override { return "Account operation failed"; }
    Error code;
};
Json StrictJson(std::string_view, std::size_t limit = 256 * 1024);
std::string Utf8(std::wstring_view);
std::wstring Wide(std::string_view);
std::string NormalizeName(std::string_view);
std::string UrlEncode(std::string_view);
std::string Base64(const Bytes&, bool url = true);
Bytes Unbase64(std::string_view);
Bytes RandomBytes(std::size_t);
Bytes Digest(std::string_view, bool sha512 = false);
bool VerifyRsa(const Bytes& modulus, const Bytes& exponent, std::string_view message,
               const Bytes& signature, bool sha512);
std::string PkceChallenge(std::string_view);
struct Grant { std::string code, nonce, verifier; };
struct Transaction {
    std::string state, nonce, verifier;
    std::int64_t expires = 0;
    bool active = false;
    std::string Begin(std::int64_t now);
    Grant Complete(std::string_view callback, std::int64_t now);
    void Cancel();
};
enum class Target { Discovery, Token, Jwks, Userinfo, ProfileRead, ProfileWrite, Device, AvatarTicket, AvatarPut, AvatarGet,
    WallpaperStats, WallpaperLike, WallpaperGrant, WallpaperTicket };
bool ValidTarget(Target, std::string_view method, std::string_view url, bool bearer) noexcept;
bool ValidAuthorizationUrl(std::string_view) noexcept;
Error MapHttpError(unsigned status, std::string_view body, Target target = Target::Token);
}
