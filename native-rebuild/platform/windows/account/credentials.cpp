#include "platform/windows/account/credentials.h"
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
namespace lume::platform::windows::account {
using namespace lume::account;
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
void Valid(const Credentials& c) {
    if (c.refreshToken.empty() || c.refreshToken.size() > 16384 || c.subject.empty() || c.subject.size() > 512 ||
        c.nonce.empty() || c.nonce.size() > 256) throw Failure(Error::CredentialCorrupt);
}
}
DpapiCredentials::DpapiCredentials(std::string directory) {
    if (directory.empty()) throw Failure(Error::Storage);
    root_ = std::filesystem::absolute(std::filesystem::path(Wide(directory))).lexically_normal();
    auto name = root_.wstring();
    CharLowerBuffW(name.data(), static_cast<DWORD>(name.size()));
    entropy_ = Digest("Lume.Account.v1|" + Utf8(name));
}
Bytes DpapiCredentials::Protect(std::string_view input, bool decrypt) {
    if (input.size() > 64 * 1024) throw Failure(decrypt ? Error::CredentialCorrupt : Error::Storage);
    DATA_BLOB source{static_cast<DWORD>(input.size()), reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
    DATA_BLOB entropy{static_cast<DWORD>(entropy_.size()), entropy_.data()}, output{};
    const auto success = decrypt
        ? CryptUnprotectData(&source, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptProtectData(&source, L"Lume account credentials", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!success) throw Failure(decrypt ? Error::CredentialCorrupt : Error::Storage);
    Bytes bytes(output.pbData, output.pbData + output.cbData);
    SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData);
    return bytes;
}
std::optional<std::string> DpapiCredentials::Read(const std::filesystem::path& path) {
    Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
        throw Failure(Error::Storage);
    }
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file.value, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !GetFileSizeEx(file.value, &size) || size.QuadPart <= 0 || size.QuadPart > 64 * 1024) throw Failure(Error::CredentialCorrupt);
    std::string data(static_cast<std::size_t>(size.QuadPart), '\0'); DWORD read = 0;
    if (!ReadFile(file.value, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size())
        throw Failure(Error::Storage);
    return data;
}
void DpapiCredentials::AtomicWrite(const std::filesystem::path& path, std::string_view data) {
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) throw Failure(Error::Storage);
    auto temp = path; temp += "." + Base64(RandomBytes(12)) + ".tmp";
    try {
        {
            Handle file{CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY, nullptr)};
            DWORD wrote = 0;
            if (file.value == INVALID_HANDLE_VALUE ||
                !WriteFile(file.value, data.data(), static_cast<DWORD>(data.size()), &wrote, nullptr) ||
                wrote != data.size() || !FlushFileBuffers(file.value)) throw Failure(Error::Storage);
        }
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw Failure(Error::Storage);
    } catch (...) { DeleteFileW(temp.c_str()); throw Failure(Error::Storage); }
}
std::optional<Credentials> DpapiCredentials::Load() {
    std::lock_guard lock(mutex_);
    auto data = Read(root_ / L"account.credentials");
    if (!data) return {};
    auto plain = Protect(*data, true);
    try {
        auto j = StrictJson(std::string_view(reinterpret_cast<char*>(plain.data()), plain.size()), 32768);
        SecureZeroMemory(plain.data(), plain.size());
        if (!j.is_object() || j.size() != 4 || j.value("version", 0) != 1) throw Failure(Error::CredentialCorrupt);
        Credentials credentials{j.at("refreshToken").get<std::string>(), j.at("subject").get<std::string>(), j.at("nonce").get<std::string>()};
        Valid(credentials); return credentials;
    } catch (...) { SecureZeroMemory(plain.data(), plain.size()); throw Failure(Error::CredentialCorrupt); }
}
void DpapiCredentials::Save(const Credentials& c) {
    std::lock_guard lock(mutex_);
    Valid(c);
    auto plain = Json{{"version", 1}, {"refreshToken", c.refreshToken}, {"subject", c.subject}, {"nonce", c.nonce}}.dump();
    Bytes encrypted;
    try { encrypted = Protect(plain, false); }
    catch (...) { SecureZeroMemory(plain.data(), plain.size()); throw; }
    SecureZeroMemory(plain.data(), plain.size());
    AtomicWrite(root_ / L"account.credentials", std::string_view(reinterpret_cast<char*>(encrypted.data()), encrypted.size()));
}
void DpapiCredentials::Clear() {
    std::lock_guard lock(mutex_);
    if (!DeleteFileW((root_ / L"account.credentials").c_str())) {
        auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) throw Failure(Error::Storage);
    }
}
void DpapiCredentials::ReserveWallpaperDownload(std::int64_t day, std::string_view subject, unsigned limit) {
    ChangeWallpaperQuota(day, subject, limit, false);
}
void DpapiCredentials::ReleaseWallpaperDownload(std::int64_t day, std::string_view subject) {
    ChangeWallpaperQuota(day, subject, subject.empty() ? GuestWallpaperTotalLimit : SignedInWallpaperReservationLimit, true);
}
void DpapiCredentials::ChangeWallpaperQuota(std::int64_t day, std::string_view subject, unsigned limit, bool release) {
    std::lock_guard lock(mutex_);
    // Signed-in admission uses a reservation ceiling; the server determines the
    // actual member/allowlist tier. Keep legacy bounded callers compatible.
    const bool validLimit = subject.empty() ? limit == GuestWallpaperTotalLimit :
        (limit == SignedInWallpaperReservationLimit || limit == MemberWallpaperDailyLimit);
    if (day < 0 || subject.size() > 512 || !validLimit) throw Failure(Error::InvalidCommand);
    // Serialize independent processes using this same per-user storage root.
    const auto name = std::wstring(L"Local") + wchar_t(92) + L"Lume.WallpaperQuota." + Wide(Base64(entropy_));
    Handle owner{CreateMutexW(nullptr, FALSE, name.c_str())};
    if (!owner.value || owner.value == INVALID_HANDLE_VALUE) throw Failure(Error::Storage);
    const auto wait = WaitForSingleObject(owner.value, 5000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) throw Failure(Error::Storage);
    struct Unlock { HANDLE handle; ~Unlock() { ReleaseMutex(handle); } } unlock{owner.value};
    const auto path = root_ / L"wallpaper-quota.dat";
    WallpaperQuota quota{day, 0, {}};
    if (const auto stored = Read(path)) {
        const auto plain = Protect(*stored, true);
        const auto json = StrictJson(std::string_view(reinterpret_cast<const char*>(plain.data()), plain.size()), 32768);
        // Version 2 adds the lifetime guest count; a version 1 ledger had none.
        const int version = json.is_object() ? json.value("version", 0) : 0;
        if (!json.is_object() || (version != 1 && version != 2) || json.size() != (version == 2 ? 5u : 4u) ||
            (version == 2 && (!json.contains("guest") || !json.at("guest").is_number_unsigned() ||
                json.at("guest") > GuestWallpaperTotalLimit)) ||
            !json.at("day").is_number_integer() || json.at("day") < 0 ||
            !json.at("total").is_number_unsigned() || json.at("total") > SignedInWallpaperReservationLimit ||
            !json.at("accounts").is_object())
            throw Failure(Error::Storage);
        quota.day = json.at("day").get<std::int64_t>(); quota.total = json.at("total").get<unsigned>();
        if (version == 2) quota.guest = json.at("guest").get<unsigned>();
        for (const auto& item : json.at("accounts").items()) {
            if (item.key().empty() || item.key().size() > 512 || !item.value().is_number_unsigned() ||
                item.value() > quota.total) throw Failure(Error::Storage);
            quota.accounts[item.key()] = item.value().get<unsigned>();
        }
        if (day > quota.day && !release) quota = WallpaperQuota{day, 0, {}, quota.guest};
    }
    if (release) {
        // A guest refund returns its lifetime slot even across midnight.
        if (subject.empty() && quota.guest) --quota.guest;
        if (day == quota.day && quota.total) {
            if (!subject.empty()) {
                const auto account=quota.accounts.find(std::string(subject));
                if (account==quota.accounts.end() || !account->second) return;
                if (!--account->second) quota.accounts.erase(account);
            }
            --quota.total;
        }
    } else {
        if (subject.empty() ? quota.guest >= limit : quota.accounts[std::string(subject)] >= limit)
            throw Failure(Error::WallpaperQuota);
        ++quota.total;
        if (subject.empty()) ++quota.guest; else ++quota.accounts[std::string(subject)];
    }
    const auto text = Json{{"version", 2}, {"day", quota.day}, {"total", quota.total}, {"accounts", quota.accounts},
        {"guest", quota.guest}}.dump();
    // Never persist a ledger that the bounded JSON reader cannot reopen.
    if (text.size() > 32768) throw Failure(Error::Storage);
    const auto encoded = Protect(text, false);
    AtomicWrite(path, std::string_view(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
}
std::string DpapiCredentials::DeviceId() {
    std::lock_guard lock(mutex_);
    if (auto saved = Read(root_ / L"account.device")) {
        if (saved->size() < 32 || saved->size() > 80 || saved->find_first_not_of("0123456789abcdefABCDEF-") != saved->npos)
            throw Failure(Error::CredentialCorrupt);
        return *saved;
    }
    auto bytes = RandomBytes(16);
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
    constexpr char hex[] = "0123456789abcdef";
    std::string id;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) id += '-';
        id += hex[bytes[i] >> 4]; id += hex[bytes[i] & 15];
    }
    AtomicWrite(root_ / L"account.device", id); return id;
}
}
