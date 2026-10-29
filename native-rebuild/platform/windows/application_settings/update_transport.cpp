#include "platform/windows/application_settings/application_settings_ports.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <filesystem>
#include <fstream>
#include <vector>

namespace lume::platform::windows {
namespace {
using namespace application_settings;
using Clock = std::chrono::steady_clock;
constexpr DWORD MaxBytes = 65536;
struct InternetHandle {
    HINTERNET value = nullptr;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};
// Close cancels in-flight I/O; HANDLE_CLOSING is the lifetime fence for callbacks.
class Request final {
public:
    explicit Request(HINTERNET handle) : handle_(handle) {}
    ~Request() {
        if (!handle_) return;
        WinHttpCloseHandle(handle_);
        if (attached_) {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [&] { return closed_; });
        }
    }
    bool Attach() {
        auto context = reinterpret_cast<DWORD_PTR>(this);
        if (!handle_ || !WinHttpSetOption(handle_, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return false;
        attached_ = WinHttpSetStatusCallback(handle_, Callback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
            WINHTTP_CALLBACK_FLAG_HANDLES, 0) != WINHTTP_INVALID_STATUS_CALLBACK;
        return attached_;
    }
    void Begin() {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_ = bytes_ = error_ = 0;
    }
    bool Await(BOOL started, DWORD event, const Cancellation& cancellation, Clock::time_point deadline) {
        const auto error = started ? ERROR_SUCCESS : GetLastError();
        std::unique_lock<std::mutex> lock(mutex_);
        if (error != ERROR_SUCCESS && error != ERROR_IO_PENDING) { error_ = error; return false; }
        for (;;) {
            if (cancellation.cancelled || Clock::now() >= deadline || error_ || closed_) return false;
            if (completed_ == event) return true;
            condition_.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::milliseconds(20)));
        }
    }
    DWORD Bytes() { std::lock_guard<std::mutex> lock(mutex_); return bytes_; }
    std::string Error(const Cancellation& cancellation, Clock::time_point deadline) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancellation.cancelled) return "update-cancelled";
        if (Clock::now() >= deadline || error_ == ERROR_WINHTTP_TIMEOUT) return "update-timeout";
        if (error_ == ERROR_WINHTTP_SECURE_FAILURE) return "update-tls";
        if (error_ == ERROR_WINHTTP_CANNOT_CONNECT || error_ == ERROR_WINHTTP_NAME_NOT_RESOLVED ||
            error_ == ERROR_WINHTTP_CONNECTION_ERROR) return "update-offline";
        return "update-network";
    }
    HINTERNET Get() const { return handle_; }
    std::array<char, 8192> buffer{};
