#include "platform/windows/folia_runtime_download.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_MALLOC
#include "vendor/miniz/miniz_tinfl.h"

namespace lume::platform::windows {
namespace {
namespace fs = std::filesystem;
using Json = music::auth::Json;
using Cancelled = music::auth::Cancelled;
using Clock = std::chrono::steady_clock;
using Sink = std::function<void(const void*, std::size_t)>;
using Transport = std::function<void(const Sink&, const Cancelled&)>;
constexpr std::uint64_t MaxArchive = 1024ULL * 1024 * 1024, MaxUnpacked = 2 * MaxArchive;
[[noreturn]] void Fail(const char* error) { throw std::runtime_error(error); }
void Require(bool value, const char* error = "invalid-response") { if (!value) Fail(error); }
void Check(const Cancelled& cancelled) { music::auth::CheckCancelled(cancelled); }
bool Hex(std::string_view value) { return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == value.npos; }
std::wstring Native(const fs::path& path) {
    auto value = path.wstring(); std::replace(value.begin(), value.end(), L'/', L'\\');
    return value.rfind(L"\\\\?\\", 0) == 0 ? value : L"\\\\?\\" + value;
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() { Close(); }
    Handle(const Handle&) = delete;
    void Close() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = INVALID_HANDLE_VALUE; }
};
struct Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Hash() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0)
            BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    }
    ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void Add(const void* data, std::size_t size) {
        Require(size <= MAXDWORD && hash && BCryptHashData(hash,
            static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), 0) >= 0, "storage");
    }
    std::string Finish() {
        std::array<UCHAR, 32> bytes{};
        Require(hash && BCryptFinishHash(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) >= 0, "storage");
        std::string result;
        for (const auto byte : bytes) { result += "0123456789abcdef"[byte >> 4]; result += "0123456789abcdef"[byte & 15]; }
        return result;
    }
};
bool Directories(const fs::path& path, bool create) {
    if (!path.is_absolute() || path.root_name().wstring().size() != 2) return false;
    auto current = path.root_path();
    for (const auto& part : path.relative_path()) {
        if (part == L"." || part == L"..") return false;
        current /= part;
        auto attributes = GetFileAttributesW(Native(current).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES && create) {
            if (!CreateDirectoryW(Native(current).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
            attributes = GetFileAttributesW(Native(current).c_str());
        }
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return true;
}
std::uint64_t Size(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    Require(file != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)), "storage");
    return (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}
HANDLE Open(const fs::path& path) {
    Require(Directories(path.parent_path(), false), "storage");
    return CreateFileW(Native(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
}
std::string ReadSmall(const fs::path& path, std::size_t limit) {
    Handle file(Open(path)); const auto size = Size(file.value); Require(size <= limit);
    std::string result(static_cast<std::size_t>(size), '\0'); DWORD count = 0;
    Require(!size || (ReadFile(file.value, result.data(), static_cast<DWORD>(size), &count, nullptr) && count == size), "storage");
    return result;
}
std::string FileHash(const fs::path& path, std::uint64_t expected, const Cancelled& cancelled) {
    Handle file(Open(path)); Require(Size(file.value) == expected);
    Hash hash; std::array<char, 65536> bytes{}; DWORD count = 0;
    for (;;) {
        Check(cancelled);
        Require(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr), "storage");
        if (!count) break;
        hash.Add(bytes.data(), count);
    }
    return hash.Finish();
}
struct Descriptor { std::string id, url, sha256; std::uint64_t size = 0, unpacked = 0; };
Descriptor Describe(const fs::path& directory) {
    const auto json = Json::parse(ReadSmall(directory / L"music-runtime.json", 16384));
    Require(json.is_object() && json.value("schemaVersion", 0) == 1);
    Descriptor value{json.at("id").get<std::string>(), json.at("url").get<std::string>(),
        json.at("sha256").get<std::string>(), json.at("size").get<std::uint64_t>(), json.at("unpackedBytes").get<std::uint64_t>()};
    Require(Hex(value.id) && Hex(value.sha256) && value.size >= 22 && value.size <= MaxArchive &&
        value.unpacked > 0 && value.unpacked <= MaxUnpacked && value.url ==
        "https://static.lumedesktop.com/downloads/LumeMusicRuntime-" + value.sha256.substr(0, 16) + ".zip");
    return value;
}
fs::path Relative(std::string_view value) {
    Require(!value.empty() && value.size() <= 2048 && value.front() != '/' && value.back() != '/' &&
        value.find_first_of("\\:") == value.npos &&
        std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }));
    const auto path = fs::u8path(value);
    Require(!path.is_absolute() && !path.has_root_path());
    for (const auto& component : path) {
        auto part = component.wstring();
        Require(!part.empty() && part != L"." && part != L".." && part.back() != L'.' && part.back() != L' ');
        std::transform(part.begin(), part.end(), part.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        part = part.substr(0, part.find(L'.'));
        Require(part != L"CON" && part != L"PRN" && part != L"AUX" && part != L"NUL" &&
            !(part.size() == 4 && (part.substr(0, 3) == L"COM" || part.substr(0, 3) == L"LPT") && part[3] >= L'1' && part[3] <= L'9'));
    }
    return path;
}
std::wstring Key(const fs::path& path) {
    auto value = path.generic_wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
Json Inventory(const fs::path& directory, const Descriptor& descriptor) {
    const auto bytes = ReadSmall(directory / L"runtime-inventory.json", 8 * 1024 * 1024);
    Hash hash; hash.Add(bytes.data(), bytes.size()); Require(hash.Finish() == descriptor.id);
    const auto value = Json::parse(bytes);
    Require(value.is_object() && value.contains("files") && value["files"].is_array() &&
        value["files"].size() > 0 && value["files"].size() <= 100000);
    return value;
}
bool Ready(const fs::path& directory, const Descriptor& descriptor) noexcept {
    try {
        const auto inventory = Inventory(directory, descriptor);
        for (const auto* required : {"LumeMusicProviderHost.exe", "main.cjs"}) {
            const auto entry = std::find_if(inventory["files"].begin(), inventory["files"].end(),
                [&](const Json& item) { return item.is_object() && item.value("path", std::string{}) == required; });
            Require(entry != inventory["files"].end());
            Handle file(Open(directory / fs::u8path(required)));
            Require(Size(file.value) == entry->at("bytes").get<std::uint64_t>());
        }
        return true;
    } catch (...) { return false; }
}
fs::path Existing(const fs::path& executable, const fs::path& root, const Descriptor& descriptor) {
    const auto bundled = executable / L"music-runtime";
    if (Ready(bundled, descriptor)) return bundled;
    const auto cached = root.parent_path() / L"music-runtime" / descriptor.id;
    return Ready(cached, descriptor) ? cached : fs::path{};
}

struct InternetHandle {
    HINTERNET value;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};
// Same callback lifetime fence as application_settings/update_transport.cpp.
class Request {
public:
    explicit Request(HINTERNET handle) : handle_(handle) {}
    ~Request() {
        if (!handle_) return;
        WinHttpCloseHandle(handle_);
        if (attached_) { std::unique_lock lock(mutex_); condition_.wait(lock, [&] { return closed_; }); }
    }
    void Attach() {
        auto context = reinterpret_cast<DWORD_PTR>(this);
        Require(handle_ && WinHttpSetOption(handle_, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)), "network");
        attached_ = WinHttpSetStatusCallback(handle_, Callback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
            WINHTTP_CALLBACK_FLAG_HANDLES, 0) != WINHTTP_INVALID_STATUS_CALLBACK;
        Require(attached_, "network");
    }
    void Begin() { std::lock_guard lock(mutex_); completed_ = bytes_ = error_ = 0; }
    void Await(BOOL started, DWORD event, const Cancelled& cancelled, Clock::time_point deadline) {
        const auto error = started ? ERROR_SUCCESS : GetLastError();
        Require(error == ERROR_SUCCESS || error == ERROR_IO_PENDING, "network");
        std::unique_lock lock(mutex_);
        for (;;) {
            Check(cancelled);
            if (Clock::now() >= deadline || error_ == ERROR_WINHTTP_TIMEOUT) Fail("timeout");
            Require(!error_ && !closed_, "network");
            if (completed_ == event) return;
            condition_.wait_for(lock, std::chrono::milliseconds(20));
        }
    }
    DWORD Bytes() { std::lock_guard lock(mutex_); return bytes_; }
    HINTERNET Get() const { return handle_; }
    std::array<char, 65536> buffer{};
private:
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD length) {
        if (!context) return;
        auto& self = *reinterpret_cast<Request*>(context);
        std::lock_guard lock(self.mutex_);
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) self.closed_ = true;
        else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) self.error_ = info && length == sizeof(WINHTTP_ASYNC_RESULT)
            ? static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError : ERROR_WINHTTP_INTERNAL_ERROR;
        else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE || status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
                 status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) { self.completed_ = status; self.bytes_ = length; }
        self.condition_.notify_all();
    }
    HINTERNET handle_;
    bool attached_ = false, closed_ = false;
    DWORD completed_ = 0, bytes_ = 0, error_ = 0;
    std::mutex mutex_;
    std::condition_variable condition_;
};
void Download(const Descriptor& descriptor, const Sink& sink, const Cancelled& cancelled) {
    Check(cancelled);
    InternetHandle session(WinHttpOpen(L"LumeMusicRuntime/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    Require(session.value && WinHttpSetTimeouts(session.value, 5000, 10000, 15000, 15000), "network");
    InternetHandle connection(WinHttpConnect(session.value, L"static.lumedesktop.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    Require(connection.value != nullptr, "network");
    const auto path = L"/downloads/LumeMusicRuntime-" + std::wstring(descriptor.sha256.begin(), descriptor.sha256.begin() + 16) + L".zip";
    Request request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    request.Attach();
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER, disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    Require(WinHttpSetOption(request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)) &&
        WinHttpSetOption(request.Get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) &&
        WinHttpSetOption(request.Get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)), "network");
    const auto deadline = Clock::now() + std::chrono::minutes(10);
    request.Begin();
    request.Await(WinHttpSendRequest(request.Get(), L"Accept-Encoding: identity\r\n", static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(&request)), WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, cancelled, deadline);
    request.Begin();
    request.Await(WinHttpReceiveResponse(request.Get(), nullptr), WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, cancelled, deadline);
    DWORD status = 0, size = sizeof(status);
    Require(WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) && status == 200, "network");
    DWORD length = 0; size = sizeof(length);
    if (WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX)) Require(length == descriptor.size);
    for (;;) {
        request.Begin();
        request.Await(WinHttpReadData(request.Get(), request.buffer.data(), static_cast<DWORD>(request.buffer.size()), nullptr),
            WINHTTP_CALLBACK_STATUS_READ_COMPLETE, cancelled, deadline);
        const auto count = request.Bytes();
        if (!count) break;
        Require(count <= request.buffer.size()); sink(request.buffer.data(), count);
    }
}

