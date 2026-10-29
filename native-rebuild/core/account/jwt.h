#pragma once
#include "core/account/security.h"
namespace lume::account {
struct TokenContext {
    std::int64_t now = 0;
    std::string nonce, subject;
    bool refresh = false;
    std::string accessToken, code;
};
struct Identity { std::string subject; std::int64_t expires = 0; };
Identity ValidateIdToken(std::string_view token, const Json& jwks, const TokenContext&);
}
