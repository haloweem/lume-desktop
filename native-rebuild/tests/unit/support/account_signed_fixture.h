#pragma once
#include "core/account/security.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <stdexcept>

namespace lume::account::testing {
// Test-only ephemeral RSA private key, never used with the real issuer.
class SignedFixture {
public:
    SignedFixture() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_RSA_ALGORITHM, nullptr, 0) < 0 ||
            BCryptGenerateKeyPair(algorithm_, &key_, 2048, 0) < 0 || BCryptFinalizeKeyPair(key_, 0) < 0)
            throw std::runtime_error("fixture key");
        ULONG size = 0;
        BCryptExportKey(key_, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &size, 0);
        Bytes blob(size);
        if (BCryptExportKey(key_, nullptr, BCRYPT_RSAPUBLIC_BLOB, blob.data(), size, &size, 0) < 0)
            throw std::runtime_error("fixture export");
        BCRYPT_RSAKEY_BLOB header{}; std::memcpy(&header, blob.data(), sizeof(header));
        const auto begin = blob.begin() + sizeof(header);
        jwks = Json{{"keys", Json::array({{{"kty", "RSA"}, {"kid", "fixture"}, {"use", "sig"},
            {"e", Base64(Bytes(begin, begin + header.cbPublicExp))},
            {"n", Base64(Bytes(begin + header.cbPublicExp, blob.end()))}}})}};
    }
    ~SignedFixture() { BCryptDestroyKey(key_); BCryptCloseAlgorithmProvider(algorithm_, 0); }
    SignedFixture(const SignedFixture&) = delete;
    SignedFixture& operator=(const SignedFixture&) = delete;
    std::string Sign(Json claims, std::string alg = "RS256", Json extraHeader = Json::object()) const {
        Json header{{"alg", alg}, {"kid", "fixture"}, {"typ", "JWT"}};
        header.update(extraHeader);
        const auto h = header.dump(); const auto p = claims.dump();
        std::string input = Base64(Bytes(h.begin(), h.end())) + "." + Base64(Bytes(p.begin(), p.end()));
        BCRYPT_ALG_HANDLE hashAlgorithm = nullptr;
        const bool sha512 = alg == "RS512";
        if (BCryptOpenAlgorithmProvider(&hashAlgorithm, sha512 ? BCRYPT_SHA512_ALGORITHM : BCRYPT_SHA256_ALGORITHM,
            nullptr, 0) < 0) throw std::runtime_error("fixture hash");
        Bytes hash(sha512 ? 64 : 32);
        const auto result = BCryptHash(hashAlgorithm, nullptr, 0, reinterpret_cast<PUCHAR>(input.data()),
            static_cast<ULONG>(input.size()), hash.data(), static_cast<ULONG>(hash.size()));
        BCryptCloseAlgorithmProvider(hashAlgorithm, 0);
        if (result < 0) throw std::runtime_error("fixture hash bytes");
        BCRYPT_PKCS1_PADDING_INFO padding{sha512 ? BCRYPT_SHA512_ALGORITHM : BCRYPT_SHA256_ALGORITHM};
        ULONG size = 0;
        BCryptSignHash(key_, &padding, hash.data(), static_cast<ULONG>(hash.size()), nullptr, 0, &size, BCRYPT_PAD_PKCS1);
        Bytes signature(size);
        if (BCryptSignHash(key_, &padding, hash.data(), static_cast<ULONG>(hash.size()),
            signature.data(), size, &size, BCRYPT_PAD_PKCS1) < 0) throw std::runtime_error("fixture sign");
        return input + "." + Base64(signature);
    }
    static Json Claims(std::int64_t now = 1000) {
        return {{"iss", Issuer}, {"aud", ClientId}, {"sub", "fixture-user"},
            {"iat", now}, {"exp", now + 3600}, {"nonce", "fixture-nonce"}};
    }
    Json jwks;
private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
};
}