class Archive {
    Handle file_, mapping_;
public:
    const unsigned char* data = nullptr;
    std::uint64_t size = 0;
    explicit Archive(const fs::path& path) : file_(Open(path)) {
        size = Size(file_.value); Require(size >= 22 && size <= MaxArchive);
        mapping_.value = CreateFileMappingW(file_.value, nullptr, PAGE_READONLY, 0, 0, nullptr);
        Require(mapping_.value != nullptr, "storage");
        data = static_cast<const unsigned char*>(MapViewOfFile(mapping_.value, FILE_MAP_READ, 0, 0, 0));
        Require(data != nullptr, "storage");
    }
    ~Archive() { if (data) UnmapViewOfFile(data); }
    const unsigned char* At(std::uint64_t offset, std::uint64_t count) const {
        Require(offset <= size && count <= size - offset); return data + offset;
    }
    unsigned U16(std::uint64_t offset) const { const auto* p = At(offset, 2); return p[0] | (unsigned(p[1]) << 8); }
    std::uint32_t U32(std::uint64_t offset) const { return U16(offset) | (std::uint32_t(U16(offset + 2)) << 16); }
};
struct Entry { fs::path path; std::string name; std::uint64_t bytes, compressed, offset; unsigned method; bool directory; };
std::vector<Entry> Entries(const Archive& zip, const Descriptor& descriptor) {
    auto end = zip.size - 22;
    const auto first = zip.size > 65557 ? zip.size - 65557 : 0;
    for (;;) {
        if (zip.U32(end) == 0x06054b50 && end + 22 + zip.U16(end + 20) == zip.size) break;
        Require(end > first); --end;
    }
    Require(zip.U16(end + 4) == 0 && zip.U16(end + 6) == 0 && zip.U16(end + 8) == zip.U16(end + 10));
    const auto count = zip.U16(end + 10); Require(count > 0 && count < 65535);
    const auto central = zip.U32(end + 16), centralSize = zip.U32(end + 12);
    Require(std::uint64_t(central) + centralSize == end);
    std::uint64_t at = central, unpacked = 0;
    std::vector<Entry> entries; std::set<std::wstring> names;
    for (unsigned index = 0; index < count; ++index) {
        Require(zip.U32(at) == 0x02014b50); zip.At(at, 46);
        const auto flags = zip.U16(at + 8), method = zip.U16(at + 10), nameSize = zip.U16(at + 28);
        const auto external = zip.U32(at + 38), unixMode = external >> 16;
        Require((flags & ~(8u | 0x800u | 6u)) == 0 && (method == 0 || method == 8) && zip.U16(at + 34) == 0 &&
            !(external & FILE_ATTRIBUTE_REPARSE_POINT) &&
            ((unixMode & 0170000u) == 0 || (unixMode & 0170000u) == 0100000u || (unixMode & 0170000u) == 0040000u));
        const std::string name(reinterpret_cast<const char*>(zip.At(at + 46, nameSize)), nameSize);
        Require(!name.empty()); const bool directory = name.back() == '/';
        const auto path = Relative(directory ? std::string_view(name).substr(0, name.size() - 1) : name);
        Require(names.insert(Key(path)).second);
        const auto compressed = zip.U32(at + 20), bytes = zip.U32(at + 24), offset = zip.U32(at + 42);
        Require(compressed != MAXDWORD && bytes != MAXDWORD && offset != MAXDWORD && bytes <= MaxUnpacked - unpacked &&
            (!directory || (bytes == 0 && compressed == 0)) && (method != 0 || compressed == bytes));
        unpacked += bytes;
        Require(zip.U32(offset) == 0x04034b50 && zip.U16(offset + 8) == method && zip.U16(offset + 6) == flags);
        const auto localNameSize = zip.U16(offset + 26), extra = zip.U16(offset + 28);
        Require(localNameSize == nameSize && std::string(reinterpret_cast<const char*>(zip.At(offset + 30, nameSize)), nameSize) == name);
        const std::uint64_t data = std::uint64_t(offset) + 30 + localNameSize + extra;
        Require(data <= central && compressed <= central - data);
        entries.push_back({path, name, bytes, compressed, data, method, directory});
        at += 46ULL + nameSize + zip.U16(at + 30) + zip.U16(at + 32);
        Require(at <= end);
    }
    Require(at == end && unpacked == descriptor.unpacked);
    return entries;
}
void ExtractFile(const Archive& zip, const Entry& entry, const fs::path& destination, const Cancelled& cancelled) {
    Require(Directories(destination.parent_path(), true), "storage");
    Handle file(CreateFileW(Native(destination).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Require(file.value != INVALID_HANDLE_VALUE, "storage");
    const auto* compressed = zip.At(entry.offset, entry.compressed);
    std::uint64_t written = 0;
    auto write = [&](const void* bytes, std::size_t size) {
        Check(cancelled); Require(size <= entry.bytes - written);
        DWORD count = 0;
        Require(size == 0 || (WriteFile(file.value, bytes, static_cast<DWORD>(size), &count, nullptr) && count == size), "storage");
        written += size;
    };
    if (entry.method == 0) {
        for (std::uint64_t at = 0; at < entry.bytes;) {
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(65536, entry.bytes - at));
            write(compressed + at, count); at += count;
        }
    } else {
        tinfl_decompressor decoder{}; tinfl_init(&decoder);
        std::array<mz_uint8, TINFL_LZ_DICT_SIZE> output{};
        std::size_t inputAt = 0, outputAt = 0;
        for (;;) {
            Check(cancelled);
            auto inputSize = static_cast<std::size_t>(entry.compressed) - inputAt, outputSize = output.size() - outputAt;
            const auto status = tinfl_decompress(&decoder, compressed + inputAt, &inputSize,
                output.data(), output.data() + outputAt, &outputSize, 0);
            inputAt += inputSize; write(output.data() + outputAt, outputSize); outputAt = (outputAt + outputSize) % output.size();
            if (status == TINFL_STATUS_DONE) { Require(inputAt == entry.compressed); break; }
            Require(status == TINFL_STATUS_HAS_MORE_OUTPUT && (inputSize || outputSize));
        }
    }
    Require(written == entry.bytes, "storage");
}
void Extract(const fs::path& archive, const fs::path& destination, const Descriptor& descriptor, const Cancelled& cancelled) {
    Archive zip(archive); const auto entries = Entries(zip, descriptor);
    std::map<std::wstring, std::uint64_t> files;
    for (const auto& entry : entries) {
        Check(cancelled);
        if (entry.directory) Require(Directories(destination / entry.path, true), "storage");
        else { ExtractFile(zip, entry, destination / entry.path, cancelled); files.emplace(Key(entry.path), entry.bytes); }
    }
    const auto inventory = Inventory(destination, descriptor);
    Require(files.erase(L"runtime-inventory.json") == 1 && inventory["files"].size() == files.size());
    for (const auto& entry : inventory["files"]) {
        Check(cancelled);
        const auto path = Relative(entry.at("path").get<std::string>());
        const auto found = files.find(Key(path)); const auto bytes = entry.at("bytes").get<std::uint64_t>();
        const auto sha256 = entry.at("sha256").get<std::string>();
        Require(found != files.end() && found->second == bytes && Hex(sha256) &&
            FileHash(destination / path, bytes, cancelled) == sha256);
        files.erase(found);
    }
    Require(files.empty() && Ready(destination, descriptor));
}
std::string Random() {
    std::array<UCHAR, 16> bytes{};
    Require(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0, "storage");
    std::string value;
    for (const auto byte : bytes) { value += "0123456789abcdef"[byte >> 4]; value += "0123456789abcdef"[byte & 15]; }
    return value;
}
fs::path Ensure(const fs::path& executable, const fs::path& accounts, const Cancelled& cancelled,
                const std::function<void(std::string_view)>& phase, const Transport& transport = {}) {
    Check(cancelled); Require(!accounts.empty() && accounts.is_absolute(), "storage");
    const auto descriptor = Describe(executable);
    static std::timed_mutex downloads;
    std::unique_lock lock(downloads, std::defer_lock);
    while (!lock.try_lock_for(std::chrono::milliseconds(20))) Check(cancelled);
    Check(cancelled);
    const auto existing = Existing(executable, accounts, descriptor);
    if (!existing.empty()) return existing;
    const auto parent = accounts.parent_path() / L"music-runtime", ready = parent / descriptor.id;
    Require(Directories(parent, true), "storage");
    const auto work = parent / (descriptor.id + "." + Random() + ".tmp");
    Require(CreateDirectoryW(Native(work).c_str(), nullptr) != FALSE, "storage");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(fs::path(Native(path)), ignored); }
    } cleanup{work};
    const auto archive = work / L"runtime.zip", content = work / L"content";
    Require(Directories(content, true), "storage");
    if (phase) phase("downloading");
    {
        Handle file(CreateFileW(Native(archive).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        Require(file.value != INVALID_HANDLE_VALUE, "storage");
        Hash hash; std::uint64_t received = 0;
        const Sink sink = [&](const void* bytes, std::size_t size) {
            Check(cancelled); Require(size <= descriptor.size - received && size <= MAXDWORD);
            DWORD written = 0;
            Require(!size || (WriteFile(file.value, bytes, static_cast<DWORD>(size), &written, nullptr) && written == size), "storage");
            hash.Add(bytes, size); received += size;
        };
        if (transport) transport(sink, cancelled); else Download(descriptor, sink, cancelled);
        Check(cancelled); Require(received == descriptor.size && hash.Finish() == descriptor.sha256);
        Require(FlushFileBuffers(file.value) != FALSE, "storage");
    }
    if (phase) phase("extracting");
    Extract(archive, content, descriptor, cancelled); Check(cancelled);
    if (!MoveFileExW(Native(content).c_str(), Native(ready).c_str(), MOVEFILE_WRITE_THROUGH))
        Require(Ready(ready, descriptor), "storage");
    return ready;
}
}
bool FoliaRuntimeAvailable(const fs::path& executable, const fs::path& accounts) noexcept {
    try { return !accounts.empty() && accounts.is_absolute() && !Existing(executable, accounts, Describe(executable)).empty(); }
    catch (...) { return false; }
}
fs::path EnsureFoliaRuntime(const fs::path& executable, const fs::path& accounts, const Cancelled& cancelled,
                           const std::function<void(std::string_view)>& phase) {
    return Ensure(executable, accounts, cancelled, phase);
}
#ifdef LUME_FOLIA_DOWNLOAD_TESTING
fs::path EnsureFoliaRuntimeForTests(const fs::path& executable, const fs::path& accounts, const Cancelled& cancelled,
    const FoliaRuntimeTestTransport& transport, const std::function<void(std::string_view)>& phase) {
    Require(static_cast<bool>(transport), "unavailable");
    return Ensure(executable, accounts, cancelled, phase, transport);
}
#endif
}
