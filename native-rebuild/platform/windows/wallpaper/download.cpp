#include "platform/windows/wallpaper/download.h"
#include "core/wallpaper/wallpaper_id_policy.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

namespace lume::platform::windows::wallpaper {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr std::size_t kChunk = 64u * 1024u;
constexpr std::size_t kHeader = 80u;
using Key = std::array<unsigned char, 32>;
struct File {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    void Close() { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); value = INVALID_HANDLE_VALUE; } }
};
struct Internet {
    HINTERNET value = nullptr;
    explicit Internet(HINTERNET v = nullptr) : value(v) {}
    ~Internet() { if (value) WinHttpCloseHandle(value); }
};
void Put32(unsigned char* p, std::uint32_t n) { for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(n >> (i * 8)); }
void Put64(unsigned char* p, std::uint64_t n) { for (int i = 0; i < 8; ++i) p[i] = static_cast<unsigned char>(n >> (i * 8)); }
std::uint32_t Get32(const unsigned char* p) { std::uint32_t n = 0; for (int i = 3; i >= 0; --i) n = (n << 8) | p[i]; return n; }
std::uint64_t Get64(const unsigned char* p) { std::uint64_t n = 0; for (int i = 7; i >= 0; --i) n = (n << 8) | p[i]; return n; }
bool Random(unsigned char* p, std::size_t n) {
    return BCryptGenRandom(nullptr, p, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}
std::string Hex(const unsigned char* bytes, std::size_t size) {
    constexpr char table[] = "0123456789abcdef"; std::string result(size * 2, '0');
    for (std::size_t i = 0; i < size; ++i) {
        result[2 * i] = table[bytes[i] >> 4]; result[2 * i + 1] = table[bytes[i] & 15];
    }
    return result;
}
std::string Unique() {
    std::array<unsigned char, 16> bytes{};
    if (!Random(bytes.data(), bytes.size())) throw std::runtime_error("random");
    return Hex(bytes.data(), bytes.size());
}
bool ReadAt(HANDLE file, std::uint64_t offset, void* output, std::size_t length) {
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) return false;
    auto* bytes = static_cast<unsigned char*>(output);
    while (length) {
        DWORD count = 0;
        if (!ReadFile(file, bytes, static_cast<DWORD>(std::min<std::size_t>(length, MAXDWORD)), &count, nullptr) || !count)
            return false;
        bytes += count; length -= count;
    }
    return true;
}
bool WriteAll(HANDLE file, const void* input, std::size_t length) {
    const auto* bytes = static_cast<const unsigned char*>(input);
    while (length) {
        DWORD count = 0;
        if (!WriteFile(file, bytes, static_cast<DWORD>(std::min<std::size_t>(length, MAXDWORD)), &count, nullptr) || !count)
            return false;
        bytes += count; length -= count;
    }
    return true;
}
bool LoadKey(const std::string& root, Key& key, std::string& error, bool create) {
    if (root.empty() || root.find('\0') != std::string::npos) {
        error = "WALLPAPER_PROTECTION_ROOT"; return false;
    }
    const auto directory = fs::u8path(root);
    const auto path = directory / "cache-key.bin";
    if (!fs::exists(path)) {
        if (!create) { error = "WALLPAPER_PROTECTION_KEY_LOST"; return false; }
        fs::create_directories(directory);
        const auto media = directory / "media";
        if (fs::exists(media)) {
            for (const auto& entry : fs::directory_iterator(media)) {
                if (entry.path().filename().u8string().find(".apk") != std::string::npos) {
                    error = "WALLPAPER_PROTECTION_KEY_LOST"; return false;
                }
            }
        }
        if (!Random(key.data(), key.size())) { error = "WALLPAPER_PROTECTION_RANDOM"; return false; }
        DATA_BLOB plain{static_cast<DWORD>(key.size()), key.data()}, encoded{};
        if (!CryptProtectData(&plain, L"Lume native wallpaper cache", nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &encoded)) {
            SecureZeroMemory(key.data(), key.size()); error = "WALLPAPER_PROTECTION_DPAPI"; return false;
        }
        const auto staging = directory / ("cache-key.bin." + Unique() + ".tmp");
        File output; output.value = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool written = output.value != INVALID_HANDLE_VALUE &&
            WriteAll(output.value, encoded.pbData, encoded.cbData) && FlushFileBuffers(output.value);
        output.Close(); LocalFree(encoded.pbData); SecureZeroMemory(key.data(), key.size());
        if (!written || (!MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) &&
            GetLastError() != ERROR_ALREADY_EXISTS && GetLastError() != ERROR_FILE_EXISTS)) {
            std::error_code ignored; fs::remove(staging, ignored);
            error = "WALLPAPER_PROTECTION_KEY_WRITE"; return false;
        }
        std::error_code ignored; fs::remove(staging, ignored);
    }
    const auto size = fs::file_size(path);
    if (!size || size > 65536) { error = "WALLPAPER_PROTECTION_KEY_INVALID"; return false; }
    std::vector<unsigned char> encoded(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()))) {
        error = "WALLPAPER_PROTECTION_KEY_READ"; return false;
    }
    DATA_BLOB blob{static_cast<DWORD>(encoded.size()), encoded.data()}, plain{};
    if (!CryptUnprotectData(&blob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain)) {
        error = "WALLPAPER_PROTECTION_DPAPI"; return false;
    }
    const bool ok = plain.cbData == key.size();
    if (ok) std::memcpy(key.data(), plain.pbData, key.size());
    SecureZeroMemory(plain.pbData, plain.cbData); LocalFree(plain.pbData);
    if (!ok) error = "WALLPAPER_PROTECTION_KEY_INVALID";
    return ok;
}
class Digest {
public:
    Digest() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return;
        DWORD length = 0, bytes = 0;
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length),
            sizeof(length), &bytes, 0) < 0) return;
        storage_.resize(length);
        ok_ = BCryptCreateHash(algorithm_, &hash_, storage_.data(), length, nullptr, 0, 0) >= 0;
    }
    ~Digest() { if (hash_) BCryptDestroyHash(hash_); if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0); }
    bool Add(const unsigned char* bytes, std::size_t length) {
        return ok_ && BCryptHashData(hash_, const_cast<PUCHAR>(bytes), static_cast<ULONG>(length), 0) >= 0;
    }
    std::string Finish() {
        Key bytes{};
        return ok_ && BCryptFinishHash(hash_, bytes.data(), static_cast<ULONG>(bytes.size()), 0) >= 0 ?
            Hex(bytes.data(), bytes.size()) : std::string{};
    }
