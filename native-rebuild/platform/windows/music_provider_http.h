#pragma once
#include "core/music/music_provider_account.h"

namespace lume::music::auth {
bool ValidHttpRequest(const HttpRequest&) noexcept;
HttpResponse SendHttp(const HttpRequest&, const Cancelled&);
std::string UrlEncode(std::string_view);
std::string Base64(std::string_view);
std::string RandomHex(std::size_t bytes);
std::string HashHex(std::string_view input, std::string_view algorithm = "MD5");
// Raw byte strings in and out, PKCS7 padding unless raw RSA is requested.
std::string Aes(std::string_view input, std::string_view key, std::string_view iv,
                bool encrypt = true);
std::string RsaEncrypt(std::string_view input, std::string_view publicPem,
                       bool raw = false);
Reply RunNetease(Operation, Json& secrets, const Cancelled&, const Exchange&);
Reply RunQQ(Operation, Json& secrets, const Cancelled&, const Exchange&);
Reply RunKugou(Operation, Json& secrets, const Cancelled&, const Exchange&);
Reply ExecuteProvider(Provider, Operation, Json& secrets, const Cancelled&);
}  // namespace lume::music::auth
