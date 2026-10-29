#pragma once

#include "platform/windows/wallpaper/download.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <array>
#include <atomic>
#include <cstring>
#include <vector>

namespace lume::shell::background_legacy {

// Read-only v1 compatibility: encryptedWallpaperFile.ts / protectedWallpaperCache.ts
// in historical lume1.0.1. The only output is the existing music-owned v2 writer.
struct Secret {
    std::vector<unsigned char> bytes;
    explicit Secret(std::size_t size) : bytes(size) {}
    ~Secret() { SecureZeroMemory(bytes.data(), bytes.size()); }
};

inline bool Read(HANDLE file, std::uint64_t offset, void* data, DWORD size) {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD read = 0;
    return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) &&
        ReadFile(file, data, size, &read, nullptr) && read == size;
}

inline std::uint64_t BigEndian(const unsigned char* bytes, unsigned size) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i) value = (value << 8) | bytes[i];
    return value;
}

inline bool Hmac(const unsigned char* key, ULONG keySize, const unsigned char* data,
    ULONG size, unsigned char* result) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
        BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return false;
    const auto status = BCryptHash(algorithm, const_cast<PUCHAR>(key), keySize,
        const_cast<PUCHAR>(data), size, result, 32);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return status >= 0;
}

struct Cipher {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    ~Cipher() {
        if (key) BCryptDestroyKey(key);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    bool Initialize(const unsigned char* master, const unsigned char* salt, const char* purpose) {
        Secret extracted(32), derived(32);
        std::string info = std::string("Lume encrypted wallpaper v1 ") + purpose;
        info.push_back('\1');
        return Hmac(salt, 32, master, 32, extracted.bytes.data()) &&
            Hmac(extracted.bytes.data(), 32, reinterpret_cast<const unsigned char*>(info.data()),
                static_cast<ULONG>(info.size()), derived.bytes.data()) &&
            BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                sizeof(BCRYPT_CHAIN_MODE_GCM), 0) >= 0 &&
            BCryptGenerateSymmetricKey(algorithm, &key, nullptr, 0, derived.bytes.data(), 32, 0) >= 0;
    }
    bool Decrypt(unsigned char* input, ULONG size, unsigned char* output,
        unsigned char* nonce, unsigned char* aad, ULONG aadSize, unsigned char* tag) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO mode;
        BCRYPT_INIT_AUTH_MODE_INFO(mode);
        mode.pbNonce = nonce; mode.cbNonce = 12;
        mode.pbAuthData = aad; mode.cbAuthData = aadSize;
        mode.pbTag = tag; mode.cbTag = 16;
        ULONG read = 0;
        unsigned char empty = 0;
        return BCryptDecrypt(key, size ? input : &empty, size, &mode, nullptr, 0,
            size ? output : &empty, size, &read, 0) >= 0 && read == size;
    }
};

inline bool Reencrypt(HANDLE input, HANDLE keyFile, std::uint64_t physical,
    std::uint64_t expectedBytes, platform::windows::wallpaper::IProtectedDownloadWriter& writer,
    const std::atomic<bool>& cancel, std::string& error) {
    constexpr std::uint64_t maxBytes = 512ull * 1024 * 1024;
    std::array<unsigned char, 112> header{};
    LARGE_INTEGER keySize{};
    if (cancel || physical < header.size() || !Read(input, 0, header.data(), static_cast<DWORD>(header.size())) ||
        std::memcmp(header.data(), "LUMEWP01", 8) || BigEndian(header.data() + 8, 4) != 1 ||
        BigEndian(header.data() + 12, 4) != 1 ||
        !GetFileSizeEx(keyFile, &keySize) || keySize.QuadPart <= 0 || keySize.QuadPart > 65536)
        return false;
    const auto size = BigEndian(header.data() + 48, 8);
    const auto records = BigEndian(header.data() + 56, 8);
    if (!size || size > maxBytes || size != expectedBytes || !records || records > 131072 ||
        BigEndian(header.data() + 64, 8) != physical ||
        physical != header.size() + size + records * 40) return false;
    for (unsigned i = 72; i < 84; ++i) if (header[i]) return false;
    Secret encoded(static_cast<std::size_t>(keySize.QuadPart)), master(32);
    if (!Read(keyFile, 0, encoded.bytes.data(), static_cast<DWORD>(encoded.bytes.size()))) return false;
    DATA_BLOB blob{static_cast<DWORD>(encoded.bytes.size()), encoded.bytes.data()}, plain{};
    if (!CryptUnprotectData(&blob, nullptr, nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &plain)) return false;
    const bool validKey = plain.cbData == master.bytes.size();
    if (validKey) std::memcpy(master.bytes.data(), plain.pbData, master.bytes.size());
    SecureZeroMemory(plain.pbData, plain.cbData); LocalFree(plain.pbData);
    Cipher headerCipher, recordCipher;
    if (!validKey || !headerCipher.Initialize(master.bytes.data(), header.data() + 16, "header") ||
        !recordCipher.Initialize(master.bytes.data(), header.data() + 16, "record") ||
        !headerCipher.Decrypt(nullptr, 0, nullptr, header.data() + 84, header.data(), 96, header.data() + 96))
        return false;
    SecureZeroMemory(master.bytes.data(), master.bytes.size());
    constexpr char domain[] = "Lume wallpaper record v1";
    std::array<unsigned char, sizeof(domain) + 24> aad{};
    std::memcpy(aad.data(), domain, sizeof(domain));
    Secret buffer(256 * 1024), ciphertext(256 * 1024 + 16);
    std::uint64_t position = header.size(), total = 0;
    for (std::uint64_t index = 0; index < records; ++index) {
        if (cancel || !Read(input, position, aad.data() + sizeof(domain), 24)) return false;
        auto* record = aad.data() + sizeof(domain);
        const auto length = BigEndian(record + 8, 4);
        if (BigEndian(record, 8) != index || !length || length > buffer.bytes.size() ||
            length > size - total || position + 40 + length > physical ||
            !Read(input, position + 24, ciphertext.bytes.data(), static_cast<DWORD>(length + 16)) ||
            !recordCipher.Decrypt(ciphertext.bytes.data(), static_cast<ULONG>(length), buffer.bytes.data(),
                record + 12, aad.data(), static_cast<ULONG>(aad.size()), ciphertext.bytes.data() + length))
            return false;
        if (cancel || !writer.Append(buffer.bytes.data(), static_cast<std::size_t>(length), error)) return false;
        SecureZeroMemory(buffer.bytes.data(), buffer.bytes.size());
        total += length; position += 40 + length;
    }
    return !cancel && total == size && position == physical && writer.Finish(error);
}

} // namespace lume::shell::background_legacy
