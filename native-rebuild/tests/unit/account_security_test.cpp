#include <iostream>
#include "core/account/security.h"
#include "core/ui/account_page_protocol.h"
#include <cstdlib>
using namespace lume::account;
namespace {
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
template<class F> void Reject(F f, Error expected, const char* why) {
    try { f(); } catch (const Failure& e) { Expect(e.code == expected, why); return; }
    Expect(false, why);
}
}
int main() {
    Expect(PkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
        "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", "RFC 7636 S256 vector");
    Transaction tx;
    auto url = tx.Begin(1000);
    Expect(url.find("https://auth.lumedesktop.com/login/oauth/authorize?") == 0, "fixed browser origin");
    Expect(url.find("code_challenge_method=S256") != std::string::npos, "PKCE required");
    const auto state = tx.state;
    Reject([&] { tx.Complete("lume://login?code=x&state=wrong", 1001); }, Error::CallbackState, "state mismatch");
    Reject([&] { tx.Complete("lume://login?code=x&state=" + state + "&state=" + state, 1001); },
        Error::CallbackInvalid, "duplicate state");
    Reject([&] { tx.Complete("lume://login?code=x&state=" + state + "&%73tate=" + state, 1001); },
        Error::CallbackInvalid, "encoded duplicate");
    for (const auto* prefix : {"http://login?", "lume://user@login?", "lume://login:80?",
                              "lume://login/evil?", "lume://login\\?"}) {
        Reject([&] { tx.Complete(std::string(prefix) + "code=x&state=" + state, 1001); },
            Error::CallbackInvalid, "strict callback URI");
    }
    Reject([&] { tx.Complete("lume://login?code=x&state=" + state + "#x", 1001); }, Error::CallbackInvalid, "fragment");
    Reject([&] { tx.Complete("lume://login?code=x&state=" + state + "&id_token=secret", 1001); }, Error::CallbackInvalid, "implicit token");
    auto grant = tx.Complete("lume://login?code=abc%2D123&state=" + state, 1001);
    Expect(grant.code == "abc-123" && !grant.nonce.empty() && grant.verifier.size() >= 43, "grant contains private PKCE");
    Reject([&] { tx.Complete("lume://login?code=x&state=" + state, 1002); }, Error::CallbackInactive, "replay");
    tx.Begin(1000);
    // Signing up on the way back (email code, password) must not expire the login.
    Expect(tx.Complete("lume://login?code=x&state=" + tx.state, 1000 + 20 * 60).code == "x", "twenty minutes still accepted");
    tx.Begin(1000);
    Reject([&] { tx.Complete("lume://login?code=x&state=" + tx.state, 1000 + LoginTransactionSeconds); }, Error::CallbackExpired, "thirty minutes");
    tx.Begin(1000);
    Reject([&] { tx.Complete("lume://login?error=access_denied&state=" + tx.state, 1002); },
        Error::AuthorizationDenied, "provider errors sanitized");
    Expect(!tx.active, "provider denial consumes transaction");
    using lume::ui::ParseAccountPageJson;
    using lume::ui::AccountAction;
    const auto command = ParseAccountPageJson(LR"({"version":1,"scope":"account","action":"updateName","value":"\ud83d\ude00\u540d"})");
    Expect(command && command->action == AccountAction::UpdateName, "Unicode command");
    for (auto text : {
        LR"({"version":1,"scope":"account","action":"login","value":"x"})",
        LR"({"version":1,"scope":"account","action":"login","action":"logout"})",
        LR"({"version":1,"scope":"account","action":"snapshot","token":"x"})",
        LR"({"version":1.0,"scope":"account","action":"snapshot"})",
        LR"({"version":1,"scope":"account","action":"updateName","value":"\ud800"})",
        LR"({"version":1,"scope":"account","action":"updateName","value":"x"})",
        LR"({"version":1,"scope":"account","action":"updateName","value":{"a":1}})",
        LR"({"version":1,"scope":"account","action":"chooseAvatar","path":"C:\\x"})"
    }) Expect(!ParseAccountPageJson(text), "strict public protocol");
    Expect(!ParseAccountPageJson(std::wstring(8193, L'x')), "command size");
    Expect(!ParseAccountPageJson(std::wstring(1, static_cast<wchar_t>(0xd800))), "invalid UTF16");
    Expect(NormalizeName("  AB  ") == "AB", "trim name");
    Expect(NormalizeName(std::string("\xF0\x9F\x98\x80") + "B").size() == 5, "count scalars not bytes");
    Reject([] { NormalizeName(std::string(33, 'a')); }, Error::InvalidName, "overlong name");
    Reject([] { NormalizeName(std::string("ab\0cd", 5)); }, Error::InvalidName, "embedded null");
    Reject([] { NormalizeName("\xC0\xAF"); }, Error::InvalidName, "overlong UTF8");
    Expect(ValidTarget(Target::Token, "POST", std::string(Issuer) + "/api/login/oauth/access_token", true), "token target");
    Expect(!ValidTarget(Target::Token, "POST", "https://auth.lumedesktop.com.evil/api/login/oauth/access_token", true), "host suffix");
    Expect(!ValidTarget(Target::Token, "POST", "https://u@auth.lumedesktop.com/api/login/oauth/access_token", true), "userinfo URL");
    Expect(!ValidTarget(Target::Token, "POST", "http://auth.lumedesktop.com/api/login/oauth/access_token", true), "no TLS downgrade");
    Expect(!ValidTarget(Target::AvatarPut, "PUT", "https://bucket.cos.ap-shanghai.myqcloud.com/a?sig=x", true), "no bearer on signed PUT");
    Expect(ValidTarget(Target::AvatarPut, "PUT", "https://bucket.cos.ap-shanghai.myqcloud.com/a?sig=x", false), "signed COS PUT");
    Expect(!ValidTarget(Target::AvatarGet, "GET", "https://127.0.0.1/a", false), "avatar SSRF denied");
    Expect(!ValidTarget(Target::AvatarGet, "GET", "https://evil.example/a", false), "untrusted avatar host");
    Expect(ValidTarget(Target::WallpaperStats,"GET",
        "https://account.lumedesktop.com/api/wallpapers/stats?id=official.v2",false),"public stats whitelist");
    Expect(!ValidTarget(Target::WallpaperStats,"GET",
        "https://account.lumedesktop.com/api/wallpapers/stats?id=official.v2&extra=x",true),"stats query exact");
    Expect(!ValidTarget(Target::WallpaperStats,"GET",
        "https://static.lumedesktop.com/api/wallpapers/stats?id=official.v2",true),"no bearer to CDN");
    Expect(!ValidTarget(Target::WallpaperLike,"POST",
        "https://account.lumedesktop.com/api/wallpapers/like",false),"guest write denied");
    Expect(ValidTarget(Target::WallpaperTicket,"GET",
        "https://account.lumedesktop.com/api/wallpapers/download/abcdefghijklmnopqrstuvwxyz",false),"bounded ticket");
    Expect(!ValidTarget(Target::WallpaperTicket,"GET",
        "https://account.lumedesktop.com/api/wallpapers/download/abcdefghijklmnopqrstuvwxyz",true),"ticket never bearer");
    Expect(MapHttpError(302,"",Target::WallpaperTicket)==Error::None&&
        MapHttpError(302,"",Target::WallpaperStats)!=Error::None,"redirect receipt allowed only for ticket metadata");
    Expect(MapHttpError(401, "{}") == Error::AuthRequired, "401 mapping");
    Expect(MapHttpError(403,"{}",Target::Device)==Error::Service&&
        MapHttpError(403,"{}",Target::WallpaperGrant)==Error::Service,
        "business permission failures do not revoke account credentials");
    for (const auto target : {Target::Discovery, Target::Jwks})
        for (const auto status : {401u, 403u, 503u})
            Expect(MapHttpError(status, "{}", target) == Error::Service,
                "public discovery/JWKS failure is not evidence of an expired account");
    Expect(MapHttpError(400, R"({"error":"invalid_grant","error_description":"secret"})") == Error::AuthRequired, "refresh invalid_grant");
    Expect(ErrorCode(MapHttpError(500, "secret token")) == "service-unavailable", "no raw error propagation");
    std::cout << "account security passed\n";
}