private:
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD length) {
        if (!context) return;
        auto& self = *reinterpret_cast<Request*>(context);
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) self.closed_ = true;
        else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
            self.error_ = info && length == sizeof(WINHTTP_ASYNC_RESULT)
                ? static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError : ERROR_WINHTTP_INTERNAL_ERROR;
        } else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                   status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
                   status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            self.completed_ = status;
            self.bytes_ = length;
        }
        self.condition_.notify_all();
    }
    HINTERNET handle_ = nullptr;
    bool attached_ = false, closed_ = false;
    DWORD completed_ = 0, bytes_ = 0, error_ = 0;
    std::mutex mutex_;
    std::condition_variable condition_;
};
class UpdateTransport final : public IUpdateTransport {
public:
    explicit UpdateTransport(bool verification) : verification_(verification) {}
    ManifestResponse Fetch(const Cancellation& cancellation) override {
        if (cancellation.cancelled) return {{}, "update-cancelled"};
        if (verification_) return {{}, "update-verification"};
        try {
            const auto deadline = Clock::now() + std::chrono::seconds(15);
            InternetHandle session(WinHttpOpen(L"LumeNative/1.0.8", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
            if (!session.value || !WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000))
                return {{}, "update-network"};
            // No caller-supplied URLs and no redirects, even to other official hosts.
            InternetHandle connection(WinHttpConnect(session.value, L"lumedesktop.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
            if (!connection.value) return {{}, "update-network"};
            Request request(WinHttpOpenRequest(connection.value, L"GET", L"/updates.json", nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
            if (!request.Attach()) return {{}, "update-network"};
            DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
            DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
            if (!WinHttpSetOption(request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) ||
                !WinHttpSetOption(request.Get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
                !WinHttpSetOption(request.Get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)))
                return {{}, "update-network"};
            if (cancellation.cancelled) return {{}, "update-cancelled"};
            request.Begin();
            if (!request.Await(WinHttpSendRequest(request.Get(), L"Accept: application/json\r\nAccept-Encoding: identity\r\nCache-Control: no-cache\r\n",
                static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(&request)),
                WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, cancellation, deadline))
                return {{}, request.Error(cancellation, deadline)};
            request.Begin();
            if (!request.Await(WinHttpReceiveResponse(request.Get(), nullptr),
                WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, cancellation, deadline))
                return {{}, request.Error(cancellation, deadline)};
            DWORD status = 0, size = sizeof(status);
            if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
                return {{}, "update-response"};
            if (status >= 300 && status < 400) return {{}, "update-redirect"};
            if (status != 200) return {{}, "update-http"};
            DWORD expected = 0;
            size = sizeof(expected);
            const bool hasLength = WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &expected, &size, WINHTTP_NO_HEADER_INDEX);
            if (hasLength && expected > MaxBytes) return {{}, "update-too-large"};
            std::string body;
            for (;;) {
                request.Begin();
                if (!request.Await(WinHttpReadData(request.Get(), request.buffer.data(),
                    static_cast<DWORD>(request.buffer.size()), nullptr), WINHTTP_CALLBACK_STATUS_READ_COMPLETE,
                    cancellation, deadline)) return {{}, request.Error(cancellation, deadline)};
                const auto bytes = request.Bytes();
                if (!bytes) break;
                if (bytes > request.buffer.size() || bytes > MaxBytes - body.size()) return {{}, "update-too-large"};
                body.append(request.buffer.data(), bytes);
            }
            if (cancellation.cancelled) return {{}, "update-cancelled"};
            if (hasLength && expected != body.size()) return {{}, "update-response"};
            return {std::move(body), {}};
        } catch (...) { return {{}, "update-network"}; }
    }
private:
    bool verification_ = false;
};

class Sha256 {
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> storage_;
public:
    Sha256() {
        DWORD length = 0, count = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
            BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &count, 0) < 0) return;
        storage_.resize(length);
        if (BCryptCreateHash(algorithm_, &hash_, storage_.data(), length, nullptr, 0, 0) < 0) hash_ = nullptr;
    }
    ~Sha256() { if (hash_) BCryptDestroyHash(hash_); if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0); }
    bool Add(const void* data, DWORD size) {
        return hash_ && BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<void*>(data)), size, 0) >= 0;
    }
    std::string Finish() {
        std::array<unsigned char, 32> hash{};
        if (!hash_ || BCryptFinishHash(hash_, hash.data(), static_cast<ULONG>(hash.size()), 0) < 0) return {};
        const char hex[] = "0123456789abcdef";
        std::string text;
        for (const auto value : hash) { text += hex[value >> 4]; text += hex[value & 15]; }
        return text;
    }
};
struct NativeFile {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~NativeFile() { Close(); }
    void Close() { if (value != INVALID_HANDLE_VALUE) { CloseHandle(value); value = INVALID_HANDLE_VALUE; } }
};
bool CheckFile(HANDLE file, const ManifestResult& release) {
    LARGE_INTEGER length{}, start{};
    if (!GetFileSizeEx(file, &length) || static_cast<std::uint64_t>(length.QuadPart) != release.installer.size ||
        !SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return false;
    Sha256 hash;
    std::array<char, 65536> buffer{};
    DWORD count = 0;
    for (;;) {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) return false;
        if (!count) break;
        if (!hash.Add(buffer.data(), count)) return false;
    }
    auto expected = release.installer.sha256;
    std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'F' ? c + ('a' - 'A') : c);
    });
    const auto actual = hash.Finish();
    return actual.size() == 64 && actual == expected;
}
class UpdateInstaller final : public IUpdateInstaller {
    bool verification_;
    std::filesystem::path directory_, prepared_;
public:
    explicit UpdateInstaller(bool verification) : verification_(verification) {}
    ~UpdateInstaller() {
        // Delete only this instance's known cache files, never user-selected paths.
        std::error_code error;
        if (!directory_.empty()) {
            std::filesystem::remove(directory_ / L"installer.part", error);
            std::filesystem::remove(directory_ / L"LumeSetup.exe", error);
            std::filesystem::remove(directory_, error);
        }
    }
    PreparedUpdate Download(const ManifestResult& release, const Cancellation& cancel,
                            const std::function<void(int)>& progress) override {
        if (verification_) return {{}, "update-verification"};
        if (release.update.status != "available" || !release.error.empty() ||
            !IsOfficialUpdateUrl(release.installer.url) || release.installer.sha256.size() != 64 ||
            release.installer.size < 2 || release.installer.size > 1073741824) return {{}, "update-manifest"};
        try {
            if (directory_.empty()) {
                wchar_t temporary[32768]{};
                const DWORD length = GetTempPathW(32768, temporary);
                if (!length || length >= 32768) return {{}, "update-cache"};
                std::array<unsigned char, 16> random{};
                if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                    return {{}, "update-cache"};
                std::wstring suffix;
                for (auto c : random) { suffix += L"0123456789abcdef"[c >> 4]; suffix += L"0123456789abcdef"[c & 15]; }
                directory_ = std::filesystem::path(temporary) / (L"LumeUpdate-" + suffix);
                if (!CreateDirectoryW(directory_.c_str(), nullptr)) { directory_.clear(); return {{}, "update-cache"}; }
            }
            const auto partial = directory_ / L"installer.part";
            NativeFile file;
            file.value = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file.value == INVALID_HANDLE_VALUE) return {{}, "update-cache"};
            std::wstring url(release.installer.url.begin(), release.installer.url.end());
            URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
            parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
            if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) return {{}, "update-manifest"};
            const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
            std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
            if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
            InternetHandle session(WinHttpOpen(L"LumeNativeUpdate/1.0.8", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
            if (!session.value || !WinHttpSetTimeouts(session.value, 5000, 10000, 15000, 15000)) return {{}, "update-network"};
            InternetHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
            if (!connection.value) return {{}, "update-network"};
            Request request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
            DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
            if (!request.Attach() || !WinHttpSetOption(request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) ||
                !WinHttpSetOption(request.Get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) return {{}, "update-network"};
            const auto deadline = Clock::now() + std::chrono::minutes(10);
            request.Begin();
            if (!request.Await(WinHttpSendRequest(request.Get(), L"Accept-Encoding: identity\r\n", static_cast<DWORD>(-1),
                WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(&request)),
                WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, cancel, deadline)) return {{}, request.Error(cancel, deadline)};
            request.Begin();
            if (!request.Await(WinHttpReceiveResponse(request.Get(), nullptr), WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,
                cancel, deadline)) return {{}, request.Error(cancel, deadline)};
            DWORD status = 0, length = sizeof(status);
            if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) return {{}, "update-response"};
            if (status >= 300 && status < 400) return {{}, "update-redirect"};
            if (status != 200) return {{}, "update-http"};
            std::uint64_t received = 0;
            Sha256 hash;
            int reported = -1;
            for (;;) {
                request.Begin();
                if (!request.Await(WinHttpReadData(request.Get(), request.buffer.data(), static_cast<DWORD>(request.buffer.size()), nullptr),
                    WINHTTP_CALLBACK_STATUS_READ_COMPLETE, cancel, deadline)) return {{}, request.Error(cancel, deadline)};
                const DWORD bytes = request.Bytes();
                if (!bytes) break;
                if (bytes > request.buffer.size() || received + bytes > release.installer.size) return {{}, "update-response"};
                DWORD written = 0;
                if (!WriteFile(file.value, request.buffer.data(), bytes, &written, nullptr) || written != bytes ||
                    !hash.Add(request.buffer.data(), bytes)) return {{}, "update-cache"};
                received += bytes;
                const int percent = static_cast<int>(received * 100 / release.installer.size);
                if (percent != reported && progress) { reported = percent; progress(percent); }
            }
            auto expected = release.installer.sha256;
            std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'F' ? c + 32 : c);
            });
            if (cancel.cancelled) return {{}, "update-cancelled"};
            if (received != release.installer.size || hash.Finish() != expected) return {{}, "update-checksum"};
            if (!FlushFileBuffers(file.value)) return {{}, "update-cache"};
            file.Close();
            prepared_ = directory_ / L"LumeSetup.exe";
            if (!MoveFileExW(partial.c_str(), prepared_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return {{}, "update-cache"};
            return {prepared_.wstring(), {}};
        } catch (...) { return {{}, "update-cache"}; }
    }
    std::string Launch(const PreparedUpdate& update, const ManifestResult& release, const std::wstring& installDirectory) override {
        if (verification_) return "update-verification";
        if (!update.error.empty() || prepared_.empty() || update.path != prepared_.wstring() ||
            installDirectory.empty() || installDirectory.find(L'"') != std::wstring::npos ||
            !std::filesystem::path(installDirectory).is_absolute()) return "update-installer";
        NativeFile file;
        // Deny writes/deletes until ShellExecute has accepted this exact verified file.
        file.value = CreateFileW(prepared_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value == INVALID_HANDLE_VALUE || !CheckFile(file.value, release)) return "update-checksum";
        const std::wstring arguments = L"/SILENT /NORESTART /LUMEUPDATE=1 /DIR=\"" + installDirectory + L"\"";
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = L"open"; info.lpFile = update.path.c_str(); info.lpParameters = arguments.c_str();
        info.nShow = SW_SHOWNORMAL;
        const bool launched = ShellExecuteExW(&info) != FALSE;
        const DWORD launchError = launched ? ERROR_SUCCESS : GetLastError();
        if (SUCCEEDED(initialized)) CoUninitialize();
        if (!launched) return launchError == ERROR_CANCELLED ? "update-install-cancelled" : "update-installer";
        if (info.hProcess) CloseHandle(info.hProcess);
        // The running installer owns the cache lifetime until installation finishes.
        directory_.clear(); prepared_.clear();
        return {};
    }
};
}
std::shared_ptr<application_settings::IUpdateTransport> CreateApplicationUpdateTransport(bool verification) {
    return std::make_shared<UpdateTransport>(verification);
}
std::shared_ptr<application_settings::IUpdateInstaller> CreateApplicationUpdateInstaller(bool verification) {
    return std::make_shared<UpdateInstaller>(verification);
}
bool VerifyApplicationUpdateFile(const std::wstring& path, const application_settings::ManifestResult& release) {
    NativeFile file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return file.value != INVALID_HANDLE_VALUE && CheckFile(file.value, release);
}
}
