#include "platform/windows/weather/weather_http.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
namespace lume::platform::windows {
namespace {
using Deadline = std::chrono::steady_clock::time_point;
struct Handle {
    HINTERNET value = nullptr;
    explicit Handle(HINTERNET h = nullptr) : value(h) {}
    ~Handle() { if (value) WinHttpCloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring Wide(std::string_view text) {
    if (text.size() > 8192) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring output(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), output.data(), length);
    return output;
}
std::string Utf8(std::wstring_view text) {
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string output(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        output.data(), length, nullptr, nullptr);
    return output;
}
bool Text(std::string_view text) {
    return text.size() <= 4096 && std::none_of(text.begin(), text.end(),
        [](unsigned char c) { return c < 32 || c == 127; });
}
struct Url {
    std::wstring host, path;
    INTERNET_PORT port = 443;
    bool secure = true, met = false, local = false;
};
std::optional<Url> ParseUrl(std::string_view input, bool loopback) {
    if (!Text(input) || input.find('#') != std::string_view::npos ||
        input.find('\\') != std::string_view::npos) return {};
    auto wide = Wide(input);
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) ||
        parts.dwUserNameLength || parts.dwPasswordLength || !parts.dwHostNameLength) return {};
    Url url;
    url.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    url.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    const auto path = url.path;
    if (parts.dwExtraInfoLength) url.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    url.port = parts.nPort;
    url.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    url.local = loopback && !url.secure && parts.nScheme == INTERNET_SCHEME_HTTP && url.host == L"127.0.0.1";
    if (url.local) return url;
    if (!url.secure || url.port != 443) return {};
    url.met = url.host == L"api.met.no";
    if (url.met) {
        if (path.find(L"/weatherapi/locationforecast/") != 0) return {};
    } else if (url.host == L"www.qweather.com") {
        if (path != L"/v2/search") {
            if (path.find(L"/weather/") != 0 || path.size() < 15 || path.substr(path.size()-5) != L".html") return {};
            const auto slug = path.substr(9, path.size()-14);
            if (!std::all_of(slug.begin(), slug.end(),
                [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-'; })) return {};
        }
    } else if (!((url.host == L"api.ip.sb" && path == L"/geoip") ||
        (url.host == L"ipinfo.io" && path == L"/json") ||
        (url.host == L"api-bdc.io" && path == L"/data/reverse-geocode-client") ||
        (url.host == L"nominatim.openstreetmap.org" && path == L"/reverse"))) return {};
    return url;
}
// The request context stays alive until HANDLE_CLOSING, the final WinHTTP
// callback. Parent handles close afterward. No callback points into UI/business.
class Operation {
public:
    explicit Operation(HINTERNET request) : handle_(request) {}
    ~Operation() {
        if (!handle_) return;
        WinHttpCloseHandle(handle_);
        if (attached_) {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [&] { return closed_; });
        }
    }
    bool Attach() {
        auto context = reinterpret_cast<DWORD_PTR>(this);
        if (!WinHttpSetOption(handle_, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return false;
        attached_ = WinHttpSetStatusCallback(handle_, Callback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) !=
            WINHTTP_INVALID_STATUS_CALLBACK;
        return attached_;
    }
    void Begin() {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_ = 0; bytes_ = 0; failed_ = false;
    }
    bool Await(BOOL started, DWORD event, const weather::Cancellation& cancel, Deadline deadline) {
        if (!started && GetLastError() != ERROR_IO_PENDING) return false;
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            if (cancel.Cancelled() || std::chrono::steady_clock::now() >= deadline || failed_ || closed_) return false;
            if (completed_ == event) return true;
            ready_.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(20)));
        }
    }
    HINTERNET Get() const { return handle_; }
    DWORD Bytes() const { return bytes_; }
    std::array<char, 16384> buffer{};
private:
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status, void*, DWORD length) {
        if (!context) return;
        auto& self = *reinterpret_cast<Operation*>(context);
        if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
            status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
            status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE &&
            status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR &&
            status != WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) return;
        std::lock_guard<std::mutex> lock(self.mutex_);
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) self.closed_ = true;
        else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) self.failed_ = true;
        else { self.completed_ = status; self.bytes_ = length; }
        self.ready_.notify_all();
    }
    HINTERNET handle_ = nullptr;
    bool attached_ = false, closed_ = false, failed_ = false;
    DWORD completed_ = 0, bytes_ = 0;
    std::mutex mutex_;
    std::condition_variable ready_;
};
std::string Header(HINTERNET request, DWORD query) {
    DWORD length = 0;
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &length, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length > 8192 || length < sizeof(wchar_t)) return {};
    std::wstring value(length / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &length, WINHTTP_NO_HEADER_INDEX)) return {};
    value.resize(length / sizeof(wchar_t));
    while (!value.empty() && value.back() == 0) value.pop_back();
    return Utf8(value);
}
std::optional<std::size_t> ContentLength(const std::string& text) {
    if (text.empty() || text.size() > 10) return {};
    std::size_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return {};
        value = value * 10 + c - '0';
    }
    return value;
}
}
weather::HttpResponse WeatherHttp::Get(const weather::HttpRequest& request, const weather::Cancellation& cancel) const {
    try {
        if (cancel.Cancelled() || !request.timeoutMs || request.timeoutMs > 60000 ||
            !request.maxBytes || request.maxBytes > weather::ProviderMaxBytes || request.headers.size() > 16) return {};
        auto url = ParseUrl(request.url, loopback_);
        if (!url) return {};
        std::wstring headers;
        for (const auto& h : request.headers) {
            if (h.first.empty() || h.first.size() > 64 || !Text(h.second) ||
                !std::all_of(h.first.begin(), h.first.end(), [](unsigned char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
                })) return {};
            headers += Wide(h.first) + L": " + Wide(h.second) + L"\r\n";
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(request.timeoutMs);
        Handle session(WinHttpOpen(L"Lume/1.0.8 (Windows desktop weather)",
            url->local || request.direct ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
        if (!session.value) return {};
        WinHttpSetTimeouts(session.value, static_cast<int>(request.timeoutMs), static_cast<int>(request.timeoutMs),
            static_cast<int>(request.timeoutMs), static_cast<int>(request.timeoutMs));
        for (int redirects = 0; redirects <= 2; ++redirects) {
            if (cancel.Cancelled() || std::chrono::steady_clock::now() >= deadline) return {};
            Handle connection(WinHttpConnect(session.value, url->host.c_str(), url->port, 0));
            if (!connection.value) return {};
            Operation operation(WinHttpOpenRequest(connection.value, L"GET", url->path.c_str(), nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url->secure ? WINHTTP_FLAG_SECURE : 0));
            if (!operation.Get() || !operation.Attach()) return {};
            DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            DWORD disabled = WINHTTP_DISABLE_COOKIES;
            DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
            DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
            if (!WinHttpSetOption(operation.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)) ||
                !WinHttpSetOption(operation.Get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
                !WinHttpSetOption(operation.Get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)) ||
                !WinHttpSetOption(operation.Get(), WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress))) return {};
            operation.Begin();
            if (!operation.Await(WinHttpSendRequest(operation.Get(), headers.c_str(), static_cast<DWORD>(headers.size()),
                WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(&operation)),
                WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, cancel, deadline)) return {};
            operation.Begin();
            if (!operation.Await(WinHttpReceiveResponse(operation.Get(), nullptr),
                WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, cancel, deadline)) return {};
            DWORD status = 0, size = sizeof(status);
            if (!WinHttpQueryHeaders(operation.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) return {};
            if (url->met && (status == 301 || status == 302 || status == 307 || status == 308)) {
                auto redirect = Header(operation.Get(), WINHTTP_QUERY_LOCATION);
                if (!redirect.empty() && redirect[0] == '/' && (redirect.size() < 2 || redirect[1] != '/'))
                    redirect = "https://api.met.no" + redirect;
                const auto next = ParseUrl(redirect, false);
                if (!next || !next->met || redirects == 2) return {};
                url = next; continue;
            }
            weather::HttpResponse response;
            response.status = static_cast<int>(status);
            response.expires = Header(operation.Get(), WINHTTP_QUERY_EXPIRES);
            response.lastModified = Header(operation.Get(), WINHTTP_QUERY_LAST_MODIFIED);
            response.retryAfter = Header(operation.Get(), WINHTTP_QUERY_RETRY_AFTER);
            if (status != 200 && status != 203) return response;
            const auto encoding = Header(operation.Get(), WINHTTP_QUERY_CONTENT_ENCODING);
            if (!encoding.empty() && encoding != "gzip" && encoding != "deflate" && encoding != "identity") return {};
            // WinHTTP removes encoding and length headers when decompressing.
            const auto expected = ContentLength(Header(operation.Get(), WINHTTP_QUERY_CONTENT_LENGTH));
            if (expected && *expected > request.maxBytes && (encoding.empty() || encoding == "identity")) return {};
            for (;;) {
                operation.Begin();
                if (!operation.Await(WinHttpReadData(operation.Get(), operation.buffer.data(),
                    static_cast<DWORD>(operation.buffer.size()), nullptr),
                    WINHTTP_CALLBACK_STATUS_READ_COMPLETE, cancel, deadline)) return {};
                const auto bytes = operation.Bytes();
                if (!bytes) break;
                if (bytes > operation.buffer.size() || bytes > request.maxBytes - response.body.size()) return {};
                response.body.append(operation.buffer.data(), bytes);
            }
            if (cancel.Cancelled() || (expected && encoding.empty() && response.body.size() != *expected)) return {};
            return response;
        }
    } catch (...) {}
    return {};
}
}