private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> storage_;
    bool ok_ = false;
};
class Cipher {
public:
    ~Cipher() {
        if (key_) BCryptDestroyKey(key_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
        if (!storage_.empty()) SecureZeroMemory(storage_.data(), storage_.size());
    }
    bool Initialize(const Key& master, const unsigned char* salt) {
        // A distinct HMAC-derived AES key for every file avoids cross-file nonce reuse.
        BCRYPT_ALG_HANDLE hmac = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
        if (BCryptOpenAlgorithmProvider(&hmac, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0)
            return false;
        DWORD bytes = 0, length = 0;
        bool ok = BCryptGetProperty(hmac, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length),
            sizeof(length), &bytes, 0) >= 0;
        std::vector<unsigned char> object(length); Key derived{};
        if (ok) ok = BCryptCreateHash(hmac, &hash, object.data(), length, const_cast<PUCHAR>(master.data()),
            static_cast<ULONG>(master.size()), 0) >= 0;
        const unsigned char domain[] = "Lume native wallpaper v2";
        if (ok) ok = BCryptHashData(hash, const_cast<PUCHAR>(domain), sizeof(domain), 0) >= 0 &&
            BCryptHashData(hash, const_cast<PUCHAR>(salt), 16, 0) >= 0 &&
            BCryptFinishHash(hash, derived.data(), static_cast<ULONG>(derived.size()), 0) >= 0;
        if (hash) BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(hmac, 0);
        if (ok) ok = BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptSetProperty(algorithm_, BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                sizeof(BCRYPT_CHAIN_MODE_GCM), 0) >= 0 &&
            BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length),
                sizeof(length), &bytes, 0) >= 0;
        if (ok) {
            storage_.resize(length);
            ok = BCryptGenerateSymmetricKey(algorithm_, &key_, storage_.data(), length, derived.data(),
                static_cast<ULONG>(derived.size()), 0) >= 0;
        }
        SecureZeroMemory(derived.data(), derived.size());
        return ok;
    }
    bool Transform(bool encrypt, const unsigned char* input, std::size_t size, unsigned char* output,
        const unsigned char* prefix, std::uint32_t index, const unsigned char* aad,
        std::size_t aadSize, unsigned char* tag) {
        std::array<unsigned char, 12> nonce{};
        std::memcpy(nonce.data(), prefix, 8); Put32(nonce.data() + 8, index);
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO mode;
        BCRYPT_INIT_AUTH_MODE_INFO(mode);
        mode.pbNonce = nonce.data(); mode.cbNonce = static_cast<ULONG>(nonce.size());
        mode.pbAuthData = const_cast<PUCHAR>(aad); mode.cbAuthData = static_cast<ULONG>(aadSize);
        mode.pbTag = tag; mode.cbTag = 16;
        ULONG processed = 0;
        unsigned char empty = 0;
        const auto in = size ? const_cast<PUCHAR>(input) : &empty;
        const auto out = size ? output : &empty;
        const auto status = encrypt ?
            BCryptEncrypt(key_, in, static_cast<ULONG>(size), &mode, nullptr, 0, out,
                static_cast<ULONG>(size), &processed, 0) :
            BCryptDecrypt(key_, in, static_cast<ULONG>(size), &mode, nullptr, 0, out,
                static_cast<ULONG>(size), &processed, 0);
        return status >= 0 && processed == size;
    }
