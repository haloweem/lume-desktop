#pragma once
#include "platform/windows/music_providers/netease_playback.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstring>

namespace xeapi_fixture {
using namespace lume::music::auth;
inline void Check(bool value, const char* step = "check") { if (!value) throw std::runtime_error(std::string("xeapi fixture: ") + step); }
inline std::string Hex(std::string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); i += 2)
        result += static_cast<char>(std::stoul(std::string(value.substr(i, 2)), nullptr, 16));
    return result;
}
inline PUCHAR Bytes(std::string_view value) { return reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())); }
inline const std::string& StaticKey() {
    static const auto value = Hex("ab1d5a430f6bb04a3f01e81ddd72bd916d5ce591248ac128714806d7f8fb1b84"); return value;
}
inline std::string Decode(std::string_view text) {
    std::string value;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%') { Check(i + 2 < text.size()); value += Hex(text.substr(i + 1, 2)); i += 2; }
        else value += text[i] == '+' ? ' ' : text[i];
    }
    return value;
}
inline Json Form(std::string_view body) {
    Json value = Json::object();
    while (!body.empty()) {
        const auto end = body.find('&'), equals = body.find('='); Check(equals != body.npos && equals < end);
        value[Decode(body.substr(0, equals))] = Decode(body.substr(equals + 1, end == body.npos ? body.npos : end - equals - 1));
        if (end == body.npos) break;
        body.remove_prefix(end + 1);
    }
    return value;
}
struct ServerKey {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    std::string publicKey;
    ServerKey() {
        Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDH_ALGORITHM, nullptr, 0) >= 0);
        constexpr wchar_t curve[] = L"curve25519";
        Check(BCryptSetProperty(algorithm, BCRYPT_ECC_CURVE_NAME,
            reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(curve)), sizeof(curve), 0) >= 0);
        Check(BCryptGenerateKeyPair(algorithm, &key, 0, 0) >= 0 && BCryptFinalizeKeyPair(key, 0) >= 0);
        std::string blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64, '\0'); ULONG size = 0;
        Check(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, Bytes(blob), static_cast<ULONG>(blob.size()), &size, 0) >= 0);
        publicKey = blob.substr(sizeof(BCRYPT_ECCKEY_BLOB), 32);
    }
    ~ServerKey() { if (key) BCryptDestroyKey(key); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
};
inline ServerKey& Server() { static ServerKey key; return key; }
inline HttpResponse KeyResponse(const HttpRequest& request) {
    const auto data = Form(request.body);
    constexpr auto signKey = "mUHCwVNWJbunMqAHf5MImuirT6plvs6VSFW62MGHstFQxhBGdEoIhLItH3djc4+FB/OKty3+lL2rGeoFBpVe5g==";
    const auto signature = Base64(HmacSha256(signKey, data.at("timestamp").get<std::string>() + data.at("nonce").get<std::string>()));
    Check(signature == data.at("signature"));
    const Json key{{"publicKey", Base64(Server().publicKey)}, {"version", "fixture"}, {"sk", "fixture"}};
    const Json response{{"code", 200}, {"data", {{"signature", signature}, {"timestamp", data.at("timestamp")},
        {"encryptedData", Base64(Aes(key.dump(), StaticKey(), ""))}}}};
    return {200, response.dump(), "application/json", {}, {}};
}
// Ephemeral server for protocol tests; the separate Node test uses RFC 7748.
inline std::string Shared(std::string_view peer) {
    struct State {
        BCRYPT_KEY_HANDLE remote = nullptr;
        BCRYPT_SECRET_HANDLE secret = nullptr;
        ~State() { if (secret) BCryptDestroySecret(secret); if (remote) BCryptDestroyKey(remote); }
    } state;
    const auto blob = [](ULONG magic, std::string_view publicKey, std::string_view privateKey) {
        BCRYPT_ECCKEY_BLOB header{magic, 32};
        std::string value(reinterpret_cast<const char*>(&header), sizeof(header));
        value.append(publicKey); value.append(32, '\0'); value.append(privateKey); return value;
    };
    const auto remote = blob(BCRYPT_ECDH_PUBLIC_GENERIC_MAGIC, peer, "");
    Check(BCryptImportKeyPair(Server().algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &state.remote,
        Bytes(remote), static_cast<ULONG>(remote.size()), 0) >= 0, "import public");
    Check(BCryptSecretAgreement(Server().key, state.remote, &state.secret, 0) >= 0);
    std::string secret(32, '\0'); ULONG size = 0;
    Check(BCryptDeriveKey(state.secret, BCRYPT_KDF_RAW_SECRET, nullptr, Bytes(secret), 32, &size, 0) >= 0 && size == 32);
    std::reverse(secret.begin(), secret.end()); return secret;
}
inline std::string OpenS(std::string_view s) {
    const auto key = HmacSha256(HmacSha256(std::string(32, '\0'), Shared(s.substr(0, 32))),
        std::string(s.substr(0, 32)) + '\1').substr(0, 16);
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_KEY_HANDLE handle = nullptr;
    Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0);
    Check(BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0) >= 0);
    Check(BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0, Bytes(key), 16, 0) >= 0);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = Bytes(s.substr(32, 12)); info.cbNonce = 12;
    info.pbTag = Bytes(s.substr(s.size() - 16)); info.cbTag = 16;
    std::string plain(s.size() - 60, '\0'); ULONG written = 0;
    const auto status = BCryptDecrypt(handle, Bytes(s.substr(44, plain.size())), static_cast<ULONG>(plain.size()),
        &info, nullptr, 0, Bytes(plain), static_cast<ULONG>(plain.size()), &written, 0);
    BCryptDestroyKey(handle); BCryptCloseAlgorithmProvider(algorithm, 0);
    Check(status >= 0 && written == plain.size(), "GCM decrypt"); return plain;
}
inline Json Payload(const HttpRequest& request) {
    const auto form = Form(request.body);
    const auto s = OpenS(Unbase64(form.at("S").get<std::string>()));
    Check(s.substr(s.find('|')) == "|android|fixture");
    auto c = form.at("C").get<std::string>();
    std::replace(c.begin(), c.end(), '-', '+'); std::replace(c.begin(), c.end(), '_', '/');
    while (c.size() % 4) c += '=';
    const auto mid = Aes(Unbase64(c), Unbase64(s.substr(0, s.find('|'))), "", false);
    auto encrypted = mid.substr(16);
    const auto rotation = (static_cast<unsigned char>(mid[0]) & 15) % encrypted.size();
    std::rotate(encrypted.begin(), encrypted.end() - rotation, encrypted.end());
    for (std::size_t i = 0; i < encrypted.size(); ++i) encrypted[i] ^= mid[i & 15];
    const auto plain = Json::parse(Aes(encrypted, StaticKey(), "", false));
    Check(plain.at("queryString") == "e_r=true");
    Check(Aes(Unbase64(form.at("R").get<std::string>()), StaticKey(), "", false) == "fixture|");
    return Form(plain.at("content").get<std::string>());
}
}
