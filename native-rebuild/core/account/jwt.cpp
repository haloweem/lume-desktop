#include "core/account/jwt.h"
#include <algorithm>
#include <limits>
namespace lume::account {
namespace {
std::string DecodeText(std::string_view text) {
    auto bytes = Unbase64(text);
    return std::string(bytes.begin(), bytes.end());
}
std::int64_t Date(const Json& j, const char* name) {
    if (!j.contains(name) || !j[name].is_number_integer() || j[name] < 0 ||
        j[name] > std::numeric_limits<std::int64_t>::max()) throw Failure(Error::InvalidToken);
    return j[name].get<std::int64_t>();
}
void HashClaim(const Json& j, const char* name, std::string_view value, bool sha512) {
    if (!j.contains(name)) return;
    if (value.empty() || !j[name].is_string()) throw Failure(Error::InvalidToken);
    auto hash = Digest(value, sha512); hash.resize(hash.size() / 2);
    if (j[name] != Base64(hash)) throw Failure(Error::InvalidToken);
}
}
Identity ValidateIdToken(std::string_view token, const Json& jwks, const TokenContext& context) {
    try {
        if (token.size() > 32768) throw Failure(Error::InvalidToken);
        auto first = token.find('.'), second = token.find('.', first == token.npos ? first : first + 1);
        if (first == token.npos || second == token.npos || token.find('.', second + 1) != token.npos) throw Failure(Error::InvalidToken);
        const auto header = StrictJson(DecodeText(token.substr(0, first)), 4096);
        const auto claims = StrictJson(DecodeText(token.substr(first + 1, second - first - 1)), 24576);
        if (!header.is_object() || !claims.is_object()) throw Failure(Error::InvalidToken);
        const auto alg = header.value("alg", "");
        if ((alg != "RS256" && alg != "RS512") || header.contains("crit") || header.contains("b64") ||
            header.contains("jku") || header.contains("jwk") || header.contains("x5u")) throw Failure(Error::InvalidToken);
        if (header.contains("typ") && header["typ"] != "JWT") throw Failure(Error::InvalidToken);
        const auto kid = header.value("kid", "");
        if (kid.size() > 256 || !jwks.is_object() || !jwks.contains("keys") ||
            !jwks["keys"].is_array() || jwks["keys"].size() > 32) throw Failure(Error::InvalidToken);
        const Json* selected = nullptr;
        for (const auto& key : jwks["keys"]) {
            if (!key.is_object() || key.value("kty", "") != "RSA" || (!kid.empty() && key.value("kid", "") != kid) ||
                (key.contains("alg") && key["alg"] != alg) || (key.contains("use") && key["use"] != "sig")) continue;
            if (key.contains("key_ops") && (!key["key_ops"].is_array() ||
                std::find(key["key_ops"].begin(), key["key_ops"].end(), "verify") == key["key_ops"].end())) continue;
            if (selected) throw Failure(Error::InvalidToken);
            selected = &key;
        }
        if (!selected || !VerifyRsa(Unbase64(selected->at("n").get<std::string>()),
            Unbase64(selected->at("e").get<std::string>()), token.substr(0, second),
            Unbase64(token.substr(second + 1)), alg == "RS512")) throw Failure(Error::InvalidToken);
        if (claims.value("iss", "") != Issuer) throw Failure(Error::InvalidToken);
        const auto& audience = claims.at("aud");
        bool match = audience.is_string() && audience == ClientId;
        if (audience.is_array()) {
            if (audience.empty() || audience.size() > 16) throw Failure(Error::InvalidToken);
            for (const auto& a : audience) {
                if (!a.is_string()) throw Failure(Error::InvalidToken);
                if (a == ClientId) match = true;
            }
            if (audience.size() > 1 && !claims.contains("azp")) throw Failure(Error::InvalidToken);
        }
        if (!match || (claims.contains("azp") && claims["azp"] != ClientId)) throw Failure(Error::InvalidToken);
        const auto expires = Date(claims, "exp"), issued = Date(claims, "iat");
        if (expires <= context.now) throw Failure(Error::TokenExpired);
        if (issued > context.now + 60 || issued >= expires ||
            (claims.contains("nbf") && Date(claims, "nbf") > context.now + 60)) throw Failure(Error::InvalidToken);
        const auto subject = claims.value("sub", "");
        if (subject.empty() || subject.size() > 512 || subject.find_first_of("\r\n\t") != subject.npos ||
            subject.find('\0') != subject.npos || (context.refresh && subject != context.subject)) throw Failure(Error::InvalidToken);
        if (!context.refresh && (context.nonce.empty() || !claims.contains("nonce"))) throw Failure(Error::InvalidToken);
        if (claims.contains("nonce") && (context.nonce.empty() || claims["nonce"] != context.nonce)) throw Failure(Error::InvalidToken);
        HashClaim(claims, "at_hash", context.accessToken, alg == "RS512");
        if (!context.refresh) HashClaim(claims, "c_hash", context.code, alg == "RS512");
        return {subject, expires};
    } catch (const Failure& failure) {
        if (failure.code == Error::TokenExpired) throw;
        throw Failure(Error::InvalidToken);
    } catch (...) { throw Failure(Error::InvalidToken); }
}
}