private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
    std::vector<unsigned char> storage_;
};
std::array<unsigned char, 24> RecordAad(const unsigned char* header, std::uint32_t index, std::uint32_t size) {
    std::array<unsigned char, 24> aad{};
    std::memcpy(aad.data(), header + 32, 16);
    Put32(aad.data() + 16, index); Put32(aad.data() + 20, size);
    return aad;
}
class EncryptedWriter final : public IProtectedDownloadWriter {
public:
    ~EncryptedWriter() override { SecureZeroMemory(pending_.data(), pending_.size()); }
    bool Initialize(const std::string& root, const std::string& staging, std::string& error) {
        Key key{};
        if (!LoadKey(root, key, error, true)) return false;
        std::memcpy(header_.data(), "LUMEWP02", 8);
        Put32(header_.data() + 8, kChunk);
        const bool generated = Random(header_.data() + 24, 24) && cipher_.Initialize(key, header_.data() + 32);
        SecureZeroMemory(key.data(), key.size());
        if (!generated) { error = "WALLPAPER_PROTECTION_CIPHER"; return false; }
        file_.value = CreateFileW(fs::u8path(staging).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_.value == INVALID_HANDLE_VALUE || !WriteAll(file_.value, header_.data(), header_.size())) {
            error = "WALLPAPER_PROTECTION_CREATE"; return false;
        }
        return true;
    }
    bool Append(const std::uint8_t* bytes, std::size_t size, std::string& error) noexcept override {
        try {
            if (finished_ || failed_ || (!bytes && size) || size > lume::wallpaper::kMaxWallpaperBytes - total_)
                return Fail(error, "WALLPAPER_PROTECTION_APPEND");
            total_ += size;
            while (size) {
                const auto count = std::min(size, kChunk - pendingSize_);
                std::memcpy(pending_.data() + pendingSize_, bytes, count);
                bytes += count; size -= count; pendingSize_ += count;
                if (pendingSize_ == kChunk && !FlushRecord(error)) return false;
            }
            return true;
        } catch (...) { return Fail(error, "WALLPAPER_PROTECTION_APPEND"); }
    }
    bool Finish(std::string& error) noexcept override {
        try {
            if (finished_ || failed_ || !total_) return Fail(error, "WALLPAPER_PROTECTION_FINISH");
            if (pendingSize_ && !FlushRecord(error)) return false;
            Put64(header_.data() + 16, total_); Put32(header_.data() + 48, 1);
            if (!cipher_.Transform(true, nullptr, 0, nullptr, header_.data() + 24, 0xffffffffu,
                header_.data(), 64, header_.data() + 64)) return Fail(error, "WALLPAPER_PROTECTION_HEADER");
            LARGE_INTEGER zero{};
            if (!SetFilePointerEx(file_.value, zero, nullptr, FILE_BEGIN) ||
                !WriteAll(file_.value, header_.data(), header_.size()) || !FlushFileBuffers(file_.value))
                return Fail(error, "WALLPAPER_PROTECTION_FLUSH");
            file_.Close(); finished_ = true; return true;
        } catch (...) { return Fail(error, "WALLPAPER_PROTECTION_FINISH"); }
    }
private:
    bool Fail(std::string& error, const char* text) noexcept { failed_ = true; error = text; return false; }
    bool FlushRecord(std::string& error) {
        std::array<unsigned char, kChunk + 16> encoded{};
        const auto aad = RecordAad(header_.data(), index_, static_cast<std::uint32_t>(pendingSize_));
        if (!cipher_.Transform(true, pending_.data(), pendingSize_, encoded.data(), header_.data() + 24,
            index_, aad.data(), aad.size(), encoded.data() + pendingSize_) ||
            !WriteAll(file_.value, encoded.data(), pendingSize_ + 16))
            return Fail(error, "WALLPAPER_PROTECTION_RECORD");
        SecureZeroMemory(pending_.data(), pendingSize_); pendingSize_ = 0; ++index_; return true;
    }
    File file_; Cipher cipher_;
    std::array<unsigned char, kHeader> header_{};
    std::array<unsigned char, kChunk> pending_{};
    std::size_t pendingSize_ = 0;
    std::uint64_t total_ = 0;
    std::uint32_t index_ = 0;
    bool finished_ = false, failed_ = false;
};

