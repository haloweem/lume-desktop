#include "platform/windows/account/http.h"
#include <winhttp.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace lume::platform::windows::account {
using namespace lume::account;
namespace {
struct Internet {
    HINTERNET h = nullptr;
    ~Internet() { if (h) WinHttpCloseHandle(h); }
};
struct NativeFailure { DWORD code; };
void WinCheck(BOOL ok) { if (!ok) throw NativeFailure{GetLastError()}; }
struct Exchange {
    std::mutex mutex;
    std::condition_variable cv;
    DWORD event = 0, error = 0, count = 0;
    std::array<char, 16384> read{};
    std::string body;
};
using Context = std::shared_ptr<Exchange>;
void CALLBACK Status(HINTERNET, DWORD_PTR context, DWORD status, LPVOID info, DWORD bytes) {
    if (!context) return;
    auto* holder = reinterpret_cast<Context*>(context);
    auto shared = *holder;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { delete holder; return; }
    {
        std::lock_guard lock(shared->mutex);
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && bytes >= sizeof(WINHTTP_ASYNC_RESULT))
            shared->error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
        else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                 status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
                 status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            shared->event = status;
            if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) shared->count = bytes;
        } else return;
    }
    shared->cv.notify_all();
}
DWORD Await(const Context& state, DWORD expected, const Cancellation& cancel,
            std::chrono::steady_clock::time_point deadline) {
    std::unique_lock lock(state->mutex);
    for (;;) {
        cancel.Check();
        if (state->error) throw NativeFailure{state->error};
        if (state->event == expected) { state->event = 0; return state->count; }
        if (std::chrono::steady_clock::now() >= deadline) throw NativeFailure{ERROR_WINHTTP_TIMEOUT};
        state->cv.wait_for(lock, std::chrono::milliseconds(25));
    }
}
std::string Header(HINTERNET request, DWORD query) {
    wchar_t buffer[512]{}; DWORD size = sizeof(buffer);
    if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, buffer, &size, WINHTTP_NO_HEADER_INDEX)) return {};
    return Utf8(std::wstring_view(buffer, size / sizeof(wchar_t)));
}
HttpResponse Attempt(const HttpRequest& request, const Cancellation& cancel, bool direct) {
    cancel.Check();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    Internet session{WinHttpOpen(L"LumeNativeAccount/1", direct ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
    if (!session.h) throw NativeFailure{GetLastError()};
    WinCheck(WinHttpSetTimeouts(session.h, 3000, 3000, 3000, 3000));
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    // Windows 10 before 20H1 rejects the TLS 1.3 flag (ERROR_INVALID_PARAMETER);
    // there TLS 1.2 alone must still work, or no account request ever would.
    if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinCheck(WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)));
    }
    DWORD retries = 0; WinCheck(WinHttpSetOption(session.h, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)));
    auto url = Wide(request.url);
    URL_COMPONENTS components{}; components.dwStructSize = sizeof(components);
    components.dwHostNameLength = components.dwUrlPathLength = components.dwExtraInfoLength = static_cast<DWORD>(-1);
    WinCheck(WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components));
    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    Internet connection{WinHttpConnect(session.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.h) throw NativeFailure{GetLastError()};
    auto method = Wide(request.method);
    Internet handle{WinHttpOpenRequest(connection.h, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!handle.h) throw NativeFailure{GetLastError()};
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    WinCheck(WinHttpSetOption(handle.h, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinCheck(WinHttpSetOption(handle.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
    auto state = std::make_shared<Exchange>(); state->body = request.body;
    if (WinHttpSetStatusCallback(handle.h, Status, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0)
        == WINHTTP_INVALID_STATUS_CALLBACK) throw NativeFailure{GetLastError()};
    auto* holder = new Context(state);
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(holder);
    if (!WinHttpSetOption(handle.h, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) {
        delete holder; throw NativeFailure{GetLastError()};
    }
    std::wstring headers = L"Accept: application/json\r\n";
    if (!request.bearer.empty()) headers += L"Authorization: Bearer " + Wide(request.bearer) + L"\r\n";
    for (const auto& [key, value] : request.headers) headers += Wide(key) + L": " + Wide(value) + L"\r\n";
    cancel.Check();
    WinCheck(WinHttpSendRequest(handle.h, headers.c_str(), static_cast<DWORD>(headers.size()),
        state->body.empty() ? WINHTTP_NO_REQUEST_DATA : state->body.data(),
        static_cast<DWORD>(state->body.size()), static_cast<DWORD>(state->body.size()), context));
    Await(state, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, cancel, deadline);
    WinCheck(WinHttpReceiveResponse(handle.h, nullptr));
    Await(state, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, cancel, deadline);
    DWORD status = 0, size = sizeof(status);
    WinCheck(WinHttpQueryHeaders(handle.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX));
    if (status >= 300 && status < 400) {
        if (status == 302 && request.target == Target::WallpaperTicket && request.bearer.empty()) {
            cancel.Check(); return {status, "", ""};
        }
        throw Failure(Error::InvalidTarget);
    }
    HttpResponse response{status, {}, Header(handle.h, WINHTTP_QUERY_CONTENT_TYPE)};
    SYSTEMTIME date{};
    DWORD dateSize = sizeof(date);
    FILETIME file{};
    if (WinHttpQueryHeaders(handle.h, WINHTTP_QUERY_DATE | WINHTTP_QUERY_FLAG_SYSTEMTIME, WINHTTP_HEADER_NAME_BY_INDEX,
            &date, &dateSize, WINHTTP_NO_HEADER_INDEX) && SystemTimeToFileTime(&date, &file)) {
        const auto ticks = (static_cast<std::uint64_t>(file.dwHighDateTime) << 32) | file.dwLowDateTime;
        if (ticks > 116444736000000000ULL) response.date = static_cast<std::int64_t>((ticks - 116444736000000000ULL) / 10000000);
    }
    for (;;) {
        cancel.Check();
        WinCheck(WinHttpReadData(handle.h, state->read.data(), static_cast<DWORD>(state->read.size()), nullptr));
        const auto count = Await(state, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, cancel, deadline);
        if (!count) break;
        if (response.body.size() + count > request.maxResponse) throw Failure(Error::InvalidResponse);
        response.body.append(state->read.data(), count);
    }
    cancel.Check(); return response;
}
}
bool MayRetryDirect(DWORD error) noexcept {
    return error == ERROR_WINHTTP_CANNOT_CONNECT || error == ERROR_WINHTTP_NAME_NOT_RESOLVED ||
        error == ERROR_WINHTTP_AUTODETECTION_FAILED || error == ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT ||
        error == ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT;
}
HttpResponse WinHttpTransport::Send(const HttpRequest& request, const Cancellation& cancel) {
    cancel.Check();
    if (!ValidTarget(request.target, request.method, request.url, !request.bearer.empty()) ||
        request.body.size() > MaxAvatarBytes || request.maxResponse > MaxAvatarBytes || !request.maxResponse ||
        request.bearer.size() > 16384 || request.bearer.find_first_of("\r\n") != request.bearer.npos ||
        request.bearer.find('\0') != request.bearer.npos) throw Failure(Error::InvalidTarget);
    for (const auto& [key, value] : request.headers) {
        if ((key != "content-type" && key != "x-lume-device-id") || value.size() > 256 ||
            value.find_first_of("\r\n") != value.npos || value.find('\0') != value.npos) throw Failure(Error::InvalidTarget);
    }
    try { return Attempt(request, cancel, false); }
    catch (const NativeFailure& failure) {
        cancel.Check();
        if (!MayRetryDirect(failure.code)) throw Failure(Error::Network);
        try { return Attempt(request, cancel, true); }
        catch (const NativeFailure&) { cancel.Check(); throw Failure(Error::Network); }
    }
}
}
