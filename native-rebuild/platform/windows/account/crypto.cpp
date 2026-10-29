#include "core/account/security.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <limits>
namespace lume::account {
namespace {
struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    explicit Algorithm(LPCWSTR name) {
        if (BCryptOpenAlgorithmProvider(&handle, name, nullptr, 0) < 0) throw Failure(Error::InvalidToken);
    }
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};
}
Bytes RandomBytes(std::size_t size) {
    if (size > 4096) throw Failure(Error::InvalidToken);
    Bytes bytes(size);
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw Failure(Error::InvalidToken);
    return bytes;
}
Bytes Digest(std::string_view text, bool sha512) {
    if (text.size() > std::numeric_limits<ULONG>::max()) throw Failure(Error::InvalidToken);
    Algorithm algorithm(sha512 ? BCRYPT_SHA512_ALGORITHM : BCRYPT_SHA256_ALGORITHM);
    Bytes bytes(sha512 ? 64 : 32);
    if (BCryptHash(algorithm.handle, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
        static_cast<ULONG>(text.size()), bytes.data(), static_cast<ULONG>(bytes.size())) < 0) throw Failure(Error::InvalidToken);
    return bytes;
}
bool VerifyRsa(const Bytes& modulus, const Bytes& exponent, std::string_view message, const Bytes& signature, bool sha512) {
    if (modulus.size() < 256 || modulus.size() > 1024 || exponent.empty() || exponent.size() > 8 ||
        signature.size() != modulus.size() || modulus.front() < 0x80) return false;
    Algorithm algorithm(BCRYPT_RSA_ALGORITHM);
    BCRYPT_RSAKEY_BLOB header{};
    header.Magic = BCRYPT_RSAPUBLIC_MAGIC; header.BitLength = static_cast<ULONG>(modulus.size() * 8);
    header.cbPublicExp = static_cast<ULONG>(exponent.size()); header.cbModulus = static_cast<ULONG>(modulus.size());
    Bytes blob(sizeof(header) + exponent.size() + modulus.size());
    std::memcpy(blob.data(), &header, sizeof(header));
    std::memcpy(blob.data() + sizeof(header), exponent.data(), exponent.size());
    std::memcpy(blob.data() + sizeof(header) + exponent.size(), modulus.data(), modulus.size());
    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptImportKeyPair(algorithm.handle, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key, blob.data(),
        static_cast<ULONG>(blob.size()), 0) < 0) return false;
    auto hash = Digest(message, sha512);
    BCRYPT_PKCS1_PADDING_INFO padding{sha512 ? BCRYPT_SHA512_ALGORITHM : BCRYPT_SHA256_ALGORITHM};
    auto status = BCryptVerifySignature(key, &padding, hash.data(), static_cast<ULONG>(hash.size()),
        const_cast<PUCHAR>(signature.data()), static_cast<ULONG>(signature.size()), BCRYPT_PAD_PKCS1);
    BCryptDestroyKey(key);
    return status >= 0;
}
}