std::wstring Wide(const std::string& value) {
    if (value.size() > 8192) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length);
    return result;
}
std::string Narrow(const std::wstring& value) {
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr); return result;
}
struct Url { std::wstring host, path; INTERNET_PORT port = 443; bool secure = true, local = false; };
std::optional<Url> ParseUrl(const std::string& value, bool loopback) {
    auto normalized = lume::wallpaper::NormalizeOfficialUrl(value);
    const auto input = Wide(normalized.empty() ? value : normalized);
    if (input.empty() || value.find_first_of("\\\r\n\t#") != std::string::npos) return {};
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(input.c_str(), static_cast<DWORD>(input.size()), 0, &parts) ||
        parts.dwUserNameLength || parts.dwPasswordLength) return {};
    Url url; url.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    url.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) url.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    url.port = parts.nPort; url.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    url.local = loopback && parts.nScheme == INTERNET_SCHEME_HTTP && url.host == L"127.0.0.1";
    if (!url.local && normalized.empty()) return {};
    return url;
}
class Operation {
public:
    explicit Operation(HINTERNET value) : handle_(value) {}
    ~Operation() {
        if (!handle_) return;
        WinHttpCloseHandle(handle_);
        if (attached_) { std::unique_lock<std::mutex> lock(mutex_); ready_.wait(lock, [&] { return closed_; }); }
    }
    bool Attach() {
        auto context = reinterpret_cast<DWORD_PTR>(this);
        if (!WinHttpSetOption(handle_, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return false;
        attached_ = WinHttpSetStatusCallback(handle_, Callback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) !=
            WINHTTP_INVALID_STATUS_CALLBACK; return attached_;
    }
    void Begin() { std::lock_guard<std::mutex> lock(mutex_); event_ = 0; count_ = 0; failed_ = false; }
    bool Await(BOOL began, DWORD event, const std::atomic<bool>& cancel) {
        if (!began && GetLastError() != ERROR_IO_PENDING) return false;
        const auto deadline = Clock::now() + std::chrono::seconds(20);
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            if (cancel.load() || failed_ || closed_ || Clock::now() >= deadline) return false;
            if (event_ == event) return true;
            ready_.wait_for(lock, std::chrono::milliseconds(20));
        }
    }
    HINTERNET Get() const { return handle_; }
    DWORD Bytes() { std::lock_guard<std::mutex> lock(mutex_); return count_; }
    std::array<unsigned char, kChunk> buffer{};
private:
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD event, void*, DWORD length) {
        if (!context) return;
        if (event != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && event != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
            event != WINHTTP_CALLBACK_STATUS_READ_COMPLETE && event != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR &&
            event != WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) return;
        auto& self = *reinterpret_cast<Operation*>(context);
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (event == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) self.closed_ = true;
        else if (event == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) self.failed_ = true;
        else { self.event_ = event; self.count_ = length; }
        self.ready_.notify_all();
    }
    HINTERNET handle_ = nullptr;
    std::mutex mutex_; std::condition_variable ready_;
    DWORD event_ = 0, count_ = 0;
    bool attached_ = false, closed_ = false, failed_ = false;
};
std::string Header(HINTERNET request, DWORD query) {
    DWORD length = 0;
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &length, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length < sizeof(wchar_t) || length > 8192) return {};
    std::wstring value(length / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &length, WINHTTP_NO_HEADER_INDEX))
        return {};
    value.resize(length / sizeof(wchar_t)); while (!value.empty() && !value.back()) value.pop_back();
    return Narrow(value);
}
bool Number(const std::string& value, std::uint64_t& out) {
    out = 0; if (value.empty() || value.size() > 12) return false;
    for (const char ch : value) { if (ch < '0' || ch > '9') return false; out = out * 10 + ch - '0'; }
    return true;
}
struct TransferResult { bool ok = false, retry = false; unsigned status = 0; std::uint64_t received = 0; std::string error; };
using Sink = std::function<bool(const unsigned char*, std::size_t, std::string&)>;
// A byte range [first, last] of a file whose full size is `total`.
struct ByteRange { std::uint64_t first = 0, last = 0, total = 0; };
TransferResult Transfer(std::string urlText, const std::atomic<bool>& cancel, bool loopback,
    bool manifest, std::uint64_t limit, std::uint64_t expected, const Sink& sink,
    std::optional<ByteRange> range = std::nullopt) {
    auto url = ParseUrl(urlText, loopback);
    if (!url) return {false, false, 0, 0, "WALLPAPER_DOWNLOAD_URL_INVALID"};
    Internet session(WinHttpOpen(L"Lume/1.0.8 (Windows wallpaper)",
        url->local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!session.value) return {false, true, 0, 0, "WALLPAPER_HTTP_SESSION"};
    WinHttpSetTimeouts(session.value, 15000, 15000, 15000, 15000);
    for (int redirects = 0; redirects <= 3; ++redirects) {
        Internet connection(WinHttpConnect(session.value, url->host.c_str(), url->port, 0));
        if (!connection.value) return {false, true, 0, 0, "WALLPAPER_HTTP_CONNECT"};
        Operation operation(WinHttpOpenRequest(connection.value, L"GET", url->path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url->secure ? WINHTTP_FLAG_SECURE : 0));
        if (!operation.Get() || !operation.Attach()) return {false, true, 0, 0, "WALLPAPER_HTTP_REQUEST"};
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        DWORD disabled = WINHTTP_DISABLE_COOKIES; DWORD auth = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        if (!WinHttpSetOption(operation.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)) ||
            !WinHttpSetOption(operation.Get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
            !WinHttpSetOption(operation.Get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &auth, sizeof(auth)))
            return {false, false, 0, 0, "WALLPAPER_HTTP_POLICY"};
        // Production CDNs must serve manifests as identity bytes. A gzip
        // variant of the large static manifest has occasionally been served
        // from a partial edge object; WinHTTP then reports a clean EOF after
        // decoding the truncated body, leaving JSON parsing to discover the
        // failure too late. Keep decompression only for the loopback fixture
        // that exercises the decoder in unit tests.
        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        const bool decoded = manifest && loopback && WinHttpSetOption(operation.Get(), WINHTTP_OPTION_DECOMPRESSION,
            &decompression, sizeof(decompression));
        std::wstring requestHeaders = decoded ? L"" : L"Accept-Encoding: identity\r\n";
        if (range)
            requestHeaders += L"Range: bytes=" + std::to_wstring(range->first) + L"-" + std::to_wstring(range->last) + L"\r\n";
        const wchar_t* headers = requestHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : requestHeaders.c_str();
        operation.Begin();
        if (!operation.Await(WinHttpSendRequest(operation.Get(), headers, requestHeaders.empty() ? 0 : static_cast<DWORD>(-1L),
            WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(&operation)),
            WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, cancel))
            return {false, true, 0, 0, "WALLPAPER_HTTP_SEND"};
        operation.Begin();
        if (!operation.Await(WinHttpReceiveResponse(operation.Get(), nullptr),
            WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, cancel))
            return {false, true, 0, 0, "WALLPAPER_HTTP_RESPONSE"};
        DWORD status = 0, bytes = sizeof(status);
        if (!WinHttpQueryHeaders(operation.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX))
            return {false, false, 0, 0, "WALLPAPER_HTTP_STATUS"};
        if (status == 301 || status == 302 || status == 307 || status == 308) {
            auto location = Header(operation.Get(), WINHTTP_QUERY_LOCATION);
            if (!location.empty() && location.front() == '/' && location.rfind("//", 0) != 0)
                location = (url->secure ? "https://" : "http://") + Narrow(url->host) +
                    (url->local ? ":" + std::to_string(url->port) : "") + location;
            auto next = ParseUrl(location, loopback);
            if (!next || redirects == 3 || (!url->local && next->local))
                return {false, false, status, 0, "WALLPAPER_HTTP_REDIRECT"};
            url = std::move(next); continue;
        }
        // A range request must be answered with exactly that range; a server
        // that ignores Range (200) makes the caller fall back to one stream.
        if (range && status == 200) return {false, false, status, 0, "WALLPAPER_RANGE_UNSUPPORTED"};
        if (status != (range ? 206u : 200u)) return {false, status == 408 || status == 425 || status == 429 || status >= 500,
            status, 0, "WALLPAPER_HTTP_" + std::to_string(status)};
        if (range) {
            const auto content = Header(operation.Get(), WINHTTP_QUERY_CONTENT_RANGE);
            const auto expectedRange = "bytes " + std::to_string(range->first) + "-" + std::to_string(range->last) +
                "/" + std::to_string(range->total);
            if (content != expectedRange) return {false, false, status, 0, "WALLPAPER_RANGE_MISMATCH"};
        }
        auto type = Header(operation.Get(), WINHTTP_QUERY_CONTENT_TYPE);
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        type = type.substr(0, type.find(';'));
        if ((manifest && type != "application/json") || (!manifest &&
            type.rfind("image/", 0) != 0 && type.rfind("video/", 0) != 0 && type != "application/octet-stream"))
            return {false, false, status, 0, "WALLPAPER_CONTENT_TYPE_INVALID"};
        auto encoding = Header(operation.Get(), WINHTTP_QUERY_CONTENT_ENCODING);
        std::transform(encoding.begin(), encoding.end(), encoding.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const bool compressed = decoded && (encoding == "gzip" || encoding == "deflate");
        if (!encoding.empty() && encoding != "identity" && !compressed)
            return {false, false, status, 0, "WALLPAPER_CONTENT_ENCODING"};
        std::uint64_t declared = 0;
        // A compressed Content-Length counts encoded bytes, not the decoded body.
        const auto lengthHeader = compressed ? std::string{} : Header(operation.Get(), WINHTTP_QUERY_CONTENT_LENGTH);
        if ((!lengthHeader.empty() && !Number(lengthHeader, declared)) || declared > limit ||
            (expected && declared && declared != expected))
            return {false, false, status, 0, "WALLPAPER_SIZE_MISMATCH"};
        TransferResult result; result.status = status;
        for (;;) {
            operation.Begin();
            if (!operation.Await(WinHttpReadData(operation.Get(), operation.buffer.data(),
                static_cast<DWORD>(operation.buffer.size()), nullptr), WINHTTP_CALLBACK_STATUS_READ_COMPLETE, cancel))
                return {false, true, status, result.received, "WALLPAPER_HTTP_READ"};
            const auto count = operation.Bytes();
            if (!count) break;
            if (count > operation.buffer.size() || count > limit - result.received)
                return {false, false, status, result.received, "WALLPAPER_DOWNLOAD_TOO_LARGE"};
            result.received += count;
            if (!sink(operation.buffer.data(), count, result.error)) return result;
        }
        if ((declared && result.received != declared) || (expected && result.received != expected))
            return {false, true, status, result.received, "WALLPAPER_SIZE_MISMATCH"};
        result.ok = !cancel.load(); return result;
    }
    return {false, false, 0, 0, "WALLPAPER_HTTP_REDIRECT"};
}
// Fetches bytes [first, last] of a `total`-byte file into `sink`, advancing
// `position`. The CDN keeps media in 4 MiB slices and a damaged slice ends a
// response early (a short body, or 206 with no body at all) while the bytes
// before it are good: keep them and ask again from where it stopped, with a
// Range. Gives up after `stallBudget` failures in a row without progress.
// `plainFirst` makes a request for the whole file use no Range (servers
// without range support still work when nothing goes wrong).
TransferResult Resume(const std::string& url, const std::atomic<bool>& cancel, bool loopback, std::uint64_t total,
    std::uint64_t first, std::uint64_t last, bool plainFirst, unsigned stallBudget, const Sink& sink,
    std::uint64_t& position) {
    unsigned stalls = 0;
    for (;;) {
        const auto before = position;
        const Sink counted = [&](const unsigned char* bytes, std::size_t size, std::string& error) {
            if (!sink(bytes, size, error)) return false;
            position += size;
            return true;
        };
        const bool plain = plainFirst && position == 0 && first == 0 && last + 1 == total;
        const auto remaining = last - position + 1;
        auto result = plain ? Transfer(url, cancel, loopback, false, total, total, counted)
                            : Transfer(url, cancel, loopback, false, remaining, remaining, counted,
                                  ByteRange{position, last, total});
        result.received = position - first;
        if (result.ok || cancel.load() || !result.retry) return result;
        stalls = position == before ? stalls + 1 : 0;
        if (stalls >= stallBudget) return result;
        for (unsigned wait = 0; wait < (stalls + 1) * 250 && !cancel.load(); wait += 20)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
// The wallpaper CDN caps each connection (measured ~200 KB/s per connection
// while the line does 20 MB/s elsewhere), so large media is fetched as
// fixed-size ranges over several connections. Ranges are handed to `sink` in
// file order on the calling thread, so hashing and the encrypted writer stay
// sequential; at most kWindow segments are buffered (~32 MB). Starts at byte
// `start` (what an earlier attempt already handed to `sink`).
constexpr std::uint64_t kSegment = 2u * 1024u * 1024u;
constexpr unsigned kConnections = 8, kWindow = 16;
TransferResult ParallelTransfer(const std::string& url, const std::atomic<bool>& cancel, bool loopback,
    std::uint64_t total, std::uint64_t start, const Sink& sink, const std::function<void(std::uint64_t)>& downloaded) {
    const auto count = static_cast<std::size_t>((total - start + kSegment - 1) / kSegment);
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::size_t, std::vector<unsigned char>> ready;
    std::size_t next = 0, consumed = 0;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> fetched{0};
    TransferResult failure;
    bool failed = false;
    auto worker = [&] {
        for (;;) {
            std::size_t index = 0;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stop.load() || failed || next >= count || next < consumed + kWindow; });
                if (stop.load() || failed || next >= count) return;
                index = next++;
            }
            const auto first = start + index * kSegment, last = std::min(total, first + kSegment) - 1;
            std::vector<unsigned char> data;
            data.reserve(static_cast<std::size_t>(last - first + 1));
            std::uint64_t position = first;
            // A short segment continues from where it stopped (see Resume).
            auto result = Resume(url, stop, loopback, total, first, last, false, 3,
                [&](const unsigned char* bytes, std::size_t length, std::string&) {
                    data.insert(data.end(), bytes, bytes + length);
                    fetched += length;
                    return true;
                }, position);
            if (!result.ok) fetched -= data.size();  // this call ends; its segment is fetched again
            std::lock_guard<std::mutex> lock(mutex);
            if (!result.ok) {
                if (!failed) { failed = true; failure = result; }
                stop = true;
            } else ready.emplace(index, std::move(data));
            changed.notify_all();
        }
    };
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < std::min<std::size_t>(kConnections, count); ++i) workers.emplace_back(worker);
    struct Join {
        std::vector<std::thread>& threads; std::atomic<bool>& stop; std::condition_variable& changed; std::mutex& mutex;
        ~Join() {
            { std::lock_guard<std::mutex> lock(mutex); stop = true; }
            changed.notify_all();
            for (auto& thread : threads) if (thread.joinable()) thread.join();
        }
    } join{workers, stop, changed, mutex};
    TransferResult result;
    result.status = 206;
    while (consumed < count) {
        std::vector<unsigned char> data;
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, std::chrono::milliseconds(100), [&] { return failed || ready.count(consumed) != 0; });
            if (cancel.load()) { stop = true; changed.notify_all(); return {false, false, 206, result.received, "WALLPAPER_CANCELLED"}; }
            if (failed) return failure;
            const auto found = ready.find(consumed);
            if (found == ready.end()) {
                lock.unlock();
                if (downloaded) downloaded(start + fetched.load());
                continue;
            }
            data = std::move(found->second);
            ready.erase(found);
            ++consumed;
        }
        changed.notify_all();
        result.received += data.size();
        if (!sink(data.data(), data.size(), result.error)) return result;
        if (downloaded) downloaded(start + fetched.load());
    }
    result.ok = start + result.received == total && !cancel.load();
    if (!result.ok && result.error.empty()) result.error = "WALLPAPER_SIZE_MISMATCH";
    return result;
}
} // namespace

