#include "core/account/backend.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
namespace lume::account {
namespace {
constexpr char AccountOrigin[] = "https://account.lumedesktop.com";
std::string Text(const Json& j, const char* name, std::size_t cap, bool required = true) {
    if (!j.contains(name)) { if (!required) return {}; throw Failure(Error::InvalidResponse); }
    if (!j[name].is_string()) throw Failure(Error::InvalidResponse);
    auto text = j[name].get<std::string>();
    if ((required && text.empty()) || text.size() > cap || text.find('\0') != text.npos ||
        text.find_first_of("\r\n") != text.npos) throw Failure(Error::InvalidResponse);
    for (auto c : Wide(text)) if (c < 32 || (c >= 127 && c <= 159)) throw Failure(Error::InvalidResponse);
    return text;
}
// Display-only profile fields: a value Lume cannot show is treated as absent,
// never as a reason to refuse an otherwise valid sign-in.
std::string ProfileText(const Json& j, const char* name, std::size_t cap) {
    try { return Text(j, name, cap, false); } catch (const Failure&) { return {}; }
}
HttpRequest Request(Target target, const char* method, const std::string& url,
                    std::string_view bearer = {}, std::string body = {}, const char* contentType = nullptr) {
    HttpRequest request;
    request.target = target; request.method = method; request.url = url; request.bearer = bearer; request.body = std::move(body);
    if (contentType) request.headers["content-type"] = contentType;
    return request;
}
bool VipTag(std::string s) {
    for (auto& c : s) {
        if (c == ',' || c == ';' || c == '|') c = ' ';
        else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::istringstream input(s); std::string tag;
    while (input >> tag) if (tag == "vip") return true;
    return false;
}
void CheckWallpaperId(std::string_view id) {
    if (!wallpaper::IsSafeWallpaperId(id)) throw Failure(Error::InvalidCommand);
}
WallpaperEngagement ParseEngagement(const Json& value) {
    if (!value.is_object() || !value.contains("ok") || value["ok"] != true ||
        !value.contains("liked") || !value["liked"].is_boolean()) throw Failure(Error::InvalidResponse);
    const auto count = [&](const char* key) {
        if (!value.contains(key) || !value[key].is_number_integer() || value[key] < 0 ||
            value[key] > 9007199254740991ULL) throw Failure(Error::InvalidResponse);
        return value[key].get<std::uint64_t>();
    };
    return {count("likes"), count("downloads"), value["liked"].get<bool>()};
}
bool WallpaperSource(std::string_view source) {
    constexpr std::string_view origin = "https://static.lumedesktop.com";
    if (source.size() > 8192 || source.substr(0, origin.size()) != origin ||
        source.find_first_of("?#\\\r\n") != source.npos || source.find('\0') != source.npos) return false;
    const auto path = source.substr(origin.size());
    for (const auto prefix : {"/lume/wallpapers/official/videos/", "/lume/wallpapers/official/images/",
        "/lume/wallpapers/static/videos/", "/lume/wallpapers/static/images/"}) {
        const std::string_view allowed(prefix);
        if (path.size() > allowed.size() && path.substr(0, allowed.size()) == allowed) {
            // The server also validates decoded path segments before granting a ticket.
            return path.find("/../") == path.npos && path.find("/./") == path.npos;
        }
    }
    return false;
}
}
Backend::Backend(std::shared_ptr<ITransport> http, std::shared_ptr<ICredentials> store, Clock clock)
    : http_(std::move(http)), store_(std::move(store)), clock_(std::move(clock)) {
    if (!http_ || !store_ || !clock_) throw Failure(Error::Service);
}
HttpResponse Backend::Send(HttpRequest request, const Cancellation& cancel, const Cancellation* accountCancellation) {
    cancel.Check();
    if (!ValidTarget(request.target, request.method, request.url, !request.bearer.empty())) throw Failure(Error::InvalidTarget);
    auto response = http_->Send(request, cancel);
    cancel.Check();
    if (response.date > 0) {
        // Within half a minute is ordinary drift and Date's one-second steps.
        const auto offset = response.date - clock_();
        skew_ = offset > 30 || offset < -30 ? offset : 0;
    }
    if (response.status == 401 && !exchanging_ && SignedIn() && !request.bearer.empty() &&
        request.bearer == access_ && (request.target == Target::Device || request.target == Target::ProfileRead ||
        request.target == Target::WallpaperStats || request.target == Target::WallpaperLike || request.target == Target::WallpaperGrant)) {
        // Access tokens can expire earlier than their advertised lifetime. Try
        // one verified refresh before treating a protected API's 401 as logout.
        Refresh(accountCancellation ? *accountCancellation : cancel);
        cancel.Check();request.bearer=access_;response=http_->Send(request,cancel);cancel.Check();
    }
    if (response.body.size() > request.maxResponse) throw Failure(Error::InvalidResponse);
    const auto error = MapHttpError(response.status, response.body, request.target);
    if (error != Error::None) throw Failure(error);
    return response;
}
Json Backend::SendJson(HttpRequest request, const Cancellation& cancel, const Cancellation* accountCancellation) {
    return StrictJson(Send(std::move(request), cancel, accountCancellation).body);
}
void Backend::Discover(const Cancellation& cancel) {
    if (discovered_) return;
    const auto j = SendJson(Request(Target::Discovery, "GET", std::string(Issuer) + "/.well-known/openid-configuration"), cancel);
    if (j.value("issuer", "") != Issuer ||
        j.value("authorization_endpoint", "") != std::string(Issuer) + "/login/oauth/authorize" ||
        j.value("token_endpoint", "") != std::string(Issuer) + "/api/login/oauth/access_token" ||
        j.value("jwks_uri", "") != std::string(Issuer) + "/.well-known/jwks" ||
        j.value("userinfo_endpoint", "") != std::string(Issuer) + "/api/userinfo") throw Failure(Error::InvalidResponse);
    discovered_ = true;
}
void Backend::Save(const Credentials& value, const Cancellation& cancel) {
    if (cancel.Cancelled() && cancel.ForgetCredentials()) cancel.Check();
    try { store_->Save(value); }
    catch (...) { throw Failure(Error::Storage); }
    if (cancel.Cancelled()) {
        // A cancelled worker must not leave a just-written credential behind.
        if (cancel.ForgetCredentials()) store_->Clear();
        throw Failure(Error::Cancelled);
    }
}
void Backend::Reset() {
    std::fill(access_.begin(), access_.end(), '\0'); access_.clear();
    if (credentials_) {
        std::fill(credentials_->refreshToken.begin(), credentials_->refreshToken.end(), '\0');
        std::fill(credentials_->nonce.begin(), credentials_->nonce.end(), '\0');
    }
    credentials_.reset(); profile_.reset(); expires_ = 0;
}
void Backend::Logout() { Reset(); store_->Clear(); }
void Backend::FailureCleanup(Error error) {
    if (error == Error::AuthRequired || error == Error::DeviceInactive || error == Error::InvalidToken ||
        error == Error::TokenExpired || error == Error::CredentialCorrupt || error == Error::Storage) {
        Reset();
        try { store_->Clear(); } catch (...) { throw Failure(Error::Storage); }
    }
}
Profile Backend::Exchange(std::string body, const Grant* grant, const Credentials* retained, const Cancellation& cancel) {
    struct ExchangeGuard { bool& active; ~ExchangeGuard(){active=false;} } guard{exchanging_};
    exchanging_=true;
    Discover(cancel);
    // Fetch verification material before the one-shot code/refresh grant is consumed.
    // After a rotation response there must be no network dependency before Save.
    const auto jwks = SendJson(Request(Target::Jwks, "GET", std::string(Issuer) + "/.well-known/jwks"), cancel);
    // A refresh can consume R0 even when the app is closing. Finish this bounded
    // request and durably save verified R1 before honouring normal shutdown.
    Cancellation rotation;
    const auto& exchangeCancel = retained ? rotation : cancel;
    const auto tokens = SendJson(Request(Target::Token, "POST", std::string(Issuer) + "/api/login/oauth/access_token", {},
        std::move(body), "application/x-www-form-urlencoded"), exchangeCancel);
    auto access = Text(tokens, "access_token", 16384);
    auto type = Text(tokens, "token_type", 32);
    std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (type != "bearer" || !tokens.contains("expires_in") || !tokens["expires_in"].is_number_integer() ||
        tokens["expires_in"] <= 0 || tokens["expires_in"] > 31536000) throw Failure(Error::InvalidResponse);
    Credentials next;
    next.refreshToken = Text(tokens, "refresh_token", 16384, false);
    if (next.refreshToken.empty() && retained) next.refreshToken = retained->refreshToken;
    if (next.refreshToken.empty()) throw Failure(Error::InvalidResponse);
    next.nonce = grant ? grant->nonce : retained->nonce;
    std::int64_t expiry = Now() + tokens["expires_in"].get<std::int64_t>();
    auto id = Text(tokens, "id_token", 32768, false);
    if (id.empty()) {
        if (!retained) throw Failure(Error::InvalidToken);
        next.subject = retained->subject;
    } else {
        auto identity = ValidateIdToken(id, jwks, {Now(), next.nonce, retained ? retained->subject : "",
            retained != nullptr, access, grant ? grant->code : ""});
        next.subject = identity.subject; expiry = std::min(expiry, identity.expires);
    }
    if (retained) {
        // R0 may already be revoked. Commit the validated private rotation before
        // any ancillary request; leave the last confirmed public profile unchanged.
        Save(next, cancel);
        access_ = access; credentials_ = next; expires_ = expiry;
    }
    const auto userinfo = SendJson(Request(Target::Userinfo, "GET", std::string(Issuer) + "/api/userinfo", access), cancel);
    if (Text(userinfo, "sub", 512) != next.subject) throw Failure(Error::InvalidToken);
    Device(access, grant != nullptr, cancel);
    auto record = GetAccount(access, next.subject, cancel);
    auto profile = PublicProfile(std::move(record), access, cancel);
    if (!retained) { cancel.Check(); Save(next, cancel); }
    cancel.Check();
    access_ = std::move(access); credentials_ = std::move(next); expires_ = expiry; profile_ = profile;
    return profile;
}
Profile Backend::Login(const Grant& grant, const Cancellation& cancel) {
    try {
        return Exchange("grant_type=authorization_code&client_id=" + std::string(ClientId) +
            "&redirect_uri=" + UrlEncode(CallbackUri) + "&code=" + UrlEncode(grant.code) +
            "&code_verifier=" + UrlEncode(grant.verifier), &grant, nullptr, cancel);
    } catch (const Failure& e) { FailureCleanup(e.code); throw; }
}
std::optional<Profile> Backend::Restore(const Cancellation& cancel) {
    try {
        cancel.Check();
        auto saved = store_->Load();
        cancel.Check();
        if (!saved) { Reset(); return {}; }
        return Exchange("grant_type=refresh_token&client_id=" + std::string(ClientId) +
            "&refresh_token=" + UrlEncode(saved->refreshToken), nullptr, &*saved, cancel);
    } catch (const Failure& e) { FailureCleanup(e.code); throw; }
}
Profile Backend::Refresh(const Cancellation& cancel) {
    if (!SignedIn()) throw Failure(Error::AuthRequired);
    try {
        const auto retained = *credentials_;
        return Exchange("grant_type=refresh_token&client_id=" + std::string(ClientId) +
            "&refresh_token=" + UrlEncode(retained.refreshToken), nullptr, &retained, cancel);
    } catch (const Failure& e) { FailureCleanup(e.code); throw; }
}
void Backend::EnsureAccess(const Cancellation& cancel) {
    if (!SignedIn()) throw Failure(Error::AuthRequired);
    if (Now() + 60 >= expires_) Refresh(cancel);
    cancel.Check();
}
void Backend::Device(std::string_view access, bool registration, const Cancellation& cancel) {
    auto request = Request(Target::Device, registration ? "POST" : "GET", std::string(AccountOrigin) + "/api/devices/session",
        access, registration ? "{}" : "", registration ? "application/json" : nullptr);
    request.headers["x-lume-device-id"] = store_->DeviceId();
    const auto j = SendJson(std::move(request), cancel);
    if (!j.is_object() || !j.contains("ok") || j["ok"] != true || !j.contains("active") || !j["active"].is_boolean())
        throw Failure(Error::InvalidResponse);
    if (!j["active"].get<bool>()) throw Failure(Error::DeviceInactive);
}
void Backend::CheckDevice(const Cancellation& cancel) {
    try { EnsureAccess(cancel); Device(access_, false, cancel); }
    catch (const Failure& e) { FailureCleanup(e.code); throw; }
}
Backend::Record Backend::GetAccount(std::string_view access, std::string_view subject, const Cancellation& cancel) {
    const auto j = SendJson(Request(Target::ProfileRead, "GET", std::string(Issuer) + "/api/get-account", access), cancel);
    if (j.value("status", "") != "ok" || !j.contains("data") || !j["data"].is_object()) throw Failure(Error::InvalidResponse);
    const auto& data = j["data"];
    Record record;
    record.profile.id = Text(data, "id", 512); record.owner = Text(data, "owner", 256); record.name = Text(data, "name", 256);
    if (record.profile.id != subject) throw Failure(Error::InvalidToken);
    record.profile.displayName = ProfileText(data, "displayName", 512);
    if (record.profile.displayName.empty() || Wide(record.profile.displayName).size() > 256) {
        // Accounts made on the email sign-up page have no display name and a
        // generated user name: show the part of the email before the @.
        const auto email = ProfileText(data, "email", 320);
        const auto local = email.substr(0, email.find('@'));
        record.profile.displayName = !local.empty() && Wide(local).size() <= 256 ? local : record.name;
    }
    if (Wide(record.profile.displayName).size() > 256) record.profile.displayName = record.profile.id;
    record.avatar = ProfileText(data, "avatar", 8192);
    if (record.avatar.empty()) record.avatar = ProfileText(data, "permanentAvatar", 8192);
    record.profile.vip = VipTag(ProfileText(data, "tag", 2048));
    if (data.contains("tags") && data["tags"].is_array()) for (const auto& tag : data["tags"])
        if (tag.is_string() && VipTag(tag.get<std::string>())) record.profile.vip = true;
    if (data.contains("properties") && data["properties"].is_object()) {
        const auto& props = data["properties"];
        if (props.contains("lume_vip")) {
            const auto& vip = props["lume_vip"];
            if (vip == true || (vip.is_string() && (vip == "true" || vip == "TRUE"))) record.profile.vip = true;
        }
        if (props.contains("lume_vip_expires_at")) {
            const auto& value = props["lume_vip_expires_at"];
            std::optional<std::int64_t> expiry;
            if (value.is_string()) expiry = ParseIsoTime(value.get<std::string>());
            else if (value.is_number_integer() && value >= 0 && value < 253402300800000LL) expiry = value.get<std::int64_t>();
            if (expiry) {
                record.profile.vipExpiresAt = IsoTime(*expiry);
                record.profile.vip = record.profile.vip && *expiry > Now() * 1000;
            } else record.profile.vip = false;
        }
    }
    return record;
}
Profile Backend::PublicProfile(Record record, std::string_view, const Cancellation& cancel) {
    record.profile.avatarUrl.clear();
    if (!record.avatar.empty() && ValidTarget(Target::AvatarGet, "GET", record.avatar, false)) {
        try {
            auto request = Request(Target::AvatarGet, "GET", record.avatar); request.maxResponse = MaxAvatarBytes;
            auto response = Send(std::move(request), cancel);
            record.profile.avatarUrl = AvatarDataUrl(ValidateAvatar(Bytes(response.body.begin(), response.body.end())));
        } catch (const Failure& e) { if (e.code == Error::Cancelled) throw; }
        catch (...) {}
    }
    cancel.Check(); return record.profile;
}
void Backend::UpdateColumn(const Record& record, const char* column, const std::string& value, const Cancellation& cancel) {
    const auto url = std::string(Issuer) + "/api/update-user?userId=" + UrlEncode(record.profile.id) + "&owner=" +
        UrlEncode(record.owner) + "&columns=" + column;
    const auto j = SendJson(Request(Target::ProfileWrite, "POST", url, access_, Json{
        {"id", record.profile.id}, {"owner", record.owner}, {"name", record.name}, {column, value}}.dump(), "application/json"), cancel);
    if (j.value("status", "") != "ok") throw Failure(Error::Service);
}
Profile Backend::UpdateName(std::string_view value, const Cancellation& cancel) {
    auto name = NormalizeName(value);
    try {
        EnsureAccess(cancel); Device(access_, false, cancel);
        auto record = GetAccount(access_, credentials_->subject, cancel);
        UpdateColumn(record, "displayName", name, cancel);
        record = GetAccount(access_, credentials_->subject, cancel);
        if (record.profile.displayName != name) throw Failure(Error::InvalidResponse);
        auto profile = PublicProfile(std::move(record), access_, cancel);
        Save(*credentials_, cancel); profile_ = profile; return profile;
    } catch (const Failure& e) { FailureCleanup(e.code); throw; }
}
Profile Backend::UploadAvatar(Avatar avatar, const Cancellation& cancel) {
    const auto checked = ValidateAvatar(std::move(avatar.bytes));
    if (checked.mime != avatar.mime) throw Failure(Error::InvalidAvatar);
    try {
        EnsureAccess(cancel); Device(access_, false, cancel);
        auto record = GetAccount(access_, credentials_->subject, cancel);
        const auto ticket = SendJson(Request(Target::AvatarTicket, "POST", std::string(AccountOrigin) + "/api/avatar/upload-url",
            access_, Json{{"mimeType", checked.mime}, {"size", checked.bytes.size()}}.dump(), "application/json"), cancel);
        if (!ticket.contains("ok") || ticket["ok"] != true) throw Failure(Error::Service);
        auto upload = Text(ticket, "uploadUrl", 8192), remote = Text(ticket, "avatarUrl", 8192);
        if (!ValidTarget(Target::AvatarPut, "PUT", upload, false) || !ValidTarget(Target::AvatarGet, "GET", remote, false))
            throw Failure(Error::InvalidTarget);
        Send(Request(Target::AvatarPut, "PUT", upload, {}, std::string(checked.bytes.begin(), checked.bytes.end()), checked.mime.c_str()), cancel);
        UpdateColumn(record, "avatar", remote, cancel);
        record = GetAccount(access_, credentials_->subject, cancel);
        if (record.avatar != remote) throw Failure(Error::InvalidResponse);
        auto profile = record.profile; profile.avatarUrl = AvatarDataUrl(checked);
        Save(*credentials_, cancel); profile_ = profile; return profile;
    } catch (const Failure& e) { FailureCleanup(e.code); throw; }
}
WallpaperEngagement Backend::WallpaperStats(std::string_view id, const Cancellation& cancel,
    const Cancellation* accountCancellation) {
    CheckWallpaperId(id);
    try {
        cancel.Check();
        // Selection changes must not discard a rotated account refresh token.
        if (SignedIn()) EnsureAccess(accountCancellation ? *accountCancellation : cancel);
        cancel.Check();
        auto request = Request(Target::WallpaperStats, "GET",
            std::string(AccountOrigin) + "/api/wallpapers/stats?id=" + std::string(id), access_);
        request.maxResponse = 16 * 1024;
        const auto result = ParseEngagement(SendJson(std::move(request), cancel, accountCancellation));
        if (!SignedIn() && result.liked) throw Failure(Error::InvalidResponse);
        return result;
    } catch (const Failure& failure) { FailureCleanup(failure.code); throw; }
}
WallpaperEngagement Backend::SetWallpaperLike(std::string_view id, bool liked, const Cancellation& cancel,
    const Cancellation* accountCancellation) {
    CheckWallpaperId(id);
    try {
        cancel.Check();
        EnsureAccess(accountCancellation ? *accountCancellation : cancel);
        cancel.Check();
        auto request = Request(Target::WallpaperLike, "POST",
            std::string(AccountOrigin) + "/api/wallpapers/like", access_,
            Json{{"id", id}, {"liked", liked}}.dump(), "application/json");
        request.maxResponse = 16 * 1024;
        const auto result = ParseEngagement(SendJson(std::move(request), cancel, accountCancellation));
        if (result.liked != liked) throw Failure(Error::InvalidResponse);
        return result;
    } catch (const Failure& failure) { FailureCleanup(failure.code); throw; }
}
void Backend::RecordWallpaperDownload(std::string_view id, std::string_view source, const Cancellation& cancel) {
    CheckWallpaperId(id);
    if (!WallpaperSource(source)) throw Failure(Error::InvalidTarget);
    try {
        if (SignedIn()) EnsureAccess(cancel);
        auto request = Request(Target::WallpaperGrant, "POST",
            std::string(AccountOrigin) + "/api/wallpapers/grant", SignedIn() ? access_ : std::string{},
            Json{{"id", id}, {"sourceUrl", source}}.dump(), "application/json");
        request.headers["x-lume-device-id"] = store_->DeviceId();
        request.maxResponse = 16 * 1024;
        const auto grant = SendJson(std::move(request), cancel);
        if (!grant.contains("ok") || grant["ok"] != true) throw Failure(Error::InvalidResponse);
        const auto ticket = Text(grant, "downloadUrl", 8192);
        if (!ValidTarget(Target::WallpaperTicket, "GET", ticket, false)) throw Failure(Error::InvalidTarget);
        auto claim = Request(Target::WallpaperTicket, "GET", ticket);
        claim.maxResponse = 16 * 1024;
        // Resolve only ticket metadata. The transport never follows this media redirect.
        if (Send(std::move(claim), cancel).status != 302) throw Failure(Error::InvalidResponse);
    } catch (const Failure& failure) { FailureCleanup(failure.code); throw; }
}
void Backend::AuthorizeWallpaperDownload(std::string_view id, std::string_view source, const Cancellation& cancel) {
    CheckWallpaperId(id); if (!WallpaperSource(source)) throw Failure(Error::InvalidTarget);
    if (SignedIn()) EnsureAccess(cancel);
    cancel.Check();
    // The server is authoritative for signed-in tiers, including the unlimited
    // allowlist; guests also get a local fail-closed guard of ten downloads in
    // total on this device, which survives restarts, logins and new days.
    const auto day=(clock_() + 8 * 3600) / 86400;
    const auto subject=SignedIn() ? credentials_->subject : std::string{};
    store_->ReserveWallpaperDownload(day,subject,
        SignedIn() ? SignedInWallpaperReservationLimit : GuestWallpaperTotalLimit);
    // Consume the server grant before starting CDN media bytes; quota/network
    // failure is never downgraded to a post-download statistics warning.
    try { RecordWallpaperDownload(id, source, cancel); }
    catch (...) {
        // No media has been admitted. Undo this local reservation only; never
        // pretend that an uncertain server ticket was refunded server-side.
        try { store_->ReleaseWallpaperDownload(day,subject); } catch (...) {}
        throw;
    }
}
}
