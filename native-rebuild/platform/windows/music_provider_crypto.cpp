#include "platform/windows/music_provider_http.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace lume::music::auth {
namespace {
constexpr std::size_t MaxInput = 1024 * 1024;
void Require(bool ok) { if (!ok) throw std::runtime_error("music crypto error"); }
PUCHAR Bytes(std::string_view text) { return reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())); }
struct Algorithm {
    BCRYPT_ALG_HANDLE value = nullptr;
    explicit Algorithm(LPCWSTR name) { Require(BCryptOpenAlgorithmProvider(&value, name, nullptr, 0) >= 0); }
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};
struct Key {
    BCRYPT_KEY_HANDLE value = nullptr;
    ~Key() { if (value) BCryptDestroyKey(value); }
};
struct Local {
    void* value = nullptr;
    ~Local() { if (value) LocalFree(value); }
};
std::string Hex(std::string_view input) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string output;
    output.reserve(input.size() * 2);
    for (unsigned char c : input) { output += alphabet[c >> 4]; output += alphabet[c & 15]; }
    return output;
}
}  // namespace
std::string UrlEncode(std::string_view input) {
    Require(input.size() <= MaxInput);
    constexpr char alphabet[] = "0123456789ABCDEF";
    std::string output;
    for (unsigned char c : input) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') output += static_cast<char>(c);
        else { output += '%'; output += alphabet[c >> 4]; output += alphabet[c & 15]; }
    }
    return output;
}
std::string Base64(std::string_view input) {
    Require(input.size() <= MaxInput);
    if (input.empty()) return {};
    DWORD size = 0;
    constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    Require(CryptBinaryToStringA(Bytes(input), static_cast<DWORD>(input.size()), flags, nullptr, &size));
    std::string output(size, '\0');
    Require(CryptBinaryToStringA(Bytes(input), static_cast<DWORD>(input.size()), flags, output.data(), &size));
    output.resize(size);
    if (!output.empty() && output.back() == '\0') output.pop_back();
    return output;
}
std::string RandomHex(std::size_t bytes) {
    Require(bytes <= 4096);
    std::string output(bytes, '\0');
    if (bytes) Require(BCryptGenRandom(nullptr, Bytes(output), static_cast<ULONG>(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0);
    auto hex = Hex(output);
    SecureZeroMemory(output.data(), output.size());
    return hex;
}
std::string HashHex(std::string_view input, std::string_view algorithm) {
    Require(input.size() <= MaxInput);
    const bool md5 = algorithm == "MD5" || algorithm == "md5";
    Require(md5 || algorithm == "SHA256" || algorithm == "sha256" || algorithm == "SHA-256");
    Algorithm hash(md5 ? BCRYPT_MD5_ALGORITHM : BCRYPT_SHA256_ALGORITHM);
    std::string output(md5 ? 16 : 32, '\0');
    Require(BCryptHash(hash.value, nullptr, 0, Bytes(input), static_cast<ULONG>(input.size()),
        Bytes(output), static_cast<ULONG>(output.size())) >= 0);
    return Hex(output);
}
std::string Aes(std::string_view input, std::string_view key, std::string_view iv, bool encrypt) {
    Require(input.size() <= MaxInput && (key.size() == 16 || key.size() == 24 || key.size() == 32) &&
        (iv.empty() || iv.size() == 16) && (encrypt || (!input.empty() && input.size() % 16 == 0)));
    Algorithm algorithm(BCRYPT_AES_ALGORITHM);
    const auto mode = iv.empty() ? BCRYPT_CHAIN_MODE_ECB : BCRYPT_CHAIN_MODE_CBC;
    Require(BCryptSetProperty(algorithm.value, BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(mode)),
        static_cast<ULONG>((wcslen(mode) + 1) * sizeof(wchar_t)), 0) >= 0);
    Key handle;
    Require(BCryptGenerateSymmetricKey(algorithm.value, &handle.value, nullptr, 0,
        Bytes(key), static_cast<ULONG>(key.size()), 0) >= 0);
    std::string workingIv(iv), output(input.size() + 16, '\0');
    ULONG written = 0;
    const auto crypt = encrypt ? BCryptEncrypt : BCryptDecrypt;
    const auto status = crypt(handle.value, Bytes(input), static_cast<ULONG>(input.size()), nullptr,
        workingIv.empty() ? nullptr : Bytes(workingIv), static_cast<ULONG>(workingIv.size()),
        Bytes(output), static_cast<ULONG>(output.size()), &written, BCRYPT_BLOCK_PADDING);
    if (status < 0) { SecureZeroMemory(output.data(), output.size()); Require(false); }
    output.resize(written);
    return output;
}
std::string RsaEncrypt(std::string_view input, std::string_view publicPem, bool raw) {
    Require(input.size() <= 1024 && !publicPem.empty() && publicPem.size() <= 16384 &&
        publicPem.find('\0') == publicPem.npos);
    DWORD size = 0;
    Require(CryptStringToBinaryA(publicPem.data(), static_cast<DWORD>(publicPem.size()),
        CRYPT_STRING_BASE64HEADER, nullptr, &size, nullptr, nullptr));
    Require(size <= 8192);
    std::vector<BYTE> der(size);
    Require(CryptStringToBinaryA(publicPem.data(), static_cast<DWORD>(publicPem.size()),
        CRYPT_STRING_BASE64HEADER, der.data(), &size, nullptr, nullptr));
    Key key;
    Local decoded;
    DWORD decodedSize = 0;
    if (publicPem.find("-----BEGIN PUBLIC KEY-----") != publicPem.npos) {
        Require(CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, der.data(), size,
            CRYPT_DECODE_ALLOC_FLAG, nullptr, &decoded.value, &decodedSize));
        auto* info = static_cast<CERT_PUBLIC_KEY_INFO*>(decoded.value);
        Require(info->Algorithm.pszObjId && std::strcmp(info->Algorithm.pszObjId, szOID_RSA_RSA) == 0);
        Require(CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, info, 0, nullptr, &key.value));
    } else {
        Require(publicPem.find("-----BEGIN RSA PUBLIC KEY-----") != publicPem.npos);
        Require(CryptDecodeObjectEx(X509_ASN_ENCODING, RSA_CSP_PUBLICKEYBLOB, der.data(), size,
            CRYPT_DECODE_ALLOC_FLAG, nullptr, &decoded.value, &decodedSize));
        Require(decodedSize >= sizeof(BLOBHEADER) + sizeof(RSAPUBKEY));
        const auto* rsa = reinterpret_cast<const RSAPUBKEY*>(static_cast<const BYTE*>(decoded.value) + sizeof(BLOBHEADER));
        Require(rsa->magic == 0x31415352 && rsa->bitlen >= 512 && rsa->bitlen <= 8192 &&
            rsa->bitlen % 8 == 0 && rsa->pubexp >= 3 && (rsa->pubexp & 1));
        const ULONG modulusSize = rsa->bitlen / 8;
        Require(decodedSize == sizeof(BLOBHEADER) + sizeof(RSAPUBKEY) + modulusSize);
        BYTE exponent[4] = {static_cast<BYTE>(rsa->pubexp >> 24), static_cast<BYTE>(rsa->pubexp >> 16),
            static_cast<BYTE>(rsa->pubexp >> 8), static_cast<BYTE>(rsa->pubexp)};
        std::size_t start = 0;
        while (start < 3 && exponent[start] == 0) ++start;
        BCRYPT_RSAKEY_BLOB header{BCRYPT_RSAPUBLIC_MAGIC, rsa->bitlen, static_cast<ULONG>(4 - start), modulusSize, 0, 0};
        std::vector<BYTE> blob(sizeof(header) + header.cbPublicExp + modulusSize);
        std::memcpy(blob.data(), &header, sizeof(header));
        std::memcpy(blob.data() + sizeof(header), exponent + start, header.cbPublicExp);
        const auto* modulus = reinterpret_cast<const BYTE*>(rsa + 1);
        std::reverse_copy(modulus, modulus + modulusSize, blob.begin() + sizeof(header) + header.cbPublicExp);
        Algorithm algorithm(BCRYPT_RSA_ALGORITHM);
        Require(BCryptImportKeyPair(algorithm.value, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key.value,
            blob.data(), static_cast<ULONG>(blob.size()), 0) >= 0);
    }
    ULONG bits = 0, count = 0;
    Require(BCryptGetProperty(key.value, BCRYPT_KEY_LENGTH, reinterpret_cast<PUCHAR>(&bits),
        sizeof(bits), &count, 0) >= 0 && bits >= 512 && bits <= 8192 && bits % 8 == 0);
    const auto modulusSize = bits / 8;
    Require(input.size() <= (raw ? modulusSize : modulusSize - 11));
    // CNG raw RSA requires a full-width, big-endian integer.
    std::string padded;
    if (raw) { padded.assign(modulusSize - input.size(), '\0'); padded.append(input); input = padded; }
    std::string output(modulusSize, '\0');
    const auto status = BCryptEncrypt(key.value, Bytes(input), static_cast<ULONG>(input.size()), nullptr,
        nullptr, 0, Bytes(output), static_cast<ULONG>(output.size()), &count, raw ? BCRYPT_PAD_NONE : BCRYPT_PAD_PKCS1);
    SecureZeroMemory(padded.data(), padded.size());
    Require(status >= 0);
    output.resize(count);
    return output;
}
}  // namespace lume::music::auth