struct EncryptedMediaReader::Impl {
    File file;
    Cipher cipher;
    std::array<unsigned char, kHeader> header{};
    std::uint64_t size = 0;
    std::mutex mutex;
};
EncryptedMediaReader::EncryptedMediaReader(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EncryptedMediaReader::~EncryptedMediaReader() = default;
std::uint64_t EncryptedMediaReader::Size() const noexcept { return impl_->size; }
std::shared_ptr<EncryptedMediaReader> EncryptedMediaReader::Open(
    const std::string& root, const std::string& path, std::string& error) noexcept {
    try {
        auto state = std::make_unique<Impl>(); Key key{};
        if (!LoadKey(root, key, error, false)) return {};
        state->file.value = CreateFileW(fs::u8path(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER physical{};
        bool ok = state->file.value != INVALID_HANDLE_VALUE &&
            ReadAt(state->file.value, 0, state->header.data(), state->header.size()) &&
            std::memcmp(state->header.data(), "LUMEWP02", 8) == 0 &&
            Get32(state->header.data() + 8) == kChunk && Get32(state->header.data() + 48) == 1 &&
            state->cipher.Initialize(key, state->header.data() + 32) &&
            state->cipher.Transform(false, nullptr, 0, nullptr, state->header.data() + 24, 0xffffffffu,
                state->header.data(), 64, state->header.data() + 64) &&
            GetFileSizeEx(state->file.value, &physical);
        SecureZeroMemory(key.data(), key.size());
        state->size = Get64(state->header.data() + 16);
        const auto records = (state->size + kChunk - 1) / kChunk;
        ok = ok && state->size > 0 && state->size <= lume::wallpaper::kMaxWallpaperBytes &&
            physical.QuadPart == static_cast<LONGLONG>(kHeader + state->size + records * 16);
        if (!ok) { error = "WALLPAPER_PROTECTED_FILE_INVALID"; return {}; }
        return std::shared_ptr<EncryptedMediaReader>(new EncryptedMediaReader(std::move(state)));
    } catch (...) { error = "WALLPAPER_PROTECTED_OPEN"; return {}; }
}
bool EncryptedMediaReader::Read(std::uint64_t offset, char* buffer, std::size_t capacity, std::size_t& read) noexcept {
    read = 0;
    try {
        if ((!buffer && capacity) || offset > impl_->size) return false;
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto desired = static_cast<std::size_t>(std::min<std::uint64_t>(capacity, impl_->size - offset));
        std::array<unsigned char, kChunk + 16> encoded{};
        std::array<unsigned char, kChunk> plain{};
        while (read < desired) {
            const auto index = static_cast<std::uint32_t>(offset / kChunk);
            const auto within = static_cast<std::size_t>(offset % kChunk);
            const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, impl_->size - std::uint64_t(index) * kChunk));
            const auto aad = RecordAad(impl_->header.data(), index, static_cast<std::uint32_t>(size));
            if (!ReadAt(impl_->file.value, kHeader + std::uint64_t(index) * (kChunk + 16), encoded.data(), size + 16) ||
                !impl_->cipher.Transform(false, encoded.data(), size, plain.data(), impl_->header.data() + 24,
                    index, aad.data(), aad.size(), encoded.data() + size)) {
                SecureZeroMemory(plain.data(), plain.size());
                if (read) SecureZeroMemory(buffer, read);
                read = 0; return false;
            }
            const auto count = std::min(desired - read, size - within);
            std::memcpy(buffer + read, plain.data() + within, count);
            SecureZeroMemory(plain.data(), plain.size());
            offset += count; read += count;
        }
        return true;
    } catch (...) { if (buffer && read) SecureZeroMemory(buffer, read); read = 0; return false; }
}
ProtectedWriterFactory MakeProtectedWriterFactory(const std::string& libraryRoot) noexcept {
    try {
        return [libraryRoot](const std::string& staging, std::string& error) -> std::unique_ptr<IProtectedDownloadWriter> {
            try {
                auto writer = std::make_unique<EncryptedWriter>();
                if (!writer->Initialize(libraryRoot, staging, error)) return {};
                return writer;
            } catch (...) { error = "WALLPAPER_PROTECTION_CREATE"; return {}; }
        };
    } catch (...) { return {}; }
}

lume::wallpaper::CatalogResult FetchCatalog(const std::atomic<bool>& cancel, const CatalogFetchOptions& options) noexcept {
    try {
        lume::wallpaper::CatalogResult combined;
        std::set<std::string> seen;
        const std::array<std::string, 2> urls{options.videoUrl, options.imageUrl};
        for (std::size_t index = 0; index < urls.size(); ++index) {
            if (cancel.load()) { combined.error = "WALLPAPER_CANCELLED"; return combined; }
            std::string text;
            TransferResult transfer;
            lume::wallpaper::CatalogResult parsed;
            // A cold first connection (proxy discovery, DNS, TLS) can time out on
            // slower networks; retry transient failures before giving up. A
            // clean EOF on a truncated identity response is discovered by
            // ParseCatalog, so production parse failures use the same budget.
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (attempt) std::this_thread::sleep_for(std::chrono::milliseconds(500 * attempt));
                if (cancel.load()) break;
                text.clear();
                transfer = Transfer(urls[index], cancel, options.allowLoopbackForTests, true, lume::wallpaper::kMaxCatalogBytes, 0,
                    [&text](const unsigned char* bytes, std::size_t size, std::string&) {
                        text.append(reinterpret_cast<const char*>(bytes), size); return true;
                    });
                if (!transfer.ok) {
                    if (!transfer.retry || attempt == 2) break;
                    continue;
                }
                parsed = lume::wallpaper::ParseCatalog(text);
                if (parsed.error.empty() || options.allowLoopbackForTests || attempt == 2) break;
            }
            if (!transfer.ok) {
                const auto collection = index == 0 ? "video:" : "image:";
                if (!combined.error.empty()) combined.error += ";";
                combined.error += std::string(collection) + (cancel.load() ? "WALLPAPER_CANCELLED" : transfer.error);
                if (cancel.load()) return combined;
                continue;
            }
            auto nextIds = seen;
            if (parsed.error.empty()) {
                const auto expectedKind = index == 0 ? lume::wallpaper::MediaKind::Video : lume::wallpaper::MediaKind::Image;
                for (const auto& item : parsed.items) {
                    auto id = item.id;
                    std::transform(id.begin(), id.end(), id.begin(),
                        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                    if (item.kind != expectedKind) { parsed.error = "WALLPAPER_CATALOG_COLLECTION"; break; }
                    if (!nextIds.insert(id).second) { parsed.error = "WALLPAPER_CATALOG_ID"; break; }
                }
                if (combined.items.size() + parsed.items.size() > lume::wallpaper::kMaxLibraryItems)
                    parsed.error = "WALLPAPER_CATALOG_SIZE";
            }
            if (!parsed.error.empty()) {
                const auto collection = index == 0 ? "video:" : "image:";
                if (!combined.error.empty()) combined.error += ";";
                combined.error += std::string(collection) + parsed.error;
                continue;
            }
            seen = std::move(nextIds);
            lume::wallpaper::ApplyOfficialCatalogPolicy(parsed.items);
            for (auto& item : parsed.items) combined.items.push_back(std::move(item));
            if (index == 0) combined.videoCatalogLoaded = true;
            else combined.imageCatalogLoaded = true;
        }
        return combined;
    } catch (...) { return {{}, "WALLPAPER_CATALOG_FETCH"}; }
}

DownloadResult Download(const lume::wallpaper::CatalogItem& item, const std::string& destination,
    const std::atomic<bool>& cancel, const ProgressCallback& progress, const DownloadOptions& options) noexcept {
    fs::path staging;
    auto cleanup = [&] { if (!staging.empty()) { std::error_code ignored; fs::remove(staging, ignored); } };
    try {
        if (cancel.load()) return {false, true, {}, "WALLPAPER_CANCELLED", 0};
        if (!ParseUrl(item.mediaUrl, options.allowLoopbackForTests))
            return {false, false, {}, "WALLPAPER_DOWNLOAD_URL_INVALID", 0};
        if (!lume::wallpaper::IsSafeWallpaperId(item.id) || !item.sizeBytes ||
            item.sizeBytes > lume::wallpaper::kMaxWallpaperBytes || item.sha256.size() != 64 ||
            !std::all_of(item.sha256.begin(), item.sha256.end(), [](unsigned char ch) {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            })) return {false, false, {}, "WALLPAPER_DOWNLOAD_METADATA", 0};
        const auto target = fs::u8path(destination);
        if (!target.is_absolute() || target.extension() != ".apk")
            return {false, false, {}, "WALLPAPER_DOWNLOAD_PATH", 0};
        if (!options.protectedWriter) return {false, false, {}, "WALLPAPER_PROTECTION_UNAVAILABLE", 0};
        fs::create_directories(target.parent_path());
        TransferResult transfer;
        // Small files gain nothing from splitting; tests on loopback keep one stream.
        bool parallel = item.sizeBytes > 2 * kSegment && !options.allowLoopbackForTests;
        // Retries stay on the CDN: a direct COS read is billed as COS egress.
        const auto normalized = lume::wallpaper::NormalizeOfficialUrl(item.mediaUrl);
        const auto& requestUrl = normalized.empty() ? item.mediaUrl : normalized;
        // One staging file, hash and byte count across attempts: a failed attempt
        // keeps what it verified-in-order wrote, and the next continues from there.
        std::unique_ptr<IProtectedDownloadWriter> writer;
        std::unique_ptr<Digest> hash;
        std::uint64_t received = 0;
        std::string error;
        const auto restart = [&]() -> bool {
            writer.reset(); cleanup();
            staging = target; staging += "." + Unique() + ".download";
            writer = options.protectedWriter(staging.u8string(), error);
            hash = std::make_unique<Digest>();
            received = 0;
            return writer != nullptr;
        };
        if (!restart()) { cleanup(); return {false, false, {}, error.empty() ? "WALLPAPER_PROTECTION_CREATE" : error, 0}; }
        // The writer takes at most 64 KiB per call; parallel segments are larger.
        const auto append = [&](const unsigned char* bytes, std::size_t size, std::string& sinkError) {
            for (std::size_t offset = 0; offset < size; offset += kChunk) {
                const auto part = std::min<std::size_t>(kChunk, size - offset);
                if (!writer->Append(bytes + offset, part, sinkError)) return false;
            }
            return true;
        };
        unsigned attempt = 1;
        const Sink feed = [&](const unsigned char* bytes, std::size_t size, std::string& sinkError) {
            if (cancel.load()) return false;
            if (!hash->Add(bytes, size)) { sinkError = "WALLPAPER_SHA256_FAILED"; return false; }
            if (!append(bytes, size, sinkError)) return false;
            received += size;
            if (progress && !parallel) { try { progress({received, item.sizeBytes, attempt}); } catch (...) {} }
            return true;
        };
        // An attempt that moved forward does not count against the budget: only
        // three attempts in a row that gain nothing end the download.
        for (unsigned stalls = 0; attempt <= 64; ++attempt) {
            if (cancel.load()) { cleanup(); return {false, true, {}, "WALLPAPER_CANCELLED", received}; }
            const auto before = received;
            if (parallel) {
                transfer = ParallelTransfer(requestUrl, cancel, options.allowLoopbackForTests, item.sizeBytes, received, feed,
                    [&](std::uint64_t fetched) {
                        if (progress) { try { progress({fetched, item.sizeBytes, attempt}); } catch (...) {} }
                    });
                if (!transfer.ok && transfer.error == "WALLPAPER_RANGE_UNSUPPORTED") {
                    // The server ignores ranges: start over on one connection.
                    parallel = false;
                    if (!restart()) break;
                    --attempt;
                    continue;
                }
            } else {
                std::uint64_t position = received;
                transfer = Resume(requestUrl, cancel, options.allowLoopbackForTests, item.sizeBytes,
                    received, item.sizeBytes - 1, true, 1, feed, position);
                if (!transfer.ok && transfer.error == "WALLPAPER_RANGE_UNSUPPORTED") {
                    // Cut short by a server without ranges: only a full restart can recover.
                    if (!restart()) break;
                    transfer.retry = true;
                }
            }
            if (transfer.ok) {
                // A single whole-file hash cannot tell which bytes are wrong (there are
                // no per-segment hashes), so a mismatch fails rather than redownloads.
                if (hash->Finish() != item.sha256) {
                    writer.reset(); cleanup(); return {false, false, {}, "WALLPAPER_SHA256_MISMATCH", received};
                }
                if (!writer->Finish(error)) {
                    writer.reset(); cleanup(); return {false, false, {}, error, received};
                }
                writer.reset();
                if (cancel.load()) { cleanup(); return {false, true, {}, "WALLPAPER_CANCELLED", received}; }
                if (!MoveFileExW(staging.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    cleanup(); return {false, false, {}, "WALLPAPER_DOWNLOAD_REPLACE", received};
                }
                return {true, false, destination, {}, received};
            }
            if (cancel.load()) break;
            if (!transfer.retry) break;
            stalls = received > before ? 0 : stalls + 1;
            if (stalls >= 3) break;
            for (unsigned wait = 0; wait < (stalls + 1) * 250 && !cancel.load(); wait += 20)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        writer.reset(); cleanup();
        if (!transfer.ok && transfer.error.empty()) transfer.error = error.empty() ? "WALLPAPER_PROTECTION_CREATE" : error;
        return {false, cancel.load(), {}, cancel.load() ? "WALLPAPER_CANCELLED" : transfer.error, received};
    } catch (...) { cleanup(); return {false, cancel.load(), {}, "WALLPAPER_DOWNLOAD_FAILED", 0}; }
}
} // namespace lume::platform::windows::wallpaper
