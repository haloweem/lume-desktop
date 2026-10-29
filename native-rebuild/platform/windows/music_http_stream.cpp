#include "platform/windows/music_http_stream.h"

#include <winhttp.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string_view>
#include <vector>

namespace lume::music {
namespace {
using Microsoft::WRL::ComPtr;
constexpr ULONG kBlockBytes = 128 * 1024;
constexpr ULONGLONG kMaxFileBytes = 64ull * 1024 * 1024 * 1024;

struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};

// The decoder consumes IStream directly; the optional Media Foundation adapter
// is also available to transport tests. This object owns one bounded block.
class HttpFlacStream final : public IStream {
public:
    HRESULT Open(const std::wstring& url) {
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
            parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
            (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
            parts.dwUserNameLength || parts.dwPasswordLength || !parts.dwHostNameLength) return E_INVALIDARG;
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
        session.value = WinHttpOpen(L"LumeMusic/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session.value || !WinHttpSetTimeouts(session.value, 2000, 2000, 2000, 2000)) return E_FAIL;
        connection.value = WinHttpConnect(session.value, host.c_str(), parts.nPort, 0);
        if (!connection.value) return E_FAIL;
        return Fetch(0, true);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (id != IID_IUnknown && id != IID_IStream && id != IID_ISequentialStream) return E_NOINTERFACE;
        *object = static_cast<IStream*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references;
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE Read(void* destination, ULONG count, ULONG* read) override {
        if (read) *read = 0;
        if (count && !destination) return STG_E_INVALIDPOINTER;
        std::lock_guard lock(mutex);
        ULONG total = 0;
        while (total < count && position < length) {
            if (position < cacheBase || position >= cacheBase + cache.size()) {
                HRESULT result = E_FAIL;
                try { result = Fetch(position, false); }
                catch (...) { result = E_OUTOFMEMORY; }
                if (FAILED(result)) { if (read) *read = total; return result; }
            }
            const auto available = cache.size() - static_cast<std::size_t>(position - cacheBase);
            const auto copy = static_cast<ULONG>(std::min<std::size_t>(count - total, available));
            if (!copy) return E_FAIL;
            std::memcpy(static_cast<char*>(destination) + total,
                cache.data() + static_cast<std::size_t>(position - cacheBase), copy);
            position += copy;
            total += copy;
        }
        if (read) *read = total;
        return total == count ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER offset, DWORD origin, ULARGE_INTEGER* result) override {
        std::lock_guard lock(mutex);
        if (origin != STREAM_SEEK_SET && origin != STREAM_SEEK_CUR && origin != STREAM_SEEK_END)
            return STG_E_INVALIDFUNCTION;
        const auto base = static_cast<LONGLONG>(origin == STREAM_SEEK_SET ? 0 :
            origin == STREAM_SEEK_CUR ? position : length);
        // Limit virtual positions as well as file sizes, avoiding signed overflow.
        if (offset.QuadPart < -base || offset.QuadPart > static_cast<LONGLONG>(kMaxFileBytes) - base)
            return STG_E_INVALIDFUNCTION;
        position = static_cast<ULONGLONG>(base + offset.QuadPart);
        if (result) result->QuadPart = position;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* state, DWORD flags) override {
        if (!state) return STG_E_INVALIDPOINTER;
        if (flags != STATFLAG_DEFAULT && flags != STATFLAG_NONAME) return STG_E_INVALIDFLAG;
        std::lock_guard lock(mutex);
        *state = {};
        state->type = STGTY_STREAM;
        state->cbSize.QuadPart = length;
        state->grfMode = STGM_READ;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
    HRESULT STDMETHODCALLTYPE Clone(IStream**) override { return E_NOTIMPL; }

private:
    ~HttpFlacStream() = default;

    static std::wstring Header(HINTERNET request, DWORD name) {
        wchar_t value[1024]{};
        DWORD size = sizeof(value);
        if (!WinHttpQueryHeaders(request, name, WINHTTP_HEADER_NAME_BY_INDEX, value, &size,
            WINHTTP_NO_HEADER_INDEX)) return {};
        return std::wstring(value, size / sizeof(wchar_t));
    }

    HRESULT Fetch(ULONGLONG offset, bool first) {
        InternetHandle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
        if (!request.value) return E_FAIL;
        const auto end = first ? offset + kBlockBytes - 1 : std::min(length - 1, offset + kBlockBytes - 1);
        const auto headers = L"Range: bytes=" + std::to_wstring(offset) + L"-" + std::to_wstring(end) +
            L"\r\nAccept-Encoding: identity\r\n" + (etag.empty() ? L"" : L"If-Range: " + etag + L"\r\n");
        if (!WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(headers.size()),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) return E_FAIL;
        DWORD status = 0, size = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 206) return E_FAIL;
        const auto range = Header(request.value, WINHTTP_QUERY_CONTENT_RANGE);
        ULONGLONG start = 0, last = 0, whole = 0;
        wchar_t trailing = 0;
        if (swscanf_s(range.c_str(), L"bytes %llu-%llu/%llu%c", &start, &last, &whole, &trailing, 1u) != 3 ||
            !whole || whole > kMaxFileBytes ||
            start != offset || last != std::min(end, whole - 1) || last < start ||
            (!first && whole != length)) return E_FAIL;
        const auto encoding = Header(request.value, WINHTTP_QUERY_CONTENT_ENCODING);
        if (!encoding.empty() && _wcsicmp(encoding.c_str(), L"identity") != 0) return E_FAIL;
        const auto expected = static_cast<ULONG>(last - start + 1);
        std::vector<char> next(expected);
        DWORD total = 0;
        // Read just the signature first; non-FLAC streams leave this path
        // without downloading a whole block before the normal URL reader.
        while (total < expected) {
            DWORD got = 0;
            const DWORD count = first && total < 4 ? std::min<DWORD>(4 - total, expected - total) : expected - total;
            if (!WinHttpReadData(request.value, next.data() + total, count, &got) || !got) return E_FAIL;
            total += got;
            if (first && total >= 4 && std::memcmp(next.data(), "fLaC", 4) != 0) return E_FAIL;
        }
        if (first) {
            if (next.size() < 4) return E_FAIL;
            length = whole;
            const auto tag = Header(request.value, WINHTTP_QUERY_ETAG);
            // If-Range requires a strong validator. Do not replay arbitrary
            // header data as a request header.
            if (tag.size() >= 2 && tag.front() == L'"' && tag.back() == L'"' &&
                tag.find_first_of(L"\r\n") == std::wstring::npos) etag = tag;
        }
        cache = std::move(next);
        cacheBase = offset;
        return S_OK;
    }

    std::atomic<ULONG> references{1};
    InternetHandle session, connection;
    std::mutex mutex;
    std::wstring path, etag;
    std::vector<char> cache;
    ULONGLONG position = 0, length = 0, cacheBase = 0;
    bool secure = false;
};
}

HRESULT CreateMusicHttpFlacStream(const std::wstring& url, IStream** stream) {
    if (!stream) return E_POINTER;
    *stream = nullptr;
    if (url.size() > 4096 || url.find_first_of(L"\r\n") != std::wstring::npos ||
        url.find(L'\0') != std::wstring::npos ||
        (url.rfind(L"https://", 0) != 0 && url.rfind(L"http://", 0) != 0)) return E_INVALIDARG;
    // Known non-FLAC extensions keep their existing native transport.
    const auto end = url.find_first_of(L"?#");
    const auto path = std::wstring_view(url).substr(0, end);
    for (const auto* extension : {L".mp3", L".m4a", L".aac", L".ogg", L".wav", L".wma"}) {
        const auto count = std::wcslen(extension);
        if (path.size() >= count && _wcsnicmp(path.data() + path.size() - count, extension, count) == 0)
            return E_INVALIDARG;
    }
    try {
        ComPtr<HttpFlacStream> source;
        source.Attach(new HttpFlacStream);
        auto result = source->Open(url);
        if (FAILED(result)) return result;
        *stream = source.Detach();
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}

HRESULT CreateMusicHttpFlacStream(const std::wstring& url, IMFByteStream** stream) {
    if (!stream) return E_POINTER;
    *stream = nullptr;
    try {
        ComPtr<IStream> source;
        auto result = CreateMusicHttpFlacStream(url, &source);
        ComPtr<IMFByteStream> bytes;
        if (SUCCEEDED(result)) result = MFCreateMFByteStreamOnStream(source.Get(), &bytes);
        ComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(result)) result = bytes.As(&attributes);
        if (SUCCEEDED(result)) result = attributes->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"audio/flac");
        if (SUCCEEDED(result)) result = attributes->SetString(MF_BYTESTREAM_ORIGIN_NAME, url.c_str());
        if (FAILED(result)) return result;
        *stream = bytes.Detach();
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
}
