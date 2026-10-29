#include <iostream>
#include "core/account/jwt.h"
#include "tests/unit/support/account_signed_fixture.h"
#include <cstdlib>
using namespace lume::account;
namespace {
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
}
int main() {
    testing::SignedFixture fixture, other;
    TokenContext context{1000, "fixture-nonce", "", false, "fixture-access", "fixture-code"};
    const auto valid = testing::SignedFixture::Claims();
    for (const auto* alg : {"RS256", "RS512"}) {
        Expect(ValidateIdToken(fixture.Sign(valid, alg), fixture.jwks, context).subject == "fixture-user", "real RSA signed JWT");
    }
    const auto reject = [&](const std::string& token, const Json& keys, const TokenContext& ctx, const char* why) {
        try { ValidateIdToken(token, keys, ctx); } catch (const Failure&) { return; }
        Expect(false, why);
    };
    reject(fixture.Sign(valid), other.jwks, context, "wrong real key");
    for (auto claim : {"iss", "aud", "nonce", "sub"}) {
        auto value = valid; value[claim] = claim == std::string("sub") ? "" : "wrong";
        reject(fixture.Sign(value), fixture.jwks, context, "invalid signed claim");
    }
    for (auto claim : {"iss", "aud", "sub", "iat", "exp", "nonce"}) {
        auto value = valid; value.erase(claim);
        reject(fixture.Sign(value), fixture.jwks, context, "missing required claim");
    }
    auto value = valid; value["exp"] = 1000;
    reject(fixture.Sign(value), fixture.jwks, context, "expired");
    value = valid; value["nbf"] = 2000;
    reject(fixture.Sign(value), fixture.jwks, context, "nbf in future");
    value = valid; value["iat"] = 2000;
    reject(fixture.Sign(value), fixture.jwks, context, "iat in future");
    value = valid; value["exp"] = "4600";
    reject(fixture.Sign(value), fixture.jwks, context, "exp wrong type");
    value = valid; value["aud"] = Json::array({ClientId, "other-client"});
    reject(fixture.Sign(value), fixture.jwks, context, "multi audience requires azp");
    value["azp"] = ClientId;
    Expect(!ValidateIdToken(fixture.Sign(value), fixture.jwks, context).subject.empty(), "multi audience correct azp");
    value["azp"] = "other-client";
    reject(fixture.Sign(value), fixture.jwks, context, "azp mismatch");
    reject(fixture.Sign(valid, "HS256"), fixture.jwks, context, "algorithm confusion");
    reject(fixture.Sign(valid, "none"), fixture.jwks, context, "none algorithm");
    reject(fixture.Sign(valid, "RS256", {{"jku", "https://evil.example"}}), fixture.jwks, context, "untrusted key URL");
    reject(fixture.Sign(valid, "RS256", {{"crit", Json::array({"x"})}}), fixture.jwks, context, "unsupported critical header");
    auto duplicate = fixture.jwks; duplicate["keys"].push_back(duplicate["keys"][0]);
    reject(fixture.Sign(valid), duplicate, context, "ambiguous kid");
    auto token = fixture.Sign(valid); token.back() = token.back() == 'A' ? 'B' : 'A';
    reject(token, fixture.jwks, context, "tampered signature");
    TokenContext refresh = context; refresh.refresh = true; refresh.subject = "fixture-user";
    value = valid; value.erase("nonce");
    Expect(ValidateIdToken(fixture.Sign(value), fixture.jwks, refresh).subject == "fixture-user", "refresh may omit nonce");
    value["nonce"] = "new-unrequested-nonce";
    reject(fixture.Sign(value), fixture.jwks, refresh, "refresh nonce must retain original");
    value = valid; value["sub"] = "different-user";
    reject(fixture.Sign(value), fixture.jwks, refresh, "refresh subject cannot change");
    value = valid; value["at_hash"] = "bad";
    reject(fixture.Sign(value), fixture.jwks, context, "access-token hash bound");
    auto hash = Digest("fixture-access"); hash.resize(hash.size() / 2); value["at_hash"] = Base64(hash);
    Expect(!ValidateIdToken(fixture.Sign(value), fixture.jwks, context).subject.empty(), "at_hash match");
    std::cout << "account real-signature JWT fixtures passed\n";
}
